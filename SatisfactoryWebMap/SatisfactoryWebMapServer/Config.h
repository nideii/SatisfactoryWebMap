#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <iomanip>

#include <nlohmann/json.hpp>

struct Config
{
    using json = nlohmann::json;

    // http server config
    std::string IP;
    int Port;
    std::string Root;
    bool APIOnly;

    // game sdk params
    size_t TNameEntryArrayOffset;
    size_t GUObjectArrayOffset;

    std::string MapManagerName;

    void Save(const std::string &configFile)
    {
        std::ofstream o(configFile);
        json j;

        j["ip"] = IP;
        j["port"] = Port;
        if (!Root.empty()) {
            j["root"] = Root;
        }
        j["apionly"] = APIOnly;
        j["gnames_offset"] = TNameEntryArrayOffset;
        j["gobjects_offset"] = GUObjectArrayOffset;
        j["mapmanager_name"] = MapManagerName;

        o << std::setw(4) << j << std::endl;
    }

    static Config Load(const std::string &configFile)
    {
        Config config{
            "0.0.0.0", 7012, "", false,
            0x4004A78, 0x4008F80, "MapManager",
        };

        std::ifstream i(configFile);
        if (!i.is_open()) {
            return config;
        }

        json j;
        i >> j;

        if (j.find("ip") != j.end()) {
            config.IP = j["ip"].get<std::string>();
        }

        if (j.find("port") != j.end()) {
            config.Port = j["port"].get<int>();
        }

        if (j.find("root") != j.end()) {
            config.Root = j["root"].get<std::string>();
        }

        if (j.find("apionly") != j.end()) {
            config.APIOnly = j["apionly"].get<bool>();
        }

        if (j.find("gnames_offset") != j.end()) {
            config.TNameEntryArrayOffset = j["gnames_offset"].get<size_t>();
        }

        if (j.find("gobjects_offset") != j.end()) {
            config.GUObjectArrayOffset = j["gobjects_offset"].get<size_t>();
        }

        if (j.find("mapmanager_name") != j.end()) {
            config.MapManagerName = j["mapmanager_name"].get<std::string>();
        }

        return config;
    }
};
