/**
 * @file mod_forkzstream.c
 * @brief FreeSWITCH module for real-time audio streaming
 *
 * This module handles real-time audio streaming functionality, including:
 * - Audio stream initialization
 * - WebSocket communication
 * - Text-to-Speech (TTS) integration
 * - Session management
 *
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#ifdef WIN32
#include <WinSock2.h>
#include <Windows.h>
#else
#include <arpa/inet.h>	///< For network address manipulation
#include <sys/socket.h> ///< For socket operations
#endif					// WIN32

#include "iforkzstream.h"	///< forkzstream interface definitions
#include "mod_forkzstream.h" ///< Module header file
#include <stdbool.h>
/// Module name constant
#define MOD_REALTIMEASR "forkzstream"

switch_status_t asr_feed(struct speech_thread_handle *sth, void *data, unsigned int len)
{
	switch_asr_handle_t *ah = sth->ah;
	switch_size_t orig_len = len;
	switch_assert(ah != NULL);
	sth->asrOK = 1;
	if (sth->asrOK == 0)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "[forkzstream]asr not ok\n");
		return SWITCH_STATUS_SUCCESS;
	}
	else
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "[forkzstream] recv data len=%d\n", len);
	}
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
		if (ah->resampler->to_len > orig_len)
		{
			if (!ah->dbuf)
			{
				void *mem;
				ah->dbuflen = ah->resampler->to_len * 2;
				mem = realloc(ah->dbuf, ah->dbuflen);
				switch_assert(mem);
				ah->dbuf = mem;
			}
			switch_assert(ah->resampler->to_len * 2 <= ah->dbuflen);
			memcpy(ah->dbuf, ah->resampler->to, ah->resampler->to_len * 2);
			data = ah->dbuf;
		}
		else
		{
			memcpy(data, ah->resampler->to, ah->resampler->to_len * 2);
		}

		len = ah->resampler->to_len;
	}

	tts_session_sendbinary(sth->session, (char *)data, len);
	return SWITCH_STATUS_SUCCESS;
}

static switch_bool_t media_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	struct speech_thread_handle *sth = (struct speech_thread_handle *)user_data;
	uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
	switch_frame_t frame = {0};
	switch_frame_t *rframe = NULL;
	switch_size_t len;
	switch_status_t st;

	frame.data = data;
	frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(switch_core_media_bug_get_session(bug)), SWITCH_LOG_ERROR,
					  "[forkzstream] mod_forkzstream bug callback type: [%d]\n", type);
	switch (type)
	{
	case SWITCH_ABC_TYPE_INIT:
	{
		switch_threadattr_t *thd_attr = NULL;

		switch_threadattr_create(&thd_attr, sth->pool);
		switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
	}
	break;

	case SWITCH_ABC_TYPE_CLOSE:
	{
		switch_status_t st;
		switch_core_session_t *session = switch_core_media_bug_get_session(bug);
		tts_session_cleanup(session);
		switch_channel_t *channel = switch_core_session_get_channel(session);

		switch_channel_set_private(channel, "realtimeasr_thread", NULL);
		if (sth->mutex && sth->cond && sth->ready)
		{
			if (switch_mutex_trylock(sth->mutex) == SWITCH_STATUS_SUCCESS)
			{
				switch_thread_cond_signal(sth->cond);
				switch_mutex_unlock(sth->mutex);
			}
		}

		switch_thread_join(&st, sth->thread);
	}
	break;

	case SWITCH_ABC_TYPE_READ:
	{
		if (switch_core_media_bug_read(bug, &frame, SWITCH_FALSE) != SWITCH_STATUS_FALSE)
		{
			if (asr_feed(sth, frame.data, frame.datalen) != SWITCH_STATUS_SUCCESS)
			{
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(switch_core_media_bug_get_session(bug)), SWITCH_LOG_ERROR,
								  "Error Feeding Data\n");
				return SWITCH_FALSE;
			}
		}
	}
	break;

	case SWITCH_ABC_TYPE_READ_REPLACE:
	{
		return SWITCH_FALSE;
	}
	break;

	case SWITCH_ABC_TYPE_WRITE_REPLACE:
	{
		return SWITCH_FALSE;
	}
	break;
	case SWITCH_ABC_TYPE_WRITE:

	{
		rframe = switch_core_media_bug_get_write_replace_frame(bug);
		len = rframe->samples;
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(switch_core_media_bug_get_session(bug)), SWITCH_LOG_ERROR,
					  "SWITCH_ABC_TYPE_WRITE TTS  Data len=%d\n", (int)len);
		if (len < rframe->samples)
		{
			memset((char *)rframe->data + (len * 2), 0, (rframe->samples - len) * 2);
		}
		rframe->datalen = rframe->samples * 2;
		switch_core_media_bug_set_write_replace_frame(bug, rframe);
	}
	break;
	default:
		break;
	}

	return SWITCH_TRUE;
}

/**
 * @brief Initialize forkzstream session
 * @param session FreeSWITCH session pointer
 * @return SWITCH_STATUS_SUCCESS on success, error code otherwise
 */
static switch_status_t forkzstream_init(switch_core_session_t *session);

/**
 * @brief Callback for handling forkzstream events
 * @param session FreeSWITCH session pointer
 * @param event_subclass Event type/subclass
 * @param json Event data in JSON format
 */
static void forkzstream_event_callback(switch_core_session_t *session, const char *event_subclass, const char *json)
{
	switch_event_t *event = NULL;
	char *uuid = switch_core_session_get_uuid(session);

	/* Create and populate event */
	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, event_subclass);
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "forkzstream-json", json);
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "forkzstream-uuid", uuid);
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Unique-ID", uuid);

	/* Fire the event */
	switch_event_fire(&event);
}
/**
 * @brief Initialize forkzstream session
 * @param session FreeSWITCH session pointer
 * @param key Session identifier
 * @param ip IP address for streaming (unused)
 * @param port Port number for streaming (unused)
 * @param keywords Text to be processed
 * @param wsurl WebSocket URL for TTS service
 * @param ah ASR handle (optional)
 * @return SWITCH_STATUS_SUCCESS on success, error code otherwise
 */
static switch_status_t forkzstream_init(switch_core_session_t *session)
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "forkzstream:: forkzstream_init in!\n");
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_status_t status;
	switch_media_bug_flag_enum_t bug_flags = 0;

	/* Check if session is already initialized */
	struct speech_thread_handle *sth = switch_channel_get_private(channel, "forkzstream_thread");
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);
	switch_codec_implementation_t read_impl = {0};
	switch_asr_handle_t *ah;
	int rate = 0;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "forkzstream:: forkzstream_init in2!\n");

	if (sth)
	{
		/* Session already initialized */
		return SWITCH_STATUS_SUCCESS;
	}

	/* Allocate ASR handle if not provided */

	if (!(ah = switch_core_session_alloc(session, sizeof(*ah))))
	{
		return SWITCH_STATUS_MEMERR;
	}

	/* Get codec information */
	switch_core_session_get_read_impl(session, &read_impl);

	/* Setup memory pool */
	if (pool)
	{
		ah->memory_pool = pool;
	}
	else
	{
		if ((status = switch_core_new_memory_pool(&ah->memory_pool)) != SWITCH_STATUS_SUCCESS)
		{
			UNPROTECT_INTERFACE(ah->asr_interface);
			return status;
		}
		switch_set_flag(ah, SWITCH_ASR_FLAG_FREE_POOL);
	}

	/* Configure audio parameters */
	rate = read_impl.actual_samples_per_second;
	ah->rate = rate;
	ah->name = "a";
	ah->samplerate = rate;
	ah->native_rate = rate;

	/* Initialize speech thread handle */
	sth = switch_core_session_alloc(session, sizeof(*sth));
	sth->pool = switch_core_session_get_pool(session);
	sth->session = session;
	sth->ah = ah;

	/* Store session information */
	switch_channel_set_private(channel, "forkzstream_thread", sth);
	switch_channel_set_private(channel, MY_BUG_NAME, sth->bug);

	/* Initialize TTS session */

	tts_session_init(session, forkzstream_event_callback);

	if ((status = switch_core_media_bug_add(session, "realtimeasr", "realtimeasr", media_callback, sth, 0,
											bug_flags | SMBF_READ_STREAM | SMBF_NO_PAUSE, &sth->bug)) !=
		SWITCH_STATUS_SUCCESS)
	{
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error adding media bug for key %s\n",
						  "realtimeasr");
		return status;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "forkzstream:: forkzstream bub added ok!\n");

	return SWITCH_STATUS_SUCCESS;
}

/**
 * @brief Validate IP address (stub implementation)
 * @param ip IP address to validate
 * @return Always returns 0 (not implemented)
 */
int validate_ip(const char *ip) { return 0; }

/**
 * @brief Validate port number
 * @param port Port number to validate
 * @return 1 if valid (1-65535), 0 otherwise
 */
int validate_port(int port) { return (port > 0 && port <= 65535); }

/**
 * @brief Validate WebSocket URL format
 * @param url URL to validate
 * @return 1 if valid, 0 otherwise
 *
 * Validates:
 * - Starts with ws:// or wss://
 * - Contains valid hostname or IP
 * - Optional port number (if present, must be valid)
 * - Optional path
 */
int is_valid_websocket_url(const char *url)
{
	const char *ws_prefix = "ws://";
	const char *wss_prefix = "wss://";
	int is_ws = strstr(url, ws_prefix) == url;
	int is_wss = strstr(url, wss_prefix) == url;

	/* Check protocol prefix */
	if (!is_ws && !is_wss)
	{
		return 0;
	}

	/* Get host start position */
	const char *host_start = url + (is_ws ? strlen(ws_prefix) : strlen(wss_prefix));
	if (*host_start == '\0')
	{
		return 0;
	}

	/* Handle IPv6 addresses */
	if (*host_start == '[')
	{
		const char *ipv6_end = strchr(host_start, ']');
		if (!ipv6_end)
		{
			return 0;
		}

		char ipv6[128];
		size_t len = ipv6_end - host_start - 1;
		if (len >= sizeof(ipv6))
			return 0;
		strncpy(ipv6, host_start + 1, len);
		ipv6[len] = '\0';

		if (!validate_ip(ipv6))
		{
			return 0;
		}
		host_start = ipv6_end + 1;
	}

	/* Extract host and port */
	const char *port_start = strchr(host_start, ':');
	const char *path_start = strchr(host_start, '/');

	char host[256];
	const char *host_end = port_start ? port_start : (path_start ? path_start : host_start + strlen(host_start));
	size_t host_len = host_end - host_start;
	if (host_len >= sizeof(host))
		return 0;
	strncpy(host, host_start, host_len);
	host[host_len] = '\0';

	/* Validate hostname */
	if (*host_start != '[' && !validate_ip(host))
	{
		for (const char *p = host; *p; p++)
		{
			if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-')
			{
				return 0;
			}
		}
	}

	/* Validate port if present */
	if (port_start)
	{
		char *end;
		long port = strtol(port_start + 1, &end, 10);

		if (end != path_start && (*end != '\0' && *end != '/'))
		{
			return 0;
		}
		if (!validate_port((int)port))
		{
			return 0;
		}
	}

	return 1;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 主命令结构体
typedef struct
{
	char *command; // "tts" 或 "stop"
	char *reqid;
	char *type;	  // 类型
	char *params; // 参数（根据类型使用联合体的不同成员）
} CommandLineArgs;

char *cat_last_n(switch_core_session_t *session, int argc, char *argv[], int n)
{
	char *ret_args = NULL;
	size_t total_length = 0;
	for (int i = n; i < argc; i++)
	{
		total_length += strlen(argv[i]);
		if (i < argc - 1)
			total_length++; // 为空格留出空间，或其他分隔符
	}
	total_length++; // 为字符串终止符 '\0' 留出空间

	// 2. 分配足够内存
	char *combined_str = malloc(total_length);
	if (combined_str == NULL)
	{
		perror("Memory allocation failed");
		return NULL;
	}
	combined_str[0] = '\0'; // 初始化为空字符串

	// 3. 使用 snprintf 安全拼接
	int offset = 0; // 跟踪当前写入位置
	for (int i = n; i < argc; i++)
	{
		printf("argc=%d,i=%d,str=%s\n", argc, i, combined_str);
		// 计算剩余空间，并安全写入
		int result = snprintf(combined_str + offset, total_length - offset, "%s", argv[i]);
		if (result < 0 || result >= total_length - offset)
		{
			// 处理写入错误或截断
			free(combined_str);
			return NULL;
		}
		offset += result;

		// 添加分隔符（如空格），如果不是最后一个元素
		if (i < argc - 1)
		{
			result = snprintf(combined_str + offset, total_length - offset, " ");
			if (result < 0 || result >= total_length - offset)
			{
				free(combined_str);
				return NULL;
			}
			offset += result;
		}
	}

	printf("Combined string: %s\n", combined_str);
	ret_args = switch_core_session_strdup(session, combined_str);
	// 4. 释放内存
	free(combined_str);
	return ret_args;
}
bool parse_command_line(switch_core_session_t *session, int argc, int real_argc, char *argv[], CommandLineArgs *args)
{
	printf("argv 0 %s\n", argv[0]);
	printf("argv 1 %s\n", argv[1]);
	printf("argv 2 %s\n", argv[2]);
	int current_index = 1; // 跳过程序名

	// 1. 解析主命令
	if (current_index >= argc)
	{
		fprintf(stderr, "错误：缺少命令 (tts|stop)\n");
		return false;
	}
	if (strcmp(argv[current_index], "start") == 0)
	{
		args->command = "start";
		current_index++;

		if (current_index >= argc)
		{
			fprintf(stderr, "stop没有reqid\n");
			return false;
		}
		args->reqid = argv[current_index];
		current_index++;
	}
	else if (strcmp(argv[current_index], "stop") == 0)
	{
		args->command = "stop";
		current_index++;
		if (current_index >= argc)
		{
			fprintf(stderr, "stop没有reqid\n");
			return false;
		}
		args->reqid = argv[current_index];
		// stop 命令通常不需要其他参数
		return true;
	}
	else
	{
		fprintf(stderr, "错误：未知命令 '%s'\n", argv[current_index]);
		return false;
	}

	// 2. 解析类型
	if (current_index >= argc)
	{
		fprintf(stderr, "错误：tts 命令需要指定类型 (file|http|websocket)\n");
		return false;
	}
	args->type = argv[current_index];
	current_index++;
	if (current_index >= argc)
	{
		fprintf(stderr, "没有参数\n");
		args->params = NULL;
	}
	else
	{
		args->params = cat_last_n(session, real_argc, argv, current_index); // argv[current_index];
		fprintf(stderr, "参数%s\n", args->params);
	}

	// 检查是否还有未处理的参数
	/*if (current_index < argc) {
		fprintf(stderr, "警告：存在未使用的参数 '%s'\n", argv[current_index]);
		}*/
	return true;
}
void the_test();
/**
 * @brief FreeSWITCH application for starting forkzstream
 * @param session FreeSWITCH session pointer
 * @param data Input parameters in format: uuid#tts|stop#spkid#speed#text#wsurl#modelid#reqid
 *
 * Handles both TTS playback and stop commands:
 * - tts: Initializes session and starts TTS playback
 * - stop: Stops current TTS playback
 */
SWITCH_STANDARD_APP(forkzstream_start)
{
	char *array[10240] = {0};
	char *session_key = NULL;
	char *tmpcmd = NULL;
	char *ori_args = NULL;

	char *spkid = NULL;
	char *speed = NULL;
	char *keywords = NULL;
	char *wsurl = NULL;
	char *modelid = NULL;
	char *reqid = NULL;
	int argc;

	/* Log input data */
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 启动，启动参数:%s\n", data);
	// printf("!!!!!!!!!forkzstream_start data=%s\n", data);
	if (zstr(data) || strlen(data) >= 10240)
	{
		// switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "forkzstream params are too long >10240, %s\n", data);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 启动参数出错，太大，退出！\n");
		return;
	}

	/* Duplicate and parse input arguments */
	ori_args = switch_core_session_strdup(session, data);
	// switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "now in forkzstream %s\n", ori_args);
	argc = 5; // zforkzstream tts|stop reqid file|ws {'url':'...'}

	CommandLineArgs args = {0};
	int real_argc = switch_split(ori_args, ' ', array);
	session_key = array[0];
	if (!parse_command_line(session, argc, real_argc, array, &args))
	{
		//	fprintf(stderr, "命令行解析失败。\n");
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 命令行解析失败，退出！\n");
		return 1;
	}

	/* 根据解析得到的 args 执行相应操作 */
	if (strcmp(args.command, "start") == 0)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 接收到tts命令类型\n");
		if (session)
		{
			/* Add session to global table */
			switch_mutex_lock(globals.mutex);
			switch_core_hash_insert(globals.session_table, session_key, session);
			switch_mutex_unlock(globals.mutex);
		}

		/* Initialize and start TTS */
		forkzstream_init(session);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app初始化成功\n");
		tts_session_text(session, args.reqid, args.type, args.params);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 合成text成功\n");
	}
	else if (strcmp(args.command, "stop") == 0)
	{
		tts_session_cleanup(session);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 接收stop停止命令\n");
	}
}
// 事件回调函数event
/**
 * @brief Callback for handling FreeSWITCH events
 * @param event Event structure containing event details
 *
 * Handles channel hangup events to cleanup TTS sessions
 */
static void switch_event_callback(switch_event_t *event)
{
	switch_core_session_t *session = NULL;
	const char *event_name = switch_event_name(event->event_id);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 事件回调，事件:%s\n", event_name);
	/* Handle channel hangup events */
	if (event->event_id == SWITCH_EVENT_CHANNEL_HANGUP)
	{
		char *uuid = switch_event_get_header(event, "Unique-ID");
		const char *cause = switch_event_get_header(event, "Hangup-Cause");

		/* Find session in global table */
		switch_mutex_lock(globals.mutex);
		session = switch_core_hash_find(globals.session_table, uuid);
		switch_mutex_unlock(globals.mutex);

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 接收到挂断事件，SWITCH_EVENT_CHANNEL_HANGUP: UUID=%s, Cause=%s\n", uuid, cause);

		/* Cleanup TTS session if found */
		if (session)
		{
			tts_session_cleanup(session);
		}
		else
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream app 接收到挂断事件，Session为空，出错！\n");
		}
	}
}
/*
SWITCH_MODULE_RUNTIME_FUNCTION(mod_example_runtime);
*/

SWITCH_MODULE_LOAD_FUNCTION(mod_forkzstream_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_forkzstream_shutdown);
SWITCH_MODULE_DEFINITION(mod_forkzstream, mod_forkzstream_load, mod_forkzstream_shutdown, NULL);

/**
 * @brief Module load function
 * @param pool Memory pool for module initialization
 * @param modname Module name
 * @return SWITCH_STATUS_SUCCESS on success
 *
 * Initializes module globals and registers application interface
 */
SWITCH_MODULE_LOAD_FUNCTION(mod_forkzstream_load)
{
	// the_test();
	switch_application_interface_t *app_interface;

	/* Initialize global structures */
	switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, pool);
	globals.pool = pool;
	switch_core_hash_init(&(globals.session_table));

	/* Create module interface */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	/* Register forkzstream application */
	SWITCH_ADD_APP(app_interface, "forkzstream", "real time asr", "real time asr to UDP server", forkzstream_start,
				   "[name]", SAF_NONE);

	/* Bind to channel hangup events */
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[forkzstream] forkzstream app,This is real time asr module!!!!!\n");
	switch_event_bind("forkzstream", SWITCH_EVENT_CHANNEL_HANGUP, SWITCH_EVENT_SUBCLASS_ANY, switch_event_callback,
					  NULL);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[forkzstream] forkzstream app Module loaded. Listening for session end events.\n");

	return SWITCH_STATUS_SUCCESS;
}

/**
 * @brief Module shutdown function
 * @return SWITCH_STATUS_SUCCESS on success
 *
 * Cleans up module resources and unbinds events
 */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_forkzstream_shutdown)
{
	/* Unbind event callback */
	switch_event_unbind_callback(switch_event_callback);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[forkzstream] forkzstream app Module unloaded. Stopped listening for events.\n");
	// printf("SWITCH_MODULE_SHUTDOWN_FUNCTION called !!!!!!");

	return SWITCH_STATUS_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:nil
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet expandtab:
 */
