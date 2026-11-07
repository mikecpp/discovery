#include "RangeScan.hpp"

#include <algorithm>

namespace {

Identifier::Options identifierOptions(const DiscoverySettings& settings, const Subnet& subnet) {
    Identifier::Options options;
    options.probeTimeoutMs = settings.probeTimeoutMs;
    options.resolveNames = settings.resolveNames;
    options.gateway = subnet.gateway;
    options.selfMac = subnet.macAddress;
    return options;
}

}

RangeScan::RangeScan(const DiscoverySettings& settings, const Subnet& subnet)
    : settings_(settings), identifier_(identifierOptions(settings, subnet)), self_(subnet.self),
      gateway_(subnet.gateway), scanning_(false), cancel_(false), scanned_(0) {
    if (settings_.threads < 1) {
        settings_.threads = 1;
    }
    status_.phase = "idle";
    status_.maxHosts = static_cast<int>(settings_.maxHosts);
}

RangeScan::~RangeScan() {
    cancel_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

RangeScan::StartResult RangeScan::start(const std::string& startIp, const std::string& endIp, std::string& error) {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    if (!Subnet::parse(startIp, first)) {
        error = "Invalid start address: " + startIp;
        return StartResult::Invalid;
    }
    if (!Subnet::parse(endIp, last)) {
        error = "Invalid end address: " + endIp;
        return StartResult::Invalid;
    }
    if (last < first) {
        error = "The start address must not be after the end address.";
        return StartResult::Invalid;
    }
    const std::uint64_t count = static_cast<std::uint64_t>(last) - first + 1;
    if (count > settings_.maxHosts) {
        error = "The range has " + std::to_string(count) + " addresses; the limit is " +
                std::to_string(settings_.maxHosts) + ".";
        return StartResult::Invalid;
    }

    std::lock_guard<std::mutex> control(controlMutex_);
    if (scanning_) {
        error = "A range scan is already running.";
        return StartResult::Busy;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    std::vector<std::string> ips;
    ips.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t address = first; address <= last; ++address) {
        ips.push_back(Subnet::toString(static_cast<std::uint32_t>(address)));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        devices_.clear();
        status_.phase = "scanning";
        status_.start = Subnet::toString(first);
        status_.end = Subnet::toString(last);
        status_.error.clear();
        status_.hostCount = static_cast<int>(count);
        status_.started = Discovery::now();
        status_.finished = 0;
    }
    scanned_ = 0;
    cancel_ = false;
    scanning_ = true;
    thread_ = std::thread(&RangeScan::run, this, std::move(ips));
    return StartResult::Started;
}

void RangeScan::stop() {
    cancel_ = true;
}

RangeStatus RangeScan::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    RangeStatus result = status_;
    result.scanned = scanned_;
    result.found = static_cast<int>(devices_.size());
    return result;
}

std::vector<Device> RangeScan::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Device> result;
    result.reserve(devices_.size());
    for (const auto& entry : devices_) {
        result.push_back(entry.second);
    }
    return result;
}

void RangeScan::run(std::vector<std::string> ips) {
    std::atomic<std::size_t> next{0};
    const std::size_t count = std::min(static_cast<std::size_t>(settings_.threads), ips.size());
    std::vector<std::thread> workers;
    workers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        workers.emplace_back(&RangeScan::worker, this, std::cref(ips), std::ref(next));
    }
    for (std::thread& thread : workers) {
        thread.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.finished = Discovery::now();
    status_.phase = cancel_ ? "stopped" : "done";
    scanning_ = false;
}

void RangeScan::worker(const std::vector<std::string>& ips, std::atomic<std::size_t>& next) {
    Icmp icmp(settings_.icmp);
    if (!icmp.isOpen()) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.error = icmp.lastError();
        return;
    }
    while (!cancel_) {
        const std::size_t index = next.fetch_add(1);
        if (index >= ips.size()) {
            break;
        }
        probe(icmp, ips[index]);
    }
}

void RangeScan::probe(Icmp& icmp, const std::string& ip) {
    const Icmp::Result result = icmp.ping(ip);
    ++scanned_;
    if (result.status != Icmp::Status::Ok) {
        return;
    }
    const std::int64_t time = Discovery::now();
    std::uint32_t address = 0;
    Subnet::parse(ip, address);
    Device device;
    device.ip = ip;
    device.address = address;
    device.online = true;
    device.identified = !settings_.identify;
    device.self = address == self_;
    device.gateway = !gateway_.empty() && ip == gateway_;
    device.ttl = result.ttl;
    device.rttMs = result.rttMs;
    device.firstSeen = time;
    device.lastSeen = time;
    device.lastChange = time;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        devices_[address] = device;
    }
    if (!settings_.identify || cancel_) {
        return;
    }
    const Identity identity = identifier_.identify(ip, result.ttl, device.self);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = devices_.find(address);
    if (found == devices_.end()) {
        return;
    }
    Device& stored = found->second;
    stored.hostname = identity.hostname;
    stored.mac = identity.mac;
    stored.vendor = identity.vendor;
    stored.type = identity.type;
    stored.os = identity.os;
    stored.openPorts = identity.openPorts;
    stored.identified = true;
}
