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

enum {
	TEST_IDLE = 0, // Don't do anything
	TEST_COUNTER, // 1 publish, period milliseconds is param0
	TEST_PUBLISH_FAST, // 2 publish events as fast as possible, number is param0, optional size in param2
	TEST_PUBLISH_OFFLINE, // 3 go offline, publish some events, then go back online, number is param0, optional size in param2
	TEST_PAUSE_PUBLISING, // 4 pause publishing
	TEST_RESUME_PUBLISING, // 5 resume publishing
	TEST_PUBLISH_OFFLINE_RESET, // 6 go offline, publish some events, reset device, number is param0, optional size in param2
    TEST_CLEAR_QUEUES, // 7 clear RAM and file-based queues
    TEST_SET_FILE_QUEUE_LEN // 8 set file queue length (param0 = length)
};

// Example:
// particle call boron5 test "4,30000"
// Replace boron5 with the name of your device
// "4,30000" is test 4, with a period of 30000 milliseconds or 30 seconds

const size_t MAX_PARAM = 4;
const unsigned long PUBLISH_PERIOD_MS = 30000;
unsigned long lastPublish = 8000 - PUBLISH_PERIOD_MS;
int counter = 0;
int testNum;
int intParam[MAX_PARAM];
String stringParam[MAX_PARAM];
size_t numParam;

int testHandler(String cmd);
void publishCounter();
void publishPaddedCounter(int size);

void setup() {
	// For testing purposes, wait 10 seconds before continuing to allow serial to connect
	// before doing PublishQueue setup so the debug log messages can be read.
	waitFor(Serial.isConnected, 10000);
    delay(1000);
    
    // This allows a graceful shutdown on System.reset()
    Particle.setDisconnectOptions(CloudDisconnectOptions().graceful(true).timeout(5000));

	Particle.function("test", testHandler);
	PublishQueueExt::instance().setup();

    // PublishQueueExt::instance().clearQueues();

}

void loop() {
    PublishQueueExt::instance().loop();

	if (testNum == TEST_COUNTER) {
		int publishPeriod = intParam[0];
		if (publishPeriod < 1) {
			publishPeriod = 15000;
		}

		if (millis() - lastPublish >= (unsigned long) publishPeriod) {
			lastPublish = millis();

			Log.info("TEST_COUNTER period=%d", publishPeriod);
			publishCounter();
		}
	}
	else
	if (testNum == TEST_PUBLISH_FAST) {
		testNum = TEST_IDLE;

		int count = intParam[0];
		int size = intParam[1];

		Log.info("TEST_PUBLISH_FAST count=%d", count);

		for(int ii = 0; ii < count; ii++) {
			publishPaddedCounter(size);
		}
	}
	else
	if (testNum == TEST_PUBLISH_OFFLINE || testNum == TEST_PUBLISH_OFFLINE_RESET) {
		int count = intParam[0];
		int size = intParam[1];

		Log.info("TEST_PUBLISH_OFFLINE count=%d", count);

		Log.info("Going to Particle.disconnect()...");
		Particle.disconnect();
		delay(2000);

		Log.info("before publishing numEvents=%u", PublishQueueExt::instance().getNumEvents());

		for(int ii = 0; ii < count; ii++) {
			publishPaddedCounter(size);
		}

		Log.info("after publishing numEvents=%u", PublishQueueExt::instance().getNumEvents());

		if (testNum == TEST_PUBLISH_OFFLINE_RESET) {
			Log.info("resetting device...");			
			delay(100);
			System.reset();
		}

		testNum = TEST_IDLE;

		Log.info("Going to Particle.connect()...");
		Particle.connect();
	}
}

void publishCounter() {
	Log.info("publishing counter=%d", counter);

	char buf[32];
	snprintf(buf, sizeof(buf), "%d", counter++);
	PublishQueueExt::instance().publish("testEvent", buf);
}

void publishPaddedCounter(int size) {
	Log.info("publishing padded counter=%d size=%d", counter, size);

	char buf[256];
	snprintf(buf, sizeof(buf), "%05d", counter++);

	if (size > 0) {
		if (size > (int)(sizeof(buf) - 1)) {
			size = (int)(sizeof(buf) - 1);
		}

		char c = 'A';
		for(size_t ii = strlen(buf); ii < (size_t)size; ii++) {
			buf[ii] = c;
			if (++c > 'Z') {
				c = 'A';
			}
		}
		buf[size] = 0;
	}

	PublishQueueExt::instance().publish("testEvent", buf);
}


int testHandler(String cmd) {
	char *mutableCopy = strdup(cmd.c_str());

	char *cp = strtok(mutableCopy, ",");

	int tempTestNum = atoi(cp);
    for(numParam = 0; numParam < MAX_PARAM; numParam++) {
        cp = strtok(NULL, ",");
        if (!cp) {
            break;
        }
        intParam[numParam] = atoi(cp);
        stringParam[numParam] = cp;
    }
    for(size_t ii = numParam; ii < MAX_PARAM; ii++) {
        intParam[ii] = 0;
        stringParam[ii] = "";
    }

	switch(tempTestNum) {
	case TEST_PAUSE_PUBLISING:
		Log.info("pausing publishing from test handler");
		PublishQueueExt::instance().setPausePublishing(true);
		break;

	case TEST_RESUME_PUBLISING:
		Log.info("resuming publishing from test handler");
		PublishQueueExt::instance().setPausePublishing(false);
		break;

    case TEST_CLEAR_QUEUES:
        Log.info("TEST_CLEAR_QUEUES");
        PublishQueueExt::instance().clearQueues();
        break;

    case TEST_SET_FILE_QUEUE_LEN:
        Log.info("set file queue length %d", intParam[0]);
        PublishQueueExt::instance().withFileQueueSize(intParam[0]);
        PublishQueueExt::instance().checkQueueLimits();
        break;

	default:
		testNum = tempTestNum;
		break;
	}

	free(mutableCopy);
	return 0;
}
