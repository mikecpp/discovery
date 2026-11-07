#include "Discovery.hpp"

#include <algorithm>
#include <chrono>

namespace {

std::int64_t millisecondsSinceEpoch() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}

Discovery::Discovery(DiscoverySettings settings)
    : settings_(std::move(settings)), identifier_(Identifier::Options()), running_(false), scanRequested_(false),
      scanned_(0), nextRescanAt_(0) {
    if (settings_.threads < 1) {
        settings_.threads = 1;
    }
    status_.phase = "idle";
    status_.threads = settings_.threads;
    status_.watchIntervalS = settings_.watchIntervalS;
    status_.rescanIntervalS = settings_.rescanIntervalS;
}

Discovery::~Discovery() {
    stop();
}

bool Discovery::start() {
    if (running_) {
        return true;
    }
    if (!Subnet::detect(settings_.interfaceName, subnet_, lastError_)) {
        return false;
    }
    {
        const Icmp icmp(settings_.icmp);
        if (!icmp.isOpen()) {
            lastError_ = "Cannot open ICMP: " + icmp.lastError();
            return false;
        }
        icmpMode_ = Icmp::modeName(icmp.mode());
    }
    Identifier::Options options;
    options.probeTimeoutMs = settings_.probeTimeoutMs;
    options.resolveNames = settings_.resolveNames;
    options.gateway = subnet_.gateway;
    options.selfMac = subnet_.macAddress;
    identifier_ = Identifier(options);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.icmpMode = icmpMode_;
        status_.hostCount = static_cast<int>(std::min(subnet_.hostCount(), settings_.maxHosts));
    }
    running_ = true;
    thread_ = std::thread(&Discovery::run, this);
    return true;
}

void Discovery::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Discovery::requestScan() {
    scanRequested_ = true;
    wake_.notify_all();
}

const Subnet& Discovery::subnet() const {
    return subnet_;
}

const std::string& Discovery::lastError() const {
    return lastError_;
}

std::vector<Device> Discovery::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Device> result;
    result.reserve(devices_.size());
    for (const auto& entry : devices_) {
        result.push_back(entry.second);
    }
    return result;
}

DiscoveryStatus Discovery::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    DiscoveryStatus result = status_;
    result.scanned = scanned_;
    result.online = 0;
    result.offline = 0;
    for (const auto& entry : devices_) {
        if (entry.second.online) {
            ++result.online;
        } else {
            ++result.offline;
        }
    }
    const std::int64_t remaining = nextRescanAt_ - now();
    result.nextRescanS = remaining > 0 ? static_cast<int>((remaining + 999) / 1000) : 0;
    return result;
}

std::int64_t Discovery::now() {
    return millisecondsSinceEpoch();
}

void Discovery::run() {
    while (running_) {
        scanRequested_ = false;
        scanAll();
        nextRescanAt_ = now() + static_cast<std::int64_t>(settings_.rescanIntervalS) * 1000;
        while (running_ && !scanRequested_ && now() < nextRescanAt_) {
            if (!waitFor(settings_.watchIntervalS)) {
                break;
            }
            watchKnown();
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.phase = "stopped";
}

void Discovery::scanAll() {
    const std::int64_t started = now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.lastScanStarted = started;
    }
    runBatch(subnet_.hosts(settings_.maxHosts), "scanning");
    std::lock_guard<std::mutex> lock(mutex_);
    status_.lastScanFinished = now();
    status_.lastScanDurationMs = status_.lastScanFinished - started;
    ++status_.scanCount;
    status_.phase = "watching";
}

void Discovery::watchKnown() {
    const std::vector<std::string> ips = knownIps();
    if (!ips.empty()) {
        runBatch(ips, "watching");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.lastWatch = now();
    status_.phase = "watching";
}

void Discovery::runBatch(const std::vector<std::string>& ips, const char* phase) {
    scanned_ = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.phase = phase;
    }
    std::atomic<std::size_t> next{0};
    const std::size_t count = std::min(static_cast<std::size_t>(settings_.threads), ips.size());
    std::vector<std::thread> workers;
    workers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        workers.emplace_back(&Discovery::worker, this, std::cref(ips), std::ref(next));
    }
    for (std::thread& thread : workers) {
        thread.join();
    }
}

void Discovery::worker(const std::vector<std::string>& ips, std::atomic<std::size_t>& next) {
    Icmp icmp(settings_.icmp);
    if (!icmp.isOpen()) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.error = icmp.lastError();
        return;
    }
    while (running_) {
        const std::size_t index = next.fetch_add(1);
        if (index >= ips.size()) {
            break;
        }
        probe(icmp, ips[index]);
    }
}

void Discovery::probe(Icmp& icmp, const std::string& ip) {
    const Icmp::Result result = icmp.ping(ip);
    const std::int64_t time = now();
    std::uint32_t address = 0;
    Subnet::parse(ip, address);
    bool needIdentify = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = devices_.find(address);
        if (result.status == Icmp::Status::Ok) {
            if (found == devices_.end()) {
                Device device;
                device.ip = ip;
                device.address = address;
                device.self = address == subnet_.self;
                device.gateway = !subnet_.gateway.empty() && ip == subnet_.gateway;
                device.firstSeen = time;
                device.lastChange = time;
                found = devices_.emplace(address, device).first;
            }
            Device& device = found->second;
            if (!device.online) {
                device.online = true;
                device.lastChange = time;
            }
            device.missed = 0;
            device.ttl = result.ttl;
            device.rttMs = result.rttMs;
            device.lastSeen = time;
            needIdentify = settings_.identify && !device.identified;
        } else if (found != devices_.end()) {
            Device& device = found->second;
            ++device.missed;
            if (device.online && device.missed >= settings_.offlineMisses) {
                device.online = false;
                device.lastChange = time;
            }
        }
    }
    ++scanned_;
    if (!needIdentify) {
        return;
    }
    const Identity identity = identifier_.identify(ip, result.ttl, address == subnet_.self);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = devices_.find(address);
    if (found == devices_.end()) {
        return;
    }
    Device& device = found->second;
    device.hostname = identity.hostname;
    device.mac = identity.mac;
    device.vendor = identity.vendor;
    device.type = identity.type;
    device.os = identity.os;
    device.openPorts = identity.openPorts;
    device.identified = true;
}

bool Discovery::waitFor(int seconds) {
    std::unique_lock<std::mutex> lock(wakeMutex_);
    wake_.wait_for(lock, std::chrono::seconds(seconds), [this] { return !running_ || scanRequested_; });
    return running_ && !scanRequested_;
}

std::vector<std::string> Discovery::knownIps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(devices_.size());
    for (const auto& entry : devices_) {
        result.push_back(entry.second.ip);
    }
    return result;
}
