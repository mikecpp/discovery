#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Subnet {
    std::string interfaceName;
    std::string address;
    std::string netmask;
    std::string network;
    std::string gateway;
    std::string macAddress;
    int prefix = 0;
    std::uint32_t self = 0;
    std::uint32_t first = 0;
    std::uint32_t last = 0;

    std::string cidr() const;
    std::size_t hostCount() const;
    std::vector<std::string> hosts(std::size_t maxHosts) const;

    static bool detect(const std::string& preferredInterface, Subnet& subnet, std::string& error);
    static std::string toString(std::uint32_t address);
    static bool parse(const std::string& text, std::uint32_t& address);
};
