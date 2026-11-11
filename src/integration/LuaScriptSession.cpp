#include "integration/LuaScriptSession.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

class LuaScriptSession::LuaRuntime
{
  public:
    sol::state lua;

    Result parseAction(const sol::object &object, Action &action)
    {
        if (object.get_type() != sol::type::table)
            return {false, "Action must be a table"};
        const sol::table table = object.as<sol::table>();
        const sol::object kindValue = table.raw_get<sol::object>("action");
        if (kindValue.get_type() != sol::type::string)
            return {false, "Action must be 'log' or 'send'"};
        const std::string kind = kindValue.as<std::string>();
        if (kind != "log" && kind != "send")
            return {false, "Action must be 'log' or 'send'"};

        action.isLog = kind == "log";
        const sol::object text =
            table.raw_get<sol::object>(action.isLog ? "message" : "message_type");
        if (text.get_type() != sol::type::string)
            return {false, "Action message must be a string"};
        action.text = text.as<std::string>();
        if (action.isLog)
            return {true, {}};
        if (action.text.empty())
            return {false, "Send message type must not be empty"};

        const sol::object delay = table.raw_get<sol::object>("delay_ms");
        if (delay.get_type() != sol::type::nil)
        {
            if (delay.get_type() != sol::type::number)
                return {false, "Send delay must be a non-negative integer"};
            const double value = delay.as<double>();
            if (!std::isfinite(value) || value < 0 || std::floor(value) != value ||
                value > std::numeric_limits<uint32_t>::max())
                return {false, "Send delay must be a non-negative uint32"};
            action.delayMs = static_cast<uint32_t>(value);
        }

        const sol::object fieldsValue = table.raw_get<sol::object>("fields");
        if (fieldsValue.get_type() == sol::type::nil)
            return {true, {}};
        if (fieldsValue.get_type() != sol::type::table)
            return {false, "Send fields must be a table"};
        const sol::table fields = fieldsValue.as<sol::table>();
        for (const auto &entry : fields)
        {
            if (entry.first.get_type() != sol::type::string)
                return {false, "Send field names must be strings"};
            const std::string name = entry.first.as<std::string>();
            switch (entry.second.get_type())
            {
            case sol::type::boolean:
                action.fields.emplace(name, entry.second.as<bool>());
                break;
            case sol::type::number: {
                const double number = entry.second.as<double>();
                if (!std::isfinite(number))
                    return {false, "Send field numbers must be finite"};
                action.fields.emplace(name, number);
                break;
            }
            case sol::type::string:
                action.fields.emplace(name, entry.second.as<std::string>());
                break;
            default:
                return {false, "Send field values must be boolean, number or string"};
            }
        }
        return {true, {}};
    }

    Result parseActions(const sol::object &object, std::vector<Action> &actions)
    {
        if (object.get_type() == sol::type::nil)
            return {true, {}};
        if (object.get_type() != sol::type::table)
            return {false, "Handler must return nil, an action or an action list"};
        const sol::table table = object.as<sol::table>();
        const sol::object kind = table.raw_get<sol::object>("action");
        if (kind.get_type() != sol::type::nil)
        {
            Action action{};
            const auto result = parseAction(object, action);
            if (result.success)
                actions.push_back(std::move(action));
            return result;
        }

        std::size_t count = 0;
        for (const auto &entry : table)
        {
            if (entry.first.get_type() != sol::type::number ||
                !entry.first.is<lua_Integer>())
                return {false, "Action list must contain consecutive integer keys"};
            const auto key = entry.first.as<lua_Integer>();
            if (key < 1)
                return {false, "Action list must contain consecutive integer keys"};
            ++count;
        }
        for (std::size_t index = 1; index <= count; ++index)
        {
            const sol::object value = table.raw_get<sol::object>(index);
            if (value.get_type() == sol::type::nil)
                return {false, "Action list must not contain gaps"};
            Action action{};
            const auto result = parseAction(value, action);
            if (!result.success)
                return result;
            actions.push_back(std::move(action));
        }
        return {true, {}};
    }
};

LuaScriptSession::LuaScriptSession(SendCallback send, LogCallback log)
    : send_(std::move(send)),
      log_(std::move(log)),
      lua_(),
      state_(State::unloaded),
      pending_()
{
    if (!send_ || !log_)
        throw std::invalid_argument("Lua script callbacks must be configured");
}

LuaScriptSession::~LuaScriptSession() = default;
LuaScriptSession::LuaScriptSession(LuaScriptSession &&) noexcept = default;
LuaScriptSession &LuaScriptSession::operator=(LuaScriptSession &&) noexcept = default;

LuaScriptSession::Result LuaScriptSession::load(const std::string &path)
{
    if (state_ == State::running || state_ == State::paused ||
        state_ == State::stopping)
        return {false, "Cannot load a script while a session is active"};

    auto next = std::make_unique<LuaRuntime>();
    next->lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math,
                             sol::lib::table);
    auto loaded = next->lua.load_file(path);
    if (!loaded.valid())
    {
        const sol::error error = loaded;
        lua_.reset();
        return fail(std::string("Script load failed: ") + error.what());
    }
    auto executed = loaded();
    if (!executed.valid())
    {
        const sol::error error = executed;
        lua_.reset();
        return fail(std::string("Script load failed: ") + error.what());
    }
    lua_ = std::move(next);
    pending_.clear();
    state_ = State::ready;
    return {true, {}};
}

LuaScriptSession::Result LuaScriptSession::start(TimePoint now)
{
    if (state_ != State::ready)
        return {false, "Script is not ready to start"};
    state_ = State::running;
    return dispatch("on_start", nullptr, now);
}

LuaScriptSession::Result LuaScriptSession::observe(
    const Message &message, TimePoint now)
{
    if (state_ != State::running)
        return {false, "Script is not running"};
    if (message.type.empty())
        return {false, "Observation type must not be empty"};
    return dispatch("on_message", &message, now);
}

LuaScriptSession::Result LuaScriptSession::pause()
{
    if (state_ != State::running)
        return {false, "Script is not running"};
    state_ = State::paused;
    return {true, {}};
}

LuaScriptSession::Result LuaScriptSession::resume()
{
    if (state_ != State::paused)
        return {false, "Script is not paused"};
    state_ = State::running;
    return {true, {}};
}

LuaScriptSession::Result LuaScriptSession::stop(TimePoint now)
{
    if (!lua_ || (state_ != State::running && state_ != State::paused &&
                  state_ != State::failed))
        return {false, "Script is not active"};
    pending_.clear();
    state_ = State::stopping;
    const Result result = dispatch("on_stop", nullptr, now);
    if (result.success && pending_.empty())
        state_ = State::ready;
    return result;
}

LuaScriptSession::Result LuaScriptSession::runDue(TimePoint now)
{
    if (state_ == State::paused)
        return {true, {}};
    if (state_ != State::running && state_ != State::stopping)
        return {false, "Script is not running or stopping"};

    std::stable_sort(
        pending_.begin(),
        pending_.end(),
        [](const PendingAction &left, const PendingAction &right) {
            return left.due < right.due;
        });
    auto it = pending_.begin();
    while (it != pending_.end())
    {
        if (it->due > now)
        {
            ++it;
            continue;
        }
        const Message message = std::move(it->message);
        it = pending_.erase(it);
        const Result result = sendAction(message);
        if (!result.success)
            return result;
    }
    if (state_ == State::stopping && pending_.empty())
        state_ = State::ready;
    return {true, {}};
}

LuaScriptSession::State LuaScriptSession::state() const
{
    return state_;
}

std::size_t LuaScriptSession::pendingCount() const
{
    return pending_.size();
}

std::optional<LuaScriptSession::TimePoint> LuaScriptSession::nextDue() const
{
    if (pending_.empty() || state_ == State::paused)
        return std::nullopt;
    return std::min_element(
               pending_.begin(),
               pending_.end(),
               [](const PendingAction &left, const PendingAction &right) {
                   return left.due < right.due;
               })
        ->due;
}

LuaScriptSession::Result LuaScriptSession::dispatch(
    const char *handler, const Message *message, TimePoint now)
{
    const sol::object handlerValue = lua_->lua.globals().raw_get<sol::object>(handler);
    if (handlerValue.get_type() == sol::type::nil)
        return {true, {}};
    if (handlerValue.get_type() != sol::type::function)
        return fail(std::string(handler) + " must be a function");

    sol::protected_function function = handlerValue.as<sol::protected_function>();
    sol::protected_function_result result = [&]() {
        if (!message)
            return function();
        sol::table argument = lua_->lua.create_table();
        argument["type"] = message->type;
        sol::table fields = lua_->lua.create_table();
        for (const auto &[name, value] : message->fields)
            std::visit([&](const auto &item) { fields[name] = item; }, value);
        argument["fields"] = fields;
        return function(argument);
    }();
    if (!result.valid())
    {
        const sol::error error = result;
        return fail(std::string(handler) + " failed: " + error.what());
    }
    if (result.return_count() == 0)
        return {true, {}};

    std::vector<Action> actions;
    const auto parsed = lua_->parseActions(result.get<sol::object>(), actions);
    if (!parsed.success)
        return fail(std::string(handler) + ": " + parsed.error);
    return process(actions, now);
}

LuaScriptSession::Result LuaScriptSession::process(
    const std::vector<Action> &actions, TimePoint now)
{
    for (const auto &action : actions)
    {
        if (action.isLog)
            log_(action.text);
        else if (action.delayMs > 0)
            pending_.push_back(
                {{action.text, action.fields}, now + std::chrono::milliseconds(action.delayMs)});
        else
        {
            const Result result = sendAction({action.text, action.fields});
            if (!result.success)
                return result;
        }
    }
    return {true, {}};
}

LuaScriptSession::Result LuaScriptSession::sendAction(const Message &message)
{
    const Result result = send_(message);
    if (!result.success)
        return fail("Send failed: " + result.error);
    return {true, {}};
}

LuaScriptSession::Result LuaScriptSession::fail(std::string error)
{
    pending_.clear();
    state_ = State::failed;
    return {false, std::move(error)};
}
