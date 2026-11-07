#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Device.hpp"
#include "Discovery.hpp"
#include "Icmp.hpp"
#include "Identifier.hpp"
#include "Subnet.hpp"

struct RangeStatus {
    std::string phase;
    std::string start;
    std::string end;
    std::string error;
    int hostCount = 0;
    int scanned = 0;
    int found = 0;
    int maxHosts = 0;
    std::int64_t started = 0;
    std::int64_t finished = 0;
};

// One-shot ping sweep of an arbitrary address range, independent of the monitored local subnet.
class RangeScan {
public:
    enum class StartResult { Started, Invalid, Busy };

    RangeScan(const DiscoverySettings& settings, const Subnet& subnet);
    ~RangeScan();

    RangeScan(const RangeScan&) = delete;
    RangeScan& operator=(const RangeScan&) = delete;

    StartResult start(const std::string& startIp, const std::string& endIp, std::string& error);
    void stop();

    RangeStatus status() const;
    std::vector<Device> devices() const;

private:
    void run(std::vector<std::string> ips);
    void worker(const std::vector<std::string>& ips, std::atomic<std::size_t>& next);
    void probe(Icmp& icmp, const std::string& ip);

    DiscoverySettings settings_;
    Identifier identifier_;
    std::uint32_t self_;
    std::string gateway_;
    std::thread thread_;
    std::mutex controlMutex_;
    std::atomic<bool> scanning_;
    std::atomic<bool> cancel_;
    std::atomic<int> scanned_;
    mutable std::mutex mutex_;
    std::map<std::uint32_t, Device> devices_;
    RangeStatus status_;
};
