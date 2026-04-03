#ifndef AUDIO_STREAMER_GLUE_H
#define AUDIO_STREAMER_GLUE_H
#include "mod_forkzstream.h"

/**
 * @file iforkzstream.h
 * @brief Interface definitions for forkzstream TTS functionality
 *
 * This header file defines the public interface for Text-to-Speech (TTS) functionality
 * provided by the forkzstream module, including session management and audio streaming.
 */

/**
 * @brief Initialize TTS session
 * @param session FreeSWITCH session pointer
 * @param responseHandler Callback function for handling events
 * @return SWITCH_STATUS_SUCCESS on success, error code otherwise
 */
switch_status_t tts_session_init(switch_core_session_t *session,event_callback_t responseHandler);

/**
 * @brief Send text for TTS synthesis
 * @param session FreeSWITCH session pointer
 * @param reqid Request ID for tracking
 * @param type Type of TTS service (file|ws)
 * @param param Parameters for TTS service (JSON format)
 * @return SWITCH_STATUS_SUCCESS on success, error code otherwise
 */
switch_status_t tts_session_text(switch_core_session_t *session,
									 const char *reqid,const char* type,const char* param);

/**
 * @brief Stop TTS session
 * @param session FreeSWITCH session pointer
 * @param reqid Request ID to stop
 * @return SWITCH_STATUS_SUCCESS on success, error code otherwise
 */
switch_status_t tts_session_stop(switch_core_session_t *session,char* reqid);

/**
 * @brief Cleanup TTS session resources
 * @param session FreeSWITCH session pointer
 * @return SWITCH_STATUS_SUCCESS on success, error code otherwise
 */
switch_status_t tts_session_cleanup(switch_core_session_t *session);

/**
 * @brief Read audio data from TTS stream
 * @param session FreeSWITCH session pointer
 * @param data Buffer to store audio data
 * @param len Pointer to length of data (input/output)
 * @return SWITCH_STATUS_SUCCESS on success, error code otherwise
 */
switch_status_t tts_read_data(switch_core_session_t *session,void *data, switch_size_t *len);

#endif //AUDIO_STREAMER_GLUE_H
