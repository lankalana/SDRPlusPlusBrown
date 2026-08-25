#include "hrfreq.h"
#include <array>
#include <charconv>
#include <cctype>
#include <utils/flog.h>

namespace hrfreq {


    std::string toString(double freq) {
        // Determine the scale
        int maxDecimals = 0;
        const char* suffix = "Hz";
        if (freq >= 1e9) {
            freq /= 1e9;
            maxDecimals = 9;
            suffix = "GHz";
        }
        else if (freq >= 1e6) {
            freq /= 1e6;
            maxDecimals = 6;
            suffix = "MHz";
        }
        else if (freq >= 1e3) {
            freq /= 1e3;
            maxDecimals = 3;
            suffix = "KHz";
        }

        std::array<char, 128> numBuf;
        const auto [numEnd, error] = std::to_chars(numBuf.data(), numBuf.data() + numBuf.size(), freq, std::chars_format::fixed, maxDecimals);
        if (error != std::errc{}) {
            return {};
        }
        std::string result(numBuf.data(), numEnd);

        // If there is a decimal point, remove the useless zeros
        if (maxDecimals) {
            result.erase(result.find_last_not_of('0') + 1);
            if (result.ends_with('.')) { result.pop_back(); }
        }

        result += suffix;
        return result;
    }

    bool isNumeric(char c) {
        return std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == ',';
    }

    std::expected<double, std::string> fromString(std::string_view str) {
        // Skip non-numeric characters
        int i = 0;
        char c;
        for (; i < str.size(); i++) {
            if (isNumeric(str[i])) { break; }
        }

        // Extract the numeric part
        std::string numeric;
        for (; i < str.size(); i++) {
            // Get the character
            c = str[i];

            // If it's a letter, stop
            if (std::isalpha(static_cast<unsigned char>(c))) { break; }

            // If isn't numeric, skip it
            if (!isNumeric(c)) { continue; }

            // If it's a comma, skip it for now. This enforces a dot as a decimal point
            if (c == ',') { continue; }

            // Add the character to the numeric string
            numeric += c;
        }

        // Attempt to parse the numeric part
        double num;
        const auto [numericEnd, error] = std::from_chars(numeric.data(), numeric.data() + numeric.size(), num);
        if (error != std::errc{} || numericEnd != numeric.data() + numeric.size()) {
            return std::unexpected(flog::format("Failed to parse numeric part: '{}'", numeric));
        }

        // If no more text is available, assume the numeric part gives a frequency in Hz
        if (i == str.size()) {
            flog::warn("No unit given, assuming it's Hz");
            return num;
        }

        // Scale the numeric value depending on the first scale character
        char scale = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i])));
        switch (scale) {
        case 'G':
            num *= 1e9;
            break;
        case 'M':
            num *= 1e6;
            break;
        case 'K':
            num *= 1e3;
            break;
        case 'H':
            break;
        default:
            flog::warn("Unknown frequency scale: '{}'", scale);
            break;
        }

        // Return the frequency
        return num;
    }
}
