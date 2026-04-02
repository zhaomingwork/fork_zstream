# fork_zstream: FreeSWITCH Real-time Audio Stream Bidirectional Forwarding Module

## 1. Project Overview

fork_zstream is a real-time audio stream bidirectional forwarding module based on FreeSWITCH, focused on实现ing bidirectional audio data transmission between FreeSWITCH and external WebSocket services. The module captures FreeSWITCH's audio stream through media bug technology, forwards downstream data (ASR stream) to WebSocket services, while receiving upstream data (TTS stream) from WebSocket services and playing it in real-time, achieving full-duplex forwarding processing of FreeSWITCH data streams.

### 1.1 Core Features

- **Bidirectional Audio Stream Forwarding**:实现 bidirectional audio data transmission between FreeSWITCH and WebSocket services
- **Downstream Data Forwarding**: Capture FreeSWITCH's audio data (ASR stream) through media bug and forward it to WebSocket services
- **Upstream Data Receiving and Playing**: Receive audio data (TTS stream) sent by WebSocket services and play it in real-time
- **Session Management**: Provide complete session lifecycle management, including initialization, processing, and cleanup
- **Event Callback Mechanism**:实现 interaction between the module and FreeSWITCH through event callbacks

### 1.2 Technical Architecture

fork_zstream adopts a modular design, mainly composed of the following parts:

- **Core Module**: mod_forkstream.c, implementing the main functional logic
- **Interface Definition**: iforkstream.h, defining the public interface for TTS functionality
- **Data Structure**: mod_forkstream.h, defining data structures and constants used by the module

## 2. System Architecture and Working Principle

### 2.1 Overall Architecture

The fork_zstream module acts as a loadable module for FreeSWITCH, integrating with the core system through FreeSWITCH's API,实现 bidirectional communication with WebSocket services. Its overall architecture is as follows:

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  FreeSWITCH     │<───>│  fork_zstream   │<───>│  WebSocket Service │
│  Core System    │     │  Module         │     │ (ASR/TTS Processing) │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

### 2.2 Working Process

1. **Module Loading**: When FreeSWITCH starts, the fork_zstream module is loaded, and the global session manager is initialized
2. **Session Initialization**: When a start request is received, the session is initialized and media processing is set up
3. **Downstream Data Forwarding**: Capture FreeSWITCH's audio data (ASR stream) through media bug, process it, and send it to the WebSocket service
4. **Upstream Data Processing**: Receive audio data (TTS stream) sent by the WebSocket service and play it to the user in real-time
5. **Session Cleanup**: Clean up session resources when the call ends

### 2.3 Key Data Structures

#### speech_thread_handle

```c
typedef struct speech_thread_handle {
    switch_core_session_t *session;      ///< Pointer to FreeSWITCH session
    switch_asr_handle_t *ah;            ///< ASR (Automatic Speech Recognition) handle
    switch_media_bug_t *bug;            ///< Media bug handle
    switch_mutex_t *mutex;              ///< Thread synchronization mutex
    switch_thread_cond_t *cond;         ///< Thread condition variable
    switch_memory_pool_t *pool;         ///< Memory pool for thread resources
    switch_thread_t *thread;            ///< Thread handle
    int ready;                          ///< Thread ready flag
    void *pAudioStreamer;               ///< Pointer to audio streamer instance
    void *pTTsStreamer;                 ///< Pointer to TTS (Text-to-Speech) streamer instance
    struct sockaddr_in server_addr;     ///< Server address for streaming
    int asrOK;                          ///< ASR status flag
} private_t;
```

This structure is the core data structure of the module, used to manage all necessary components of the speech processing thread.

## 3. Core Function Modules

### 3.1 Session Management

Session management is one of the core functions of the fork_zstream module, responsible for creating, maintaining, and cleaning up forwarding sessions.

#### Session Initialization

```c
static switch_status_t forkstream_init(switch_core_session_t *session)
{
    // Check if the session is already initialized
    // Allocate ASR processing handle
    // Configure audio parameters
    // Initialize speech thread processing structure
    // Store session information
    // Initialize TTS session
    // Add media bug
}
```

This function is responsible for initializing the forwarding session, including allocating resources, configuring parameters, and setting up media processing.

#### Session Cleanup

```c
static void switch_event_callback(switch_event_t *event)
{
    // Handle channel hangup event
    // Find session
    // Clean up TTS session
}
```

When the call ends, session resources are cleaned up through event callbacks to ensure proper release of system resources.

### 3.2 Bidirectional Data Forwarding

Bidirectional data forwarding is the core function of the fork_zstream module,实现ing bidirectional audio data transmission between FreeSWITCH and WebSocket services through media bug.

#### Media Processing Callback

```c
static switch_bool_t realtimeasr_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    struct speech_thread_handle *sth = (struct speech_thread_handle *)user_data;
    uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
    switch_frame_t frame = {0};
    switch_frame_t *rframe = NULL;
    switch_size_t len;

    switch (type)
    {
    case SWITCH_ABC_TYPE_READ:
        // Read downstream audio data (ASR stream)
        if (switch_core_media_bug_read(bug, &frame, SWITCH_FALSE) != SWITCH_STATUS_FALSE)
        {
            // Process and forward to WebSocket service
            asr_feed(sth, frame.data, frame.datalen);
        }
        break;
    
    case SWITCH_ABC_TYPE_WRITE_REPLACE:
        // Process upstream audio data (TTS stream)
        rframe = switch_core_media_bug_get_write_replace_frame(bug);
        len = rframe->samples;
        // Process and play audio data
        break;
    
    // Other event handling...
    }

    return SWITCH_TRUE;
}
```

This callback function is the core of the module, responsible for handling media events and实现ing bidirectional data forwarding:
- **Downstream Data**: Capture FreeSWITCH's audio data (ASR stream) and forward it to the WebSocket service
- **Upstream Data**: Receive audio data (TTS stream) sent by the WebSocket service and play it in real-time

#### Audio Data Processing and Forwarding

```c
switch_status_t asr_feed(struct speech_thread_handle *sth, void *data, unsigned int len)
{
    switch_asr_handle_t *ah = sth->ah;
    switch_size_t orig_len = len;
    
    // Check ASR status
    sth->asrOK = 1;
    
    // Handle audio resampling
    if (ah->native_rate && ah->samplerate && ah->native_rate != ah->samplerate)
    {
        // Create resampler
        // Process audio data
    }

    // Send audio data to WebSocket service
    tts_session_sendbinary(sth->session, (char *)data, len);
    return SWITCH_STATUS_SUCCESS;
}
```

This function is responsible for processing audio data, including resampling and sending to the WebSocket service.

### 3.3 WebSocket Communication

The WebSocket communication module is responsible for establishing connections with external WebSocket services, sending and receiving audio data.

#### TTS Session Initialization

```c
switch_status_t tts_session_init(switch_core_session_t *session, event_callback_t responseHandler);
```

Initialize the TTS session, establish a connection with the WebSocket service, and set up event callback functions.

#### Send Audio Data

```c
switch_status_t tts_session_sendbinary(switch_core_session_t *session, char *data, unsigned int len);
```

Send audio data to the WebSocket service.

#### Send Text for TTS Synthesis

```c
switch_status_t tts_session_text(switch_core_session_t *session,
                                 const char *reqid, const char* type, const char* param);
```

Send text to the WebSocket service for TTS synthesis, supporting different types of TTS services.

## 4. Technical Implementation Details

### 4.1 Media Bug Technology

The fork_zstream module uses FreeSWITCH's media bug technology to capture and process audio data:

```c
if ((status = switch_core_media_bug_add(session, "realtimeasr", "realtimeasr", realtimeasr_callback, sth, 0,
                                        bug_flags | SMBF_READ_STREAM | SMBF_NO_PAUSE, &sth->bug)) !=
    SWITCH_STATUS_SUCCESS)
{
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error adding media bug for key %s\n",
                      "realtimeasr");
    return status;
}
```

By setting the `SMBF_READ_STREAM` flag, the module can capture audio stream data.

### 4.2 Audio Resampling

When the audio sampling rate does not match, the module automatically creates a resampler to ensure correct processing of audio data:

```c
if (ah->native_rate && ah->samplerate && ah->native_rate != ah->samplerate)
{
    if (!ah->resampler)
    {
        if (switch_resample_create(&ah->resampler, ah->samplerate, ah->native_rate, (uint32_t)orig_len,
                                   SWITCH_RESAMPLE_QUALITY, 1) != SWITCH_STATUS_SUCCESS)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Unable to create resampler!\n");
            return SWITCH_STATUS_GENERR;
        }
    }

    switch_resample_process(ah->resampler, data, len / 2);
    // Process resampled data
}
```

### 4.3 Thread Safety

The module uses mutexes and condition variables to ensure thread safety:

```c
// Initialize mutex
switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, pool);

// Thread-safe session access
switch_mutex_lock(globals.mutex);
session = switch_core_hash_find(globals.session_table, uuid);
switch_mutex_unlock(globals.mutex);
```

### 4.4 Command Parsing

The module supports parsing different types of commands, including starting and stopping forwarding:

```c
bool parse_command_line(switch_core_session_t *session, int argc, int real_argc, char *argv[], CommandLineArgs *args)
{
    // Parse main command (start|stop)
    // Parse request ID
    // Parse TTS type
    // Parse parameters
}
```

## 5. Application Scenarios

### 5.1 Real-time Speech Recognition System

The fork_zstream module can be used to build real-time speech recognition systems, forwarding FreeSWITCH's audio stream to speech recognition services while receiving recognition results.

### 5.2 Intelligent Voice Interaction System

By integrating the fork_zstream module, intelligent voice interaction systems can be built to implement:
- Real-time speech recognition
- Intelligent semantic understanding
- Text-to-speech response
- Real-time audio playback

### 5.3 Remote Audio Processing

The fork_zstream module can forward FreeSWITCH's audio stream to remote servers for processing, such as:
- Audio analysis
- Voice enhancement
- Noise reduction
- Other audio processing algorithms

### 5.4 Multilingual Translation System

By integrating with translation services, multilingual translation systems can be built:
- Capture user voice
- Forward to translation service
- Receive translation results
- Convert to target language voice and play

## 6. Code Optimization Suggestions

### 6.1 Error Handling Optimization

The current error handling in the code is relatively simple. It is recommended to add more detailed error handling and logging to improve the reliability and maintainability of the system.

### 6.2 Memory Management Optimization

It is recommended to use FreeSWITCH's memory pool mechanism to reduce the risk of memory leaks:

```c
// Before optimization
char *combined_str = malloc(total_length);
if (combined_str == NULL)
{
    perror("Memory allocation failed");
    return NULL;
}

// After optimization
char *combined_str = switch_core_alloc(pool, total_length);
if (combined_str == NULL)
{
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Memory allocation failed");
    return NULL;
}
```

### 6.3 Configuration Parameterization

It is recommended to change hard-coded parameters to configurable parameters to improve system flexibility:

```c
// Before optimization
#define SWITCH_RECOMMENDED_BUFFER_SIZE 8192

// After optimization
static int buffer_size = SWITCH_RECOMMENDED_BUFFER_SIZE;

// Read from configuration file when module loads
switch_conf_get_int(conf, "buffer_size", &buffer_size);
```

### 6.4 Performance Optimization

For real-time applications like audio processing, performance is crucial. Suggestions:

1. Use more efficient audio processing algorithms
2. Reduce thread switching overhead
3. Optimize WebSocket communication
4. Implement batch processing of audio data

### 6.5 Fault Tolerance Mechanism

It is recommended to add fault tolerance mechanisms to improve system stability:

1. WebSocket connection disconnection and reconnection mechanism
2. Audio data buffering and retransmission mechanism
3. Graceful handling of abnormal situations

## 7. Summary and Outlook

The fork_zstream module provides powerful real-time audio stream bidirectional forwarding functionality for FreeSWITCH,实现ing full-duplex forwarding processing of FreeSWITCH data streams through WebSocket protocol communication with external services.

### 7.1 Technical Value

- **实现ed bidirectional real-time audio data transmission between FreeSWITCH and external services**: Provides flexible audio processing capabilities for voice applications
- **Based on WebSocket protocol**:实现ed efficient communication with external services
- **Modular design**: Easy to integrate and extend
- **Real-time performance**: Ensures real-time transmission and processing of audio data

### 7.2 Future Development

1. **Support for more communication protocols**: Extend support for more types of communication protocols, such as gRPC, MQTT, etc.
2. **Enhanced audio processing capabilities**: Integrate more audio processing algorithms, such as noise reduction, voice enhancement, etc.
3. **Performance optimization**: Further optimize audio processing performance to support more concurrent sessions
4. **More configuration options**: Add more configuration options to improve system flexibility
5. **Support for more service types**: Extend support for more types of external services, such as cloud service providers' APIs

### 7.3 Conclusion

The fork_zstream module is a powerful FreeSWITCH audio stream bidirectional forwarding module that provides reliable real-time audio processing capabilities for voice applications. Through continuous optimization and extension, it is expected to become an indispensable part of the FreeSWITCH ecosystem, providing support for various voice applications.

---

**References**

- [FreeSWITCH Official Documentation](https://freeswitch.org/confluence/display/FREESWITCH/FreeSWITCH+Documentation)
- [WebSocket Protocol Specification](https://tools.ietf.org/html/rfc6455)
- [FreeSWITCH Media Handling Documentation](https://freeswitch.org/confluence/display/FREESWITCH/Media+Handling)
- [Real-time Audio Processing Technology Overview](https://en.wikipedia.org/wiki/Audio_signal_processing)