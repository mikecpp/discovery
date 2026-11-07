#pragma once

#include <string>
#include <utility>
#include <vector>

struct HttpResponse {
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    void setHeader(const std::string& name, const std::string& value);

    static HttpResponse html(std::string body, int status = 200);
    static HttpResponse text(std::string body, int status = 200);
    static HttpResponse json(std::string body, int status = 200);
    static HttpResponse redirect(const std::string& location, int status = 303);
    static const char* reasonPhrase(int status);
};
