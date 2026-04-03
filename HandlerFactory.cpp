/**
 * @file HandlerFactory.cpp
 * @brief 下载器工厂实现文件
 * 
 * 实现了根据不同类型创建对应下载器的工厂方法
 */

#include "HandlerFactory.h"
 
#include "WebSocketHandler.h"
 

/**
 * @brief 创建指定类型的下载器
 * @param type 下载器类型枚举值
 * @return std::unique_ptr<IHandler> 下载器智能指针
 * @throws std::invalid_argument 当传入不支持的下载类型时抛出异常
 * 
 * 根据传入的下载类型创建对应的具体下载器实例：
 * - HTTP: 创建HttpHandler
 * - WEBSOCKET: 创建WebSocketHandler 
 * - LOCAL_FILE: 创建LocalFileHandler
 */
std::unique_ptr<IHandler> HandlerFactory::create(DownloadType type) {
    switch (type) {
 
        case DownloadType::WEBSOCKET: 
            return std::make_unique<WebSocketHandler>();
 
        default: 
            throw std::invalid_argument("Unsupported download type");
    }
}
