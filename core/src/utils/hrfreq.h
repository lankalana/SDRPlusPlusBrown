#pragma once
#include <expected>
#include <string>
#include <string_view>

namespace hrfreq {
    /**
     * Convert a frequency to a human-readable string.
     * @param freq Frequency in Hz.
     * @return Human-readable representation of the frequency.
    */
    std::string toString(double freq);

    /**
     * Convert a human-readable representation of a frequency to a frequency value.
     * @param str String containing the human-readable frequency.
     * @return The decoded frequency, or a description of the parse error.
    */
    std::expected<double, std::string> fromString(std::string_view str);
}
