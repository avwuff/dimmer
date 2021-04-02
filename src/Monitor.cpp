//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2017 Casey Langen
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "Monitor.h"
#include "Util.h"
#include <map>
#include "json.hpp"

using namespace dimmer;
using namespace nlohmann;

constexpr float DEFAULT_OPACITY = 0.0f;
constexpr int DEFAULT_TEMPERATURE = -1;

struct MonitorOptions {
    float opacity;
    int temperature;
    bool enabled;

    MonitorOptions() {
        this->opacity = DEFAULT_OPACITY;
        this->temperature = DEFAULT_TEMPERATURE;
        this->enabled = true;
    }
};

static std::map<std::wstring, std::shared_ptr<MonitorOptions>> monitorOptions;
static bool pollingEnabled = false;
static bool globalEnabled = true;

static std::wstring getConfigFilename() {
    return getDataDirectory() + L"\\config.json";
}

static std::wstring getConfigFilename(std::wstring configName) {
    return getDataDirectory() + L"\\" + configName + L".json";
}

static BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM data) {
    auto monitors = reinterpret_cast<std::vector<Monitor>*>(data);
    int index = (int) monitors->size();
    monitors->push_back(Monitor(monitor, index));
    return TRUE;
}

static MonitorOptions& options(Monitor& monitor) {
    auto id = monitor.getId();
    if (monitorOptions.find(id) == monitorOptions.end()) {
        monitorOptions[id] = std::make_shared<MonitorOptions>();
    }
    return *monitorOptions[id];
}

namespace dimmer {
    std::vector<Monitor> queryMonitors() {
        std::vector<Monitor> result;

        EnumDisplayMonitors(
            nullptr,
            nullptr,
            &MonitorEnumProc,
            reinterpret_cast<LPARAM>(&result));

        // Also look through the display devices list
        DISPLAY_DEVICE dispDevice;
        ZeroMemory(&dispDevice, sizeof(dispDevice));
        dispDevice.cb = sizeof(dispDevice);

        DWORD screenID = 0;
        while (EnumDisplayDevices(NULL, screenID, &dispDevice, 0))
        {
            // important: make copy of DeviceName
            WCHAR name[sizeof(dispDevice.DeviceName)];
            wcscpy(name, dispDevice.DeviceName);

            if (EnumDisplayDevices(name, 0, &dispDevice, EDD_GET_DEVICE_INTERFACE_NAME))
            {


                // at this point dispDevice.DeviceID contains a unique identifier for the monitor
                // find this monitor in our Monitor map and set it in there.
                for (int i = 0; i < result.size(); i++) {

                    WCHAR newname[sizeof(dispDevice.DeviceName)];
                    wcscpy(newname, dispDevice.DeviceName);

                    // The dispDevice.DeviceName contains more information than the s ystem info.szDevice, so we want to truncate.
                    // Truncate:
                    if (wcslen(newname) > wcslen(result[i].info.szDevice)) {
                        newname[wcslen(result[i].info.szDevice)] = 0x00;
                    }

                    if (wcscmp(result[i].info.szDevice, newname) == 0) {
                        result[i].setId(dispDevice.DeviceID);
                    }
                }
            }

            ++screenID;
        }

        return result;
    }

    float getMonitorOpacity(Monitor& monitor) {
        return options(monitor).opacity;
    }

    void setMonitorOpacity(Monitor& monitor, float opacity) {
        options(monitor).opacity = opacity;
        saveConfig();
    }

    int getMonitorTemperature(Monitor& monitor) {
        return options(monitor).temperature;
    }

    void setMonitorTemperature(Monitor& monitor, int temperature) {
        options(monitor).temperature = temperature;
        saveConfig();
    }

    bool isPollingEnabled() {
        return pollingEnabled;
    }

    void setPollingEnabled(bool enabled) {
        pollingEnabled = enabled;
        saveConfig();
    }

    extern bool isDimmerEnabled() {
        return globalEnabled;
    }

    extern void setDimmerEnabled(bool enabled) {
        if (globalEnabled != enabled) {
            globalEnabled = enabled;
            saveConfig();
        }
    }

    bool isMonitorEnabled(Monitor& monitor) {
        return options(monitor).enabled;
    }

    void setMonitorEnabled(Monitor& monitor, bool enabled) {
        options(monitor).enabled = enabled;
        saveConfig();
    }

    void parseConfig(std::string config) {
        try {
            json j = json::parse(config);
            auto m = j.find("monitors");
            if (m != j.end()) {
                for (auto it = (*m).begin(); it != (*m).end(); ++it) {
                    auto key = u8to16(it.key());
                    auto value = it.value();
                    auto options = std::make_shared<MonitorOptions>();
                    options->opacity = value.value<float>("opacity", DEFAULT_OPACITY);
                    options->temperature = value.value<int>("temperature", DEFAULT_TEMPERATURE);
                    options->enabled = value.value<bool>("enabled", true);
                    monitorOptions[key] = options;
                }
            }

            auto g = j.find("general");
            if (g != j.end()) {
                pollingEnabled = (*g).value("pollingEnabled", false);
                globalEnabled = (*g).value("globalEnabled", true);
            }
        }
        catch (...) {
            /* move on... */
        }
    }

    void loadConfig() {
        std::string config = fileToString(getConfigFilename());
        parseConfig(config);
    }
    
    // Support loading a config from a file path
    void loadConfig(LPWSTR configName) {
        try {
            std::string config = fileToString(getConfigFilename(configName));
            parseConfig(config);
        }
        catch (...) {
            // nothing
        }
    }

    void saveConfig() {
        json j = { { "monitors", { } } };
        json& m = j["monitors"];

        auto monitors = queryMonitors();
        for (auto monitor : monitors) {
            m[u16to8(monitor.getId())] = {
                { "opacity", getMonitorOpacity(monitor) },
                { "temperature", getMonitorTemperature(monitor) },
                { "enabled", isMonitorEnabled(monitor) }
            };
        }

        j["general"] = {
            { "globalEnabled", globalEnabled },
            { "pollingEnabled", pollingEnabled }
        };

        stringToFile(getConfigFilename(), j.dump(2));
    }
}