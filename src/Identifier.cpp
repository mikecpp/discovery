#include "Identifier.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/if_ether.h>
#include <sys/sysctl.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <fstream>
#include <sstream>
#include <vector>

#include "Oui.hpp"

namespace {

const int kProbePorts[] = {21, 22, 23, 53, 80, 135, 139, 443, 445, 548, 554, 631, 1883, 3389,
                           5000, 5900, 8008, 8009, 8080, 8443, 9100, 62078};

bool hasPort(const std::vector<int>& ports, int port) {
    return std::find(ports.begin(), ports.end(), port) != ports.end();
}

bool anyPort(const std::vector<int>& ports, std::initializer_list<int> wanted) {
    for (const int port : wanted) {
        if (hasPort(ports, port)) {
            return true;
        }
    }
    return false;
}

std::string osFromTtl(int ttl) {
    if (ttl <= 0) {
        return {};
    }
    if (ttl > 128) {
        return "Embedded / network OS";
    }
    if (ttl > 64) {
        return "Windows";
    }
    return "Linux / Unix";
}

std::string localOs() {
    utsname info{};
    if (::uname(&info) != 0) {
        return "Linux";
    }
    return std::string(info.sysname) + " " + info.release;
}

std::string localHostname() {
    char buffer[256];
    if (::gethostname(buffer, sizeof(buffer)) != 0) {
        return {};
    }
    buffer[sizeof(buffer) - 1] = '\0';
    return buffer;
}

}

Identifier::Identifier(Options options) : options_(std::move(options)) {
}

Identity Identifier::identify(const std::string& ip, int ttl, bool self) const {
    Identity identity;
    if (self) {
        identity.hostname = localHostname();
        identity.mac = options_.selfMac;
    } else {
        identity.openPorts = probePorts(ip);
        identity.mac = arpLookup(ip);
        if (options_.resolveNames) {
            identity.hostname = resolveName(ip);
        }
    }
    identity.vendor = Oui::vendor(identity.mac);
    classify(ip, ttl, self, identity);
    return identity;
}

#if defined(__APPLE__)
std::string Identifier::arpLookup(const std::string& ip) {
    in_addr target{};
    if (::inet_pton(AF_INET, ip.c_str(), &target) != 1) {
        return {};
    }
    int mib[] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_LLINFO};
    std::size_t size = 0;
    if (::sysctl(mib, 6, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return {};
    }
    std::vector<char> buffer(size);
    if (::sysctl(mib, 6, buffer.data(), &size, nullptr, 0) != 0) {
        return {};
    }
    for (std::size_t offset = 0; offset + sizeof(rt_msghdr) <= size;) {
        const auto* message = reinterpret_cast<const rt_msghdr*>(buffer.data() + offset);
        offset += message->rtm_msglen;
        if (message->rtm_msglen == 0) {
            break;
        }
        const auto* address = reinterpret_cast<const sockaddr_inarp*>(message + 1);
        if (address->sin_addr.s_addr != target.s_addr) {
            continue;
        }
        const std::size_t addressLength =
            address->sin_len > 0 ? 1 + ((address->sin_len - 1) | (sizeof(std::uint32_t) - 1)) : sizeof(std::uint32_t);
        const auto* link =
            reinterpret_cast<const sockaddr_dl*>(reinterpret_cast<const char*>(address) + addressLength);
        if (link->sdl_family != AF_LINK || link->sdl_alen != 6) {
            continue;
        }
        const auto* bytes = reinterpret_cast<const unsigned char*>(LLADDR(link));
        char text[18];
        std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3],
                      bytes[4], bytes[5]);
        return text;
    }
    return {};
}
#else
std::string Identifier::arpLookup(const std::string& ip) {
    std::ifstream file("/proc/net/arp");
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string address;
        std::string hardwareType;
        std::string flags;
        std::string mac;
        if (!(fields >> address >> hardwareType >> flags >> mac)) {
            continue;
        }
        if (address == ip && mac != "00:00:00:00:00:00") {
            return mac;
        }
    }
    return {};
}
#endif

std::string Identifier::resolveName(const std::string& ip) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (::inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        return {};
    }
    char host[NI_MAXHOST];
    const int result = ::getnameinfo(reinterpret_cast<const sockaddr*>(&address), sizeof(address), host,
                                     sizeof(host), nullptr, 0, NI_NAMEREQD);
    return result == 0 ? std::string(host) : std::string();
}

std::vector<int> Identifier::probePorts(const std::string& ip) const {
    std::vector<int> open;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (::inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        return open;
    }

    struct Probe {
        int port;
        int socket;
    };
    std::vector<Probe> probes;
    std::vector<pollfd> descriptors;
    for (const int port : kProbePorts) {
#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
        const int sock = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sock < 0) {
            continue;
        }
#else
        const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            continue;
        }
        ::fcntl(sock, F_SETFL, ::fcntl(sock, F_GETFL) | O_NONBLOCK);
        ::fcntl(sock, F_SETFD, FD_CLOEXEC);
#endif
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        const int connected = ::connect(sock, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (connected == 0) {
            open.push_back(port);
            ::close(sock);
            continue;
        }
        if (errno != EINPROGRESS) {
            ::close(sock);
            continue;
        }
        probes.push_back({port, sock});
        pollfd descriptor{};
        descriptor.fd = sock;
        descriptor.events = POLLOUT;
        descriptors.push_back(descriptor);
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options_.probeTimeoutMs);
    std::size_t pending = descriptors.size();
    while (pending > 0) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }
        const int ready = ::poll(descriptors.data(), descriptors.size(), static_cast<int>(remaining.count()));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            break;
        }
        for (std::size_t index = 0; index < descriptors.size(); ++index) {
            pollfd& descriptor = descriptors[index];
            if (descriptor.fd < 0 || descriptor.revents == 0) {
                continue;
            }
            int error = 0;
            socklen_t length = sizeof(error);
            if (::getsockopt(descriptor.fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0 &&
                (descriptor.revents & POLLOUT) != 0) {
                open.push_back(probes[index].port);
            }
            ::close(descriptor.fd);
            descriptor.fd = -1;
            --pending;
        }
    }
    for (const pollfd& descriptor : descriptors) {
        if (descriptor.fd >= 0) {
            ::close(descriptor.fd);
        }
    }
    std::sort(open.begin(), open.end());
    return open;
}

void Identifier::classify(const std::string& ip, int ttl, bool self, Identity& identity) const {
    const std::vector<int>& ports = identity.openPorts;
    const std::string& vendor = identity.vendor;
    identity.os = osFromTtl(ttl);

    if (self) {
        identity.type = "This computer";
        identity.os = localOs();
        return;
    }
    if (!options_.gateway.empty() && ip == options_.gateway) {
        identity.type = "Router / gateway";
        if (identity.os.empty() || ttl > 128) {
            identity.os = "Router firmware";
        }
        return;
    }
    if (vendor == "Espressif") {
        identity.type = "IoT device (ESP)";
        identity.os = "ESP firmware";
        return;
    }
    if (vendor == "Raspberry Pi") {
        identity.type = "Raspberry Pi";
        identity.os = "Linux";
        return;
    }
    if (vendor == "Apple") {
        if (hasPort(ports, 62078)) {
            identity.type = "iPhone / iPad";
            identity.os = "iOS";
        } else if (anyPort(ports, {22, 548, 5900, 88})) {
            identity.type = "Mac";
            identity.os = "macOS";
        } else {
            identity.type = "Apple device";
            identity.os = "iOS / macOS";
        }
        return;
    }
    if (hasPort(ports, 62078)) {
        identity.type = "iPhone / iPad";
        identity.os = "iOS";
        return;
    }
    if (hasPort(ports, 3389) || (hasPort(ports, 445) && hasPort(ports, 135))) {
        identity.type = "Windows PC";
        identity.os = "Windows";
        return;
    }
    if (vendor == "Hikvision" || vendor == "Dahua" || hasPort(ports, 554)) {
        identity.type = "IP camera";
        identity.os = "Embedded Linux";
        return;
    }
    if (vendor == "Brother" || vendor == "Canon" || vendor == "Epson" ||
        (anyPort(ports, {9100, 631}) && !anyPort(ports, {22, 445}))) {
        identity.type = "Printer";
        identity.os = "Printer firmware";
        return;
    }
    if (vendor == "Synology" || vendor == "QNAP" || (hasPort(ports, 445) && hasPort(ports, 22))) {
        identity.type = "NAS / file server";
        identity.os = "Linux";
        return;
    }
    if (vendor == "Sonos") {
        identity.type = "Speaker";
        identity.os = "Embedded Linux";
        return;
    }
    if (vendor == "Philips Hue") {
        identity.type = "Smart home hub";
        identity.os = "Embedded Linux";
        return;
    }
    if (vendor == "Roku" || vendor == "Amazon" || anyPort(ports, {8008, 8009})) {
        identity.type = vendor == "Google" || hasPort(ports, 8009) ? "Chromecast / Google device" : "Media player";
        identity.os = "Embedded Linux";
        return;
    }
    if (vendor == "Nintendo" || vendor == "Sony") {
        identity.type = "Game console";
        identity.os = vendor + " system software";
        return;
    }
    if (vendor == "VMware" || vendor == "VirtualBox" || vendor == "QEMU" || vendor == "Docker" ||
        identity.mac.rfind("00:15:5d", 0) == 0) {
        identity.type = "Virtual machine";
        return;
    }
    if (vendor == "TP-Link" || vendor == "Netgear" || vendor == "Ubiquiti" || vendor == "Cisco" ||
        (vendor == "ASUS" && ttl > 128)) {
        identity.type = "Network device";
        identity.os = "Network firmware";
        return;
    }
    if (vendor == "Samsung" || vendor == "Xiaomi" || vendor == "Huawei" || vendor == "LG") {
        identity.type = ttl > 64 ? "Smart device" : "Phone / smart device";
        identity.os = ttl > 64 ? identity.os : "Android / Linux";
        return;
    }
    if (hasPort(ports, 22)) {
        identity.type = "Linux / Unix host";
        return;
    }
    if (hasPort(ports, 1883)) {
        identity.type = "MQTT broker";
        return;
    }
    if (ttl > 128) {
        identity.type = "Network / embedded device";
        return;
    }
    if (ttl > 64) {
        identity.type = "Windows PC";
        return;
    }
    if (ttl > 0) {
        identity.type = anyPort(ports, {80, 443}) ? "Linux device with web UI" : "Linux / Unix host";
        return;
    }
    identity.type = "Unknown device";
}
