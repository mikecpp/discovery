#include "HttpUtil.hpp"

#include <algorithm>
#include <cctype>

namespace {

char lowerCharacter(unsigned char character) {
    return static_cast<char>(std::tolower(character));
}

int hexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

}

bool HttpUtil::parseRequestHead(const std::string& head, HttpRequest& request) {
    const std::size_t lineEnd = std::min(head.find("\r\n"), head.size());
    const std::string requestLine = head.substr(0, lineEnd);
    const std::size_t firstSpace = requestLine.find(' ');
    const std::size_t secondSpace = requestLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos ||
        requestLine.find(' ', secondSpace + 1) != std::string::npos) {
        return false;
    }
    request.method = requestLine.substr(0, firstSpace);
    const std::string target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    request.version = requestLine.substr(secondSpace + 1);
    if (request.method.empty() || request.version.rfind("HTTP/", 0) != 0) {
        return false;
    }
    const std::size_t questionMark = target.find('?');
    request.path = urlDecode(target.substr(0, questionMark), false);
    if (questionMark != std::string::npos) {
        request.query = parseQuery(target.substr(questionMark + 1));
    }
    if (request.path.empty() || request.path.front() != '/') {
        return false;
    }
    std::size_t position = lineEnd + 2;
    while (position < head.size()) {
        std::size_t end = head.find("\r\n", position);
        if (end == std::string::npos) {
            end = head.size();
        }
        const std::string line = head.substr(position, end - position);
        position = end + 2;
        if (line.empty()) {
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return false;
        }
        request.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return true;
}

std::map<std::string, std::string> HttpUtil::parseQuery(const std::string& text) {
    std::map<std::string, std::string> result;
    std::size_t position = 0;
    while (position <= text.size()) {
        std::size_t end = text.find('&', position);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string pair = text.substr(position, end - position);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            const std::string key = urlDecode(pair.substr(0, equals), true);
            const std::string value = equals == std::string::npos ? "" : urlDecode(pair.substr(equals + 1), true);
            result[key] = value;
        }
        position = end + 1;
    }
    return result;
}

std::string HttpUtil::urlDecode(const std::string& text, bool plusAsSpace) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '%' && index + 2 < text.size()) {
            const int high = hexValue(text[index + 1]);
            const int low = hexValue(text[index + 2]);
            if (high >= 0 && low >= 0) {
                result += static_cast<char>(high * 16 + low);
                index += 2;
                continue;
            }
        }
        result += (plusAsSpace && character == '+') ? ' ' : character;
    }
    return result;
}

std::string HttpUtil::toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), lowerCharacter);
    return text;
}

std::string HttpUtil::trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}
