#include "Api.hpp"

#include "Json.hpp"

std::string Api::status(const DiscoveryStatus& status, const Subnet& subnet) {
    JsonObject object;
    object.set("phase", status.phase)
        .set("icmpMode", status.icmpMode)
        .set("error", status.error)
        .set("interface", subnet.interfaceName)
        .set("address", subnet.address)
        .set("netmask", subnet.netmask)
        .set("subnet", subnet.cidr())
        .set("gateway", subnet.gateway)
        .set("mac", subnet.macAddress)
        .set("hostCount", status.hostCount)
        .set("scanned", status.scanned)
        .set("online", status.online)
        .set("offline", status.offline)
        .set("devices", status.online + status.offline)
        .set("scanCount", status.scanCount)
        .set("threads", status.threads)
        .set("watchInterval", status.watchIntervalS)
        .set("rescanInterval", status.rescanIntervalS)
        .set("nextRescan", status.nextRescanS)
        .set("lastScanStarted", status.lastScanStarted)
        .set("lastScanFinished", status.lastScanFinished)
        .set("lastScanDuration", status.lastScanDurationMs)
        .set("lastWatch", status.lastWatch)
        .set("now", Discovery::now());
    return object.str();
}

std::string Api::devices(const std::vector<Device>& devices) {
    JsonArray array;
    for (const Device& item : devices) {
        array.pushRaw(device(item));
    }
    JsonObject object;
    object.setRaw("devices", array.str()).set("now", Discovery::now());
    return object.str();
}

std::string Api::range(const RangeStatus& status, const std::vector<Device>& devices) {
    JsonArray array;
    for (const Device& item : devices) {
        array.pushRaw(device(item));
    }
    const std::int64_t end = status.finished ? status.finished : Discovery::now();
    JsonObject object;
    object.set("phase", status.phase)
        .set("start", status.start)
        .set("end", status.end)
        .set("error", status.error)
        .set("hostCount", status.hostCount)
        .set("scanned", status.scanned)
        .set("found", status.found)
        .set("maxHosts", status.maxHosts)
        .set("started", status.started)
        .set("finished", status.finished)
        .set("duration", status.started ? end - status.started : std::int64_t{0})
        .setRaw("devices", array.str())
        .set("now", Discovery::now());
    return object.str();
}

std::string Api::device(const Device& device) {
    JsonArray ports;
    for (const int port : device.openPorts) {
        ports.push(port);
    }
    JsonObject object;
    object.set("ip", device.ip)
        .set("hostname", device.hostname)
        .set("mac", device.mac)
        .set("vendor", device.vendor)
        .set("type", device.type)
        .set("os", device.os)
        .setRaw("openPorts", ports.str())
        .set("ttl", device.ttl)
        .set("rtt", device.rttMs)
        .set("online", device.online)
        .set("self", device.self)
        .set("gateway", device.gateway)
        .set("identified", device.identified)
        .set("firstSeen", device.firstSeen)
        .set("lastSeen", device.lastSeen)
        .set("lastChange", device.lastChange);
    return object.str();
}

std::string Api::message(const std::string& text) {
    JsonObject object;
    object.set("message", text);
    return object.str();
}
