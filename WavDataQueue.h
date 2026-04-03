#ifndef WAVDATAQUEUE_H
#define WAVDATAQUEUE_H


#include <condition_variable>
#include <fstream>

#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


/**
 * @file WavDataQueue.h
 * @brief 音频数据队列类
 * 
 * 线程安全的音频数据队列，用于在 TTS 系统中存储和管理音频数据
 * 支持阻塞和非阻塞的数据获取方式
 */
class WavDataQueue
{
public:
    /**
     * @brief 添加WAV数据到队列
     * @param data 音频数据指针
     * @param size 数据大小（字节）
     * 
     * 如果size为0，表示数据结束
     */
    void push(const char *data, size_t size)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (size == 0) {
            std::cout << "rec b''!!!tts will end when play end!!" << std::endl;
            is_finished = true;
            lock.unlock();
            return;
        }
        if (data == nullptr || size == 0) {
            lock.unlock();
            return;
        }

        // 等待队列有空位（按字节计算）
        not_full_.wait(lock, [this, size]() { return max_capacity_ == 0 || (buffer_size_ + size <= max_capacity_); });

        // 将数据追加到缓冲区
        buffer_.insert(buffer_.end(), data, data + size);
        buffer_size_ += size;
        //std::cout<<"now buff size="<<buffer_size_<<std::endl;
        lock.unlock();
        not_empty_.notify_all(); // 通知所有等待的消费者
    }
    
    /**
     * @brief 保存缓冲区数据到文件
     * 
     * 将当前缓冲区中的所有数据保存到 /freeswitch/src/mod/asr_tts/mod_forkzstream/build/output.pcm 文件
     */
    void save_file()
    {
        std::unique_lock<std::mutex> lock(mutex_);


        std::ofstream outfile("/freeswitch/src/mod/asr_tts/mod_forkzstream/build/output.pcm", std::ios::out | std::ios::binary);
    
        // 检查文件是否成功打开
        if (!outfile.is_open()) {
            std::cerr << "Error: Could not open file for writing." << std::endl;
            return ;
        }
    
        // 遍历 deque，将每个字符逐个写入文件
        for (char c : buffer_) {
            outfile.put(c); // 使用 put() 方法写入单个字符
            // 也可以使用 outfile.write(&c, sizeof(c)); 效果相同
        }
    
        // 关闭文件流
        outfile.close();
        return ;
    }
    
    /**
     * @brief 从队列获取指定大小的WAV数据（阻塞直到有足够数据）
     * @param size 要获取的数据大小（字节）
     * @return 包含音频数据的向量
     */
    std::vector<char> pop(size_t size)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待缓冲区有足够数据
        not_empty_.wait(lock, [this, size]() { return buffer_size_ >= size; });

        // 创建结果向量
        std::vector<char> result;
        result.reserve(size);

        // 从缓冲区前端提取数据
        auto end_it = buffer_.begin() + size;
        result.insert(result.end(), buffer_.begin(), end_it);
        buffer_.erase(buffer_.begin(), end_it);
        buffer_size_ -= size;

        lock.unlock();
        not_full_.notify_all(); // 通知生产者有空位
        return result;
    }
    
    /**
     * @brief 获取缓冲区中的所有剩余数据
     * @param chunk 用于存储数据的向量
     * @param size 期望的数据大小（实际可能小于此值）
     * @return 0表示成功
     */
    int last_pop(std::vector<char> &chunk, size_t size)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        chunk.clear();
        chunk.reserve(size);
        size_t last_size=buffer_.end()-buffer_.begin();
        if (last_size <= 0)
        {
            return 0;
        }
        
        auto end_it =buffer_.end();
        chunk.insert(chunk.end(), buffer_.begin(), end_it);

        buffer_.erase(buffer_.begin(), end_it);
        buffer_size_ -= last_size;

        lock.unlock();

        return 0;
    }
    
    /**
     * @brief 非阻塞尝试获取指定大小数据
     * @param chunk 用于存储数据的向量
     * @param size 要获取的数据大小（字节）
     * @return 0表示成功，1表示数据不足，2表示数据结束
     */
    int try_pop(std::vector<char> &chunk, size_t size)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // buffer_size_ smaller than size and is_finished is true
        if (buffer_size_ < size && is_finished == true) {
            lock.unlock();
            is_finished=false;
            return 2;
        }
        if (buffer_size_ < size) {
            lock.unlock();
            return 1;
        }

        chunk.clear();
        chunk.reserve(size);
        auto end_it = buffer_.begin() + size;
        chunk.insert(chunk.end(), buffer_.begin(), end_it);

        buffer_.erase(buffer_.begin(), end_it);
        buffer_size_ -= size;

        lock.unlock();
        not_full_.notify_all();
        return 0;
    }

    /**
     * @brief 清空队列
     */
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
        buffer_size_ = 0;
        is_finished = false;
        not_full_.notify_all(); // 通知所有等待的生产者
    }

    /**
     * @brief 设置最大容量（0表示无限制）
     * @param capacity 最大容量（字节）
     */
    void set_max_capacity(size_t capacity)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        max_capacity_ = capacity;
        if (capacity == 0) not_full_.notify_all();
    }

    /**
     * @brief 返回当前缓冲区的字节数
     * @return 缓冲区大小（字节）
     */
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_size_;
    }

    /**
     * @brief 检查队列是否为空
     * @return 是否为空
     */
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_size_ == 0;
    }

private:
    std::deque<char> buffer_;            // 字节数据缓冲区
    size_t buffer_size_ = 0;            // 当前缓冲区大小（字节）
    mutable std::mutex mutex_;           // 互斥锁
    std::condition_variable not_empty_;  // 非空条件变量
    std::condition_variable not_full_;   // 非满条件变量
    size_t max_capacity_ = 0;            // 最大字节容量（0=无限制）
    bool is_finished = false;            // 数据是否结束标志
};






#endif // WAVDATAQUEUE_H