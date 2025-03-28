#include "PublishQueueExtRK.h"

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
        .withFilenameExtension("pq")
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
        // 
        String queueFilePath = fileQueue.getPathForFileNum(fileNum); // .pq (publish queue) file

        bResult = event.saveData(queueFilePath.c_str()) == SYSTEM_ERROR_NONE;
        if (bResult) {
            _log.trace("saved event to fileNum %d", fileNum);

            // Save the meta data
            int fd = open(queueFilePath.c_str(), O_RDWR);
            if (fd != -1) {
                struct stat sb = {0};
                fstat(fd, &sb);

                lseek(fd, 0, SEEK_END);

                particle::Variant meta;
                meta.set("name", event.name());
                meta.set("content-type", (int)event.contentType());

                String metaJson = meta.toJSON();
                write(fd, metaJson.c_str(), metaJson.length());

                QueueFileTrailer trailer = {0};
                trailer.magic = kQueueFileTrailerMagic;
                trailer.dataSize = (uint32_t) sb.st_size;
                trailer.metaSize = (uint16_t) metaJson.length();
                
                write(fd, &trailer, sizeof(trailer));

                close(fd);

                _log.trace("saved meta dataSize=%lu metaSize=%u %s ", trailer.dataSize, trailer.metaSize, metaJson.c_str());

                fileQueue.addFileToQueue(fileNum);                
            }
            else {
                _log.error("error opening %s", queueFilePath.c_str());
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

bool PublishQueueExt::publish(const char *eventName, const Variant &data, ContentType type) {
    CloudEvent event;

    event.name(eventName);
    event.data(data);
    event.contentType(type);

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
        curFileNum = fileQueue.getFileFromQueue(false);
        if (curFileNum == 0) {
            // No events, can sleep
            canSleep = true;

            // Stay in stateWaitEvent
            return;
        }

        curEvent.clear();

        String queueFilePath = fileQueue.getPathForFileNum(curFileNum);

        bool isValid = true;

        int fd = open(queueFilePath.c_str(), O_RDWR);
        if (fd == -1) {
            isValid = false;
        }

        size_t fileSize = 0;

        if (isValid) {
            struct stat sb = {0};
            fstat(fd, &sb);
            fileSize = (size_t)sb.st_size;
            _log.trace("reading fileNum=%d fileSize=%u", curFileNum, fileSize);
            
            if (fileSize < sizeof(QueueFileTrailer)) {
                _log.info("queue files size %d is too small %s", fileSize, queueFilePath.c_str());
                isValid = false;
            }
        }

        QueueFileTrailer trailer = {0};
        if (isValid) {
            lseek(fd, fileSize - sizeof(QueueFileTrailer), SEEK_SET);
            
            read(fd, &trailer, sizeof(trailer));

            if (trailer.magic != kQueueFileTrailerMagic) {
                _log.info("queue files invalid magic 0x%08lx %s", trailer.magic, queueFilePath.c_str());
                isValid = false;
            }
            if ((trailer.dataSize > fileSize) || ((trailer.dataSize + trailer.metaSize) > fileSize)) {
                _log.info("invalid sizes dataSize=%lu metaSize=%u %s", trailer.dataSize, trailer.metaSize, queueFilePath.c_str());
                isValid = false;
            }
        }
        Variant meta;

        if (isValid) {
            char *metaJson = new char[trailer.metaSize + 1];
            if (metaJson) {
                lseek(fd, trailer.dataSize, SEEK_SET);

                read(fd, metaJson, trailer.metaSize);

                metaJson[trailer.metaSize] = 0;
                meta = Variant::fromJSON(metaJson);

                delete[] metaJson;
            }
            else {
                _log.info("failed to allocate meta metaSize=%u %s", trailer.metaSize, queueFilePath.c_str());
                isValid = false;
            }
        }

        if (isValid) {
            lseek(fd, 0, SEEK_SET);
            ftruncate(fd, trailer.dataSize);
        }
        
        if (fd != -1) {
            close(fd);
            fd = -1;
        }

        if (isValid) {
            curEvent.loadData(queueFilePath.c_str());
        }
        if (isValid) {
            curEvent.name(meta.get("name").asString().c_str());
            if (meta.has("content-type")) {
                curEvent.contentType((ContentType) meta.get("content-type").asInt());
            }
        }

        if (!isValid || !curEvent.isValid()) {
            // Probably a corrupted file, discard
            _log.info("discarding corrupted file %d", curFileNum);
            fileQueue.getFileFromQueue(true);
            fileQueue.removeFileNum(curFileNum, false);
            curFileNum = 0;
            return;
        }

        _log.trace("read event %d from queue size=%d", curFileNum, curEvent.size());
    }

    stateTime = millis();

    if (!CloudEvent::canPublish(curEvent.size())) {
        // Can't publish yet (rate limited)
        // Stay in stateWaitEvent
        return;
    }


    // This message is monitored by the automated test tool. If you edit this, change that too.
    _log.trace("publishing fileNum=%d event=%s", curFileNum, curEvent.name());

    if (!Particle.publish(curEvent)) {
        _log.error("published failed immediately, discarding");
        deleteCurEvent();
        stateHandler = &PublishQueueExt::stateWaitEvent;
        durationMs = waitBetweenPublish;
        return;
    }

    stateHandler = &PublishQueueExt::statePublishWait;
    canSleep = false;
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
        curFileNum = 0;
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
