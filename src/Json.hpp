#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Json {
public:
    static std::string quote(const std::string& text);
    static std::string number(double value);
};

class JsonObject {
public:
    JsonObject& set(const std::string& key, const std::string& value);
    JsonObject& set(const std::string& key, const char* value);
    JsonObject& set(const std::string& key, int value);
    JsonObject& set(const std::string& key, std::int64_t value);
    JsonObject& set(const std::string& key, std::uint64_t value);
    JsonObject& set(const std::string& key, double value);
    JsonObject& set(const std::string& key, bool value);
    JsonObject& setRaw(const std::string& key, const std::string& raw);
    std::string str() const;

private:
    std::string body_;
};

class JsonArray {
public:
    JsonArray& pushRaw(const std::string& raw);
    JsonArray& push(const std::string& value);
    JsonArray& push(int value);
    std::string str() const;

private:
    std::string body_;
};
