#include "Json.hpp"

#include <cmath>
#include <cstdio>

std::string Json::quote(const std::string& text) {
    std::string result = "\"";
    for (const unsigned char character : text) {
        switch (character) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (character < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                result += buffer;
            } else {
                result += static_cast<char>(character);
            }
        }
    }
    result += '"';
    return result;
}

std::string Json::number(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

JsonObject& JsonObject::set(const std::string& key, const std::string& value) {
    return setRaw(key, Json::quote(value));
}

JsonObject& JsonObject::set(const std::string& key, const char* value) {
    return setRaw(key, Json::quote(value));
}

JsonObject& JsonObject::set(const std::string& key, int value) {
    return setRaw(key, std::to_string(value));
}

JsonObject& JsonObject::set(const std::string& key, std::int64_t value) {
    return setRaw(key, std::to_string(value));
}

JsonObject& JsonObject::set(const std::string& key, std::uint64_t value) {
    return setRaw(key, std::to_string(value));
}

JsonObject& JsonObject::set(const std::string& key, double value) {
    return setRaw(key, Json::number(value));
}

JsonObject& JsonObject::set(const std::string& key, bool value) {
    return setRaw(key, value ? "true" : "false");
}

JsonObject& JsonObject::setRaw(const std::string& key, const std::string& raw) {
    if (!body_.empty()) {
        body_ += ',';
    }
    body_ += Json::quote(key) + ":" + raw;
    return *this;
}

std::string JsonObject::str() const {
    return "{" + body_ + "}";
}

JsonArray& JsonArray::pushRaw(const std::string& raw) {
    if (!body_.empty()) {
        body_ += ',';
    }
    body_ += raw;
    return *this;
}

JsonArray& JsonArray::push(const std::string& value) {
    return pushRaw(Json::quote(value));
}

JsonArray& JsonArray::push(int value) {
    return pushRaw(std::to_string(value));
}

std::string JsonArray::str() const {
    return "[" + body_ + "]";
}
