#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

class WebServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest& request)>;
    using RequestObserver = std::function<void(const HttpRequest& request, const HttpResponse& response)>;

    struct Options {
        std::string address = "0.0.0.0";
        std::uint16_t port = 8080;
        std::string title = "Device Discovery";
        std::size_t maxBodyBytes = 64 * 1024;
    };

    explicit WebServer(Options options);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    bool start();
    void stop();
    bool isRunning() const;
    std::uint16_t port() const;
    const std::string& lastError() const;

    void addRoute(const std::string& method, const std::string& path, Handler handler);
    void setRequestObserver(RequestObserver observer);

private:
    void serve();
    void handleClient(int client);
    HttpResponse dispatch(const HttpRequest& request);
    bool sendResponse(int client, const HttpResponse& response);
    HttpResponse showInfo(const HttpRequest& request) const;
    HttpResponse serveAsset(const HttpRequest& request) const;
    HttpResponse notFound(const HttpRequest& request) const;

    Options options_;
    int listener_;
    std::uint16_t boundPort_;
    std::atomic<bool> running_;
    std::thread thread_;
    std::map<std::string, Handler> routes_;
    RequestObserver observer_;
    std::mutex mutex_;
    std::string lastError_;
};
