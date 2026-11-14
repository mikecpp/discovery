#include "NetworkInfo.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdio>
#include <fstream>
#include <map>

#if defined(__APPLE__)
#include <net/if_dl.h>
#else
#include <netpacket/packet.h>
#endif

namespace {

int prefixLength(const unsigned char* bytes, std::size_t size) {
    int bits = 0;
    for (std::size_t index = 0; index < size; ++index) {
        for (unsigned char value = bytes[index]; value & 0x80; value = static_cast<unsigned char>(value << 1)) {
            ++bits;
        }
    }
    return bits;
}

std::string formatMac(const unsigned char* bytes, std::size_t size) {
    if (size != 6) {
        return "";
    }
    bool zero = true;
    for (std::size_t index = 0; index < size; ++index) {
        zero = zero && bytes[index] == 0;
    }
    if (zero) {
        return "";
    }
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3],
                  bytes[4], bytes[5]);
    return buffer;
}

#if !defined(__APPLE__)
std::uint64_t sysfsNumber(const std::string& name, const char* file) {
    std::ifstream stream("/sys/class/net/" + name + "/" + file);
    std::uint64_t value = 0;
    stream >> value;
    return value;
}
#endif

}

std::vector<NetworkInterface> NetworkInfo::interfaces() {
    std::vector<NetworkInterface> result;
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return result;
    }
    std::map<std::string, std::size_t> index;
    for (ifaddrs* item = list; item != nullptr; item = item->ifa_next) {
        if (item->ifa_name == nullptr) {
            continue;
        }
        const std::string name = item->ifa_name;
        auto found = index.find(name);
        if (found == index.end()) {
            found = index.emplace(name, result.size()).first;
            result.emplace_back();
            result.back().name = name;
        }
        NetworkInterface& entry = result[found->second];
        entry.up = (item->ifa_flags & IFF_UP) != 0;
        entry.running = (item->ifa_flags & IFF_RUNNING) != 0;
        entry.loopback = (item->ifa_flags & IFF_LOOPBACK) != 0;
        if (item->ifa_addr == nullptr) {
            continue;
        }
        const int family = item->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6) {
            char text[INET6_ADDRSTRLEN] = {};
            NetworkAddress address;
            if (family == AF_INET) {
                const auto* addr = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
                ::inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text));
                address.family = "ipv4";
                if (item->ifa_netmask != nullptr) {
                    const auto* mask = reinterpret_cast<const sockaddr_in*>(item->ifa_netmask);
                    address.prefix = prefixLength(reinterpret_cast<const unsigned char*>(&mask->sin_addr), 4);
                }
            } else {
                const auto* addr = reinterpret_cast<const sockaddr_in6*>(item->ifa_addr);
                ::inet_ntop(AF_INET6, &addr->sin6_addr, text, sizeof(text));
                address.family = "ipv6";
                if (item->ifa_netmask != nullptr) {
                    const auto* mask = reinterpret_cast<const sockaddr_in6*>(item->ifa_netmask);
                    address.prefix = prefixLength(reinterpret_cast<const unsigned char*>(&mask->sin6_addr), 16);
                }
            }
            address.address = text;
            entry.addresses.push_back(address);
        }
#if defined(__APPLE__)
        if (family == AF_LINK) {
            const auto* link = reinterpret_cast<const sockaddr_dl*>(item->ifa_addr);
            entry.mac = formatMac(reinterpret_cast<const unsigned char*>(LLADDR(link)), link->sdl_alen);
            if (item->ifa_data != nullptr) {
                const auto* data = static_cast<const if_data*>(item->ifa_data);
                entry.rxBytes = data->ifi_ibytes;
                entry.txBytes = data->ifi_obytes;
                entry.mtu = static_cast<int>(data->ifi_mtu);
            }
        }
#else
        if (family == AF_PACKET) {
            const auto* link = reinterpret_cast<const sockaddr_ll*>(item->ifa_addr);
            entry.mac = formatMac(link->sll_addr, link->sll_halen);
        }
#endif
    }
    ::freeifaddrs(list);
#if !defined(__APPLE__)
    for (NetworkInterface& entry : result) {
        entry.rxBytes = sysfsNumber(entry.name, "statistics/rx_bytes");
        entry.txBytes = sysfsNumber(entry.name, "statistics/tx_bytes");
        entry.mtu = static_cast<int>(sysfsNumber(entry.name, "mtu"));
    }
#endif
    return result;
}
