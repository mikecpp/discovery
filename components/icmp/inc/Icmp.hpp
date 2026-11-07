#pragma once

#include <netinet/in.h>

#include <chrono>
#include <cstdint>
#include <string>

class Icmp {
public:
    enum class Status { Ok, Timeout, Unreachable, SendFail, RecvFail, NotOpen, InvalidAddress };
    enum class Mode { None, Datagram, Raw, External };

    struct Options {
        int timeoutMs = 1000;
        int dataSize = 56;
        bool allowExternal = true;
        std::string externalCommand = "/usr/bin/ping";
    };

    struct Result {
        Status status = Status::NotOpen;
        int ttl = 0;
        double rttMs = 0.0;
    };

    explicit Icmp(Options options);
    ~Icmp();

    Icmp(const Icmp&) = delete;
    Icmp& operator=(const Icmp&) = delete;

    bool isOpen() const;
    Mode mode() const;
    const std::string& lastError() const;

    Result ping(const std::string& ip);

    static const char* statusName(Status status);
    static const char* modeName(Mode mode);

private:
    using Clock = std::chrono::steady_clock;

    void open();
    bool openSocket(int type);
    Result pingSocket(const sockaddr_in& address);
    Result pingExternal(const std::string& ip);
    bool sendEcho(const sockaddr_in& address, std::uint16_t sequence);
    Result receiveReply(const sockaddr_in& address, std::uint16_t sequence, Clock::time_point sent,
                        Clock::time_point deadline);

    Options options_;
    int socket_;
    Mode mode_;
    std::uint16_t id_;
    std::uint16_t sequence_;
    std::string lastError_;
};
