#ifndef UTIL_PRINTER_HPP
#define UTIL_PRINTER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#include "print.hpp"
#include "thread/affinity.hpp"

namespace util
{

class printer final
{
public:
    inline explicit printer(int core_id) noexcept;
    inline ~printer() noexcept;

    template <typename... Args>
    void print(const std::string& message, Args... args) noexcept;

    inline void stop() noexcept { this->running_ = false; }

    printer()                          = delete;
    printer(const printer&)            = delete;
    printer(printer&&)                 = delete;
    printer& operator=(const printer&) = delete;
    printer& operator=(printer&&)      = delete;

private:
    void push(std::string value) noexcept
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
     }

    inline void flush() noexcept;

    std::queue<std::string> queue_;
    std::mutex              queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool>       running_;
    std::thread             printer_thread_;
};

printer::printer(int core_id) noexcept
    : running_(true)
{
    this->printer_thread_ = std::thread([this, core_id]
    {
        [[maybe_unused]] bool success = use_core(core_id);
        this->flush();
    });
}

printer::~printer() noexcept
{
    this->running_.store(false);
    if (this->printer_thread_.joinable()) this->printer_thread_.join();
}

template <typename... Args>
void printer::print(const std::string& message, Args... args) noexcept
{
    std::string formatted_message = detail::format(message, args...);

    std::ostringstream oss;
    oss << formatted_message;

    this->push(oss.str());
}

void printer::flush() noexcept
{
    while (this->running_)
    {
        std::unique_lock<std::mutex> lock(this->queue_mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || !this->running_; });

        while (!this->queue_.empty())
        {
            const auto& content = this->queue_.front();
            println(content);
            this->queue_.pop();
        }
    }
}

}  // namespace util

#endif  // UTIL_PRINTER_HPP
