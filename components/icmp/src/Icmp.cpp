#include "Icmp.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

extern char** environ;

namespace {

constexpr int kMaxDataSize = 1400;
constexpr std::size_t kReceiveBufferSize = 2048;

// ICMP echo header layout (RFC 792); defined locally because Linux (icmphdr) and BSD/macOS (icmp) disagree.
struct EchoHeader {
    std::uint8_t type;
    std::uint8_t code;
    std::uint16_t checksum;
    std::uint16_t id;
    std::uint16_t sequence;
};
static_assert(sizeof(EchoHeader) == 8, "ICMP echo header must be 8 bytes");

void setCloseOnExec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

int createPipeCloexec(int fds[2]) {
#if defined(__linux__)
    return ::pipe2(fds, O_CLOEXEC);
#else
    if (::pipe(fds) != 0) {
        return -1;
    }
    setCloseOnExec(fds[0]);
    setCloseOnExec(fds[1]);
    return 0;
#endif
}

std::uint16_t checksum(const unsigned char* data, std::size_t size) {
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index + 1 < size; index += 2) {
        sum += static_cast<std::uint32_t>(data[index] << 8 | data[index + 1]);
    }
    if (size % 2 == 1) {
        sum += static_cast<std::uint32_t>(data[size - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return htons(static_cast<std::uint16_t>(~sum));
}

std::uint16_t nextId() {
    static std::atomic<std::uint16_t> counter{0};
    const std::uint16_t pid = static_cast<std::uint16_t>(::getpid());
    return static_cast<std::uint16_t>((pid << 8) ^ counter++);
}

int millisecondsUntil(std::chrono::steady_clock::time_point deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return remaining.count() <= 0 ? 0 : static_cast<int>(remaining.count());
}

double parseNumberAfter(const std::string& text, const std::string& key, bool& found) {
    const std::size_t position = text.find(key);
    found = position != std::string::npos;
    return found ? std::strtod(text.c_str() + position + key.size(), nullptr) : 0.0;
}

}

Icmp::Icmp(Options options)
    : options_(std::move(options)), socket_(-1), mode_(Mode::None), id_(nextId()), sequence_(0) {
    if (options_.dataSize < 0) {
        options_.dataSize = 0;
    }
    if (options_.dataSize > kMaxDataSize) {
        options_.dataSize = kMaxDataSize;
    }
    if (options_.timeoutMs < 1) {
        options_.timeoutMs = 1;
    }
    open();
}

Icmp::~Icmp() {
    if (socket_ >= 0) {
        ::close(socket_);
    }
}

bool Icmp::isOpen() const {
    return mode_ != Mode::None;
}

Icmp::Mode Icmp::mode() const {
    return mode_;
}

const std::string& Icmp::lastError() const {
    return lastError_;
}

void Icmp::open() {
    if (openSocket(SOCK_DGRAM)) {
        mode_ = Mode::Datagram;
        return;
    }
    const std::string datagramError = lastError_;
    if (openSocket(SOCK_RAW)) {
        mode_ = Mode::Raw;
        return;
    }
    const std::string rawError = lastError_;
    if (options_.allowExternal && ::access(options_.externalCommand.c_str(), X_OK) == 0) {
        mode_ = Mode::External;
        lastError_.clear();
        return;
    }
    lastError_ = "datagram socket: " + datagramError + "; raw socket: " + rawError;
    if (options_.allowExternal) {
        lastError_ += "; " + options_.externalCommand + " is not executable";
    }
}

bool Icmp::openSocket(int type) {
#if defined(SOCK_CLOEXEC)
    socket_ = ::socket(AF_INET, type | SOCK_CLOEXEC, IPPROTO_ICMP);
#else
    socket_ = ::socket(AF_INET, type, IPPROTO_ICMP);
#endif
    if (socket_ < 0) {
        lastError_ = std::strerror(errno);
        return false;
    }
#if !defined(SOCK_CLOEXEC)
    setCloseOnExec(socket_);
#endif
    int enabled = 1;
    ::setsockopt(socket_, IPPROTO_IP, IP_RECVTTL, &enabled, sizeof(enabled));
    return true;
}

Icmp::Result Icmp::ping(const std::string& ip) {
    Result result;
    if (mode_ == Mode::None) {
        result.status = Status::NotOpen;
        return result;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (::inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        result.status = Status::InvalidAddress;
        return result;
    }
    if (mode_ == Mode::External) {
        return pingExternal(ip);
    }
    return pingSocket(address);
}

Icmp::Result Icmp::pingSocket(const sockaddr_in& address) {
    Result result;
    const std::uint16_t sequence = ++sequence_;
    const Clock::time_point sent = Clock::now();
    const Clock::time_point deadline = sent + std::chrono::milliseconds(options_.timeoutMs);
    if (!sendEcho(address, sequence)) {
        result.status = (errno == EHOSTUNREACH || errno == ENETUNREACH || errno == EACCES) ? Status::Unreachable
                                                                                          : Status::SendFail;
        return result;
    }
    return receiveReply(address, sequence, sent, deadline);
}

bool Icmp::sendEcho(const sockaddr_in& address, std::uint16_t sequence) {
    std::vector<unsigned char> packet(sizeof(EchoHeader) + static_cast<std::size_t>(options_.dataSize), 0);
    EchoHeader header{};
    header.type = ICMP_ECHO;
    header.code = 0;
    header.id = htons(id_);
    header.sequence = htons(sequence);
    std::memcpy(packet.data(), &header, sizeof(header));
    for (std::size_t index = sizeof(header); index < packet.size(); ++index) {
        packet[index] = static_cast<unsigned char>(index);
    }
    const std::uint16_t sum = checksum(packet.data(), packet.size());
    std::memcpy(packet.data() + offsetof(EchoHeader, checksum), &sum, sizeof(sum));

    while (true) {
        const ssize_t sent = ::sendto(socket_, packet.data(), packet.size(), 0,
                                      reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (sent == static_cast<ssize_t>(packet.size())) {
            return true;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

Icmp::Result Icmp::receiveReply(const sockaddr_in& address, std::uint16_t sequence, Clock::time_point sent,
                                Clock::time_point deadline) {
    Result result;
    unsigned char buffer[kReceiveBufferSize];
    alignas(cmsghdr) unsigned char control[64];
    while (true) {
        const int wait = millisecondsUntil(deadline);
        if (wait <= 0) {
            result.status = Status::Timeout;
            return result;
        }
        pollfd descriptor{};
        descriptor.fd = socket_;
        descriptor.events = POLLIN;
        const int ready = ::poll(&descriptor, 1, wait);
        if (ready == 0) {
            result.status = Status::Timeout;
            return result;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            result.status = Status::RecvFail;
            return result;
        }

        sockaddr_in from{};
        iovec vector{};
        vector.iov_base = buffer;
        vector.iov_len = sizeof(buffer);
        msghdr message{};
        message.msg_name = &from;
        message.msg_namelen = sizeof(from);
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        const ssize_t received = ::recvmsg(socket_, &message, MSG_DONTWAIT);
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            result.status = (errno == ECONNREFUSED || errno == EHOSTUNREACH || errno == ENETUNREACH)
                                ? Status::Unreachable
                                : Status::RecvFail;
            return result;
        }
        if (from.sin_addr.s_addr != address.sin_addr.s_addr) {
            continue;
        }

        const unsigned char* icmpData = buffer;
        std::size_t icmpLength = static_cast<std::size_t>(received);
        int ttl = 0;
#if defined(__APPLE__)
        // macOS delivers the IP header on ICMP datagram sockets too; Linux strips it.
        const bool hasIpHeader = true;
#else
        const bool hasIpHeader = mode_ == Mode::Raw;
#endif
        if (hasIpHeader) {
            if (icmpLength < sizeof(ip)) {
                continue;
            }
            ip ipHeader{};
            std::memcpy(&ipHeader, buffer, sizeof(ipHeader));
            const std::size_t headerLength = static_cast<std::size_t>(ipHeader.ip_hl) * 4;
            if (icmpLength < headerLength + sizeof(EchoHeader)) {
                continue;
            }
            ttl = ipHeader.ip_ttl;
            icmpData += headerLength;
            icmpLength -= headerLength;
        } else {
            if (icmpLength < sizeof(EchoHeader)) {
                continue;
            }
            for (cmsghdr* item = CMSG_FIRSTHDR(&message); item != nullptr; item = CMSG_NXTHDR(&message, item)) {
                if (item->cmsg_level == IPPROTO_IP && item->cmsg_type == IP_TTL) {
                    int value = 0;
                    std::memcpy(&value, CMSG_DATA(item), sizeof(value));
                    ttl = value;
                }
            }
        }

        EchoHeader header{};
        std::memcpy(&header, icmpData, sizeof(header));
        if (header.type != ICMP_ECHOREPLY || ntohs(header.sequence) != sequence) {
            continue;
        }
        if (mode_ == Mode::Raw && ntohs(header.id) != id_) {
            continue;
        }
        result.status = Status::Ok;
        result.ttl = ttl;
        result.rttMs = std::chrono::duration<double, std::milli>(Clock::now() - sent).count();
        return result;
    }
}

Icmp::Result Icmp::pingExternal(const std::string& ip) {
    Result result;
    int pipeFds[2];
    if (createPipeCloexec(pipeFds) != 0) {
        result.status = Status::SendFail;
        return result;
    }

#if defined(__APPLE__)
    // BSD ping takes -W in milliseconds; iputils ping takes seconds.
    const std::string timeoutText = std::to_string(options_.timeoutMs);
#else
    const int timeoutSeconds = (options_.timeoutMs + 999) / 1000;
    const std::string timeoutText = std::to_string(timeoutSeconds);
#endif
    const std::string sizeText = std::to_string(options_.dataSize);
    std::vector<std::string> arguments = {options_.externalCommand, "-n", "-c", "1", "-W", timeoutText,
                                          "-s", sizeText, ip};
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    ::posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    pid_t pid = 0;
    const int spawned = ::posix_spawn(&pid, options_.externalCommand.c_str(), &actions, nullptr, argv.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(pipeFds[1]);
    if (spawned != 0) {
        ::close(pipeFds[0]);
        lastError_ = std::strerror(spawned);
        result.status = Status::SendFail;
        return result;
    }

    std::string output;
    char chunk[512];
    while (true) {
        const ssize_t count = ::read(pipeFds[0], chunk, sizeof(chunk));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        output.append(chunk, static_cast<std::size_t>(count));
    }
    ::close(pipeFds[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status)) {
        result.status = Status::RecvFail;
        return result;
    }
    const int code = WEXITSTATUS(status);
    if (code == 0) {
        bool found = false;
        result.ttl = static_cast<int>(parseNumberAfter(output, "ttl=", found));
        result.rttMs = parseNumberAfter(output, "time=", found);
        result.status = Status::Ok;
    } else if (code == 1) {
        result.status = output.find("nreachable") != std::string::npos ? Status::Unreachable : Status::Timeout;
    } else {
        result.status = Status::SendFail;
    }
    return result;
}

const char* Icmp::statusName(Status status) {
    switch (status) {
    case Status::Ok:
        return "ok";
    case Status::Timeout:
        return "timeout";
    case Status::Unreachable:
        return "unreachable";
    case Status::SendFail:
        return "send failed";
    case Status::RecvFail:
        return "receive failed";
    case Status::NotOpen:
        return "not open";
    case Status::InvalidAddress:
        return "invalid address";
    }
    return "unknown";
}

const char* Icmp::modeName(Mode mode) {
    switch (mode) {
    case Mode::None:
        return "none";
    case Mode::Datagram:
        return "datagram socket";
    case Mode::Raw:
        return "raw socket";
    case Mode::External:
        return "external ping";
    }
    return "unknown";
}
