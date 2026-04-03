/**
 * @file forkzstream.cpp
 * @brief TTS音频流处理实现
 * 
 * 实现了TTS（Text-to-Speech）音频流的播放和管理功能，包括：
 * - WebSocket通信
 * - 音频数据队列管理
 * - 实时音频播放
 * - 会话管理和事件处理
 */

#include "HandlerFactory.h"
#include "mod_forkzstream.h"
#include "nlohmann/json.hpp"

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <switch_buffer.h>
#include <switch_json.h>
#include <vector>
#define ASIO_STANDALONE 1

#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include "asio.hpp"
#include <regex>
#include "WebSocketHandler.h"

#define FRAME_SIZE_8000 320

/**
 * @brief 去除字符串首尾空白字符
 * @param str 输入字符串
 * @return 去除空白后的字符串
 */
std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}
/**
 * @class TTsStreamer
 * @brief 处理TTS音频流的播放和管理
 */
class TTsStreamer
{
	public:
	TTsStreamer() {}

	// 删除拷贝构造和赋值运算符
	TTsStreamer(const TTsStreamer &) = delete;
	TTsStreamer &operator=(const TTsStreamer &) = delete;
	std::string handleJson(std::string invalid_json)
	{
		return invalid_json;
      try{
		 std::string input = invalid_json;
   if (input.front() == '{') input = input.substr(1);
    if (input.back() == '}') input = input.substr(0, input.size() - 1);

    std::vector<std::string> keys = {"speed", "spk_id", "req_id", "model_id", "url", "text"};

    std::map<std::string, std::string> result;

    std::vector<std::pair<std::string, size_t>> key_positions;

    for (const auto& key : keys) {
        std::string key_with_colon = key + ":";
        size_t pos = input.find(key_with_colon);
        if (pos != std::string::npos) {
            key_positions.push_back({key, pos});
        }
    }

    std::sort(key_positions.begin(), key_positions.end(), 
              [](const auto& a, const auto& b) { return a.second < b.second; });

    for (size_t i = 0; i < key_positions.size(); ++i) {
        std::string key = key_positions[i].first;
        size_t key_pos = key_positions[i].second;
        size_t value_start = key_pos + key.length() + 1; 
        size_t value_end;

        if (i < key_positions.size() - 1) {
            value_end = key_positions[i+1].second;
            std::string value_str = input.substr(value_start, value_end - value_start);
            value_str = trim(value_str);
            if (!value_str.empty() && value_str.back() == ',') {
                value_str = value_str.substr(0, value_str.size() - 1);
            }
            value_str = trim(value_str);
            result[key] = value_str;
        } else {
            std::string value_str = input.substr(value_start);
            value_str = trim(value_str);
            result[key] = value_str;
        }
    }
    nlohmann::json newjson;
    for (const auto& kv : result) {
        std::cout << "Key: " << kv.first << ", Value: " << kv.second << std::endl;
        newjson[kv.first] = kv.second;
	}

		return newjson.dump();
	  }catch(const std::exception& e){
		std::cout<<"handleJson error:"<<e.what()<<std::endl;
		return "";
	  }
	}

/**
 * @brief 获取单例实例
 */
static TTsStreamer *getInstance()
{
	static TTsStreamer instance; // C++11保证线程安全
	return &instance;
}
/**
 * @brief 初始化会话
 * @param uuid 会话ID
 * @param callback 事件回调函数
 */
void initsessions(const char *uuid, event_callback_t callback)
{
	
	m_sessionId = uuid;
	m_notify = callback;
}

/**
 * @brief TTS事件回调处理
 * @param event 事件类型
 * @param message 事件消息
 * @param length 消息长度
 * @param reqid 请求ID
 */
void eventTtsCallback(const char *event, const char *message, size_t length, const char *reqid)
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
					  "[forkzstream] forkzstream eventTtsCallback,event=%s,msg=%s\n",event,message);
	std::string event_str(event);

	std::string result = "";
	std::string msg(message ? message : "no message");

		if(event_str=="cmd")
	{
		if(msg=="interrupt_clean")
		{
			this->ttsQueue.clear();
              is_interrupt.store(true,std::memory_order_release);
		}
	    if(msg=="interrupt_only")
		{
               is_interrupt.store(true,std::memory_order_release);
		}
 
	    if(msg=="resume")
		{
               is_interrupt.store(false,std::memory_order_release);
		}
		return;

	}
    std::replace(msg.begin(), msg.end(), '"', ' ');
	std::replace(msg.begin(), msg.end(), '{', ' ');
	std::replace(msg.begin(), msg.end(), '}', ' ');
	std::replace(msg.begin(), msg.end(), ',', ' ');
	std::cout<<"msg="<<msg<<std::endl;

	switch_core_session_t *psession = switch_core_session_locate(m_sessionId.c_str());

	if (event_str == "error") {
		result = "{\"reqid\":\"" + std::string(reqid) + "\",\"type\":\"error\",\"info\":\"" + msg + "\"}";
        std::cout<<"error!!!!thread will out!!!result="<<result<<std::endl;
		thread_run.store(false, std::memory_order_release);
	} else {
		result = "{\"reqid\":\"" + std::string(reqid) + "\",\"type\":\"ok\",\"info\":\"" + msg + "\"}";
	}

	if (psession) {

		m_notify(psession, "mod_forkzstream::event", result.c_str());
		switch_core_session_rwunlock(psession);
	}
}
/**
 * @brief 检查字符串是否以指定前缀开头
 */
bool startsWith(const std::string &str, const std::string &prefix)
{
	if (prefix.length() > str.length()) return false;
	return str.substr(0, prefix.length()) == prefix;
}

/**
 * @brief 音频播放线程
 * @param session 会话指针
 * @param reqid 请求ID
 * @return 播放状态
 */
switch_status_t z_play_thread(switch_core_session *session, std::string reqid)
{
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
					  "[forkzstream] forkzstream cpp session ok ,switch_ivr_play_file 线程启动!,reqid=%s\n",reqid.c_str());

	switch_channel *channel;
	char *file;
	const char *timer_name = "soft_timer";
	short buf[960];
	char dtmf[128];
	uint32_t interval = 0, samples = 0;
	uint32_t samplerate = 8000, framelen = 0;
	uint32_t channels = 1;
	size_t len = 0, ilen = 0;
	switch_frame_t write_frame = {0};
	switch_timer_t timer = {0};
	switch_core_thread_session thread_session;
	switch_codec_t codec = {0};

	// switch_file_handle fh;
	char *codec_name;
	int x;
	int stream_id;
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);
	switch_codec_implementation_t read_impl;

	channel = switch_core_session_get_channel(session);
	switch_core_session_get_read_impl(session, &read_impl);
	assert(channel != NULL);

	switch_channel_answer(channel);
	switch_channel_audio_sync(channel);
	timer_name = switch_channel_get_variable(channel, "timer_name");
	write_frame.data = buf;
	write_frame.buflen = sizeof(buf);

	if (!switch_channel_media_ready(channel)) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "[forkzstream] forkzstream cpp switch_channel_media_ready not !\n");
		m_notify(session, "mod_forkzstream::event",
				 "{\"type\":\"error\",\"info\":\"switch_channel_media_not ready \"}");
		return SWITCH_STATUS_FALSE;
	}
	interval = read_impl.microseconds_per_packet / 1000;
	channels = read_impl.number_of_channels;
	codec_name = "L16";
	samples = read_impl.samples_per_packet;
	framelen = read_impl.encoded_bytes_per_packet;
	channels = read_impl.number_of_channels;
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
					  "[forkzstream] forkzstream cpp OPEN FILE framelen=%d samples=%d  channels  %d interval=%d\n", framelen, samples, channels,
					  interval);

	if (switch_core_codec_init(&codec, codec_name, NULL, NULL, 8000, interval, read_impl.number_of_channels,
							   SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE, NULL,
							   pool) == SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Raw Codec Activated\n");
		write_frame.codec = &codec;
		samples = codec.implementation->samples_per_packet;
		framelen = codec.implementation->decoded_bytes_per_packet;
		channels = codec.implementation->number_of_channels;
	} else {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
						  "[forkzstream] forkzstream cpp Raw Codec Activation Failed %s@%dhz %d channels %dms\n", codec_name, samplerate, channels,
						  interval);
		m_notify(session, "mod_forkzstream::event", "{\"type\":\"error\",\"info\":\"Raw Codec Activation Failed  \"}");
		return SWITCH_STATUS_GENERR;
	}

	if (timer_name) {
		if (switch_core_timer_init(&timer, timer_name, interval, samples, pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "setup timer failed!\n");
			switch_core_codec_destroy(&codec);
			return SWITCH_STATUS_GENERR;
		}
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
						  "[forkzstream] forkzstream cpp setup timer success %d bytes per %d ms!\n", len, interval);
	}
	write_frame.rate = samplerate;

	if (timer_name) {
		switch_core_service_session(session);
	}

	ilen = framelen;
    int totoalloop=100000;
	while (totoalloop>0 && switch_channel_get_state(channel) == CS_EXECUTE && thread_run.load(std::memory_order_acquire) == true) {
        totoalloop=totoalloop-1;
		int done = 0;

		size_t thesize = framelen;
		std::vector<char> chunk;
		int ret = ttsQueue.try_pop(chunk, thesize);
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp 尝试从队列里获取数据，获取结果:%d\n",ret);
		
		if(is_interrupt.load(std::memory_order_acquire) == true)
		{
			switch_sleep(1);
			continue;
		}
		if (ret == 0) {
			std::memcpy(buf, chunk.data(), framelen);
			ilen = framelen;
		} else if (ret == 1) {
			switch_sleep(1000);
			continue;
		} else {
           switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp 检测到结束标记，发送结束信息给服务端:%d\n",ret);
		   	if (WebSocketHandler* derived = dynamic_cast<WebSocketHandler*>(Handler.get())) {
        derived->writeText("detect_end");
    }
		   switch_sleep(1);
			continue;
			if (!is_playback.load(std::memory_order_acquire)) {
				thread_run.store(false, std::memory_order_release);
			}
			break;
		}

		write_frame.datalen = (uint32_t)ilen;
		write_frame.samples = (uint32_t)ilen / 2;
		write_frame.buflen = (uint32_t)ilen;
		write_frame.rate = 8000;
		write_frame.channels = 1;
#ifdef SWAP_LINEAR
		switch_swap_linear(write_frame.data, (int)write_frame.datalen / 2);
#endif

		int32_t volgranular = 4;
		int32_t vol = 4;
		switch_change_sln_volume_granular((int16_t *)write_frame.data, write_frame.datalen / 2, volgranular);

		if (switch_core_session_write_frame(session, &write_frame, SWITCH_IO_FLAG_NONE, 0) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp 实时播放线程里switch_core_session_write_frame出错\n");
			switch_sleep(1000);
			continue;
		}
		switch_ivr_parse_all_messages(session);
		if (timer_name) {
			if ((x = switch_core_timer_next(&timer)) < 0) { break; }
		} else {
			switch_frame *read_frame;
			if (switch_core_session_read_frame(session, &read_frame, -1, 0) != SWITCH_STATUS_SUCCESS) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp 实时播放线程里switch_core_session_read_frame出错\n");
				switch_sleep(1000);
				continue;
			}
		}
	}

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "[forkzstream] forkzstream cpp 一次播放的线程将要结束\n");
	std::string result_json = "{\"reqid\":\"" + reqid + "\",\"type\":\"finish\",\"info\":\"tts session finish\"}";
	m_notify(session, "mod_forkzstream::event", result_json.c_str());
	switch_core_codec_destroy(&codec);

	if (timer_name) {
		switch_core_thread_session_end(session);
		switch_core_timer_destroy(&timer);
	}
	this->Handler->cancel();
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "[forkzstream] forkzstream cpp 一次播放的线程结束,reqid=%s\n",reqid.c_str());
	
	return status;
}

/**
 * @brief 停止播放线程
 */
void stop_play_thread()
{
	 switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp 停止实时语音播放线程\n");
	thread_run.store(false, std::memory_order_release);
	if (play_thread.joinable()) { play_thread.join(); }
	ttsQueue.clear();
}

void start_play_tts_thread(switch_core_session_t *session, std::string reqid)
{
	 switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp 开启实时语音播放线程\n");
	thread_run.store(true, std::memory_order_release);
	play_thread = std::thread([this, session, reqid] { this->z_play_thread(session, reqid); });
}

~TTsStreamer() { this->stop_play_thread(); }

/**
 * @brief 创建下载器并开始下载
 * @param surl 源URL
 * @param text 要合成的文本
 * @param spkid 说话人ID
 * @param speed 语速
 * @param modelid 模型ID
 * @param reqid 请求ID
 */
void create_stream(std::string reqid, std::string type, std::string param)
{
    try{
	   switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] 进入create_stream,type:%s,param:%s\n",type.c_str(),param.c_str());
	nlohmann::json param_json;
	 

	if (type == "ws") { 
		  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp 进入ws分支\n");
		Handler = HandlerFactory::create(HandlerFactory::DownloadType::WEBSOCKET); 
	}

	auto callback = std::bind(&TTsStreamer::eventTtsCallback, this, std::placeholders::_1, std::placeholders::_2,
						  std::placeholders::_3, std::placeholders::_4);
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp websocketHandler参数:%s\n",param.c_str());
    if(param[0]!='{')
    {
        param="{\"url\":\""+param+"\"}";
    }
	 switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp websocketHandler处理后参数:%s\n",param.c_str());
	std::string new_param=handleJson(param);
		 if(new_param=="")
		 {
			std::cout << "param error="<<param<<std::endl;
			 switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp websocketHandler参数出错:%s\n",param.c_str());
			return;
		 }
 switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] forkzstream cpp websocketHandler最终参数:%s\n",new_param.c_str());
    nlohmann::json params_json = nlohmann::json::parse(new_param);
    params_json["session_id"]=this->m_sessionId;
	this->callId=params_json["callId"];
	this->tenantId=params_json["tenantId"];
	this->botid=params_json["botid"];
	this->fsInstanceId=params_json["fsInstanceId"];
    params_json["req_id"]=reqid;
    Handler->startHandler(this->ttsQueue, callback, params_json.dump(),this->thread_run);
    }
    catch(const std::exception& e)
    {
		 switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] create_stream 出错:%s\n",e.what());
    }
}

private:
std::string m_sessionId;
std::string callId;
std::string tenantId;
std::string botid;
std::string fsInstanceId;

event_callback_t m_notify;
std::atomic<bool> thread_run{false};
WavDataQueue ttsQueue;
mutable std::mutex mutex_;
std::atomic<bool> is_playback{false};

std::atomic<bool> is_interrupt{false}; 

public:
std::thread play_thread;				 // 播放线程
std::unique_ptr<IHandler> Handler; // 下载器指针
}
;

// static TTsStreamer static_tts;
extern "C" {


switch_status_t stream_session_send_data(switch_core_session_t *session, uint8_t *buffer, size_t len)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	auto *bug = (switch_media_bug_t *)switch_channel_get_private(channel, MY_BUG_NAME);
	if (!bug) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
						  "[forkzstream] forkzstream cpp stream_session_send_text failed because no bug\n");
		return SWITCH_STATUS_FALSE;
	}
	auto *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);

	if (!tech_pvt) return SWITCH_STATUS_FALSE;
	//auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);
	//if (pAudioStreamer && buffer && len > 0) pAudioStreamer->writeBinary(buffer, len);

	return SWITCH_STATUS_SUCCESS;
}
/**
 * @brief 清理TTS会话资源
 * @param session 会话指针
 * @return 状态码
 */
switch_status_t tts_session_cleanup(switch_core_session_t *session)
{
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
						  "[forkzstream] forkzstream cpp 清空该session \n");
	switch_channel_t *channel = switch_core_session_get_channel(session);
	private_t *tech_pvt = (private_t *)switch_channel_get_private(channel, "forkzstream_thread");

	if (!tech_pvt) { 
		
					switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
						  "[forkzstream] forkzstream cpp tech_pvt为空，返回 \n");
		
		return SWITCH_STATUS_FALSE; }

	if (tech_pvt->pTTsStreamer) {
		auto *as = (TTsStreamer *)tech_pvt->pTTsStreamer;
		tech_pvt->pTTsStreamer = nullptr;
		if (as) {
			as->stop_play_thread();
			delete as;
		}
	}

	return SWITCH_STATUS_SUCCESS;
}

/**
 * @brief 停止TTS会话
 * @param session 会话指针
 * @param reqid 请求ID
 * @return 状态码
 */
switch_status_t tts_session_stop(switch_core_session_t *session, char *reqid)
{
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
						  "[forkzstream] forkzstream cpp 停止该session \n");
	switch_channel_t *channel = switch_core_session_get_channel(session);
	private_t *tech_pvt = (private_t *)switch_channel_get_private(channel, "forkzstream_thread");

	if (!tech_pvt) { 

			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
						  "[forkzstream] forkzstream cpp tech_pvt为空，返回 \n");
		return SWITCH_STATUS_FALSE; 
		}

	if (tech_pvt->pTTsStreamer) {
		auto *as = (TTsStreamer *)tech_pvt->pTTsStreamer;
		//as->eventTtsCallback("CONNECTION_DROPPED", "no message", 0, reqid);
		as->stop_play_thread();
	}

	return SWITCH_STATUS_SUCCESS;
}


switch_status_t tts_session_sendbinary(switch_core_session_t *session,   char *buf, size_t len)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	private_t *tech_pvt = (private_t *)switch_channel_get_private(channel, "forkzstream_thread");

	if (!tech_pvt || !tech_pvt->pTTsStreamer) { return SWITCH_STATUS_FALSE; }

	auto *as = (TTsStreamer *)tech_pvt->pTTsStreamer;
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
						  "[forkzstream] forkzstream cpp 发送数据 %d\n",len);

	WebSocketHandler* Handler = dynamic_cast<WebSocketHandler*>(as->Handler.get());
	Handler->sendbinary(buf,len);

	return SWITCH_STATUS_SUCCESS;
}

/**
 * @brief 发送TTS文本进行合成
 * @param session 会话指针
 * @param wsurl WebSocket URL
 * @param text 要合成的文本
 * @param spkid 说话人ID
 * @param speed 语速
 * @param modelid 模型ID
 * @param reqid 请求ID
 * @return 状态码
 */
switch_status_t tts_session_text(switch_core_session_t *session, const char *reqid, const char *type, const char *param)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	private_t *tech_pvt = (private_t *)switch_channel_get_private(channel, "forkzstream_thread");

	if (!tech_pvt || !tech_pvt->pTTsStreamer) { return SWITCH_STATUS_FALSE; }

	auto *as = (TTsStreamer *)tech_pvt->pTTsStreamer;
	as->stop_play_thread();
	as->start_play_tts_thread(session, reqid);
	as->create_stream(std::string(reqid), std::string(type), std::string(param));

	return SWITCH_STATUS_SUCCESS;
}

/**
 * @brief 初始化TTS会话
 * @param session 会话指针
 * @param responseHandler 响应回调函数
 * @param keywords 关键词(未使用)
 * @param wsurl WebSocket URL(未使用)
 * @return 状态码
 */
switch_status_t tts_session_init(switch_core_session_t *session, event_callback_t responseHandler)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	struct speech_thread_handle *sth =
		(struct speech_thread_handle *)switch_channel_get_private(channel, "forkzstream_thread");

	if (!sth) { 
		
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
						  "[forkzstream] forkzstream cpp 没找到sth,返回 \n");
		return SWITCH_STATUS_SUCCESS; 
		
		}

	if (sth && !sth->pTTsStreamer) {
		TTsStreamer *thetts = new TTsStreamer();
		sth->pTTsStreamer = (void *)thetts;
		thetts->initsessions(switch_core_session_get_uuid(session), responseHandler);
	}

	return SWITCH_STATUS_SUCCESS;
}
}
