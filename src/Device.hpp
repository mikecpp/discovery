#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Device {
    std::string ip;
    std::uint32_t address = 0;
    std::string hostname;
    std::string mac;
    std::string vendor;
    std::string type;
    std::string os;
    std::vector<int> openPorts;
    int ttl = 0;
    double rttMs = 0.0;
    bool online = false;
    bool self = false;
    bool gateway = false;
    bool identified = false;
    int missed = 0;
    std::int64_t firstSeen = 0;
    std::int64_t lastSeen = 0;
    std::int64_t lastChange = 0;
};
