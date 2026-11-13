#include "integration/LuaScriptSession.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void requireSuccess(const LuaScriptSession::Result &result)
{
    if (!result.success)
        throw std::runtime_error(result.error);
}

class FakeAdapter
{
  public:
    std::vector<LuaScriptSession::Message> sent;
    std::vector<std::string> logs;

    LuaScriptSession::Result send(const LuaScriptSession::Message &request)
    {
        if (request.type == "send.failure")
            return {false, "adapter rejected request"};
        sent.push_back(request);
        return {true, {}};
    }
};

LuaScriptSession makeSession(FakeAdapter &adapter)
{
    return LuaScriptSession(
        [&](const auto &request) { return adapter.send(request); },
        [&](const auto &message) { adapter.logs.push_back(message); });
}

LuaScriptSession::Message observation(const std::string &type)
{
    return {type, {{"fault_active", true}, {"voltage", 3.75}, {"label", "cell"}}};
}

void lifecycleAndCancellation(const char *script)
{
    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    require(adapter.logs == std::vector<std::string>{"starting"}, "start log");
    require(adapter.sent.size() == 1, "immediate start send");
    require(adapter.sent[0].type == "start.immediate", "start type");
    require(session.pendingCount() == 1, "start delay not queued");

    requireSuccess(session.observe(observation("bms.status"), start));
    require(session.pendingCount() == 2, "reaction not queued");
    require(session.nextDue() == start + 25ms, "incorrect first deadline");
    requireSuccess(session.runDue(start + 24ms));
    require(adapter.sent.size() == 1, "reaction sent early");
    requireSuccess(session.runDue(start + 25ms));
    require(adapter.sent.size() == 2, "reaction not sent");
    require(adapter.sent.back().type == "bms.reaction", "reaction type");
    require(
        adapter.sent.back().fields.size() == 3 &&
            std::get<bool>(adapter.sent.back().fields.at("observed")) &&
            std::get<double>(adapter.sent.back().fields.at("voltage")) == 3.75 &&
            std::get<std::string>(adapter.sent.back().fields.at("label")) == "cell",
        "reaction field");

    requireSuccess(session.stop(start + 30ms));
    require(session.state() == LuaScriptSession::State::stopping, "stop state");
    require(session.pendingCount() == 1, "stop did not replace pending actions");
    require(!session.observe(observation("bms.status"), start + 31ms).success,
            "observation accepted while stopping");
    requireSuccess(session.runDue(start + 40ms));
    require(adapter.sent.size() == 3, "stop send missing");
    require(adapter.sent.back().type == "stop.delayed", "stop type");
    require(session.state() == LuaScriptSession::State::ready, "not ready");
    require(!session.runDue(start + 100ms).success, "stopped session ran");
    require(adapter.logs.back() == "stopping", "stop log");
}

void pauseAndResume(const char *script)
{
    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    requireSuccess(session.pause());
    require(!session.nextDue(), "paused session has a timer deadline");
    requireSuccess(session.runDue(start + 100ms));
    require(adapter.sent.size() == 1, "paused session sent an action");
    require(!session.observe(observation("bms.status"), start + 100ms).success,
            "paused session accepted observation");
    requireSuccess(session.resume());
    require(session.nextDue() == start + 50ms, "deadline changed during pause");
    requireSuccess(session.runDue(start + 100ms));
    require(adapter.sent.back().type == "start.delayed",
            "overdue action did not run");
}

void overdueActionsRunInDeadlineOrder(const char *script)
{
    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    requireSuccess(session.observe(observation("bms.status"), start));
    requireSuccess(session.runDue(start + 100ms));
    require(adapter.sent.size() == 3, "overdue actions missing");
    require(adapter.sent[1].type == "bms.reaction", "earlier action ran late");
    require(adapter.sent[2].type == "start.delayed", "later action ran early");
}

void dueActionsPreserveOrderAndFutureActions(const char *script)
{
    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    auto first = observation("bms.status");
    first.fields["label"] = std::string("first");
    auto second = observation("bms.status");
    second.fields["label"] = std::string("second");
    requireSuccess(session.observe(first, start));
    requireSuccess(session.observe(second, start));
    requireSuccess(session.runDue(start + 25ms));
    require(adapter.sent.size() == 3, "due actions missing");
    require(std::get<std::string>(adapter.sent[1].fields.at("label")) == "first" &&
                std::get<std::string>(adapter.sent[2].fields.at("label")) == "second",
            "equal-deadline actions reordered");
    require(session.pendingCount() == 1, "future action not retained");
    requireSuccess(session.runDue(start + 50ms));
    require(adapter.sent.back().type == "start.delayed", "future action missing");
}

void failuresAndRecovery(const char *script, const char *otherScript)
{
    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    auto result = session.observe(observation("bms.failure"), start);
    require(!result.success && result.error.find("adapter rejected") != std::string::npos,
            "send failure not reported");
    require(session.state() == LuaScriptSession::State::failed, "failure state");
    require(session.pendingCount() == 0, "failure retained pending actions");
    requireSuccess(session.stop(start));
    requireSuccess(session.runDue(start + 10ms));

    requireSuccess(session.start(start + 20ms));
    result = session.observe(observation("bms.invalid"), start + 20ms);
    require(!result.success && !result.error.empty(), "invalid action accepted");
    require(session.state() == LuaScriptSession::State::failed, "invalid state");
    requireSuccess(session.load(otherScript));
    requireSuccess(session.start(start + 30ms));
    const auto sentCount = adapter.sent.size();
    requireSuccess(session.observe(observation("bms.failure"), start + 30ms));
    require(adapter.sent.size() == sentCount, "old script handler survived reload");
}

void delayedFailure(const char *script)
{
    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    requireSuccess(session.observe(observation("bms.delayed_failure"), start));
    const auto result = session.runDue(start + 5ms);
    require(
        !result.success && result.error.find("adapter rejected") != std::string::npos,
        "delayed send failure not reported");
    require(session.state() == LuaScriptSession::State::failed, "failure state");
    require(session.pendingCount() == 0, "failure retained pending actions");
}

void rejectsInvalidActionListBeforeSending(const char *script)
{
    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    const auto sentCount = adapter.sent.size();
    const auto result = session.observe(observation("bms.partial_invalid"), start);
    require(!result.success, "invalid action list accepted");
    require(adapter.sent.size() == sentCount, "partially invalid list sent an action");
}

void reportsMalformedActionsAndScriptErrors(const char *script)
{
    for (const char *type : {
             "bms.invalid_delay", "bms.invalid_fields", "bms.sparse",
             "bms.script_error"})
    {
        FakeAdapter adapter;
        auto session = makeSession(adapter);
        const LuaScriptSession::TimePoint start{};
        requireSuccess(session.load(script));
        requireSuccess(session.start(start));
        const auto result = session.observe(observation(type), start);
        require(!result.success && !result.error.empty(), "invalid action accepted");
        require(session.state() == LuaScriptSession::State::failed, "failure state");
        require(session.pendingCount() == 0, "failure retained pending actions");
    }

    FakeAdapter adapter;
    auto session = makeSession(adapter);
    const LuaScriptSession::TimePoint start{};
    requireSuccess(session.load(script));
    requireSuccess(session.start(start));
    requireSuccess(session.observe(observation("bms.metatable"), start));
    require(adapter.logs.back() == "raw list", "raw action list was not processed");
}

void rejectsMissingCallbacks()
{
    bool rejected = false;
    try
    {
        LuaScriptSession session({}, [](const auto &) {});
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected, "missing send callback accepted");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "Expected paths to Lua fixtures\n";
        return 1;
    }

    try
    {
        lifecycleAndCancellation(argv[1]);
        pauseAndResume(argv[1]);
        overdueActionsRunInDeadlineOrder(argv[1]);
        dueActionsPreserveOrderAndFutureActions(argv[1]);
        failuresAndRecovery(argv[1], argv[2]);
        delayedFailure(argv[1]);
        rejectsInvalidActionListBeforeSending(argv[1]);
        reportsMalformedActionsAndScriptErrors(argv[1]);
        rejectsMissingCallbacks();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
