#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Device.hpp"
#include "Icmp.hpp"
#include "Identifier.hpp"
#include "Subnet.hpp"

struct DiscoverySettings {
    std::string interfaceName;
    int threads = 32;
    std::size_t maxHosts = 4096;
    int watchIntervalS = 5;
    int rescanIntervalS = 120;
    int offlineMisses = 2;
    bool identify = true;
    bool resolveNames = true;
    int probeTimeoutMs = 500;
    Icmp::Options icmp;
};

struct DiscoveryStatus {
    std::string phase;
    std::string icmpMode;
    std::string error;
    int hostCount = 0;
    int scanned = 0;
    int online = 0;
    int offline = 0;
    int scanCount = 0;
    int threads = 0;
    int watchIntervalS = 0;
    int rescanIntervalS = 0;
    int nextRescanS = 0;
    std::int64_t lastScanStarted = 0;
    std::int64_t lastScanFinished = 0;
    std::int64_t lastScanDurationMs = 0;
    std::int64_t lastWatch = 0;
};

class Discovery {
public:
    explicit Discovery(DiscoverySettings settings);
    ~Discovery();

    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;

    bool start();
    void stop();
    void requestScan();

    const Subnet& subnet() const;
    const std::string& lastError() const;
    std::vector<Device> devices() const;
    DiscoveryStatus status() const;

    static std::int64_t now();

private:
    void run();
    void scanAll();
    void watchKnown();
    void runBatch(const std::vector<std::string>& ips, const char* phase);
    void worker(const std::vector<std::string>& ips, std::atomic<std::size_t>& next);
    void probe(Icmp& icmp, const std::string& ip);
    bool waitFor(int seconds);
    std::vector<std::string> knownIps() const;

    DiscoverySettings settings_;
    Subnet subnet_;
    Identifier identifier_;
    std::string lastError_;
    std::string icmpMode_;
    std::thread thread_;
    std::atomic<bool> running_;
    std::atomic<bool> scanRequested_;
    std::atomic<int> scanned_;
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    mutable std::mutex mutex_;
    std::map<std::uint32_t, Device> devices_;
    DiscoveryStatus status_;
    std::int64_t nextRescanAt_;
};
