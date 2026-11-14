#pragma once

#include <string>
#include <vector>

#include "Device.hpp"
#include "Discovery.hpp"
#include "NetworkInfo.hpp"
#include "RangeScan.hpp"
#include "Subnet.hpp"
#include "SystemMonitor.hpp"

class Api {
public:
    static std::string status(const DiscoveryStatus& status, const Subnet& subnet);
    static std::string devices(const std::vector<Device>& devices);
    static std::string range(const RangeStatus& status, const std::vector<Device>& devices);
    static std::string device(const Device& device);
    static std::string system(const SystemSnapshot& snapshot);
    static std::string network(const std::vector<NetworkInterface>& interfaces);
    static std::string message(const std::string& text);
};
