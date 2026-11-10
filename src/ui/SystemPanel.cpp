#include "ui/SystemPanel.h"

#include "can/CanBus.h"
#include "dbc/DbcModel.h"
#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

// -- Public API ---------------------------------------------------------------

void SystemPanel::setDbc(const DbcModel *dbc)
{
    dbc_ = dbc;
    findMessages();
    updateCyclicSender();
}

void SystemPanel::setCanBus(CanBus *bus)
{
    cyclicSender_.reset();
    bus_ = bus;
    commandError_ = false;
    if (bus_)
    {
        cyclicSender_ = std::make_unique<CyclicCanSender>(
            [bus](const CanFrame &frame) { return bus->send(frame); });
        updateCyclicSender();
    }
}

bool SystemPanel::transmitFailed() const
{
    return commandError_ || (cyclicSender_ && cyclicSender_->failed());
}

void SystemPanel::pushFrame(const DecodedFrame &frame)
{
    if (!watchIds_.count(frame.raw.id))
        return;

    for (const auto &[sigName, val] : frame.signals)
    {
        auto it = liveVals_.find(sigName);
        if (it != liveVals_.end())
        {
            it->second.value = val;
            it->second.received = true;
        }
    }
}

void SystemPanel::render()
{
    if (ImGui::BeginTabBar("##SystemTabs"))
    {
        if (ImGui::BeginTabItem("Control"))
        {
            ImGui::BeginChild("##ControlGridRoot", ImVec2(0.0f, 0.0f), false);
            renderStates();
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cell Temperatures"))
        {
            renderTemperatures();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cell Voltages"))
        {
            renderVoltages();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cell Balancing"))
        {
            renderBalancing();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

// -- Private ------------------------------------------------------------------

// Messages (and the signals within them) that this panel monitors.
static constexpr const char *kWatchedMessages[] = {
    "system_status",
    "imd_measurements",
    "string_sensors_1",
    "string_ntc_temps_1_6",
    "string_ntc_temps_7_8",
    "cell_temp_summary",
    "string_input_voltages_and_pwm_1",
    "string_aux_outputs",
    "operational_values_1",
    "operational_values_2",
    "cell_voltage_summary",
};

static constexpr const char *kControlOverviewMessages[] = {
    "system_status",
    "imd_measurements",
    "contactor_coil_currnets",
    "string_sensors_1",
    "operational_values_1",
    "operational_values_2",
    "cell_temp_summary",
    "cell_voltage_summary",
};

static bool isInList(const std::string &name, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        if (name == list[i])
            return true;
    }
    return false;
}

void SystemPanel::findMessages()
{
    cmdFound_ = false;
    watchIds_.clear();
    liveVals_.clear();
    controlMessageViews_.clear();

    if (!dbc_)
        return;

    for (const auto &msg : dbc_->messages())
    {
        if (msg.name == "system_commands")
        {
            cmdId_ = msg.id;
            cmdFound_ = true;
            continue;
        }

        bool watch = false;
        watch = isInList(
            msg.name,
            kWatchedMessages,
            sizeof(kWatchedMessages) / sizeof(kWatchedMessages[0]));

        const bool showInControlOverview = isInList(
            msg.name,
            kControlOverviewMessages,
            sizeof(kControlOverviewMessages) /
                sizeof(kControlOverviewMessages[0]));

        if (showInControlOverview)
        {
            MessageView mv;
            mv.name = msg.name;
            mv.signals.reserve(msg.signals.size());
            for (const auto &sig : msg.signals)
                mv.signals.push_back(sig.name);
            controlMessageViews_.push_back(std::move(mv));
            watch = true;
        }

        // Watch all current/future per-cell module frames by prefix.
        if (!watch)
        {
            watch = msg.name.rfind("cell_temp_", 0) == 0 ||
                    msg.name.rfind("cell_voltage_", 0) == 0 ||
                    msg.name.rfind("cell_balance_current_", 0) == 0 ||
                    msg.name.rfind("cell_balancing_status_", 0) == 0 ||
                    msg.name.rfind("cell_balancing_commands_", 0) == 0 ||
                    msg.name.rfind("string_ntc_temps_", 0) == 0;
        }

        if (watch)
        {
            watchIds_.insert(msg.id);
            for (const auto &sig : msg.signals)
                liveVals_.emplace(sig.name, LiveVal{});
        }
    }
}

bool SystemPanel::encodeCommand(CanFrame &frame) const
{
    if (!dbc_ || !cmdFound_)
        return false;

    std::vector<std::pair<std::string, double>> vals = {
        {"operation_req", operationReq_ ? 1.0 : 0.0},
        {"enable_imd", enableImd_ ? 1.0 : 0.0},
        {"service_request", serviceRequest_ ? 1.0 : 0.0},
        {"external_bus_disconnected", externalBusDisconnected_ ? 1.0 : 0.0},
    };

    return dbc_->encode(cmdId_, vals, frame);
}

void SystemPanel::updateCyclicSender()
{
    if (!cyclicSender_)
        return;

    CanFrame frame;
    if (!encodeCommand(frame))
    {
        commandError_ = true;
        cyclicSender_->configure(frame, std::chrono::milliseconds{100}, false);
        return;
    }
    commandError_ = false;
    cyclicSender_->configure(
        frame,
        std::chrono::milliseconds{static_cast<int>(cycleMs_)},
        cyclicEnabled_);
}

void SystemPanel::renderCommands()
{
    ImGui::Text("Commands");
    if (!cmdFound_)
    {
        ImGui::TextColored(
            ImVec4(1, 0.4f, 0.4f, 1), "Message not found in DBC");
        return;
    }
    ImGui::Separator();

    constexpr ImGuiTableFlags kCmdTableFlags =
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##CmdVarsTable", 2, kCmdTableFlags))
    {
        ImGui::TableNextColumn();
        bool changed = ImGui::Checkbox("operation_req", &operationReq_);
        changed |= ImGui::Checkbox("enable_imd", &enableImd_);
        ImGui::TableNextColumn();
        changed |= ImGui::Checkbox("service_request", &serviceRequest_);
        changed |= ImGui::Checkbox(
            "external_bus_disconnected", &externalBusDisconnected_);
        if (changed)
            updateCyclicSender();
        ImGui::EndTable();
    }

    ImGui::Separator();

    bool changed = ImGui::Checkbox("Cyclic TX", &cyclicEnabled_);
    if (cyclicEnabled_)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        changed |=
            ImGui::InputFloat("ms##cyc", &cycleMs_, 10.0f, 100.0f, "%.0f");
        if (!std::isfinite(cycleMs_))
            cycleMs_ = 100.0f;
        cycleMs_ = std::clamp(cycleMs_, 1.0f, 86400000.0f);
    }
    if (changed)
        updateCyclicSender();
    if (cyclicSender_ && cyclicSender_->failed())
        ImGui::TextColored(
            ImVec4(1, 0.4f, 0.4f, 1),
            "CAN transmit failed. Disable and re-enable cyclic TX to retry.");

    ImGui::Spacing();
    const bool canSend = bus_ && bus_->isOpen();
    if (!canSend)
        ImGui::BeginDisabled();
    if (ImGui::Button("Send Once", ImVec2(100.0f, 0.0f)))
    {
        CanFrame frame;
        commandError_ = !encodeCommand(frame) || !cyclicSender_ ||
                        !cyclicSender_->sendOnce(frame);
    }
    if (!canSend)
    {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(CAN not connected)");
    }
}

void SystemPanel::renderStates()
{
    updateContactorCommandState();

    ImGui::Text("Control Overview");
    ImGui::Separator();

    auto getInt = [&](const char *sig, bool &ok) -> int {
        auto it = liveVals_.find(sig);
        ok = (it != liveVals_.end() && it->second.received);
        return ok ? static_cast<int>(it->second.value) : 0;
    };

    constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_Borders |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_SizingFixedFit;

    auto row = [&](const char *label, const char *sig) {
        bool ok;
        const int iv = getInt(sig, ok);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        if (!ok)
            ImGui::TextDisabled("-");
        else
            ImGui::Text("%d", iv);
        ImGui::TableSetColumnIndex(2);
        const char *lbl = (ok && dbc_)
                              ? dbc_->valueLabel(sig, static_cast<int64_t>(iv))
                              : nullptr;
        ImGui::TextDisabled("%s", lbl ? lbl : "-");
    };

    constexpr ImGuiTableFlags kGridTbl = ImGuiTableFlags_SizingStretchSame;
    const float commandH = 230.0f;
    const float contactorH = 160.0f;
    const float topSectionH =
        commandH + ImGui::GetStyle().ItemSpacing.y + contactorH;

    if (ImGui::BeginTable("##ControlOverviewGrid", 2, kGridTbl))
    {
        ImGui::TableNextColumn();
        if (ImGui::BeginChild(
                "##LeftControlStack",
                ImVec2(0.0f, topSectionH),
                false,
                ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse))
        {
            if (ImGui::BeginChild(
                    "##CmdCard",
                    ImVec2(0.0f, commandH),
                    true,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse))
                renderCommands();
            ImGui::EndChild();

            ImGui::Spacing();

            if (ImGui::BeginChild(
                    "##ContactorBox",
                    ImVec2(0.0f, contactorH),
                    true,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse))
                renderContactorBox();
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild(
                "##CoreStatesBox",
                ImVec2(0.0f, topSectionH),
                true,
                ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse))
        {
            if (ImGui::BeginTable("##CoreStatesTbl", 3, kTbl))
            {
                ImGui::TableSetupColumn(
                    "Signal", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    "Value", ImGuiTableColumnFlags_WidthFixed, 52.0f);
                ImGui::TableSetupColumn(
                    "Label", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableHeadersRow();

                row("hsm_state", "hsm_state");
                row("hvil_state", "hvil_state");
                row("imd_active", "imd_active");
                row("imd_test_status", "imd_test_status");

                {
                    bool ok;
                    const int iv = getInt("disconnect_forewarning", ok);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("disconnect_forewarning");
                    ImGui::TableSetColumnIndex(1);
                    if (!ok)
                        ImGui::TextDisabled("-");
                    else if (iv == 0)
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "0");
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "1");
                    ImGui::TableSetColumnIndex(2);
                    if (ok)
                    {
                        const char *lbl = dbc_ ? dbc_->valueLabel(
                                                     "disconnect_forewarning",
                                                     static_cast<int64_t>(iv))
                                               : nullptr;
                        ImGui::TextDisabled(
                            "%s", lbl ? lbl : (iv ? "WARN" : "OK"));
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }
                }

                row("bus_connection_state", "bus_connection_state");
                row("manual_disconnect_state", "manual_disconnect_state");

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::BeginChild("##MessageValuesBox", ImVec2(0.0f, 0.0f), true))
        renderControlMessageValues();
    ImGui::EndChild();
}

void SystemPanel::updateContactorCommandState()
{
    auto it = liveVals_.find("hsm_state");
    if (it == liveVals_.end() || !it->second.received)
    {
        hasContactorCommandState_ = false;
        return;
    }

    hasContactorCommandState_ = true;
    const int64_t hsm = static_cast<int64_t>(it->second.value);
    const char *hsmLabel = dbc_ ? dbc_->valueLabel("hsm_state", hsm) : nullptr;
    const std::string state = hsmLabel ? std::string(hsmLabel) : std::string();

    if (state == "REGULAR_OPERATION" || state == "HV_OPERATION" ||
        state == "HV_READY")
    {
        positiveContactorCommanded_ = true;
        negativeContactorCommanded_ = true;
        prechargeContactorCommanded_ = false;
        return;
    }

    if (state == "TOP" || state == "INIT" || state == "STANDBY" ||
        state == "FAULTED_STANDBY" || state == "SLEEP" || state == "SERVICE")
    {
        positiveContactorCommanded_ = false;
        negativeContactorCommanded_ = false;
        prechargeContactorCommanded_ = false;
        return;
    }

    if (state.find("CLOSE_NEGATIVE_CONTACTOR") != std::string::npos)
        negativeContactorCommanded_ = true;
    if (state.find("CLOSE_PRECHARGE_CONTACTOR") != std::string::npos)
        prechargeContactorCommanded_ = true;
    if (state.find("CLOSE_POSITIVE_CONTACTOR") != std::string::npos)
        positiveContactorCommanded_ = true;

    if (state.find("OPEN_NEG_CON") != std::string::npos)
        negativeContactorCommanded_ = false;
    if (state.find("OPEN_PRECHARGE_CONTACTOR") != std::string::npos)
        prechargeContactorCommanded_ = false;
    if (state.find("OPEN_PRE_POS_CON") != std::string::npos)
    {
        prechargeContactorCommanded_ = false;
        positiveContactorCommanded_ = false;
    }
}

void SystemPanel::renderContactorBox()
{
    ImGui::Text("Contactors");
    ImGui::SameLine();
    ImGui::TextDisabled("(Blue: Command  Green: Aux)");
    ImGui::Separator();

    struct ContactorSpec
    {
        const char *title;
        const char *stateSig;
        const char *auxSig;
        const char *coilCurrentSig;
        const bool *commanded;
    };

    const ContactorSpec specs[] = {
        {"Positive",
         "positive_contactor_state",
         "positive_contactor_aux_state",
         "pos_con_coil_current",
         &positiveContactorCommanded_},
        {"Negative",
         "negative_contactor_state",
         "negative_contactor_aux_state",
         "neg_con_coil_current",
         &negativeContactorCommanded_},
        {"Precharge",
         "precharge_contactor_state",
         "precharge_contactor_aux_state",
         "pre_con_coil_current",
         &prechargeContactorCommanded_},
    };

    constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_SizingStretchSame;
    if (!ImGui::BeginTable("##ContactorTable", 3, kTableFlags))
        return;

    for (int i = 0; i < 3; ++i)
    {
        const ContactorSpec &s = specs[i];
        ImGui::TableNextColumn();

        char tileId[48];
        std::snprintf(tileId, sizeof(tileId), "##contactor_%d", i);
        ImGui::BeginChild(
            tileId,
            ImVec2(0.0f, 84.0f),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        auto stIt = liveVals_.find(s.stateSig);
        auto auxIt = liveVals_.find(s.auxSig);
        auto coilIt = liveVals_.find(s.coilCurrentSig);
        const bool hasState =
            (stIt != liveVals_.end() && stIt->second.received);
        const bool hasAux =
            (auxIt != liveVals_.end() && auxIt->second.received);
        const bool hasCoil =
            (coilIt != liveVals_.end() && coilIt->second.received);

        const bool cmdOn = hasContactorCommandState_ ? *s.commanded : false;
        const bool auxOn = hasAux && auxIt->second.value > 0.5;

        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        const float boxSize = 9.0f;
        const float pad = 5.0f;
        const float gap = 4.0f;

        const ImVec2 pBlue0(
            winPos.x + winSize.x - (boxSize * 2.0f) - gap - pad,
            winPos.y + pad);
        const ImVec2 pBlue1(pBlue0.x + boxSize, pBlue0.y + boxSize);
        const ImVec2 pAux0(
            winPos.x + winSize.x - boxSize - pad, winPos.y + pad);
        const ImVec2 pAux1(pAux0.x + boxSize, pAux0.y + boxSize);

        ImU32 blueCol = hasContactorCommandState_
                            ? (cmdOn ? IM_COL32(70, 140, 255, 255)
                                     : IM_COL32(110, 110, 110, 255))
                            : IM_COL32(80, 80, 80, 255);
        ImU32 auxCol = hasAux ? (auxOn ? IM_COL32(45, 210, 90, 255)
                                       : IM_COL32(110, 110, 110, 255))
                              : IM_COL32(80, 80, 80, 255);

        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pBlue0, pBlue1, blueCol, 2.0f);
        draw->AddRect(pBlue0, pBlue1, IM_COL32(30, 30, 30, 255), 2.0f);
        draw->AddRectFilled(pAux0, pAux1, auxCol, 2.0f);
        draw->AddRect(pAux0, pAux1, IM_COL32(30, 30, 30, 255), 2.0f);

        ImGui::TextDisabled("%s", s.title);

        const char *stateLabel = nullptr;
        if (hasState && dbc_)
            stateLabel = dbc_->valueLabel(
                s.stateSig, static_cast<int64_t>(stIt->second.value));
        if (!stateLabel)
            stateLabel = hasState ? "Unknown" : "N/A";

        char coilText[32];
        if (hasCoil)
            std::snprintf(
                coilText, sizeof(coilText), "%.3f A", coilIt->second.value);
        else
            std::snprintf(coilText, sizeof(coilText), "N/A");

        const ImVec2 stateSize = ImGui::CalcTextSize(stateLabel);
        const ImVec2 coilSize = ImGui::CalcTextSize(coilText);
        const float stackedH =
            stateSize.y + ImGui::GetStyle().ItemSpacing.y + coilSize.y;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > stackedH)
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + (avail.y - stackedH) * 0.5f);
        if (avail.x > stateSize.x)
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() + (avail.x - stateSize.x) * 0.5f);
        if (hasState)
            ImGui::TextUnformatted(stateLabel);
        else
            ImGui::TextDisabled("%s", stateLabel);

        if (avail.x > coilSize.x)
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() + (avail.x - coilSize.x) * 0.5f);
        if (hasCoil)
            ImGui::Text("%s", coilText);
        else
            ImGui::TextDisabled("%s", coilText);

        ImGui::EndChild();
    }

    ImGui::EndTable();
}

void SystemPanel::renderControlMessageValues()
{
    ImGui::Text("Message Values");
    ImGui::Separator();

    if (controlMessageViews_.empty())
    {
        ImGui::TextDisabled("No configured control messages found in DBC.");
        return;
    }

    for (const MessageView &msg : controlMessageViews_)
    {
        if (!ImGui::CollapsingHeader(
                msg.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;

        char tableId[96];
        std::snprintf(
            tableId, sizeof(tableId), "##CtrlMsgTbl_%s", msg.name.c_str());
        constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_Borders |
                                         ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingStretchProp;

        if (!ImGui::BeginTable(tableId, 3, kTbl))
            continue;

        ImGui::TableSetupColumn(
            "Signal", ImGuiTableColumnFlags_WidthStretch, 1.7f);
        ImGui::TableSetupColumn(
            "Value", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(
            "Label", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableHeadersRow();

        for (const std::string &sig : msg.signals)
        {
            auto it = liveVals_.find(sig);
            const bool has = (it != liveVals_.end() && it->second.received);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sig.c_str());

            ImGui::TableSetColumnIndex(1);
            if (!has)
            {
                ImGui::TextDisabled("-");
            }
            else
            {
                const double v = it->second.value;
                const double iv = static_cast<double>(static_cast<int64_t>(v));
                if (std::fabs(v - iv) < 1e-6)
                    ImGui::Text(
                        "%lld",
                        static_cast<long long>(static_cast<int64_t>(v)));
                else
                    ImGui::Text("%.3f", v);
            }

            ImGui::TableSetColumnIndex(2);
            if (!has || !dbc_)
            {
                ImGui::TextDisabled("-");
            }
            else
            {
                const int64_t enumValue =
                    static_cast<int64_t>(it->second.value);
                const char *lbl = dbc_->valueLabel(sig, enumValue);
                ImGui::TextDisabled("%s", lbl ? lbl : "-");
            }
        }

        ImGui::EndTable();
    }
}

void SystemPanel::renderTemperatures()
{
    ImGui::Text("Cell Temperatures");
    ImGui::Separator();

    ImGui::TextDisabled("Summary");

    auto drawSummaryCard = [&](const char *id,
                               const char *title,
                               const char *sig,
                               const char *idxSig = nullptr) {
        const ImVec2 cardSize(220.0f, 82.0f);
        ImGui::BeginChild(id, cardSize, true);

        ImGui::TextDisabled("%s", title);
        ImGui::Separator();

        auto itV = liveVals_.find(sig);
        const bool hasVal = (itV != liveVals_.end() && itV->second.received);

        if (!hasVal)
        {
            ImGui::TextDisabled("N/A");
        }
        else if (idxSig)
        {
            auto itI = liveVals_.find(idxSig);
            const bool hasIdx =
                (itI != liveVals_.end() && itI->second.received);
            if (hasIdx)
                ImGui::Text(
                    "%.1f degC  [Cell %d]",
                    itV->second.value,
                    static_cast<int>(itI->second.value));
            else
                ImGui::Text("%.1f degC", itV->second.value);
        }
        else
        {
            ImGui::Text("%.1f degC", itV->second.value);
        }

        ImGui::EndChild();
    };

    drawSummaryCard("##AvgTempCard", "Average", "avg_cell_temp");
    ImGui::SameLine();
    drawSummaryCard(
        "##MaxTempCard", "Maximum", "max_cell_temp", "max_cell_temp_index");
    ImGui::SameLine();
    drawSummaryCard(
        "##MinTempCard", "Minimum", "min_cell_temp", "min_cell_temp_index");

    ImGui::Spacing();
    ImGui::Text("Module Thermistor Grid");

    constexpr int kCellsPerModule = 12;
    constexpr int kThermistorsPerModule = 2;
    constexpr int kTotalModules = 16;
    constexpr int kModuleCols = 2;
    constexpr float kTileHeight = 48.0f;
    constexpr float kModuleCardHeight = 100.0f;

    constexpr ImGuiTableFlags kGridTbl = ImGuiTableFlags_Borders |
                                         ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingStretchSame;
    constexpr ImGuiTableFlags kModuleTbl = ImGuiTableFlags_SizingStretchSame;

    if (!ImGui::BeginTable("##TempModuleColumns", kModuleCols, kModuleTbl))
        return;

    for (int module = 0; module < kTotalModules; ++module)
    {
        ImGui::TableNextColumn();

        char moduleHdr[96];
        const int firstCell = module * kCellsPerModule + 1;
        const int lastCell = (module + 1) * kCellsPerModule;
        std::snprintf(
            moduleHdr,
            sizeof(moduleHdr),
            "Module %02d  (Cells %d-%d)",
            module + 1,
            firstCell,
            lastCell);

        char moduleCardId[48];
        std::snprintf(
            moduleCardId, sizeof(moduleCardId), "##TempModuleCard_%d", module);
        ImGui::BeginChild(
            moduleCardId,
            ImVec2(0.0f, kModuleCardHeight),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextDisabled("%s", moduleHdr);
        ImGui::Spacing();

        char moduleTblId[48];
        std::snprintf(
            moduleTblId, sizeof(moduleTblId), "##TempModule_%d", module);
        if (!ImGui::BeginTable(moduleTblId, kThermistorsPerModule, kGridTbl))
        {
            ImGui::EndChild();
            continue;
        }

        for (int t = 0; t < kThermistorsPerModule; ++t)
        {
            ImGui::TableNextColumn();

            char tileId[40];
            std::snprintf(
                tileId, sizeof(tileId), "##tempTile_m%d_t%d", module, t);
            ImGui::BeginChild(tileId, ImVec2(0.0f, kTileHeight), false);

            const int thermSignalIndex = module * kThermistorsPerModule + t;
            char sig[32];
            std::snprintf(sig, sizeof(sig), "cell_temp_%d", thermSignalIndex);
            auto it = liveVals_.find(sig);
            const bool ok = (it != liveVals_.end() && it->second.received);

            ImGui::SetWindowFontScale(0.82f);
            ImGui::TextDisabled(
                "T%d (Therm %02d)", t + 1, thermSignalIndex + 1);
            ImGui::SetWindowFontScale(1.0f);

            char tempText[32];
            if (ok)
                std::snprintf(
                    tempText, sizeof(tempText), "%.1f degC", it->second.value);
            else
                std::snprintf(tempText, sizeof(tempText), "N/A");

            const ImVec2 textSize = ImGui::CalcTextSize(tempText);
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float cx = (avail.x - textSize.x) * 0.5f;
            const float cy = (avail.y - textSize.y) * 0.5f;
            if (cx > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
            if (cy > 0.0f)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + cy);

            if (ok)
                ImGui::TextUnformatted(tempText);
            else
                ImGui::TextDisabled("%s", tempText);

            ImGui::EndChild();
        }

        ImGui::EndTable();
        ImGui::EndChild();
    }

    ImGui::EndTable();
}

void SystemPanel::renderVoltages()
{
    ImGui::Text("Voltages");
    ImGui::Separator();

    ImGui::TextDisabled("Summary");

    auto drawSummaryCard = [&](const char *id,
                               const char *title,
                               const char *sig,
                               const char *idxSig = nullptr) {
        const ImVec2 cardSize(220.0f, 82.0f);
        ImGui::BeginChild(id, cardSize, true);

        ImGui::TextDisabled("%s", title);
        ImGui::Separator();

        auto itV = liveVals_.find(sig);
        const bool hasVal = (itV != liveVals_.end() && itV->second.received);

        if (!hasVal)
        {
            ImGui::TextDisabled("N/A");
        }
        else if (idxSig)
        {
            auto itI = liveVals_.find(idxSig);
            const bool hasIdx =
                (itI != liveVals_.end() && itI->second.received);
            if (hasIdx)
                ImGui::Text(
                    "%.2f V  [Cell %d]",
                    itV->second.value,
                    static_cast<int>(itI->second.value));
            else
                ImGui::Text("%.2f V", itV->second.value);
        }
        else
        {
            ImGui::Text("%.2f V", itV->second.value);
        }

        ImGui::EndChild();
    };

    drawSummaryCard("##AvgVoltCard", "Average", "avg_cell_voltage");
    ImGui::SameLine();
    drawSummaryCard(
        "##MaxVoltCard",
        "Maximum",
        "max_cell_voltage",
        "max_cell_voltage_index");
    ImGui::SameLine();
    drawSummaryCard(
        "##MinVoltCard",
        "Minimum",
        "min_cell_voltage",
        "min_cell_voltage_index");

    ImGui::Spacing();
    ImGui::Text("Module Voltage Grid");

    constexpr int kCellsPerModule = 12;
    constexpr int kTotalModules = 16;
    constexpr int kModuleCols = 2;
    constexpr int kGridCols = 6;
    constexpr float kTileHeight = 48.0f;
    constexpr float kModuleCardHeight = 160.0f;
    constexpr ImGuiTableFlags kGridTbl = ImGuiTableFlags_Borders |
                                         ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingStretchSame;
    constexpr ImGuiTableFlags kModuleTbl = ImGuiTableFlags_SizingStretchSame;

    if (!ImGui::BeginTable("##VoltModuleColumns", kModuleCols, kModuleTbl))
        return;

    for (int module = 0; module < kTotalModules; ++module)
    {
        ImGui::TableNextColumn();

        char moduleHdr[96];
        std::snprintf(
            moduleHdr,
            sizeof(moduleHdr),
            "Module %02d  (Cells %d-%d)",
            module + 1,
            module * kCellsPerModule,
            module * kCellsPerModule + (kCellsPerModule - 1));

        char moduleCardId[48];
        std::snprintf(
            moduleCardId, sizeof(moduleCardId), "##VoltModuleCard_%d", module);
        ImGui::BeginChild(
            moduleCardId,
            ImVec2(0.0f, kModuleCardHeight),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextDisabled("%s", moduleHdr);
        ImGui::Spacing();

        char moduleTblId[48];
        std::snprintf(
            moduleTblId, sizeof(moduleTblId), "##VoltModule_%d", module);
        if (!ImGui::BeginTable(moduleTblId, kGridCols, kGridTbl))
        {
            ImGui::EndChild();
            continue;
        }

        for (int c = 0; c < kCellsPerModule; ++c)
        {
            ImGui::TableNextColumn();

            const int cell = module * kCellsPerModule + c;
            char tileId[32];
            std::snprintf(tileId, sizeof(tileId), "##voltTile_%d", cell);
            ImGui::BeginChild(tileId, ImVec2(0.0f, kTileHeight), false);

            char sig[32];
            std::snprintf(sig, sizeof(sig), "cell_voltage_%d", cell);
            auto it = liveVals_.find(sig);
            const bool ok = (it != liveVals_.end() && it->second.received);

            ImGui::SetWindowFontScale(0.82f);
            ImGui::TextDisabled("%02d", cell);
            ImGui::SetWindowFontScale(1.0f);

            char voltText[32];
            if (ok)
                std::snprintf(
                    voltText, sizeof(voltText), "%.2f V", it->second.value);
            else
                std::snprintf(voltText, sizeof(voltText), "N/A");

            const ImVec2 textSize = ImGui::CalcTextSize(voltText);
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float cx = (avail.x - textSize.x) * 0.5f;
            const float cy = (avail.y - textSize.y) * 0.5f;
            if (cx > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
            if (cy > 0.0f)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + cy);

            if (ok)
                ImGui::TextUnformatted(voltText);
            else
                ImGui::TextDisabled("%s", voltText);

            ImGui::EndChild();
        }

        ImGui::EndTable();
        ImGui::EndChild();
    }

    ImGui::EndTable();
}

void SystemPanel::renderBalancing()
{
    ImGui::Text("Cell Balancing");
    ImGui::Separator();

    constexpr int kCellsPerModule = 12;
    constexpr int kTotalModules = 16;
    constexpr int kModuleCols = 2;

    int activeCount = 0;
    for (int cell = 0; cell < kTotalModules * kCellsPerModule; ++cell)
    {
        char statusSig[64];
        std::snprintf(
            statusSig, sizeof(statusSig), "cell_%d_balance_status", cell);
        auto stIt = liveVals_.find(statusSig);
        const bool hasStatus =
            (stIt != liveVals_.end() && stIt->second.received);
        if (hasStatus && stIt->second.value > 0.5)
            ++activeCount;
    }

    ImGui::Text(
        "Active cells: %d / %d", activeCount, kTotalModules * kCellsPerModule);
    ImGui::Spacing();
    ImGui::TextDisabled("Blue: command  Green: active");

    ImGui::Text("Cell Balancing Grid");

    constexpr ImGuiTableFlags kGridTbl = ImGuiTableFlags_Borders |
                                         ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingStretchSame;
    constexpr int kGridCols = 6;
    constexpr float kTileHeight = 48.0f;
    constexpr float kModuleCardHeight = 160.0f;
    constexpr ImGuiTableFlags kModuleTbl = ImGuiTableFlags_SizingStretchSame;

    if (!ImGui::BeginTable("##BalModuleColumns", kModuleCols, kModuleTbl))
        return;

    for (int module = 0; module < kTotalModules; ++module)
    {
        ImGui::TableNextColumn();

        char moduleHdr[96];
        std::snprintf(
            moduleHdr,
            sizeof(moduleHdr),
            "Module %02d  (Cells %d-%d)",
            module + 1,
            module * kCellsPerModule,
            module * kCellsPerModule + (kCellsPerModule - 1));

        char moduleCardId[48];
        std::snprintf(
            moduleCardId, sizeof(moduleCardId), "##BalModuleCard_%d", module);
        ImGui::BeginChild(
            moduleCardId,
            ImVec2(0.0f, kModuleCardHeight),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextDisabled("%s", moduleHdr);
        ImGui::Spacing();

        char moduleTblId[48];
        std::snprintf(
            moduleTblId, sizeof(moduleTblId), "##BalModule_%d", module);
        if (!ImGui::BeginTable(moduleTblId, kGridCols, kGridTbl))
        {
            ImGui::EndChild();
            continue;
        }

        for (int c = 0; c < kCellsPerModule; ++c)
        {
            ImGui::TableNextColumn();

            const int cell = module * kCellsPerModule + c;

            char tileId[32];
            std::snprintf(tileId, sizeof(tileId), "##balTile_%d", cell);
            ImGui::BeginChild(tileId, ImVec2(0.0f, kTileHeight), false);

            char statusSig[64];
            char commandSig[64];
            char currentSig[64];
            std::snprintf(
                statusSig, sizeof(statusSig), "cell_%d_balance_status", cell);
            std::snprintf(
                commandSig,
                sizeof(commandSig),
                "cell_%d_balance_command",
                cell);
            // DBC signal uses "balnce" typo; keep exact signal name for lookup.
            std::snprintf(
                currentSig, sizeof(currentSig), "cell_%d_balnce_current", cell);

            auto stIt = liveVals_.find(statusSig);
            auto cmdIt = liveVals_.find(commandSig);
            auto curIt = liveVals_.find(currentSig);

            const bool hasStatus =
                (stIt != liveVals_.end() && stIt->second.received);
            const bool hasCommand =
                (cmdIt != liveVals_.end() && cmdIt->second.received);
            const bool hasCurrent =
                (curIt != liveVals_.end() && curIt->second.received);
            const bool commanded = hasCommand && cmdIt->second.value > 0.5;
            const bool active = hasStatus && stIt->second.value > 0.5;

            // Small command + status indicators in the top-right corner.
            {
                const ImVec2 winPos = ImGui::GetWindowPos();
                const ImVec2 winSize = ImGui::GetWindowSize();
                const float boxSize = 8.0f;
                const float pad = 4.0f;
                const float gap = 4.0f;

                const ImVec2 pBlue0(
                    winPos.x + winSize.x - (boxSize * 2.0f) - gap - pad,
                    winPos.y + pad);
                const ImVec2 pBlue1(pBlue0.x + boxSize, pBlue0.y + boxSize);
                const ImVec2 pGreen0(
                    winPos.x + winSize.x - boxSize - pad, winPos.y + pad);
                const ImVec2 pGreen1(pGreen0.x + boxSize, pGreen0.y + boxSize);

                ImU32 blueCol = IM_COL32(80, 80, 80, 255);
                if (hasCommand)
                    blueCol = commanded ? IM_COL32(70, 140, 255, 255)
                                        : IM_COL32(110, 110, 110, 255);

                ImU32 greenCol = IM_COL32(80, 80, 80, 255);
                if (hasStatus)
                    greenCol = active ? IM_COL32(45, 210, 90, 255)
                                      : IM_COL32(110, 110, 110, 255);

                ImDrawList *draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(pBlue0, pBlue1, blueCol, 2.0f);
                draw->AddRect(pBlue0, pBlue1, IM_COL32(30, 30, 30, 255), 2.0f);
                draw->AddRectFilled(pGreen0, pGreen1, greenCol, 2.0f);
                draw->AddRect(
                    pGreen0, pGreen1, IM_COL32(30, 30, 30, 255), 2.0f);
            }

            ImGui::SetWindowFontScale(0.82f);
            ImGui::TextDisabled("%02d", cell);
            ImGui::SetWindowFontScale(1.0f);

            char balText[48];
            if (!hasStatus || !hasCurrent)
            {
                std::snprintf(balText, sizeof(balText), "N/A");
            }
            else
            {
                std::snprintf(
                    balText, sizeof(balText), "%.2f A", curIt->second.value);
            }

            const ImVec2 textSize = ImGui::CalcTextSize(balText);
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float cx = (avail.x - textSize.x) * 0.5f;
            const float cy = (avail.y - textSize.y) * 0.5f;
            if (cx > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
            if (cy > 0.0f)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + cy);

            if (!hasStatus || !hasCurrent)
                ImGui::TextDisabled("%s", balText);
            else
                ImGui::TextUnformatted(balText);

            ImGui::EndChild();
        }

        ImGui::EndTable();
        ImGui::EndChild();
    }

    ImGui::EndTable();
}
