#include "Particle.h"

#include "PublishQueueExtRK.h"

// System thread defaults to on in 6.2.0 and later and this line is not required
#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED);
#endif

SerialLogHandler logHandler(LOG_LEVEL_INFO, { // Logging level for non-application messages
	{ "app.pubq", LOG_LEVEL_TRACE },
	{ "app.seqfile", LOG_LEVEL_TRACE }
});

const char *eventName = "testConfirmedDelete";
const std::chrono::milliseconds testPublishPeriod = 2min;

void myHookResponseHandler(CloudEvent event);

void setup() {
    // For testing purposes, wait 10 seconds before continuing to allow serial to connect
	// before doing PublishQueue setup so the debug log messages can be read.
	waitFor(Serial.isConnected, 10000);
    delay(1000);
    
    // This allows a graceful shutdown on System.reset()
    Particle.setDisconnectOptions(CloudDisconnectOptions().graceful(true).timeout(5000));

    // Subscribe to the hook-response event. Be sure to set your integration topic to:
    // {{PARTICLE_DEVICE_ID}}/hook-response/{{PARTICLE_EVENT_NAME}}
    String responseEvent = String::format("%s/hook-response/%s/", System.deviceID().c_str(), eventName);
    Particle.subscribe(responseEvent, myHookResponseHandler);
    Log.info("response topic: %s", responseEvent.c_str());

	PublishQueueExt::instance()
        .withDeferredDelete(true)
        .setup();
}

void loop() {
    PublishQueueExt::instance().loop();

    static unsigned long lastPublishMs = 0;
    static int counter = 0;

    if (millis() - lastPublishMs >= testPublishPeriod.count()) {
        lastPublishMs = millis();

        Variant data;
        data.set("counter", ++counter);
        data.set("ts", (int)lastPublishMs);

    	PublishQueueExt::instance()
            .publish(eventName, data);
    }
}


void myHookResponseHandler(CloudEvent event) {
	PublishQueueExt::instance().confirmDelete();
}

