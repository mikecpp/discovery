#include "Subnet.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/sysctl.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

struct Candidate {
    std::string name;
    std::uint32_t address = 0;
    std::uint32_t netmask = 0;
};

int prefixLength(std::uint32_t netmask) {
    int length = 0;
    while (netmask & 0x80000000u) {
        ++length;
        netmask <<= 1;
    }
    return length;
}

#if defined(__APPLE__)
std::size_t roundUp(std::size_t length) {
    return length > 0 ? 1 + ((length - 1) | (sizeof(std::uint32_t) - 1)) : sizeof(std::uint32_t);
}

bool defaultRoute(std::string& interfaceName, std::uint32_t& gateway) {
    int mib[] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY};
    std::size_t size = 0;
    if (::sysctl(mib, 6, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return false;
    }
    std::vector<char> buffer(size);
    if (::sysctl(mib, 6, buffer.data(), &size, nullptr, 0) != 0) {
        return false;
    }
    for (std::size_t offset = 0; offset + sizeof(rt_msghdr) <= size;) {
        const auto* message = reinterpret_cast<const rt_msghdr*>(buffer.data() + offset);
        offset += message->rtm_msglen;
        if (message->rtm_msglen == 0) {
            break;
        }
        if ((message->rtm_flags & RTF_IFSCOPE) != 0) {
            continue;
        }
        const sockaddr* addresses[RTAX_MAX] = {};
        const char* cursor = reinterpret_cast<const char*>(message + 1);
        for (int index = 0; index < RTAX_MAX; ++index) {
            if ((message->rtm_addrs & (1 << index)) != 0) {
                addresses[index] = reinterpret_cast<const sockaddr*>(cursor);
                cursor += roundUp(addresses[index]->sa_len);
            }
        }
        const sockaddr* destination = addresses[RTAX_DST];
        const sockaddr* via = addresses[RTAX_GATEWAY];
        if (destination == nullptr || destination->sa_family != AF_INET || via == nullptr ||
            via->sa_family != AF_INET) {
            continue;
        }
        if (reinterpret_cast<const sockaddr_in*>(destination)->sin_addr.s_addr != 0) {
            continue;
        }
        char name[IF_NAMESIZE];
        if (::if_indextoname(message->rtm_index, name) == nullptr) {
            continue;
        }
        interfaceName = name;
        gateway = ntohl(reinterpret_cast<const sockaddr_in*>(via)->sin_addr.s_addr);
        return true;
    }
    return false;
}

std::string hardwareAddress(const std::string& interfaceName) {
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return {};
    }
    std::string result;
    for (const ifaddrs* item = list; item != nullptr; item = item->ifa_next) {
        if (item->ifa_addr == nullptr || item->ifa_addr->sa_family != AF_LINK || interfaceName != item->ifa_name) {
            continue;
        }
        const auto* link = reinterpret_cast<const sockaddr_dl*>(item->ifa_addr);
        if (link->sdl_alen != 6) {
            continue;
        }
        const auto* bytes = reinterpret_cast<const unsigned char*>(LLADDR(link));
        char buffer[18];
        std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2],
                      bytes[3], bytes[4], bytes[5]);
        result = buffer;
        break;
    }
    ::freeifaddrs(list);
    return result;
}
#else
bool defaultRoute(std::string& interfaceName, std::uint32_t& gateway) {
    std::ifstream file("/proc/net/route");
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string name;
        std::string destination;
        std::string gatewayText;
        unsigned flags = 0;
        if (!(fields >> name >> destination >> gatewayText >> std::hex >> flags)) {
            continue;
        }
        if (destination == "00000000" && (flags & 0x2) != 0) {
            interfaceName = name;
            gateway = ntohl(static_cast<std::uint32_t>(std::strtoul(gatewayText.c_str(), nullptr, 16)));
            return true;
        }
    }
    return false;
}

std::string hardwareAddress(const std::string& interfaceName) {
    const int probe = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (probe < 0) {
        return {};
    }
    ifreq request{};
    std::strncpy(request.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);
    std::string result;
    if (::ioctl(probe, SIOCGIFHWADDR, &request) == 0) {
        char buffer[18];
        const auto* bytes = reinterpret_cast<const unsigned char*>(request.ifr_hwaddr.sa_data);
        std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2],
                      bytes[3], bytes[4], bytes[5]);
        result = buffer;
    }
    ::close(probe);
    return result;
}
#endif

}

std::string Subnet::cidr() const {
    return network + "/" + std::to_string(prefix);
}

std::size_t Subnet::hostCount() const {
    return last < first ? 0 : static_cast<std::size_t>(last - first) + 1;
}

std::vector<std::string> Subnet::hosts(std::size_t maxHosts) const {
    std::vector<std::string> result;
    const std::size_t count = hostCount() < maxHosts ? hostCount() : maxHosts;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(toString(first + static_cast<std::uint32_t>(index)));
    }
    return result;
}

bool Subnet::detect(const std::string& preferredInterface, Subnet& subnet, std::string& error) {
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        error = std::string("getifaddrs failed: ") + std::strerror(errno);
        return false;
    }
    std::vector<Candidate> candidates;
    for (const ifaddrs* item = list; item != nullptr; item = item->ifa_next) {
        if (item->ifa_addr == nullptr || item->ifa_netmask == nullptr || item->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((item->ifa_flags & IFF_UP) == 0 || (item->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        Candidate candidate;
        candidate.name = item->ifa_name;
        candidate.address = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_addr)->sin_addr.s_addr);
        candidate.netmask = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_netmask)->sin_addr.s_addr);
        candidates.push_back(candidate);
    }
    ::freeifaddrs(list);

    std::string routeInterface;
    std::uint32_t gateway = 0;
    const bool hasRoute = defaultRoute(routeInterface, gateway);

    const Candidate* chosen = nullptr;
    for (const Candidate& candidate : candidates) {
        if (!preferredInterface.empty()) {
            if (candidate.name == preferredInterface) {
                chosen = &candidate;
                break;
            }
            continue;
        }
        if (hasRoute && candidate.name == routeInterface && prefixLength(candidate.netmask) < 32) {
            chosen = &candidate;
            break;
        }
    }
    if (chosen == nullptr && preferredInterface.empty()) {
        for (const Candidate& candidate : candidates) {
            if (prefixLength(candidate.netmask) < 32) {
                chosen = &candidate;
                break;
            }
        }
    }
    if (chosen == nullptr) {
        error = preferredInterface.empty() ? "No network interface with an IPv4 subnet was found"
                                           : "Interface " + preferredInterface + " has no IPv4 address";
        return false;
    }

    subnet.interfaceName = chosen->name;
    subnet.prefix = prefixLength(chosen->netmask);
    subnet.self = chosen->address;
    subnet.address = toString(chosen->address);
    subnet.netmask = toString(chosen->netmask);
    const std::uint32_t network = chosen->address & chosen->netmask;
    const std::uint32_t broadcast = network | ~chosen->netmask;
    subnet.network = toString(network);
    if (subnet.prefix >= 31) {
        subnet.first = network;
        subnet.last = broadcast;
    } else {
        subnet.first = network + 1;
        subnet.last = broadcast - 1;
    }
    if (hasRoute && routeInterface == chosen->name) {
        subnet.gateway = toString(gateway);
    }
    subnet.macAddress = hardwareAddress(chosen->name);
    return true;
}

std::string Subnet::toString(std::uint32_t address) {
    in_addr value{};
    value.s_addr = htonl(address);
    char buffer[INET_ADDRSTRLEN];
    return ::inet_ntop(AF_INET, &value, buffer, sizeof(buffer)) == nullptr ? std::string() : std::string(buffer);
}

bool Subnet::parse(const std::string& text, std::uint32_t& address) {
    in_addr value{};
    if (::inet_pton(AF_INET, text.c_str(), &value) != 1) {
        return false;
    }
    address = ntohl(value.s_addr);
    return true;
}
