#include "PublishQueueExtRK.h"
#include "FileHelperRK.h" // https://github.com/rickkas7/FileHelperRK

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

PublishQueueExt *PublishQueueExt::_instance;

static Logger _log("app.pubq");


PublishQueueExt &PublishQueueExt::instance() {
    if (!_instance) {
        _instance = new PublishQueueExt();
    }
    return *_instance;
}

PublishQueueExt &PublishQueueExt::withFileQueueSize(size_t size) {
    fileQueueSize = size; 

    if (stateHandler) {
        _log.trace("withFileQueueSize(%u)", fileQueueSize);
        checkQueueLimits();
    }
    return *this; 
}

void PublishQueueExt::setup() {
    if (system_thread_get_state(nullptr) != spark::feature::ENABLED) {
        _log.error("SYSTEM_THREAD(ENABLED) is required");
        return;
    }

    os_mutex_recursive_create(&mutex);

    fileQueue
        .withFilenameExtension("json")
        .scanDir();

    checkQueueLimits();

    stateHandler = &PublishQueueExt::stateConnectWait;
}

void PublishQueueExt::loop() {
    if (stateHandler) {
        stateHandler(*this);
    }
}

bool PublishQueueExt::publish(CloudEvent event) {
    bool bResult = false;
    
    int fileNum = fileQueue.reserveFile();


    if (fileNum) {
        // Save metadata to the json file
        String pathJson = fileQueue.getPathForFileNum(fileNum);

        particle::Variant obj;
        obj.set("name", event.name());
        obj.set("content-type", (int)event.contentType());
        bResult = FileHelperRK::storeVariant(pathJson, obj) == SYSTEM_ERROR_NONE;
        if (bResult) {
            // Save data
            String pathDat = fileQueue.getPathForFileNum(fileNum, "dat");
            bResult = event.saveData(pathDat) == SYSTEM_ERROR_NONE;
            if (bResult) {
                _log.trace("saved event to fileNum %d", fileNum);
                fileQueue.addFileToQueue(fileNum);
            }
        }

        if (!bResult) {
            _log.error("error saving event to fileNum %d", fileNum);
        }
    }
    else {
        _log.error("error reserving file in queue");
    }

    return bResult;
}

bool PublishQueueExt::publish(const char *eventName) {
    CloudEvent event;

    event.name(eventName);

    return publish(event);
}


bool PublishQueueExt::publish(const char *eventName, const char *data) {
    CloudEvent event;

    event.name(eventName);
    event.data(data);

    return publish(event);
}


bool PublishQueueExt::publish(const char *eventName, const Variant &data) {
    CloudEvent event;

    event.name(eventName);
    event.data(data);

    return publish(event);

}

void PublishQueueExt::clearQueues() {
    WITH_LOCK(*this) {
        fileQueue.removeAll(true);
    }

    _log.trace("clearQueues");
}

void PublishQueueExt::setPausePublishing(bool value) { 
    pausePublishing = value; 

    if (!value) {
        // When resuming publishing, update the canSleep flag
        if (getNumEvents() != 0) {
            canSleep = false;
        }
    }
}



void PublishQueueExt::checkQueueLimits() {
    WITH_LOCK(*this) {
        while(fileQueue.getQueueLen() > (int)fileQueueSize) {
            int fileNum = fileQueue.getFileFromQueue(true);
            if (fileNum) {
                fileQueue.removeFileNum(fileNum, false);
                _log.info("discarded event %d", fileNum);
            }
        }
    }
}

size_t PublishQueueExt::getNumEvents() {
    size_t result = 0;

    WITH_LOCK(*this) {
        result = fileQueue.getQueueLen();
    }

    return result;
}

void PublishQueueExt::publishCompleteCallback(bool succeeded, const char *eventName, const char *eventData) {
    if (publishCompleteUserCallback) {
        publishCompleteUserCallback(succeeded, eventName, eventData);
    }
}


void PublishQueueExt::stateConnectWait() {
    canSleep = (pausePublishing || getNumEvents() == 0);

    if (Particle.connected()) {
        stateTime = millis();
        durationMs = waitAfterConnect;
        stateHandler = &PublishQueueExt::stateWaitEvent;
    }
}


void PublishQueueExt::stateWaitEvent() {
    if (!Particle.connected()) {
        stateHandler = &PublishQueueExt::stateConnectWait;
        return;
    }

    if (pausePublishing) {
        canSleep = true;
        // Stay in stateWaitEvent
        return;
    }

    if (millis() - stateTime < durationMs) {
        canSleep = (getNumEvents() == 0);
        // Stay in stateWaitEvent
        return;
    }
    
    if (curFileNum == 0) {
        // getFileFromQueue only accesses the list in RAM, not on disk, so this
        // can be called frequently without affecting performance.
        curFileNum = fileQueue.getFileFromQueue(false);
        if (curFileNum == 0) {
            // No events, can sleep
            canSleep = true;

            // Stay in stateWaitEvent
            return;
        }

        curEvent.clear();

        String pathJson = fileQueue.getPathForFileNum(curFileNum);
        particle::Variant obj;
        FileHelperRK::readVariant(pathJson, obj);
        curEvent.name(obj.get("name").asString().c_str());
        if (obj.has("content-type")) {
            curEvent.contentType((ContentType) obj.get("content-type").asInt());
        }


        String pathDat = fileQueue.getPathForFileNum(curFileNum, "dat");
        curEvent.loadData(pathDat);
        if (!curEvent.isValid()) {
            // Probably a corrupted file, discard
            _log.info("discarding corrupted file %d", curFileNum);
            fileQueue.getFileFromQueue(true);
            fileQueue.removeFileNum(curFileNum, false);
            return;
        }
        stateTime = 0;

    }

    if (stateTime != 0 && millis() - stateTime >= waitRateLimitCheck) {
        // Stay in stateWaitEvent
        return;
    }

    stateTime = millis();

    if (!CloudEvent::canPublish(curEvent.size())) {
        // Can't publish yet (rate limited)
        // Stay in stateWaitEvent
        return;
    }

    stateHandler = &PublishQueueExt::statePublishWait;
    canSleep = false;

    // This message is monitored by the automated test tool. If you edit this, change that too.
    _log.trace("publishing fileNum=%d event=%s", curFileNum, curEvent.name());

    if (!Particle.publish(curEvent)) {
        _log.error("published failed immediately, discarding");
        deleteCurEvent();
        durationMs = waitBetweenPublish;
        return;
    }

}

void PublishQueueExt::deleteCurEvent() {
    int fileNum = fileQueue.getFileFromQueue(false);
    if (fileNum == curFileNum) {
        fileQueue.getFileFromQueue(true);
        fileQueue.removeFileNum(fileNum, false);
        _log.trace("removed file %d", fileNum);
    }
    curFileNum = 0;
    curEvent.clear();

}

void PublishQueueExt::statePublishWait() {
    if (curEvent.isSending()) {
        // Stay in statePublishWait
        return;
    }


    if (!curEvent.isValid()) {
        _log.trace("publish failed invalid %d (discarding)", curFileNum);
        deleteCurEvent();
        durationMs = waitBetweenPublish;
    }
    else
    if (curEvent.isSent()) {
        _log.trace("publish success %d", curFileNum);
        deleteCurEvent();
        durationMs = waitBetweenPublish;
    }
    else {
        _log.trace("publish failed %d (retrying)", curFileNum);
        durationMs = waitAfterFailure;
    }

    stateHandler = &PublishQueueExt::stateWaitEvent;
    stateTime = millis();
}


PublishQueueExt::PublishQueueExt() {
    fileQueue.withDirPath("/usr/pubqueue2");
}

PublishQueueExt::~PublishQueueExt() {

}
