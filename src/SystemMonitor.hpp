#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct CpuSample {
    std::int64_t time = 0;
    double usage = 0.0;
};

struct SystemSnapshot {
    std::string hostname;
    std::string os;
    std::string kernel;
    std::string arch;
    std::string cpuModel;
    int cores = 0;
    std::int64_t uptimeS = 0;
    double load[3] = {0.0, 0.0, 0.0};
    std::uint64_t memoryTotal = 0;
    std::uint64_t memoryUsed = 0;
    double cpuUsage = 0.0;
    std::vector<double> coreUsage;
    std::vector<CpuSample> history;
    int intervalMs = 0;
    std::int64_t windowMs = 0;
};

class SystemMonitor {
public:
    explicit SystemMonitor(int intervalMs = 1000, std::size_t historySize = 120);
    ~SystemMonitor();

    SystemMonitor(const SystemMonitor&) = delete;
    SystemMonitor& operator=(const SystemMonitor&) = delete;

    void start();
    void stop();

    SystemSnapshot snapshot() const;

private:
    struct CpuTicks {
        std::uint64_t busy = 0;
        std::uint64_t total = 0;
    };

    void run();
    void sample();
    static bool readTicks(CpuTicks& total, std::vector<CpuTicks>& cores);
    static double usage(const CpuTicks& before, const CpuTicks& after);

    int intervalMs_;
    std::size_t historySize_;
    std::string hostname_;
    std::string os_;
    std::string kernel_;
    std::string arch_;
    std::string cpuModel_;
    std::thread thread_;
    std::atomic<bool> running_;
    CpuTicks lastTotal_;
    std::vector<CpuTicks> lastCores_;
    mutable std::mutex mutex_;
    std::deque<CpuSample> history_;
    double cpuUsage_;
    std::vector<double> coreUsage_;
};
