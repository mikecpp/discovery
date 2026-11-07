#pragma once

#include <string>

class Oui {
public:
    static std::string vendor(const std::string& mac);
    static bool isRandomized(const std::string& mac);
};
