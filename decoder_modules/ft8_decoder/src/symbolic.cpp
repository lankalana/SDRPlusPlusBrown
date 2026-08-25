
#include "symbolic.h"

#include <cctype>

namespace {

bool isSignalStrength(std::string_view maybeStrength) {
    if (maybeStrength.size() < 2) return false;
    if (maybeStrength[0] != '+' && maybeStrength[0] != '-') return false;
    for (const char c : maybeStrength.substr(1)) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

bool isLocator(std::string_view maybeLocator) {
    return maybeLocator.size() == 4
        && std::isupper(static_cast<unsigned char>(maybeLocator[0]))
        && std::isupper(static_cast<unsigned char>(maybeLocator[1]))
        && std::isdigit(static_cast<unsigned char>(maybeLocator[2]))
        && std::isdigit(static_cast<unsigned char>(maybeLocator[3]));
}

}

std::string extractCallsignFromFT8(std::string_view message) {
    std::string_view first;
    std::string_view previous;
    std::string_view last;
    std::size_t partCount = 0;

    while (!message.empty()) {
        const auto start = message.find_first_not_of(' ');
        if (start == std::string_view::npos) {
            break;
        }
        message.remove_prefix(start);
        const auto end = message.find(' ');
        const auto part = message.substr(0, end);
        if (partCount++ == 0) {
            first = part;
        }
        previous = last;
        last = part;
        if (end == std::string_view::npos) {
            break;
        }
        message.remove_prefix(end + 1);
    }

    if (partCount > 1 && (last == "RR73" || last == "RRR" || last == "73" || isLocator(last))) {
        return std::string(previous);
    }
    if (partCount > 1 && ((last.size() > 1 && last[0] == 'R' && isSignalStrength(last.substr(1))) || isSignalStrength(last))) {
        return std::string(previous);
    }
    if (partCount == 2 && first == "CQ") {
        return std::string(last);
    }

    return {};
}

