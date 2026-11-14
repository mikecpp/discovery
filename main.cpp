#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#include "kconfig.h"

#include "Api.hpp"
#include "Discovery.hpp"
#include "RangeScan.hpp"

#if defined(CONFIG_COMPONENT_WEBSERVER)
#include "WebServer.hpp"
#endif

namespace {

volatile std::sig_atomic_t stopRequested = 0;

void onSignal(int) {
    stopRequested = 1;
}

void log(const char* level, const std::string& message) {
    const std::time_t time = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
    std::printf("[%s] %s %s\n", level, stamp, message.c_str());
    std::fflush(stdout);
}

DiscoverySettings loadSettings() {
    DiscoverySettings settings;
    settings.interfaceName = CONFIG_DISCOVERY_INTERFACE;
    settings.threads = CONFIG_DISCOVERY_THREADS;
    settings.maxHosts = static_cast<std::size_t>(CONFIG_DISCOVERY_MAX_HOSTS);
    settings.watchIntervalS = CONFIG_DISCOVERY_WATCH_INTERVAL;
    settings.rescanIntervalS = CONFIG_DISCOVERY_RESCAN_INTERVAL;
    settings.offlineMisses = CONFIG_DISCOVERY_OFFLINE_MISSES;
#if defined(CONFIG_DISCOVERY_IDENTIFY)
    settings.identify = true;
    settings.probeTimeoutMs = CONFIG_DISCOVERY_PROBE_TIMEOUT_MS;
#if defined(CONFIG_DISCOVERY_RESOLVE_NAMES)
    settings.resolveNames = true;
#else
    settings.resolveNames = false;
#endif
#else
    settings.identify = false;
#endif
    settings.icmp.timeoutMs = CONFIG_ICMP_TIMEOUT_MS;
    settings.icmp.dataSize = CONFIG_ICMP_DATA_SIZE;
#if defined(CONFIG_ICMP_EXTERNAL_FALLBACK)
    settings.icmp.allowExternal = true;
    settings.icmp.externalCommand = CONFIG_ICMP_EXTERNAL_COMMAND;
#else
    settings.icmp.allowExternal = false;
#endif
    return settings;
}

#if defined(CONFIG_COMPONENT_WEBSERVER)
std::string serverUrl(const WebServer& server) {
    const std::string address = CONFIG_WEBSERVER_ADDRESS;
    const std::string host = (address == "0.0.0.0" || address.empty()) ? "localhost" : address;
    return "http://" + host + ":" + std::to_string(server.port()) + "/";
}

#if defined(CONFIG_WEBSERVER_OPEN_BROWSER)
#if !defined(__APPLE__)
bool isWsl() {
    std::ifstream file("/proc/sys/kernel/osrelease");
    std::string release;
    std::getline(file, release);
    return release.find("microsoft") != std::string::npos || release.find("Microsoft") != std::string::npos;
}
#endif

void openBrowser(const std::string& url) {
#if defined(__APPLE__)
    const std::string opener = "open";
#else
    const std::string opener = isWsl() ? "explorer.exe" : "xdg-open";
#endif
    // Check the opener exists first: the backgrounded launch always returns 0.
    const std::string command = "command -v " + opener + " >/dev/null 2>&1 && { " + opener + " '" + url +
                                "' </dev/null >/dev/null 2>&1 & }";
    if (std::system(command.c_str()) != 0) {
        log("ERROR", "Cannot open web browser with " + opener);
    }
}
#endif

void registerRoutes(WebServer& server, Discovery& discovery, RangeScan& rangeScan) {
    server.addRoute("GET", "/api/status", [&discovery](const HttpRequest&) {
        return HttpResponse::json(Api::status(discovery.status(), discovery.subnet()));
    });
    server.addRoute("GET", "/api/devices", [&discovery](const HttpRequest&) {
        return HttpResponse::json(Api::devices(discovery.devices()));
    });
    server.addRoute("POST", "/api/scan", [&discovery](const HttpRequest&) {
        discovery.requestScan();
        return HttpResponse::json(Api::message("A full scan has been requested."), 202);
    });
    server.addRoute("GET", "/api/range", [&rangeScan](const HttpRequest&) {
        return HttpResponse::json(Api::range(rangeScan.status(), rangeScan.devices()));
    });
    server.addRoute("POST", "/api/range", [&rangeScan](const HttpRequest& request) {
        const std::string start = request.queryValue("start");
        const std::string end = request.queryValue("end");
        std::string error;
        switch (rangeScan.start(start, end, error)) {
        case RangeScan::StartResult::Started:
            return HttpResponse::json(Api::message("Scanning " + start + " - " + end + "."), 202);
        case RangeScan::StartResult::Busy:
            return HttpResponse::json(Api::message(error), 409);
        case RangeScan::StartResult::Invalid:
            break;
        }
        return HttpResponse::json(Api::message(error), 400);
    });
    server.addRoute("POST", "/api/range/stop", [&rangeScan](const HttpRequest&) {
        rangeScan.stop();
        return HttpResponse::json(Api::message("The range scan is stopping."), 202);
    });
}
#else
void printDevices(const Discovery& discovery) {
    const DiscoveryStatus status = discovery.status();
    std::printf("\n%-16s %-6s %-28s %-26s %s\n", "IP", "State", "Type", "Hostname", "Vendor");
    for (const Device& device : discovery.devices()) {
        std::printf("%-16s %-6s %-28s %-26s %s\n", device.ip.c_str(), device.online ? "on" : "off",
                    device.type.c_str(), device.hostname.c_str(), device.vendor.c_str());
    }
    std::printf("%s: %d online, %d offline, scan %d/%d\n", status.phase.c_str(), status.online, status.offline,
                status.scanned, status.hostCount);
    std::fflush(stdout);
}
#endif

}

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    const DiscoverySettings settings = loadSettings();
    Discovery discovery(settings);
    if (!discovery.start()) {
        log("ERROR", "Discovery failed to start: " + discovery.lastError());
        return 1;
    }
    const Subnet& subnet = discovery.subnet();
    log("INFO", "Scanning " + subnet.cidr() + " on " + subnet.interfaceName + " (" + subnet.address + ") using " +
                    discovery.status().icmpMode + " with " + std::to_string(CONFIG_DISCOVERY_THREADS) + " threads");

#if defined(CONFIG_COMPONENT_WEBSERVER)
    WebServer::Options options;
    options.address = CONFIG_WEBSERVER_ADDRESS;
    options.port = static_cast<std::uint16_t>(CONFIG_WEBSERVER_PORT);
    options.title = CONFIG_WEBSERVER_TITLE;

    RangeScan rangeScan(settings, subnet);
    WebServer server(options);
    registerRoutes(server, discovery, rangeScan);
    if (!server.start()) {
        log("ERROR", "Web server failed to start: " + server.lastError());
        return 1;
    }
    const std::string url = serverUrl(server);
    log("INFO", "Web server listening on " + url);
#if defined(CONFIG_WEBSERVER_OPEN_BROWSER)
    openBrowser(url);
#endif
    while (stopRequested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    server.stop();
#else
    while (stopRequested == 0) {
        for (int tick = 0; tick < CONFIG_DISCOVERY_WATCH_INTERVAL * 5 && stopRequested == 0; ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        printDevices(discovery);
    }
#endif

    discovery.stop();
    log("INFO", "Discovery stopped");
    return 0;
}
