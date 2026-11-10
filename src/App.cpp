#include <windows.h>

#include "App.h"
#include "EmbeddedDbc.h"
#include "EmbeddedFaultDbc.h"
#include "can/VirtualInterface.h"
#include "imgui.h"

static constexpr int kBaudrates[] = {1000000, 500000, 250000, 125000};
static constexpr const char *kBaudrateLabels[] = {
    "1 Mbit/s", "500 kbit/s", "250 kbit/s", "125 kbit/s"};

App::App()
{
    dbc_.loadFromString(EmbeddedDbc::kContent);
    faultDbc_.loadFromString(EmbeddedFaultDbc::kContent);
    systemPanel_.setDbc(&dbc_);
    faultPanel_.setDbc(&faultDbc_);
    plotPanel_.setDbc(&dbc_);

    ImGuiIO &io = ImGui::GetIO();
    textScale_ = io.FontGlobalScale;
    dockingEnabled_ = (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0;
}
App::~App()
{
    disconnectCan();
}

void App::render()
{
    // ── Full-screen host window ───────────────────────────────────────────
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    constexpr ImGuiWindowFlags kHostFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##Host", nullptr, kHostFlags);
    ImGui::PopStyleVar(2);

    renderMenuBar();

    // ── Drain CAN RX frames and distribute to all panels ─────────────────
    if (bus_ && bus_->isOpen())
    {
        auto frames = bus_->drainRxFrames();
        for (const auto &f : frames)
        {
            busMonitor_.pushFrame(f);
            systemPanel_.pushFrame(f);
            faultPanel_.pushFrame(f);
            parametersPanel_.pushFrame(f);
            plotPanel_.pushFrame(f);
        }
    }

    // ── Tab bar ───────────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("##MainTabs"))
    {
        if (ImGui::BeginTabItem("Bus Monitor"))
        {
            busMonitor_.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Signals"))
        {
            systemPanel_.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Faults"))
        {
            faultPanel_.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Plots"))
        {
            plotPanel_.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Parameters"))
        {
            parametersPanel_.render();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    if (showConnectWindow_)
        renderConnectWindow();
    if (showSettingsWindow_)
        renderSettingsWindow();

    if (showDemoWindow_)
        ImGui::ShowDemoWindow(&showDemoWindow_);
    if (showMetricsWindow_)
        ImGui::ShowMetricsWindow(&showMetricsWindow_);
}

void App::renderMenuBar()
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Exit"))
            PostQuitMessage(0);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("CAN"))
    {
        const bool connected = bus_ && bus_->isOpen();
        if (!connected && ImGui::MenuItem("Connect\u2026"))
        {
            showConnectWindow_ = true;
            pcanDevices_ = PcanInterface::scanDevices();
            pcanDeviceIdx_ = 0;
        }
        if (connected && ImGui::MenuItem("Disconnect"))
            disconnectCan();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Settings", nullptr, &showSettingsWindow_);
        ImGui::EndMenu();
    }

    // Right-aligned status indicators
    const char *busLabel =
        (bus_ && bus_->isOpen()) ? "[CAN connected]" : "[CAN disconnected]";
    const char *dbcLabel = "bms_main_v1.dbc";
    const bool transmitFailed = systemPanel_.transmitFailed();

    float rightWidth = ImGui::CalcTextSize(busLabel).x +
                       ImGui::CalcTextSize("  ").x +
                       ImGui::CalcTextSize(dbcLabel).x + 16.0f;
    if (transmitFailed)
        rightWidth += ImGui::CalcTextSize("CAN TX failed").x +
                      ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - rightWidth);
    ImGui::TextDisabled("%s  %s", busLabel, dbcLabel);
    if (transmitFailed)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "CAN TX failed");
    }

    ImGui::EndMenuBar();
}

void App::renderConnectWindow()
{
    ImGui::SetNextWindowSize(ImVec2(400.0f, 220.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin(
            "CAN Connection",
            &showConnectWindow_,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const char *ifaceItems[] = {"Virtual (loopback)", "PCAN-Basic (USB)"};
    ImGui::Combo("Interface", &ifaceType_, ifaceItems, 2);

    if (ifaceType_ == 1)
    {
        if (pcanDevices_.empty())
        {
            ImGui::TextColored(
                ImVec4(1, 0.4f, 0.4f, 1), "No PCAN devices found");
            if (ImGui::SmallButton("Rescan"))
            {
                pcanDevices_ = PcanInterface::scanDevices();
                pcanDeviceIdx_ = 0;
            }
        }
        else
        {
            if (ImGui::BeginCombo(
                    "Device", pcanDevices_[pcanDeviceIdx_].label.c_str()))
            {
                for (int i = 0; i < static_cast<int>(pcanDevices_.size()); ++i)
                {
                    bool selected = (i == pcanDeviceIdx_);
                    if (ImGui::Selectable(
                            pcanDevices_[i].label.c_str(), selected))
                        pcanDeviceIdx_ = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::SmallButton("Rescan"))
            {
                pcanDevices_ = PcanInterface::scanDevices();
                pcanDeviceIdx_ = 0;
            }
        }
    }

    ImGui::Combo("Baud rate", &baudrateIdx_, kBaudrateLabels, 4);

    ImGui::Spacing();

    bool canConnect = true;
    if (ifaceType_ == 1 && pcanDevices_.empty())
        canConnect = false;

    if (!canConnect)
        ImGui::BeginDisabled();
    if (ImGui::Button("Connect", ImVec2(110.0f, 0.0f)))
    {
        connectCan();
        showConnectWindow_ = false;
    }
    if (!canConnect)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
        showConnectWindow_ = false;

    ImGui::End();
}

void App::applyTheme()
{
    if (themeIdx_ == 1)
        ImGui::StyleColorsLight();
    else if (themeIdx_ == 2)
        ImGui::StyleColorsClassic();
    else
        ImGui::StyleColorsDark();
}

void App::renderSettingsWindow()
{
    ImGui::SetNextWindowSize(ImVec2(420.0f, 320.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin(
            "Settings", &showSettingsWindow_, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGuiIO &io = ImGui::GetIO();

    ImGui::Text("Display");
    ImGui::Separator();

    if (ImGui::SliderFloat("Text scale", &textScale_, 0.80f, 2.00f, "%.2fx"))
        io.FontGlobalScale = textScale_;

    const char *themeItems[] = {"Dark", "Light", "Classic"};
    if (ImGui::Combo("Theme", &themeIdx_, themeItems, 3))
        applyTheme();

    if (ImGui::Checkbox("Enable docking", &dockingEnabled_))
    {
        if (dockingEnabled_)
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        else
            io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
    }

    ImGui::Spacing();
    ImGui::Text("Developer");
    ImGui::Separator();
    ImGui::Checkbox("Show ImGui Demo window", &showDemoWindow_);
    ImGui::Checkbox("Show ImGui Metrics window", &showMetricsWindow_);

    ImGui::Spacing();
    if (ImGui::Button("Reset display defaults"))
    {
        textScale_ = 1.0f;
        io.FontGlobalScale = textScale_;
        themeIdx_ = 0;
        applyTheme();
    }

    ImGui::End();
}

void App::connectCan()
{
    disconnectCan();

    std::unique_ptr<ICanInterface> iface;
    if (ifaceType_ == 0)
    {
        iface = std::make_unique<VirtualInterface>();
    }
    else
    {
        iface = std::make_unique<PcanInterface>();
    }

    bus_ = std::make_unique<CanBus>(std::move(iface));
    if (dbc_.isLoaded())
        bus_->setDbc(&dbc_);
    uint32_t channel = 0;
    if (ifaceType_ == 1 && !pcanDevices_.empty())
        channel = static_cast<uint32_t>(pcanDevices_[pcanDeviceIdx_].handle);

    const uint32_t baud = static_cast<uint32_t>(kBaudrates[baudrateIdx_]);
    if (!bus_->open(channel, baud))
    {
        bus_.reset();
        return;
    }
    systemPanel_.setCanBus(bus_.get());
    faultPanel_.setCanBus(bus_.get());
    parametersPanel_.setCanBus(bus_.get());
}

void App::disconnectCan()
{
    if (bus_)
    {
        systemPanel_.setCanBus(nullptr);
        bus_->close();
        bus_.reset();
        faultPanel_.setCanBus(nullptr);
        parametersPanel_.setCanBus(nullptr);
    }
}
