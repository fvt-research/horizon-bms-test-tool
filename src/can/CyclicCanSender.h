#pragma once

#include "can/CanFrame.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class CyclicCanSender
{
  public:
    using Send = std::function<bool(const CanFrame &)>;

    explicit CyclicCanSender(Send send);
    ~CyclicCanSender();

    CyclicCanSender(const CyclicCanSender &) = delete;
    CyclicCanSender &operator=(const CyclicCanSender &) = delete;

    void configure(const CanFrame &frame, std::chrono::milliseconds period, bool enabled);
    bool sendOnce(const CanFrame &frame);
    bool failed() const;

  private:
    void run();

    Send send_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    CanFrame frame_;
    std::chrono::milliseconds period_{100};
    std::chrono::steady_clock::time_point nextSend_;
    bool enabled_ = false;
    bool stopped_ = false;
    bool failed_ = false;
};
