#pragma once
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <stdint.h>
#include <mutex>
#include "sdrpp_export.h"

namespace flog {
    enum Type {
        TYPE_DEBUG,
        TYPE_INFO,
        TYPE_WARNING,
        TYPE_ERROR,
        _TYPE_COUNT
    };

    struct LogRec {
        int64_t ts;
        Type typ;
        std::string message;
    };

    extern std::mutex outMtx;
    extern std::vector<LogRec> logRecords;

    void setMemoryLogEnabled(bool enabled);
    bool isMemoryLogEnabled();


    // IO functions
    void __log__(Type type, const std::string& message);
    std::string normalizeFormatString(std::string_view fmt);

    namespace detail {
        template <typename>
        inline constexpr bool alwaysFalse = false;

        template <typename T>
        std::string formatArgument(T&& value) {
            using Value = std::remove_cvref_t<T>;
            if constexpr (std::is_same_v<Value, int8_t>) {
                return std::format("{}", static_cast<int>(value));
            }
            else if constexpr (std::is_same_v<Value, uint8_t>) {
                return std::format("{}", static_cast<unsigned int>(value));
            }
            else if constexpr (std::is_enum_v<Value>) {
                return std::format("{}", static_cast<std::underlying_type_t<Value>>(value));
            }
            else if constexpr (std::formattable<Value, char>) {
                return std::format("{}", static_cast<Value>(value));
            }
            else if constexpr (requires { std::string{std::forward<T>(value)}; }) {
                return std::string{std::forward<T>(value)};
            }
            else {
                static_assert(alwaysFalse<Value>, "flog argument is neither formattable nor convertible to std::string");
            }
        }
    }

    // Formatting function
    template <typename... Args>
    inline std::string format(std::string_view fmt, Args&&... args) {
        auto formattedArgs = std::tuple{detail::formatArgument(std::forward<Args>(args))...};
        return std::apply([&](auto&... values) {
            return std::vformat(normalizeFormatString(fmt), std::make_format_args(values...));
        }, formattedArgs);
    }

    // Logging functions
    template <typename... Args>
    void log(Type type, std::string_view fmt, Args&&... args) {
        __log__(type, format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline void debug(std::string_view fmt, Args&&... args) {
        log(TYPE_DEBUG, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void info(std::string_view fmt, Args&&... args) {
        log(TYPE_INFO, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void warn(std::string_view fmt, Args&&... args) {
        log(TYPE_WARNING, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void error(std::string_view fmt, Args&&... args) {
        log(TYPE_ERROR, fmt, std::forward<Args>(args)...);
    }
}
