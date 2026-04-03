#ifndef IHandler_H
#define IHandler_H
#include <functional>
#include "WavDataQueue.h"
#include <regex>
#include <atomic>
#include <switch.h>  // 必须前置包含
#include <switch_types.h>  // 依赖 switch.h

/**
 * @file IHandler.h
 * @brief 下载器接口定义
 * 
 * 定义下载器接口
 */



/**
 * @brief 下载回调函数类型
 * @param event 事件类型
 * @param message 事件消息
 * @param length 消息长度
 * @param reqid 请求ID
 */
using DownloadCallback = std::function<void(const char* event, const char* message, 
                                         size_t length, const char* reqid)>;


struct python_data{
     WavDataQueue* queue_ptr;
     DownloadCallback call_back;
     std::atomic<bool>* thread_run;
};

/**
 * @class IHandler
 * @brief 下载器接口
 * 
 * 定义下载器的统一接口
 */
class IHandler {
public:
    virtual ~IHandler() = default;
    
    /**
     * @brief 开始下载
     * @param url 下载URL
     * @param text 要下载的文本内容
     * @param ttsQueue 音频数据队列
     * @param call_back 下载回调
     * @param params 额外参数
     */
    virtual void startHandler(
                         WavDataQueue& ttsQueue, DownloadCallback call_back,
                         const std::string& params,std::atomic<bool>& thread_run) = 0;
    
    /**
     * @brief 取消下载
     */
    virtual void cancel() = 0;
    std::string parseParam(std::string& invalid_json)
    {
        // std::string invalid_json = R"({url: ws://111.205.137.58:30036/ws, user-name: John, text: 您好，王明。, some.key: value, "already_quoted": good})";

  //  std::string invalid_json = R"({url: ws://111.205.137.58:30036/ws, modelid: 2, text: 您好，王明。, speed: 1.0})";

// 1. 先修复key的引号（和之前一样）
std::string fixed_json = std::regex_replace(invalid_json, std::regex(R"(([{,]\s*)([^:]+?)(\s*:))"), R"($1"$2"$3)");

// 2. 尝试修复字符串value的引号
// 这个模式尝试匹配 ": value,  " 或 ": value}  " 的模式，并认为value需要引号。
// 它非常脆弱！它可能会错误地匹配数字、布尔值等。
std::regex value_pattern(R"(:(\s*)([^,\}\[\]]+?)(\s*)([,}\]]))"); 
// 假设所有非数字、非布尔的值都是字符串（这是危险的）
fixed_json = std::regex_replace(fixed_json, value_pattern, R"(: $1"$2"$3$4)");

std::cout << fixed_json << std::endl;

    return fixed_json;
    }

    // void sendbinary(char *buffer, size_t len) {};
};

#endif // IHandler_H
