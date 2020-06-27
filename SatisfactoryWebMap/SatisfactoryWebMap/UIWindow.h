#pragma once

#include "imgui.h"
#include "imgui_internal.h"

static inline ImVec2 operator - (const ImVec2 &a)
{
    return { -a.x, -a.y };
}

static inline ImVec2 operator + (const ImVec2 &a, const ImVec2 &b)
{
    return { a.x + b.x, a.y + b.y };
}

static inline ImVec2 operator - (const ImVec2 &a, const ImVec2 &b)
{
    return { a.x - b.x, a.y - b.y };
}

static inline ImVec2 operator * (const ImVec2 &a, const ImVec2 &b)
{
    return { a.x * b.x, a.y * b.y };
}

static inline ImVec2 operator / (const ImVec2 &a, const ImVec2 &b)
{
    return { a.x / b.x, a.y / b.y };
}

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>
#include <utility>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <filesystem>

#include <tlhelp32.h>

#include "InjectHelper.h"
#include "HttpClient.h"
#include "Image.h"
#include "Utils.h"

#include "../SatisfactoryWebMapServer/Config.h"

struct ID3D11ShaderResourceView;

ID3D11ShaderResourceView *CreateTexture(const uint8_t *data, int width, int height);

const std::vector<std::string> RespTypeName = {
    "Default", "Beacon", "Crate", "Hub",
    "Ping", "Player", "Radar Tower", "Resource",
    "Space Elevator", "Starting Pod", "Train", "Train Station",
    "Vehicle", "Vehicle Docking Station",
};

struct UIWindow
{
    std::atomic<bool> stop;
    StopWatch time;

    struct WindowConfig
    {
        int width;
        int height;
    };

    WindowConfig settings {
        360 + 30, 680,
    };

    bool useCN = false;
    Config config;
    std::wstring updateUrl;
    std::wstring stopUrl;

    const ImVec4 lable_color = ImVec4(1.f, 0.84f, 0.f, 1.f);

    mutable std::shared_mutex m;

    float lastMapUpdate = 0.f;
    float lastProcessCheck = 0.f;
    DWORD processId = 0;
    std::string window_title;

    std::atomic<bool> serverStarted = false;
    std::string status;

    Image mapImage;
    ID3D11ShaderResourceView *mapTextureView = nullptr;

    Image iconImage;
    ID3D11ShaderResourceView *iconTextureView = nullptr;

    Image iconRedImage;
    ID3D11ShaderResourceView *iconRedTextureView = nullptr;

    struct ActorResp
    {
        uint8_t type;
        std::vector<float> pos;
        
        std::string display;
        ImVec2 local_pos;
    };

    std::vector<ActorResp> actorsList;
    std::vector<ActorResp> actorsListDisp;

    int selectedActor = 0;

    bool error_nodll = false;
    bool error_injectDll = false;
    bool error_maptexture = false;
    bool error_invalidProcess = false;

    std::thread updateWorker;

    ImVec2 WorldToPixle(float x, float y) const
    {
        if (!mapImage) {
            return { -1, -1 };
        }

        // from https://satisfactory-calculator.com/
        const float ylim = 1.0f / (375e3f + 375e3f);
        const float xlim = 1.0f / (425301.832031f + 324698.832031f);

        return { (x + 324698.832031f) * xlim * mapImage.width, (y + 375e3f) * ylim * mapImage.height };
    }

    static void WarningPopup(const std::string &title, const std::string &message, bool &error_show)
    {
        std::string id = title + "##" + message;

        if (error_show)
            ImGui::OpenPopup(id.c_str());

        if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("%s", message.c_str());
            ImGui::Separator();

            if (ImGui::Button("OK", ImVec2(200, 0))) {
                ImGui::CloseCurrentPopup();
                error_show = false;
            }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
    }
    
    void UpdateMap()
    {
        using json = nlohmann::json;

        HANDLE readyEvent = OpenEventA(EVENT_MODIFY_STATE, false, "SatisfactoryWebMapServerReadyEvent");
        if (readyEvent == NULL) {
            std::unique_lock _(m);
            actorsList.clear();

            if (serverStarted) {
                status = "server has stopped";
                serverStarted = false;
            }

            return;
        }

        bool isReady = WaitForSingleObject(readyEvent, 0) != WAIT_OBJECT_0;
        CloseHandle(readyEvent);

        if (!isReady) {
            std::unique_lock _(m);
            status = "wait for server ready";
            return;
        }

        if (!serverStarted) {
            serverStarted = true;
            status = "server is running";
        }

        auto data = HttpClient("GET", updateUrl);
        if (data.empty()) {
            return;
        }

        lastMapUpdate = time.sec();
        auto actors = json::parse(data);
        {
            if (actors.find("status") == actors.end()) {
                std::unique_lock _(m);
                status = "invalid data";
                return;
            }

            auto status = actors["status"].get<std::string>();
            if (status == "err") {
                std::unique_lock _(m);
                status = "error: " + actors["msg"].get<std::string>();
                return;
            }

            auto type = actors["type"].get<std::string>();
            if (type != "FeatureCollection") {
                std::unique_lock _(m);
                status = "invalid data: unkown type";
                return;
            }

            if (!actors["features"].is_array()) {
                std::unique_lock _(m);
                status = "invalid data";
                return;
            }
        }

        std::vector<ActorResp> items;
        for (const auto &feature : actors["features"]) {
            auto type = feature["type"].get<std::string>();
            if (type != "Feature") {
                continue;
            }

            items.push_back({
                feature["properties"]["type"].get<uint8_t>(),
                feature["geometry"]["coordinates"].get<std::vector<float>>(),
            });
        }

        {
            std::unique_lock _(m);
            actorsList = std::move(items);
        }
    }

    void SetupUI()
    {
        wchar_t localeName[32];

        GetSystemDefaultLocaleName(localeName, sizeof(localeName));

        mapImage = Image::open("map.png.png");
        if (mapImage) {
            mapTextureView = CreateTexture(mapImage, mapImage.width, mapImage.height);
        }

        iconImage = Image::open("MapCompass_Circle_Border.tga").resize(16, 16);
        if (iconImage) {
            iconTextureView = CreateTexture(iconImage, iconImage.width, iconImage.height);

            iconRedImage = iconImage.copy();

            int p = 0;
            for (int h = 0; h < iconRedImage.height; ++h) {
                for (int w = 0; w < iconRedImage.width; ++w) {
                    iconRedImage[p + 1] = 0;
                    iconRedImage[p + 2] = 0;

                    p += 4;
                }
            }
            iconRedTextureView = CreateTexture(iconRedImage, iconRedImage.width, iconRedImage.height);
        }

        error_maptexture = !mapTextureView;
        status = "waiting...";

        config = Config::Load("config.json");
        if (config.IP == "0.0.0.0") {
            config.IP = "127.0.0.1";
        }

        updateUrl = std::atow("http://" + config.IP + ":" + std::to_string(config.Port) + "/api/actors");
        stopUrl = std::atow("http://" + config.IP + ":" + std::to_string(config.Port) + "/api/stop");

        stop = false;

        updateWorker = std::thread([&] { 
            while (!stop) {
                UpdateMap();
                Sleep(3000);
            }
        });
    }

    void FindGame()
    {
        HWND hwnd = FindWindowA("UnrealWindow", "Satisfactory  ");
        if (IsWindow(hwnd)) {
            GetWindowThreadProcessId(hwnd, &processId);
        }

        if (processId == 0) {
            processId = FindProcessByName(L"FactoryGame-Win64-Shipping.exe");
        }
    }

    void UpdateUI()
    {
        const float ContainerWidth = ImGui::GetWindowWidth() - 15;

        {
            if (processId == 0 && time.sec() - lastProcessCheck > 3.f) {
                FindGame();
                lastProcessCheck = time.sec();
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(lable_color, "Process ID : ");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ContainerWidth * 0.5f);
            if (ImGui::InputInt("##ProcessID", (int *)&processId)) {
                window_title.clear();
            }

            ImGui::SameLine();
            if (ImGui::Button(" S ")) {
                FindGame();
            }
        }

        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(lable_color, "Title      : ");
            ImGui::SameLine();

            if (processId != 0 && window_title.empty()) {
                HWND hWnd = FindWindowByPid(processId);
                if (IsWindow(hWnd)) {
                    char title[32];
                    if (GetWindowTextA(hWnd, title, sizeof(title))) {
                        window_title = title;
                    }
                }
            }

            if (window_title.empty()) {
                ImGui::TextUnformatted("None");
            } else {
                ImGui::TextUnformatted(window_title.c_str());
            }
        }

        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(lable_color, "Status     : ");
            ImGui::SameLine();
            ImGui::TextUnformatted(status.c_str());

            ImGui::SameLine(ContainerWidth - 85.f);

            if (!serverStarted) {
                if (ImGui::Button("Start Server", { 94.f, 23.f })) {
                    if (processId) {
                        try {
                            FILE *f = fopen("SatisfactoryWebMapServer.dll", "rb");
                            if (f == nullptr) {
                                status = "missing dll";
                                error_nodll = true;
                            } else {
                                fclose(f);

                                std::wstring dll;
                                dll.resize(MAX_PATH);
                                GetFullPathNameW(L"SatisfactoryWebMapServer.dll", dll.size(), &dll[0], nullptr);

                                InjectDll(processId, dll);
                                status = "starting web server...";
                            }
                        } catch (const std::exception &) {
                            error_injectDll = true;
                            status = "dll inject fails";
                        }
                    } else {
                        error_invalidProcess = true;
                    }
                }
            } else if (ImGui::Button("Stop Server", { 94.f, 23.f })) {
                ImGui::OpenPopup("Stop Server");
            }

            // Popup
            if (ImGui::BeginPopupModal("Stop Server", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Confirm to stop server?");
                ImGui::Separator();

                if (ImGui::Button("Yes", ImVec2(75, 0))) {
                    ImGui::CloseCurrentPopup();
                    std::thread([this] {
                        HttpClient("GET", stopUrl);
                    }).detach();
                    status = "stopping web server...";
                }

                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(75, 0))) {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        ImGui::Separator();

        {
            // save map start postition
            auto cur = ImGui::GetCursorPos();

            ImGui::Image((void *)mapTextureView, ImVec2{ (float)mapImage.width, (float)mapImage.height });

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(lable_color, "Current List");

            if (actorsList.size()) {
                actorsListDisp.clear();

                std::unique_lock _(m);
                actorsListDisp = std::move(actorsList);
                actorsList.clear();

                for (auto &actor : actorsListDisp) {
                    actor.display = RespTypeName[actor.type];
                    actor.local_pos = WorldToPixle(actor.pos[0], actor.pos[1]);
                }
            }

            ImGui::SetNextItemWidth(ContainerWidth);
            ImGui::ListBox("##ActorsList", &selectedActor, [](void *ptr, const int idx, const char **out_text) -> bool {
                const UIWindow &ui = *(UIWindow *)ptr;
                if (idx < 0 || idx >= ui.actorsListDisp.size()) {
                    return false;
                }

                *out_text = ui.actorsListDisp[idx].display.c_str();
                return true;
            }, this, actorsListDisp.size(), 5);


            // center the icon
            cur = cur - ImVec2{ 8.f, 8.f };
            auto iconSize = ImVec2{ (float)iconRedImage.width, (float)iconRedImage.height };

            for (const auto &actor : actorsListDisp) {
                ImGui::SetCursorPos(cur + actor.local_pos);
                if (actor.type != 5) {
                    ImGui::Image((void *)iconTextureView, iconSize);
                }
            }

            for (const auto &actor : actorsListDisp) {
                ImGui::SetCursorPos(cur + actor.local_pos);
                if (actor.type == 5) {
                    ImGui::Image((void *)iconRedTextureView, iconSize);
                }
            }
        }

        {
            WarningPopup("Error", "Unabel to find Web server dll", error_nodll);
            WarningPopup("Error", "Unable to open map image", error_maptexture);
            WarningPopup("Error", "Unable to inject dll", error_injectDll);
            WarningPopup("Error", "Invalid game process id", error_invalidProcess);
        }
    }

    void StopUI()
    {
        stop = true;
        if (updateWorker.joinable()) {
            updateWorker.join();
        }
    }
};
