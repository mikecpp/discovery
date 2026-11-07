#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpUtil.hpp"

std::string HttpRequest::header(const std::string& name) const {
    const auto found = headers.find(HttpUtil::toLower(name));
    return found == headers.end() ? std::string() : found->second;
}

std::string HttpRequest::queryValue(const std::string& name) const {
    const auto found = query.find(name);
    return found == query.end() ? std::string() : found->second;
}

void HttpResponse::setHeader(const std::string& name, const std::string& value) {
    const std::string lower = HttpUtil::toLower(name);
    for (auto& header : headers) {
        if (HttpUtil::toLower(header.first) == lower) {
            header.second = value;
            return;
        }
    }
    headers.emplace_back(name, value);
}

HttpResponse HttpResponse::html(std::string body, int status) {
    HttpResponse response;
    response.status = status;
    response.body = std::move(body);
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    return response;
}

HttpResponse HttpResponse::text(std::string body, int status) {
    HttpResponse response;
    response.status = status;
    response.body = std::move(body);
    response.setHeader("Content-Type", "text/plain; charset=utf-8");
    return response;
}

HttpResponse HttpResponse::json(std::string body, int status) {
    HttpResponse response;
    response.status = status;
    response.body = std::move(body);
    response.setHeader("Content-Type", "application/json; charset=utf-8");
    response.setHeader("Cache-Control", "no-store");
    return response;
}

HttpResponse HttpResponse::redirect(const std::string& location, int status) {
    HttpResponse response;
    response.status = status;
    response.setHeader("Location", location);
    return response;
}

const char* HttpResponse::reasonPhrase(int status) {
    switch (status) {
    case 100:
        return "Continue";
    case 200:
        return "OK";
    case 202:
        return "Accepted";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}
