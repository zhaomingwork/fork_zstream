/**
 * @file HandlerFactory.h
 * @brief 下载器工厂类头文件
 * 
 * 定义下载器工厂类和下载类型枚举
 */

#pragma once
#include "IHandler.h"
#include <memory>
#include <stdexcept>

// 前置声明替代直接包含（减少编译依赖）
 
class WebSocketHandler;
 

/**
 * @class HandlerFactory
 * @brief 下载器工厂类
 * 
 * 用于创建不同类型的下载器实例
 */
class HandlerFactory {
public:
    /**
     * @enum DownloadType
     * @brief 下载类型枚举
     */
    enum class DownloadType { 
        HTTP,        ///< HTTP协议下载
        WEBSOCKET,   ///< WebSocket协议下载 
        LOCAL_FILE   ///< 本地文件下载
    };
    
    /**
     * @brief 创建下载器实例
     * @param type 下载类型枚举值
     * @return std::unique_ptr<IHandler> 下载器智能指针
     * 
     * 根据指定类型创建对应的下载器实例
     */
    static std::unique_ptr<IHandler> create(DownloadType type);
};
