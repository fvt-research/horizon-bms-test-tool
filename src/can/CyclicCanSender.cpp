#include "can/CyclicCanSender.h"

#include <utility>

CyclicCanSender::CyclicCanSender(Send send) : send_(std::move(send))
{
    thread_ = std::thread(&CyclicCanSender::run, this);
}

CyclicCanSender::~CyclicCanSender()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    changed_.notify_one();
    thread_.join();
}

void CyclicCanSender::configure(
    const CanFrame &frame, std::chrono::milliseconds period, bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_ = frame;
        if (!enabled)
            failed_ = false;
        const bool start = enabled && !failed_;
        if (start && (!enabled_ || period != period_))
            nextSend_ = std::chrono::steady_clock::now() + period;
        period_ = period;
        enabled_ = start;
    }
    changed_.notify_one();
}

bool CyclicCanSender::sendOnce(const CanFrame &frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const bool success = send_(frame);
    if (enabled_)
    {
        nextSend_ = std::chrono::steady_clock::now() + period_;
        changed_.notify_one();
    }
    if (!success && enabled_)
    {
        failed_ = true;
        enabled_ = false;
        changed_.notify_one();
    }
    return success;
}

bool CyclicCanSender::failed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

void CyclicCanSender::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopped_)
    {
        if (!enabled_)
        {
            changed_.wait(lock, [this] { return stopped_ || enabled_; });
            continue;
        }

        const auto deadline = nextSend_;
        if (changed_.wait_until(lock, deadline) != std::cv_status::timeout)
            continue;
        if (stopped_ || !enabled_ || nextSend_ != deadline)
            continue;

        if (!send_(frame_))
        {
            failed_ = true;
            enabled_ = false;
        }
        else
        {
            nextSend_ = std::chrono::steady_clock::now() + period_;
        }
    }
}
