#pragma once

#include "can/CanBus.h"
#include "can/CyclicCanSender.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class DbcModel;

/// Tab 2 — left pane: system_commands TX controls (BMU → BMS).
///           right pane: BMS overview split into States / Temperatures /
///           Voltages.
/// Renders inline into the current tab item (no Begin/End window).
class SystemPanel
{
  public:
    void setDbc(const DbcModel *dbc);
    void setCanBus(CanBus *bus);
    void pushFrame(const DecodedFrame &frame);
    void render();
    bool transmitFailed() const;

  private:
    void findMessages();
    bool encodeCommand(CanFrame &frame) const;
    void updateCyclicSender();
    void renderCommands();
    void renderStates();
    void renderControlMessageValues();
    void renderContactorBox();
    void updateContactorCommandState();
    void renderTemperatures();
    void renderVoltages();
    void renderBalancing();

    struct MessageView
    {
        std::string name;
        std::vector<std::string> signals;
    };

    const DbcModel *dbc_ = nullptr;
    CanBus *bus_ = nullptr;
    std::unique_ptr<CyclicCanSender> cyclicSender_;

    // ── TX: system_commands ───────────────────────────────────────────────
    uint32_t cmdId_ = 0;
    bool cmdFound_ = false;

    bool operationReq_ = false;
    bool enableImd_ = false;
    bool serviceRequest_ = false;
    bool externalBusDisconnected_ = false;

    bool cyclicEnabled_ = false;
    float cycleMs_ = 100.0f;
    bool commandError_ = false;

    // ── RX: overview signals ──────────────────────────────────────────────
    struct LiveVal
    {
        double value = 0.0;
        bool received = false;
    };

    std::unordered_set<uint32_t> watchIds_; ///< IDs of messages we consume
    std::unordered_map<std::string, LiveVal>
        liveVals_; ///< keyed by signal name

    std::vector<MessageView> controlMessageViews_;

    bool positiveContactorCommanded_ = false;
    bool negativeContactorCommanded_ = false;
    bool prechargeContactorCommanded_ = false;
    bool hasContactorCommandState_ = false;
};
