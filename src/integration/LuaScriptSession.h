#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

/// Runs trusted Lua action handlers through caller-supplied callbacks.
/// The caller supplies observations and drives delayed actions using a steady
/// clock. This class has no sandbox or execution time limit and is not
/// thread-safe; callbacks must not re-enter the session.
class LuaScriptSession
{
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Value = std::variant<bool, double, std::string>;
    using Fields = std::map<std::string, Value>;

    /// A named observation or send request with scalar fields.
    struct Message
    {
        std::string type;
        Fields fields;
    };

    /// Operation outcome; error describes a failed operation.
    struct Result
    {
        bool success;
        std::string error;
    };

    /// Called synchronously to send an action; return a failure on send errors.
    using SendCallback = std::function<Result(const Message &)>;
    /// Called synchronously for log actions.
    using LogCallback = std::function<void(const std::string &)>;

    enum class State
    {
        unloaded,
        ready,
        running,
        paused,
        stopping,
        failed
    };

    /// Both callbacks are required and must remain usable for the session's
    /// lifetime. Callback exceptions fail the session and cancel pending sends.
    LuaScriptSession(SendCallback send, LogCallback log);
    ~LuaScriptSession();

    LuaScriptSession(const LuaScriptSession &) = delete;
    LuaScriptSession &operator=(const LuaScriptSession &) = delete;
    LuaScriptSession(LuaScriptSession &&) noexcept;
    LuaScriptSession &operator=(LuaScriptSession &&) noexcept;

    /// Load and execute a script, replacing the previous one when inactive.
    Result load(const std::string &path);
    /// Start a ready session and invoke its optional on_start handler.
    Result start(TimePoint now);
    /// Pass a named observation to the optional on_message handler while running.
    Result observe(const Message &message, TimePoint now);
    /// Pause dispatch and delayed sends without changing their deadlines.
    Result pause();
    /// Resume a paused session; overdue sends run on the next runDue call.
    Result resume();
    /// Cancel pending sends and invoke on_stop. Delayed stop actions must be
    /// driven with runDue before the session becomes ready again.
    Result stop(TimePoint now);
    /// Send actions due at or before now in deadline order. Does nothing while
    /// paused; a send failure fails the session and cancels pending actions.
    Result runDue(TimePoint now);

    State state() const;
    /// Number of queued delayed sends, including any delayed stop actions.
    std::size_t pendingCount() const;
    /// Earliest pending deadline, or no deadline if paused or the queue is empty.
    std::optional<TimePoint> nextDue() const;

  private:
    class LuaRuntime;

    struct Action
    {
        bool isLog;
        std::string text;
        Fields fields;
        uint32_t delayMs;
    };

    struct PendingAction
    {
        Message message;
        TimePoint due;
    };

    Result dispatch(const char *handler, const Message *message, TimePoint now);
    Result process(const std::vector<Action> &actions, TimePoint now);
    Result sendAction(const Message &message);
    Result fail(std::string error);

    SendCallback send_;
    LogCallback log_;
    std::unique_ptr<LuaRuntime> lua_;
    State state_;
    std::vector<PendingAction> pending_;
};
