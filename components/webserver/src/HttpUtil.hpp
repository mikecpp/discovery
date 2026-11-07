#pragma once

#include <map>
#include <string>

#include "HttpRequest.hpp"

class HttpUtil {
public:
    static bool parseRequestHead(const std::string& head, HttpRequest& request);
    static std::map<std::string, std::string> parseQuery(const std::string& text);
    static std::string urlDecode(const std::string& text, bool plusAsSpace);
    static std::string toLower(std::string text);
    static std::string trim(const std::string& text);
};
