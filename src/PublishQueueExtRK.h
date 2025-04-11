#ifndef __PUBLISHQUEUEEXTRK_H
#define __PUBLISHQUEUEEXTRK_H

// Github: https://github.com/rickkas7/PublishQueueExtRK
// License: MIT

#include "Particle.h"

#ifndef SYSTEM_VERSION_630
#error "This library requires Device OS 6.3.0 or later"
#endif


#include "Particle.h"
#include "SequentialFileRK.h" // https://github.com/rickkas7/SequentialFileRK

#include <deque>

/**
 * @brief Class for asynchronous publishing of events
 * 
 */
class PublishQueueExt {
public:
    /**
     * @brief This structure is at the end of the publish queue file
     */
    struct QueueFileTrailer { // 16 bytes
        uint32_t magic; //!< kQueueFileTrailerMagic
        uint32_t dataSize; //!< size of the event data at the beginning of the file
        uint16_t metaSize; //!< size of the JSON meta data (not null terminated)
        uint16_t reserved; //!< not used, set to 0
    };

    static const uint32_t kQueueFileTrailerMagic = 0x55fcab58; //!< Magic bytes stored in the QueueFileTrailer structure

    /**
     * @brief Gets the singleton instance of this class
     * 
     * You cannot construct a PublishQueueExt object as a global variable,
     * stack variable, or with new. You can only request the singleton instance.
     */
    static PublishQueueExt &instance();

    /**
     * @brief Sets the file-based queue size (default is 100)
     * 
     * @param size The maximum number of files to store (one event per file)
     * 
     * If you exceed this number of events, the oldest event is discarded.
     */
    PublishQueueExt &withFileQueueSize(size_t size);

    /**
     * @brief Gets the file queue size
     */
    size_t getFileQueueSize() const { return fileQueueSize; };

    /**
     * @brief Sets the directory to use as the queue directory. This is required!
     * 
     * @param dirPath the pathname, Unix-style with / as the directory separator. 
     * 
     * Typically you create your queue either at the top level ("/myqueue") or in /usr
     * ("/usr/myqueue"). The directory will be created if necessary, however only one
     * level of directory will be created. The parent must already exist.
     * 
     * The dirPath can end with a slash or not, but if you include it, it will be
     * removed.
     * 
     * You must call this as you cannot use the root directory as a queue!
     */
    PublishQueueExt &withDirPath(const char *dirPath) { fileQueue.withDirPath(dirPath); return *this; };

    /**
     * @brief Gets the directory path set using withDirPath()
     * 
     * The returned path will not end with a slash.
     */
    const char *getDirPath() const { return fileQueue.getDirPath(); };

    /**
     * @brief Adds a callback function to call with publish is complete
     * 
     * @param cb Callback function or C++ lambda.
     * @return PublishQueueExt& 
     * 
     * The callback has this prototype and can be a function or a C++11 lambda, which allows the callback to be a class method.
     * 
     * void callback(const CloudEvent &event)
     * 
     * The parameters are:
     * - event: The CloudEvent object that was just sent
     * 
     * You can determine success/failure, examine the event. or the event data, by using methods of the CloudEvent class
     * 
     * Note that this callback will be called from the background thread used for publishing. You should not
     * perform any lengthy operations and you should avoid using large amounts of stack space during this
     * callback. 
     */
    PublishQueueExt &withPublishCompleteUserCallback(std::function<void(const CloudEvent &event)> cb) { publishCompleteUserCallback = cb; return *this; };


    /**
     * @brief You must call this from setup() to initialize this library
     */
    void setup();

    /**
     * @brief You must call the loop method from the global loop() function!
     */
    void loop();

    /**
     * @brief Publish an event
     * 
     * @param event 
     * @return true 
     * @return false 
     */
    bool publish(CloudEvent event);

	/**
	 * @brief Overload for publishing an event
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
	 */
    bool publish(const char *eventName);

	/**
	 * @brief Overload for publishing an event
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @param data The UTF-8 text event data as a c-string.  It is copied by this method.
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
	 */
	bool publish(const char *eventName, const char *data);

	/**
	 * @brief Overload for publishing an event from a Variant
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @param data Reference to a Variant object holding the data. It is copied by this method.
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
     * 
     * In some cases the content type can be inferred, such as when the `Variant` is a `VariantMap`
     * but normally you will want to use the overload with a `ContentType`.
	 */
	bool publish(const char *eventName, const Variant &data);


	/**
	 * @brief Overload for publishing an event with a Variant and ContentType
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @param data Reference to a Variant object holding the data. It is copied by this method.
     * 
     * @param type The ContentType of the data
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
     * 
     * Content Type Constant    MIME Type	               Value
     * ContentType::TEXT        text/plain; charset=utf-8  0
     * ContentType::JPEG        image/jpeg                 22
     * ContentType::PNG         image/png                  23
     * ContentType::BINARY      application/octet-stream   42
     * ContentType::STRUCTURED                             65001
	 */
    bool publish(const char *eventName, const Variant &data, ContentType type);


    /**
     * @brief Empty the file based queue. Any queued events are discarded and the files deleted.
     */
    void clearQueues();

    /**
     * @brief Pause or resume publishing events
     * 
     * @param value The value to set, true = pause, false = normal operation
     * 
     * If called while a publish is in progress, that publish will still proceed, but
     * the next event (if any) will not be attempted.
     * 
     * This is used by the automated test tool; you probably won't need to manually
     * manage this under normal circumstances.
     */
    void setPausePublishing(bool value);

    /**
     * @brief Gets the state of the pause publishing flag
     */
    bool getPausePublishing() const { return pausePublishing; };

    /**
     * @brief Determine if it's a good time to go to sleep
     * 
     * If a publish is not in progress and the queue is empty, returns true. 
     * 
     * If pausePublishing is true, then return true if either the current publish has
     * completed, or not cloud connected.
     */
    bool getCanSleep() const { return canSleep; };

    /**
     * @brief Gets the total number of events queued
     * 
     * This operation is fast; the file queue length is stored in RAM,
     * so this command does not need to access the file system.
     * 
     * If an event is currently being sent, the result includes this event.
     */
    size_t getNumEvents();

    /**
     * @brief Check the queue limit, discarding events as necessary
     */
    void checkQueueLimits();
    
    /**
     * @brief Lock the queue protection mutex
     * 
     * This is done internally; you probably won't need to call this yourself.
     * It needs to be public for the WITH_LOCK() macro to work properly.
     */
    void lock() { os_mutex_recursive_lock(mutex); };

    /**
     * @brief Attempt the queue protection mutex
     */
    bool tryLock() { return os_mutex_recursive_trylock(mutex); };

    /**
     * @brief Unlock the queue protection mutex
     */
    void unlock() { os_mutex_recursive_unlock(mutex); };


protected:
    /**
     * @brief Constructor 
     * 
     * This class is a singleton; you never create one of these directly. Use 
     * PublishQueueExt::instance() to get the singleton instance.
     */
    PublishQueueExt();

    /**
     * @brief Destructor
     * 
     * This class is never deleted; once the singleton is created it cannot
     * be destroyed.
     */
    virtual ~PublishQueueExt();

    /**
     * @brief This class is not copyable
     */
    PublishQueueExt(const PublishQueueExt&) = delete;

    /**
     * @brief This class is not copyable
     */
    PublishQueueExt& operator=(const PublishQueueExt&) = delete;

    /**
     * @brief Delete the current event in curFileNum
     */
    void deleteCurEvent();

    /**
     * @brief State handler for waiting to connect to the Particle cloud
     * 
     * Next state: stateWait
     */
    void stateConnectWait();

    /**
     * @brief State handler for waiting for an event
     * 
     * stateTime and durationMs determine whether to stay in this state waiting, or whether
     * to publish and go into statePublishWait.
     * 
     * Next state: stateWaitRateLimit or stateConnectWait
     */
    void stateWaitEvent();

    /**
     * @brief State handler for waiting for publish to complete
     * 
     * Next state: stateWait
     */
    void statePublishWait();

    /**
     * @brief SequentialFileRK library object for maintaining the queue of files on the POSIX file system
     */
    SequentialFile fileQueue;

    /**
     * @brief File used for temporary data
     */
    String tempFileName = "temp.dat";
    String tempFilePath; //!< Fill path name to tempFileName

    size_t fileQueueSize = 100; //!< size of the queue on the flash file system

    os_mutex_recursive_t mutex; //!< mutex for protecting the queue

    CloudEvent curEvent; //!< Current event being published
    int curFileNum = 0; //!< Current file number being published
    unsigned long stateTime = 0; //!< millis() value when entering the state, used for stateWait
    unsigned long durationMs = 0; //!< how long to wait before publishing in milliseconds, used in stateWait
    bool pausePublishing = false; //!< flag to pause publishing (used from automated test)
    bool canSleep = false; //!< returns true if this is a good time to go to sleep

    unsigned long waitAfterConnect = 500; //!< time to wait after Particle.connected() before publishing
    unsigned long waitBetweenPublish = 10; //!< how long to wait in milliseconds between publishes
    unsigned long waitAfterFailure = 30000; //!< how long to wait after failing to publish before trying again

    std::function<void(const CloudEvent &event)> publishCompleteUserCallback = 0; //!< User callback for publish complete

    std::function<void(PublishQueueExt&)> stateHandler = 0; //!< state handler (stateConnectWait, stateWait, etc).

    static PublishQueueExt *_instance; //!< singleton instance of this class
};

#endif /* __PUBLISHQUEUEEXTRK_H */
