#pragma once

#include <map>
#include <string>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;
    std::string body;

    std::string header(const std::string& name) const;
    std::string queryValue(const std::string& name) const;
};
