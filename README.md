# PublishQueueExtRK

**Queued publish for Particle devices using typed and extended publish**

This is version of [PublishQueuePosixRK](https://github.com/rickkas7/PublishQueuePosixRK/) that is designed for
use with [typed and extended publish](https://docs.particle.io/reference/device-os/typed-publish/) in Device OS 6.3.0 and later.

This library can only be used Device OS 6.3.0 and later, but offers several benefits:

- Increased publish rate limit, no longer limited to 1 publish per second
- Increased event data size, up to 16384 bytes
- Binary and structured values in event data

You can also find [full browseable API documentation](https://rickkas7.github.io/PublishQueueExtRK/index.html) at the link
or in the docs directory of this repository.

- Repository: https://github.com/rickkas7/PublishQueueExtRK
- License: MIT


## Usage

In many cases, you simply call this from setup:

```cpp
PublishQueueExt::instance().setup();
```

And this from loop:

```cpp
PublishQueueExt::instance().setup();
```

To publish you do something like this:

```
PublishQueueExt::instance().publish("testEvent", buf, PRIVATE | WITH_ACK);
```

### File queue

The default maximum file queue size is 100, which corresponds to 100 events. Each event takes is stored in 
a single file. In many cases, an event will fit in a single 512-byte flash sector, but it could require two,
or three, for a full 1024 byte event with the overhead. A full 16384 byte event could take 33 sectors.

```cpp
PublishQueueExt::instance().withFileQueueSize(50);
```

## Dependencies

This library depends on an additional library:

- [SequentialFileRK](https://github.com/rickkas7/SequentialFileRK) manages the queue on the flash file system


## API

---

### void PublishQueueExt::setup() 

You must call this from setup() to initialize this library.

```
void setup()
```

---

### void PublishQueueExt::loop() 

You must call the loop method from the global loop() function!

```
void loop()
```
---

### PublishQueueExt & PublishQueueExt::withFileQueueSize(size_t size) 

Sets the file-based queue size (default is 100)

```
PublishQueueExt & withFileQueueSize(size_t size)
```

#### Parameters
* `size` The maximum number of files to store (one event per file)

If you exceed this number of events, the oldest event is discarded.

---

### size_t PublishQueueExt::getFileQueueSize() const 

Gets the file queue size.

```
size_t getFileQueueSize() const
```

---

### PublishQueueExt & PublishQueueExt::withDirPath(const char * dirPath) 

Sets the directory to use as the queue directory. This is required!

```
PublishQueueExt & withDirPath(const char * dirPath)
```

#### Parameters
* `dirPath` the pathname, Unix-style with / as the directory separator.

Typically you create your queue either at the top level ("/myqueue") or in /usr ("/usr/myqueue"). The directory will be created if necessary, however only one level of directory will be created. The parent must already exist.

The dirPath can end with a slash or not, but if you include it, it will be removed.

You must call this as you cannot use the root directory as a queue!

---

### const char * PublishQueueExt::getDirPath() const 

Gets the directory path set using withDirPath()

```
const char * getDirPath() const
```

The returned path will not end with a slash.

---

### bool PublishQueueExt::publish(CloudEvent event) 

This is the recommended version to use, which takes a `CloudEvent` that includes the event name and 
can include typed data, binary data, or structured data.

```
bool publish(CloudEvent event);
```

---

### bool PublishQueueExt::publish(const char * eventName, const Variant &data, ContentType type) 

You can also `Variant` that can include typed data, binary data, or structured data.

```
bool publish(const char *eventName, const Variant &data);
```

#### Parameters
* `eventName` The name of the event (63 character maximum).

* `data` The event data as a `Variant` object reference.

* `type` The `ContentType` of the data

The data is written to a file on the file system before this call returns.

```
Content Type Constant    MIME Type	                Value
ContentType::TEXT        text/plain; charset=utf-8  0
ContentType::JPEG        image/jpeg                 22
ContentType::PNG         image/png                  23
ContentType::BINARY      application/octet-stream   42
ContentType::STRUCTURED                             65001
```

### bool PublishQueueExt::publish(const char * eventName, const Variant &data) 

This overload takes a `Variant` but not a `ContentType`. It should only be used when passing
a `VariantMap` for structured data.

```
bool publish(const char *eventName, const Variant &data);
```

#### Parameters
* `eventName` The name of the event (63 character maximum).

* `data` The event data as a `Variant` object reference.

The data is written to a file on the file system before this call returns.

---

### bool PublishQueueExt::publish(const char * eventName) 

Overload for publishing an event.

```
bool publish(const char * eventName)
```

#### Parameters
* `eventName` The name of the event (63 character maximum).


#### Returns
true if the event was queued or false if it was not.

This function almost always returns true. If you queue more events than fit in the buffer the oldest (sometimes second oldest) is discarded.

---

### bool PublishQueueExt::publish(const char * eventName, const char * data) 

Overload for publishing an event.

```
bool publish(const char * eventName, const char * data)
```

#### Parameters
* `eventName` The name of the event (63 character maximum).

* `data` The event data as UTF-8 text. Up to 1024 bytes depending on the Device OS version and device.

#### Returns
true if the event was queued or false if it was not.

For larger data and typed data, use the overload that takes a `CloudEvent` or `Variant`.

This function almost always returns true. If you queue more events than fit in the buffer the oldest (sometimes second oldest) is discarded.

---

### void PublishQueueExt::clearQueues() 

Empty both the RAM and file based queues. Any queued events are discarded.

```
void clearQueues()
```

---

### void PublishQueueExt::setPausePublishing(bool value) 

Pause or resume publishing events.

```
void setPausePublishing(bool value)
```

#### Parameters
* `value` The value to set, true = pause, false = normal operation

If called while a publish is in progress, that publish will still proceed, but the next event (if any) will not be attempted.

This is used by the automated test tool; you probably won't need to manually manage this under normal circumstances.

---

### bool PublishQueueExt::getPausePublishing() const 

Gets the state of the pause publishing flag.

```
bool getPausePublishing() const
```
---

### bool PublishQueueExt::getCanSleep() const 

Determine if it's a good time to go to sleep. 

```
bool getCanSleep() const
```

If a publish is not in progress and the queue is empty, returns true.

If pausePublishing is true, then return true if either the current publish has completed, or not cloud connected.

---

### size_t PublishQueueExt::getNumEvents() 

Gets the total number of events queued.

```
size_t getNumEvents()
```

This is the number of events in the RAM-based queue and the file-based queue. This operation is fast; the file queue length is stored in RAM, so this command does not need to access the file system.

If an event is currently being sent, the result includes this event.

---

### void PublishQueueExt::lock() 

Lock the queue protection mutex.

```
void lock()
```

This is done internally; you probably won't need to call this yourself. It needs to be public for the WITH_LOCK() macro to work properly.

---

### bool PublishQueueExt::tryLock() 

Attempt the queue protection mutex.

```
bool tryLock()
```

---

### void PublishQueueExt::unlock() 

Unlock the queue protection mutex.

```
void unlock()
```

## Version History

### 0.0.4 (2025-04-08)

- Fixed a bug where queue cleanup did not work properly and could also leave corrupted files in the queue if you exceeded the file queue size.

### 0.0.3 (2025-04-07)

- Fixed a bug where if a queued file failed to publish (such as because of a connectivity issue), the file in the queue would be corrupted and would be discarded instead of retransmitted later.

### 0.0.2 (2025-03-24)

- Moved 2-test-suite.cpp into the example directory. It as accidentally in the src directory.

### 0.0.1 (2025-02-19)

- Initial version
