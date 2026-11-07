#pragma once

#include <string>
#include <vector>

struct Identity {
    std::string hostname;
    std::string mac;
    std::string vendor;
    std::string type;
    std::string os;
    std::vector<int> openPorts;
};

class Identifier {
public:
    struct Options {
        int probeTimeoutMs = 500;
        bool resolveNames = true;
        std::string gateway;
        std::string selfMac;
    };

    explicit Identifier(Options options);

    Identity identify(const std::string& ip, int ttl, bool self) const;

    static std::string arpLookup(const std::string& ip);
    static std::string resolveName(const std::string& ip);

private:
    std::vector<int> probePorts(const std::string& ip) const;
    void classify(const std::string& ip, int ttl, bool self, Identity& identity) const;

    Options options_;
};
