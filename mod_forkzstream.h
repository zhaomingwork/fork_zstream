#ifndef MOD_AUDIO_STREAM_H
#define MOD_AUDIO_STREAM_H

#include <switch.h>

/**
 * @file mod_forkzstream.h
 * @brief FreeSWITCH module for audio streaming functionality
 */

/* Module name constant */
#define MY_BUG_NAME "forkzstream"

/* Event type definitions */
#define EVENT_CONNECT           "mod_forkzstream::connect"      ///< Connection established event
#define EVENT_DISCONNECT        "mod_forkzstream::disconnect"  ///< Connection terminated event
#define EVENT_ERROR             "mod_forkzstream::error"       ///< Error occurred event
#define EVENT_JSON              "mod_forkzstream::json"         ///< JSON message received event
#define EVENT_PLAY              "mod_forkzstream::play"         ///< Audio playback event
/**
 * @struct speech_thread_handle
 * @brief Handle for speech processing thread
 * 
 * Contains all necessary components for managing a speech processing thread
 * within FreeSWITCH environment.
 */
struct speech_thread_handle {
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
};


typedef struct speech_thread_handle private_t;  ///< Alias for speech_thread_handle

/**
 * @typedef event_callback_t
 * @brief Callback function type for handling events
 * @param session FreeSWITCH session pointer
 * @param event_subclass Event type/subclass
 * @param json Event data in JSON format
 */
typedef void (*event_callback_t)(switch_core_session_t* session, const char* event_subclass, const char* json);

/**
 * @enum notifyEvent_t
 * @brief Enumeration of notification event types
 */
enum notifyEvent_t {
    CONNECT_SUCCESS,     ///< Connection established successfully
    CONNECT_ERROR,       ///< Connection failed
    CONNECTION_DROPPED,  ///< Connection was dropped
    MESSAGE,            ///< Text message received
	BINARY,             ///< Binary data received
	INFO                ///< Informational message
};

/**
 * @struct forkzstream_session_info_s
 * @brief Session information structure
 */
typedef struct forkzstream_session_info_s {
	switch_core_session_t *session;  ///< Pointer to FreeSWITCH session
	char uuid[40];                  ///< Session UUID string
} forkzstream_session_info_t;

/**
 * @struct session_manager_t
 * @brief Global session manager structure
 * 
 * Manages all active sessions with thread-safe access
 */
typedef struct {
	switch_mutex_t *mutex;          ///< Mutex for thread safety
	switch_hash_t *session_table;   ///< Hash table for session storage
	switch_memory_pool_t *pool;     ///< Memory pool for manager resources
} session_manager_t;

/// Global session manager instance
static session_manager_t globals = {0};
 

#endif //MOD_AUDIO_STREAM_H
