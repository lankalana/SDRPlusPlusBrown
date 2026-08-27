#pragma once
#include <format>
#include <string>
#include <string_view>

namespace utils {
    inline std::string formatFreq(double freq) {
        const auto [scaled, suffix] = freq >= 1000000.0
            ? std::pair{ freq / 1000000.0, std::string_view{ "MHz" } }
            : freq >= 1000.0
                ? std::pair{ freq / 1000.0, std::string_view{ "KHz" } }
                : std::pair{ freq, std::string_view{ "Hz" } };

        auto result = std::format("{:.6f}", scaled);
        result.erase(result.find_last_not_of('0') + 1);
        if (result.ends_with('.')) { result.pop_back(); }
        result += suffix;
        return result;
    }
}
