#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct NetworkAddress {
    std::string family;
    std::string address;
    int prefix = 0;
};

struct NetworkInterface {
    std::string name;
    std::string mac;
    bool up = false;
    bool running = false;
    bool loopback = false;
    int mtu = 0;
    std::uint64_t rxBytes = 0;
    std::uint64_t txBytes = 0;
    std::vector<NetworkAddress> addresses;
};

class NetworkInfo {
public:
    static std::vector<NetworkInterface> interfaces();
};
