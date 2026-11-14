#include "SystemMonitor.hpp"

#include <sys/utsname.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#else
#include <sys/sysinfo.h>
#endif

namespace {

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

#if defined(__APPLE__)
std::string sysctlString(const char* name) {
    std::size_t size = 0;
    if (::sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return "";
    }
    std::string value(size, '\0');
    if (::sysctlbyname(name, &value[0], &size, nullptr, 0) != 0) {
        return "";
    }
    value.resize(value.find('\0') == std::string::npos ? size : value.find('\0'));
    return value;
}
#else
std::string fileValue(const char* path, const std::string& key, char separator) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.compare(0, key.size(), key) != 0) {
            continue;
        }
        const std::size_t split = line.find(separator, key.size());
        if (split == std::string::npos) {
            continue;
        }
        std::string value = line.substr(split + 1);
        const std::size_t first = value.find_first_not_of(" \t\"");
        const std::size_t last = value.find_last_not_of(" \t\"");
        return first == std::string::npos ? "" : value.substr(first, last - first + 1);
    }
    return "";
}
#endif

std::string osName(const struct utsname& uts) {
#if defined(__APPLE__)
    (void)uts;
    const std::string version = sysctlString("kern.osproductversion");
    return version.empty() ? "macOS" : "macOS " + version;
#else
    const std::string pretty = fileValue("/etc/os-release", "PRETTY_NAME", '=');
    return pretty.empty() ? uts.sysname : pretty;
#endif
}

std::string cpuModel(const struct utsname& uts) {
#if defined(__APPLE__)
    std::string model = sysctlString("machdep.cpu.brand_string");
#else
    std::string model = fileValue("/proc/cpuinfo", "model name", ':');
    if (model.empty()) {
        model = fileValue("/proc/cpuinfo", "Model", ':');
    }
#endif
    return model.empty() ? uts.machine : model;
}

std::int64_t uptimeSeconds() {
#if defined(__APPLE__)
    struct timeval boot {};
    std::size_t size = sizeof(boot);
    if (::sysctlbyname("kern.boottime", &boot, &size, nullptr, 0) != 0) {
        return 0;
    }
    return nowMs() / 1000 - boot.tv_sec;
#else
    struct sysinfo info {};
    return ::sysinfo(&info) == 0 ? info.uptime : 0;
#endif
}

void memory(std::uint64_t& total, std::uint64_t& used) {
#if defined(__APPLE__)
    std::size_t size = sizeof(total);
    if (::sysctlbyname("hw.memsize", &total, &size, nullptr, 0) != 0) {
        total = 0;
    }
    vm_statistics64_data_t stats {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (::host_statistics64(::mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) !=
        KERN_SUCCESS) {
        used = 0;
        return;
    }
    const std::uint64_t page = static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
    const std::uint64_t available =
        (static_cast<std::uint64_t>(stats.free_count) + stats.inactive_count + stats.speculative_count) * page;
    used = total > available ? total - available : 0;
#else
    std::ifstream file("/proc/meminfo");
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    std::uint64_t available = 0;
    total = 0;
    while (file >> key >> value >> unit) {
        if (key == "MemTotal:") {
            total = value * 1024;
        } else if (key == "MemAvailable:") {
            available = value * 1024;
        }
    }
    used = total > available ? total - available : 0;
#endif
}

}

SystemMonitor::SystemMonitor(int intervalMs, std::size_t historySize)
    : intervalMs_(intervalMs < 100 ? 100 : intervalMs), historySize_(historySize == 0 ? 1 : historySize),
      running_(false), cpuUsage_(0.0) {
    char host[256] = {};
    if (::gethostname(host, sizeof(host) - 1) == 0) {
        hostname_ = host;
    }
    struct utsname uts {};
    if (::uname(&uts) == 0) {
        os_ = osName(uts);
        kernel_ = std::string(uts.sysname) + " " + uts.release;
        arch_ = uts.machine;
        cpuModel_ = cpuModel(uts);
    }
}

SystemMonitor::~SystemMonitor() {
    stop();
}

void SystemMonitor::start() {
    if (running_.exchange(true)) {
        return;
    }
    readTicks(lastTotal_, lastCores_);
    thread_ = std::thread(&SystemMonitor::run, this);
}

void SystemMonitor::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

SystemSnapshot SystemMonitor::snapshot() const {
    SystemSnapshot result;
    result.hostname = hostname_;
    result.os = os_;
    result.kernel = kernel_;
    result.arch = arch_;
    result.cpuModel = cpuModel_;
    result.cores = static_cast<int>(std::thread::hardware_concurrency());
    result.uptimeS = uptimeSeconds();
    if (::getloadavg(result.load, 3) < 0) {
        result.load[0] = result.load[1] = result.load[2] = 0.0;
    }
    memory(result.memoryTotal, result.memoryUsed);
    result.intervalMs = intervalMs_;
    result.windowMs = static_cast<std::int64_t>(intervalMs_) * static_cast<std::int64_t>(historySize_);

    std::lock_guard<std::mutex> lock(mutex_);
    result.cpuUsage = cpuUsage_;
    result.coreUsage = coreUsage_;
    result.history.assign(history_.begin(), history_.end());
    return result;
}

void SystemMonitor::run() {
    const auto step = std::chrono::milliseconds(intervalMs_);
    auto next = std::chrono::steady_clock::now() + step;
    while (running_) {
        while (running_ && std::chrono::steady_clock::now() < next) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!running_) {
            break;
        }
        sample();
        next += step;
    }
}

void SystemMonitor::sample() {
    CpuTicks total;
    std::vector<CpuTicks> cores;
    if (!readTicks(total, cores)) {
        return;
    }
    const double overall = usage(lastTotal_, total);
    std::vector<double> perCore(cores.size(), 0.0);
    for (std::size_t index = 0; index < cores.size() && index < lastCores_.size(); ++index) {
        perCore[index] = usage(lastCores_[index], cores[index]);
    }
    lastTotal_ = total;
    lastCores_ = cores;

    std::lock_guard<std::mutex> lock(mutex_);
    cpuUsage_ = overall;
    coreUsage_ = perCore;
    history_.push_back(CpuSample{nowMs(), overall});
    while (history_.size() > historySize_) {
        history_.pop_front();
    }
}

double SystemMonitor::usage(const CpuTicks& before, const CpuTicks& after) {
    if (after.total <= before.total || after.busy < before.busy) {
        return 0.0;
    }
    const double value = 100.0 * static_cast<double>(after.busy - before.busy) /
                         static_cast<double>(after.total - before.total);
    return value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value);
}

bool SystemMonitor::readTicks(CpuTicks& total, std::vector<CpuTicks>& cores) {
    total = CpuTicks{};
    cores.clear();
#if defined(__APPLE__)
    natural_t count = 0;
    processor_info_array_t info = nullptr;
    mach_msg_type_number_t infoCount = 0;
    if (::host_processor_info(::mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &count, &info, &infoCount) !=
        KERN_SUCCESS) {
        return false;
    }
    for (natural_t cpu = 0; cpu < count; ++cpu) {
        const integer_t* ticks = info + cpu * CPU_STATE_MAX;
        const auto tick = [ticks](int state) {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(ticks[state]));
        };
        CpuTicks core;
        core.busy = tick(CPU_STATE_USER) + tick(CPU_STATE_SYSTEM) + tick(CPU_STATE_NICE);
        core.total = core.busy + tick(CPU_STATE_IDLE);
        total.busy += core.busy;
        total.total += core.total;
        cores.push_back(core);
    }
    ::vm_deallocate(::mach_task_self(), reinterpret_cast<vm_address_t>(info), infoCount * sizeof(integer_t));
    return count > 0;
#else
    std::ifstream file("/proc/stat");
    std::string line;
    bool found = false;
    while (std::getline(file, line)) {
        if (line.compare(0, 3, "cpu") != 0) {
            break;
        }
        std::istringstream fields(line);
        std::string name;
        std::uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        fields >> name >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        CpuTicks ticks;
        ticks.busy = user + nice + system + irq + softirq + steal;
        ticks.total = ticks.busy + idle + iowait;
        if (name == "cpu") {
            total = ticks;
            found = true;
        } else {
            cores.push_back(ticks);
        }
    }
    return found;
#endif
}
