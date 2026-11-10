#pragma once

#include "can/ICanInterface.h"
#include "can/CanFrame.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class DbcModel;

/// A decoded CAN frame: the raw bytes plus signal values extracted via a DBC.
struct DecodedFrame
{
    CanFrame                                    raw;
    std::string                                 messageName;
    std::vector<std::pair<std::string, double>> signals; ///< {signal name, physical value}
};

/// Wraps an ICanInterface with a background RX thread and a decoded-frame queue.
/// Call drainRxFrames() each UI render frame to consume new data.
class CanBus
{
public:
    explicit CanBus(std::unique_ptr<ICanInterface> iface);
    ~CanBus();

    bool open(uint32_t channel, uint32_t baudrate);
    void close();
    bool isOpen() const;

    /// Transmit a raw CAN frame. Thread-safe.
    bool send(const CanFrame& frame);

    /// Attach the DBC model used to decode incoming frames.
    /// Pointer must remain valid for the lifetime of this CanBus.
    void setDbc(const DbcModel* dbc);

    /// Drain all decoded frames buffered since the last call (UI thread only).
    std::vector<DecodedFrame> drainRxFrames();

    std::string lastError() const;

private:
    void rxLoop();

    std::unique_ptr<ICanInterface> iface_;
    const DbcModel*                dbc_     = nullptr;

    mutable std::mutex       ifaceMutex_;
    std::thread              rxThread_;
    std::atomic<bool>        running_  { false };

    std::mutex               rxMutex_;
    std::queue<DecodedFrame> rxQueue_;
};
