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

class LuaScriptSession
{
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Value = std::variant<bool, double, std::string>;
    using Fields = std::map<std::string, Value>;

    struct Message
    {
        std::string type;
        Fields fields;
    };

    struct Result
    {
        bool success;
        std::string error;
    };

    using SendCallback = std::function<Result(const Message &)>;
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

    LuaScriptSession(SendCallback send, LogCallback log);
    ~LuaScriptSession();

    LuaScriptSession(const LuaScriptSession &) = delete;
    LuaScriptSession &operator=(const LuaScriptSession &) = delete;
    LuaScriptSession(LuaScriptSession &&) noexcept;
    LuaScriptSession &operator=(LuaScriptSession &&) noexcept;

    Result load(const std::string &path);
    Result start(TimePoint now);
    Result observe(const Message &message, TimePoint now);
    Result pause();
    Result resume();
    Result stop(TimePoint now);
    Result runDue(TimePoint now);

    State state() const;
    std::size_t pendingCount() const;
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
