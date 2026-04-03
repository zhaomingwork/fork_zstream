/**
 * @file WebSocketHandler.h
 * @brief WebSocket下载器实现
 *
 * 通过WebSocket协议实现TTS音频下载功能
 */
#define ASIO_STANDALONE 1
#include "IHandler.h"
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include "asio.hpp"
#include "nlohmann/json.hpp"
// WebSocket客户端类型定义
typedef websocketpp::client<websocketpp::config::asio_client> wsclient;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

/**
 * @class WebSocketHandler
 * @brief WebSocket下载器类
 */
class WebSocketHandler : public IHandler
{
private:
    std::vector<char> buffer;              ///< 音频数据缓冲区
    std::atomic<bool> is_cancelled{false}; ///< 下载取消标志
    std::thread worker_thread;             ///< 工作线程
    std::unique_ptr<wsclient> tts_client;  ///< WebSocket客户端

    websocketpp::connection_hdl m_connection_hdl; ///< 当前连接句柄

    std::atomic<bool> is_tts{false};
    std::atomic<bool> is_asr{false};
    std::string callid;
    DownloadCallback call_back_func;

public:
    /**
     * @brief 构造函数
     */
    WebSocketHandler() = default;
    /**
     * @brief 析构函数
     *
     * 断开连接并清理资源
     */
    ~WebSocketHandler() override
    {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler析构函数调用\n");
        // std::cout<<"WebSocketHandler析构函数调用"<<std::endl;
        this->disconnect();
        if (worker_thread.joinable())
        {
            worker_thread.join();
        }
        //    std::cout<<"WebSocketHandler析构函数"<<std::endl;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler析构函数调用退出\n");
    }
    void handleCmd(std::string cmd)
    {
        if (cmd == "ttsstop_clean")
        {
            // this->istts.store(false);
             call_back_func("cmd", "interrupt_clean", 0, callid.c_str());
            this->is_tts.store(false, std::memory_order_release);
        }
        if (cmd == "ttsstop_only")
        {
            // this->istts.store(false);
             call_back_func("cmd", "interrupt_only", 0, callid.c_str());
            this->is_tts.store(false, std::memory_order_release);
        }
        if (cmd == "ttsstart")
        {
            // this->istts.store(false);
            this->is_tts.store(true, std::memory_order_release);
        }
        if (cmd == "asrstop")
        {
            // this->istts.store(false);
            this->is_asr.store(false, std::memory_order_release);
        }
        if (cmd == "asrstart")
        {
            // this->istts.store(false);
            this->is_asr.store(true, std::memory_order_release);
        }
        if (cmd == "close")
        {
            // this->istts.store(false);
            this->cancel();
        }
    }
    /**
     * @brief 断开WebSocket连接
     */
    void disconnect()
    {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler断开websocket连接\n");
        if (!this->worker_thread.joinable())
        {
            return;
        }

        try
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler关闭客户端连接\n");
            this->tts_client->stop();
            if (auto con = tts_client->get_con_from_hdl(m_connection_hdl))
            {
                if (con->get_state() == websocketpp::session::state::open)
                {
                    con->terminate(websocketpp::lib::error_code());
                }
            }
        }
        catch (std::exception const &e)
        {
            std::cout << "Error disconnecting: " << e.what() << std::endl;
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler断开websocket连接错误,错误:%s\n", e.what());
        }
    }
    /**
     * @brief 检查WebSocket连接状态
     * @return 是否已连接
     */
    bool isConnected()
    {
        try
        {
            wsclient::connection_ptr con = tts_client->get_con_from_hdl(m_connection_hdl);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler判断是否连接\n");
            return con->get_state() == websocketpp::session::state::open;
        }
        catch (const websocketpp::exception &e)
        {
            return false;
        }
    }

    void sendbinary(char *buffer, size_t len)
    {
        // std::cout<<"send bytes len="<<len<<std::endl;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 进入发送二进制数据流，大小:%d\n", len);
        if (!this->isConnected())
        {
            return;
        }
        if (is_asr.load(std::memory_order_acquire) == false)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 发送二进制数据流已经被停止\n");
            this->writeMsg("发送二进制数据流已经被停止");
            return;
        }
        //   std::cout << "尝试发送数据大小: " << len << std::endl;
        //   std::string sendtext(text);
        websocketpp::lib::error_code ec;
        tts_client->send(this->m_connection_hdl, buffer, len, websocketpp::frame::opcode::binary, ec);
        if (ec)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 发送二进制数据流失败，原因:%s\n", ec.message().c_str());
            //  std::cout << "发送binary失败: " << ec.message() << std::endl;
        }
    };

    void writeMsg(const char *text)
    {
        char initialMetadata[8192];
        sprintf(initialMetadata,
                "{\"callId\":\"%s\",\"msg\":\"%s\"}",
                this->callid.c_str(), text);

        this->writeText(initialMetadata);
    }
    /**
     * @brief 发送文本消息
     * @param text 要发送的文本
     */
    void writeText(const char *text)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 进入发送文本数据，文本:%s\n", text);
        if (!this->isConnected())
        {
            return;
        }

        std::string sendtext(text);
        websocketpp::lib::error_code ec;
        tts_client->send(this->m_connection_hdl, sendtext, websocketpp::frame::opcode::text, ec);
        if (ec)
        {
            //  std::cout << "发送消息失败: " << ec.message() << std::endl;
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 发送文本数据失败！:%s\n", ec.message().c_str());
        }
    }

    /**
     * @brief 发送TTS请求JSON
     * @param text 要合成的文本
     * @param spkid 说话人ID
     * @param speed 语速
     * @param modelid 模型ID
     * @param reqid 请求ID
     * @param sessionid 会话ID
     */
    void writeTTsJson(const char *callId, const char *tenantId, const char *botid,
                      const char *fsInstanceId, const char *reqid, const char *sessionid)
    {
        if (!this->isConnected())
        {
            return;
        }

        char initialMetadata[8192];
        if (callId != NULL && strlen(callId) > 0)
        {
            sprintf(initialMetadata,
                    "{\"callId\":\"%s\",\"tenantId\":\"%s\","
                    "\"botid\":\"%s\",\"fsInstanceId\":\"%s\",\"sessionid\":\"%s\"}",
                    callId, tenantId, botid, fsInstanceId, sessionid);
        }
        this->writeText(initialMetadata);
    }
    /**
     * @brief 启动WebSocket下载
     * @param url WebSocket服务器URL
     * @param text 要合成的文本
     * @param ttsQueue 音频数据队列
     * @param call_back 回调函数
     * @param params 参数字符串(JSON格式)
     */
    void startHandler(WavDataQueue &ttsQueue,
                  DownloadCallback call_back,
                  const std::string &params, std::atomic<bool> &thread_run) override
    {

        // this->parseParam(params);
        // 解析参数
        // std::cout<<"websocket params="<<params<<std::endl;
        call_back_func=call_back;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 启动，参数为:%s\n", params.c_str());
        //{"model_id":"2",
        /* "session_id":"1300d241-9ecf-4ddf-a0d7-667592f8737c",
         "speed":"1.0","spk_id":"0",
         "text":"您好，王明。我是妇幼医院骨科的随访护士小李。想了解您出院后恢复情况，伤口有没有红肿？疼痛程度一到十分您打几分？",
         "url":"ws://111.205.137.58:30036/ws"}
         - `callId`
 - `tenantId`
 - `botid`
 - `fsInstanceId`

         */
        try
        {
            nlohmann::json params_json = nlohmann::json::parse(params);
            std::string sessionid = params_json["session_id"];
            std::string callId = params_json["callId"];
            std::string tenantId = params_json["tenantId"];
            std::string botid = params_json["botid"];
            std::string fsInstanceId = params_json["fsInstanceId"];
            std::string url = params_json["url"];
            std::string text = params_json["text"];
            std::string reqid = callId;
            this->callid = callId;

            // 初始化WebSocket客户端
            tts_client.reset(new wsclient());
            tts_client->set_access_channels(websocketpp::log::alevel::all);
            tts_client->clear_access_channels(websocketpp::log::alevel::frame_payload);
            tts_client->init_asio();
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 服务器地址:%s\n", url.c_str());

            // 设置连接处理器
            tts_client->set_open_handler([this, botid, callId, tenantId, fsInstanceId, reqid, sessionid, call_back](websocketpp::connection_hdl hdl)
                                         {
            m_connection_hdl = hdl;
            writeTTsJson(callId.c_str(), tenantId.c_str(), botid.c_str(), fsInstanceId.c_str(), reqid.c_str(), sessionid.c_str());
             this->writeMsg("connect succeed!");
            // 通知连接成功
              switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 客户端连接成功\n");
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "connected");
            char *json_str = cJSON_PrintUnformatted(root);
            call_back("ok", json_str, 0, reqid.c_str());
            cJSON_Delete(root);
            switch_safe_free(json_str); });

            // 设置消息处理器
            tts_client->set_message_handler([this, reqid, call_back, &ttsQueue](websocketpp::connection_hdl hdl, message_ptr msg)
                                            {
                            std::cout << "recv msg len 0=: " << msg->get_payload().length() << std::endl;
            switch (msg->get_opcode()) {
              //  std::cout << "recv msg len=: " << msg->get_payload().length() << std::endl;
               
                case websocketpp::frame::opcode::text: {
                     
                    std::string const &data = msg->get_payload();
                     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 客户端接收到文本数据:%s\n",data.c_str());
                     this->handleCmd(data);
                    call_back("ok", data.c_str(), 0, reqid.c_str());
                } break;

                case websocketpp::frame::opcode::binary: {
                    std::string const &data = msg->get_payload();
                      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 客户端接收到二进制数据:%d\n",data.size());
                   
                           if(is_tts.load(std::memory_order_acquire) == false)
        {
             switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 接收tts数据流已经被停止\n");
            this->writeMsg("接收tts数据流已经被停止");
        }
        else
                   
                    ttsQueue.push(data.c_str(), data.size());
                } break;
            } });

            // 设置失败处理器
            tts_client->set_fail_handler([this, reqid, call_back](websocketpp::connection_hdl hdl)
                                         {
            auto con = tts_client->get_con_from_hdl(hdl);
            std::string msg = con->get_ec().message();
            this->writeMsg("connect error!");
              switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 客户端出错:%s,请求id:%s\n",msg.c_str(),reqid.c_str());
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "error");
            cJSON_AddStringToObject(cJSON_AddObjectToObject(root, "message"), "error", msg.c_str());
            
            char *json_str = cJSON_PrintUnformatted(root);
            call_back("error", json_str, 0, reqid.c_str());
            
            cJSON_Delete(root);
            switch_safe_free(json_str); });

            // 设置关闭处理器
            tts_client->set_close_handler([this, reqid, call_back](websocketpp::connection_hdl hdl)
                                          {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "ws disconnected");
              this->writeMsg("connect closed!");
            cJSON_AddStringToObject(cJSON_AddObjectToObject(root, "message"), "reason", "close");
               switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 客户端关闭\n");
            char *json_str = cJSON_PrintUnformatted(root);
            call_back("error", json_str, 0, reqid.c_str());
            
            cJSON_Delete(root);
            switch_safe_free(json_str); });

            // 建立连接
            websocketpp::lib::error_code ec;
            wsclient::connection_ptr con = tts_client->get_connection(url, ec);
            if (ec)
            {
                //  std::cout << "无法创建连接: " << ec.message() << std::endl;
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 客户端无法创建连接\n");
                call_back("error", "connection error", 0, reqid.c_str());
                return;
            }

            tts_client->connect(con);

            // 启动工作线程运行IO服务
            worker_thread = std::thread([this]
                                        { tts_client->run(); });
        }
        catch (const std::exception &e)
        {
            // std::cout << "websocket Handler出错: " << e.what() << std::endl;
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[forkzstream] WebSocketHandler 启动出错，错误：%s\n", e.what());
        }
    }

    /**
     * @brief 取消下载
     */
    void cancel() override
    {
        is_cancelled = true;
        disconnect();
    }
};