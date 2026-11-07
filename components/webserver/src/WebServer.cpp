#include "WebServer.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "EmbeddedAssets.hpp"
#include "HttpUtil.hpp"

namespace {

constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
constexpr int kSocketTimeoutSeconds = 10;
constexpr int kPollIntervalMilliseconds = 200;

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void setCloseOnExec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

int createSocketCloexec(int domain, int type, int protocol) {
#if defined(SOCK_CLOEXEC)
    return ::socket(domain, type | SOCK_CLOEXEC, protocol);
#else
    const int fd = ::socket(domain, type, protocol);
    if (fd >= 0) {
        setCloseOnExec(fd);
    }
    return fd;
#endif
}

int acceptCloexec(int listener) {
#if defined(__linux__)
    return ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
#else
    const int fd = ::accept(listener, nullptr, nullptr);
    if (fd >= 0) {
        setCloseOnExec(fd);
    }
    return fd;
#endif
}

bool sendAll(int socket, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t sent = ::send(socket, data, size, kSendFlags);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool sendAll(int socket, const std::string& data) {
    return sendAll(socket, data.data(), data.size());
}

void configureClient(int client) {
    timeval timeout{};
    timeout.tv_sec = kSocketTimeoutSeconds;
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#if defined(SO_NOSIGPIPE)
    int enabled = 1;
    ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
}

bool parseContentLength(const std::string& text, std::size_t& length) {
    if (text.empty()) {
        length = 0;
        return true;
    }
    if (text.find_first_not_of("0123456789") != std::string::npos || text.size() > 18) {
        return false;
    }
    length = static_cast<std::size_t>(std::strtoull(text.c_str(), nullptr, 10));
    return true;
}

bool isApiPath(const std::string& path) {
    return path.rfind("/api/", 0) == 0;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const char* contentType(const std::string& path) {
    struct Type {
        const char* extension;
        const char* type;
    };
    static const Type types[] = {
        {".html", "text/html; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".ico", "image/x-icon"},
        {".woff2", "font/woff2"},
    };
    for (const Type& type : types) {
        if (endsWith(path, type.extension)) {
            return type.type;
        }
    }
    return "application/octet-stream";
}

const EmbeddedAsset* findAsset(const std::string& path) {
    for (std::size_t index = 0; index < kEmbeddedAssetCount; ++index) {
        if (path == kEmbeddedAssets[index].path) {
            return &kEmbeddedAssets[index];
        }
    }
    return nullptr;
}

std::string jsonEscape(const std::string& text) {
    std::string result;
    for (const char character : text) {
        if (character == '"' || character == '\\') {
            result += '\\';
        }
        result += character;
    }
    return result;
}

}

WebServer::WebServer(Options options)
    : options_(std::move(options)), listener_(-1), boundPort_(0), running_(false) {
    routes_["GET /api/info"] = [this](const HttpRequest& request) { return showInfo(request); };
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (running_) {
        return true;
    }
    lastError_.clear();

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options_.port);
    if (::inet_pton(AF_INET, options_.address.c_str(), &address.sin_addr) != 1) {
        lastError_ = "Invalid bind address: " + options_.address;
        return false;
    }

    listener_ = createSocketCloexec(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) {
        lastError_ = std::string("Cannot create socket: ") + std::strerror(errno);
        return false;
    }
    int enabled = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(listener_, 16) < 0) {
        lastError_ = "Cannot listen on " + options_.address + ":" + std::to_string(options_.port) + ": " +
                     std::strerror(errno);
        ::close(listener_);
        listener_ = -1;
        return false;
    }

    socklen_t length = sizeof(address);
    if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
        boundPort_ = ntohs(address.sin_port);
    }

    running_ = true;
    thread_ = std::thread(&WebServer::serve, this);
    return true;
}

void WebServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (listener_ >= 0) {
        ::close(listener_);
        listener_ = -1;
    }
}

bool WebServer::isRunning() const {
    return running_;
}

std::uint16_t WebServer::port() const {
    return boundPort_;
}

const std::string& WebServer::lastError() const {
    return lastError_;
}

void WebServer::addRoute(const std::string& method, const std::string& path, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_[method + " " + path] = std::move(handler);
}

void WebServer::setRequestObserver(RequestObserver observer) {
    std::lock_guard<std::mutex> lock(mutex_);
    observer_ = std::move(observer);
}

void WebServer::serve() {
    while (running_) {
        pollfd descriptor{};
        descriptor.fd = listener_;
        descriptor.events = POLLIN;
        if (::poll(&descriptor, 1, kPollIntervalMilliseconds) <= 0) {
            continue;
        }
        const int client = acceptCloexec(listener_);
        if (client < 0) {
            continue;
        }
        configureClient(client);
        handleClient(client);
        ::close(client);
    }
}

void WebServer::handleClient(int client) {
    std::string buffer;
    char chunk[8192];
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        if (buffer.size() > kMaxHeaderBytes) {
            sendResponse(client, HttpResponse::text("Request header is too large.\n", 431));
            return;
        }
        const ssize_t received = ::recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return;
        }
        buffer.append(chunk, static_cast<std::size_t>(received));
    }

    HttpRequest request;
    if (!HttpUtil::parseRequestHead(buffer.substr(0, headerEnd), request)) {
        sendResponse(client, HttpResponse::text("Bad request.\n", 400));
        return;
    }

    std::size_t contentLength = 0;
    if (!parseContentLength(request.header("content-length"), contentLength)) {
        sendResponse(client, HttpResponse::text("Invalid Content-Length header.\n", 400));
        return;
    }
    if (contentLength > options_.maxBodyBytes) {
        sendResponse(client, HttpResponse::text("The request body is too large.\n", 413));
        return;
    }
    if (contentLength > 0 && HttpUtil::toLower(request.header("expect")) == "100-continue") {
        sendAll(client, "HTTP/1.1 100 Continue\r\n\r\n");
    }

    request.body = buffer.substr(headerEnd + 4);
    request.body.reserve(contentLength);
    while (request.body.size() < contentLength) {
        const ssize_t received = ::recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return;
        }
        request.body.append(chunk, static_cast<std::size_t>(received));
    }
    request.body.resize(contentLength);

    const HttpResponse response = dispatch(request);
    sendResponse(client, response);

    RequestObserver observer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        observer = observer_;
    }
    if (observer) {
        observer(request, response);
    }
}

HttpResponse WebServer::dispatch(const HttpRequest& request) {
    Handler handler;
    bool pathExists = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = routes_.find(request.method + " " + request.path);
        if (found != routes_.end()) {
            handler = found->second;
        } else {
            for (const auto& entry : routes_) {
                const std::size_t space = entry.first.find(' ');
                if (entry.first.compare(space + 1, std::string::npos, request.path) == 0) {
                    pathExists = true;
                    break;
                }
            }
        }
    }
    if (!handler) {
        if (pathExists) {
            return HttpResponse::text("Method not allowed.\n", 405);
        }
        if (request.method == "GET" && !isApiPath(request.path)) {
            return serveAsset(request);
        }
        return notFound(request);
    }
    try {
        return handler(request);
    } catch (const std::exception& error) {
        return HttpResponse::text(std::string("Internal server error: ") + error.what() + "\n", 500);
    } catch (...) {
        return HttpResponse::text("Internal server error.\n", 500);
    }
}

bool WebServer::sendResponse(int client, const HttpResponse& response) {
    std::string head = "HTTP/1.1 " + std::to_string(response.status) + " " +
                       HttpResponse::reasonPhrase(response.status) + "\r\n";
    for (const auto& header : response.headers) {
        head += header.first + ": " + header.second + "\r\n";
    }
    head += "Content-Length: " + std::to_string(response.body.size()) + "\r\nConnection: close\r\n\r\n";
    return sendAll(client, head) && sendAll(client, response.body);
}

HttpResponse WebServer::showInfo(const HttpRequest&) const {
    return HttpResponse::json("{\"title\":\"" + jsonEscape(options_.title) + "\"}");
}

HttpResponse WebServer::serveAsset(const HttpRequest& request) const {
    const EmbeddedAsset* asset = findAsset(request.path);
    const bool hashed = request.path.rfind("/assets/", 0) == 0;
    if (asset == nullptr && !hashed) {
        asset = findAsset("/index.html");
    }
    if (asset == nullptr) {
        return notFound(request);
    }
    HttpResponse response;
    response.body.assign(reinterpret_cast<const char*>(asset->data), asset->size);
    response.setHeader("Content-Type", contentType(asset->path));
    response.setHeader("Cache-Control", hashed ? "public, max-age=31536000, immutable" : "no-cache");
    return response;
}

HttpResponse WebServer::notFound(const HttpRequest& request) const {
    if (isApiPath(request.path)) {
        return HttpResponse::json("{\"message\":\"Not found: " + jsonEscape(request.path) + "\"}", 404);
    }
    return HttpResponse::text("Not found: " + request.path + "\n", 404);
}
