#include "can/CyclicCanSender.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct RecordingTransport
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<CanFrame> frames;
    bool fail = false;

    bool send(const CanFrame &frame)
    {
        std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(frame);
        changed.notify_all();
        return !fail;
    }

    bool waitForCount(std::size_t count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, 2s, [&] { return frames.size() >= count; });
    }
};

void sendsWithoutRenderingAndStops()
{
    RecordingTransport transport;
    CyclicCanSender sender(
        [&](const CanFrame &frame) { return transport.send(frame); });
    CanFrame frame;
    frame.id = 42;
    frame.data[0] = 1;
    sender.configure(frame, 30ms, true);
    require(transport.waitForCount(2), "cyclic frames were not sent");

    frame.data[0] = 2;
    sender.configure(frame, 30ms, true);
    std::size_t countAfterUpdate;
    {
        std::lock_guard<std::mutex> lock(transport.mutex);
        countAfterUpdate = transport.frames.size();
    }
    require(
        transport.waitForCount(countAfterUpdate + 1),
        "updated frame was not sent");
    sender.configure(frame, 30ms, false);

    std::unique_lock<std::mutex> lock(transport.mutex);
    require(transport.frames.back().data[0] == 2, "stale frame was sent");
    const auto count = transport.frames.size();
    require(
        !transport.changed.wait_for(
            lock, 120ms, [&] { return transport.frames.size() != count; }),
        "frame sent after disabling cyclic transmission");
}

void reportsFailureAndRequiresRetry()
{
    RecordingTransport transport;
    transport.fail = true;
    CyclicCanSender sender(
        [&](const CanFrame &frame) { return transport.send(frame); });
    CanFrame frame;
    sender.configure(frame, 20ms, true);
    require(transport.waitForCount(1), "failed send was not attempted");
    require(sender.failed(), "send failure was not reported");

    sender.configure(frame, 20ms, true);
    {
        std::unique_lock<std::mutex> lock(transport.mutex);
        require(
            !transport.changed.wait_for(
                lock, 100ms, [&] { return transport.frames.size() > 1; }),
            "sender retried without acknowledgement");
        transport.fail = false;
    }
    sender.configure(frame, 20ms, false);
    sender.configure(frame, 20ms, true);
    require(transport.waitForCount(2), "sender did not resume after retry");
    require(!sender.failed(), "failure was not cleared after retry");
}

void sendsOnceWhenCyclicDisabled()
{
    RecordingTransport transport;
    CyclicCanSender sender(
        [&](const CanFrame &frame) { return transport.send(frame); });
    CanFrame frame;
    frame.id = 17;
    require(sender.sendOnce(frame), "one-shot send failed");
    std::lock_guard<std::mutex> lock(transport.mutex);
    require(transport.frames.size() == 1, "one-shot send count is incorrect");
    require(transport.frames[0].id == 17, "one-shot frame is incorrect");
}

void destructionStopsTheWorker()
{
    RecordingTransport transport;
    {
        CyclicCanSender sender(
            [&](const CanFrame &frame) { return transport.send(frame); });
        sender.configure(CanFrame{}, 20ms, true);
        require(transport.waitForCount(1), "cyclic send did not start");
    }

    std::unique_lock<std::mutex> lock(transport.mutex);
    const auto count = transport.frames.size();
    require(
        !transport.changed.wait_for(
            lock, 100ms, [&] { return transport.frames.size() != count; }),
        "frame sent after sender destruction");
}

} // namespace

int main()
{
    try
    {
        sendsWithoutRenderingAndStops();
        reportsFailureAndRequiresRetry();
        sendsOnceWhenCyclicDisabled();
        destructionStopsTheWorker();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
