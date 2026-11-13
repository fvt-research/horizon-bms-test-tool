#pragma once

#include "can/CanFrame.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

/// Sends a configured CAN frame on a worker thread independently of the UI.
/// Public operations are synchronized; the callback must not re-enter this
/// sender and must remain usable until the sender is destroyed.
class CyclicCanSender
{
  public:
    /// Returns false if transmission fails; called from the worker or sendOnce.
    /// Must not throw.
    using Send = std::function<bool(const CanFrame &)>;

    explicit CyclicCanSender(Send send);
    ~CyclicCanSender();

    CyclicCanSender(const CyclicCanSender &) = delete;
    CyclicCanSender &operator=(const CyclicCanSender &) = delete;

    /// Update the cyclic frame and period. Provide a positive period.
    /// Disabling clears a failure latch; re-enable to retry after a failure.
    void configure(const CanFrame &frame, std::chrono::milliseconds period, bool enabled);
    /// Send immediately; when cyclic sending is enabled, restart its deadline.
    bool sendOnce(const CanFrame &frame);
    /// True if a send failed while cyclic sending was enabled.
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
