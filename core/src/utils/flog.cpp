#include "flog.h"
#include <charconv>
#include <mutex>
#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#ifndef FLOG_ANDROID_TAG
#define FLOG_ANDROID_TAG    "flog"
#endif
#endif


namespace flog {

    std::mutex outMtx;
    std::vector<LogRec> logRecords;
    int performAdhocFileLogging = -1;
    static const char *adhocLogFileName = "/tmp/sdrpp.adhoc.log";
    static bool memoryLogEnabled =
#ifdef __ANDROID__
        true;
#else
        false;
#endif

    void setMemoryLogEnabled(bool enabled) {
        std::lock_guard<std::mutex> lck(outMtx);
        memoryLogEnabled = enabled;
        if (!memoryLogEnabled) {
            logRecords.clear();
        }
    }

    bool isMemoryLogEnabled() {
        std::lock_guard<std::mutex> lck(outMtx);
        return memoryLogEnabled;
    }

    const char* TYPE_STR[_TYPE_COUNT] = {
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR"
    };


#ifdef _WIN32
#define COLOR_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
    const WORD TYPE_COLORS[_TYPE_COUNT] = {
        FOREGROUND_GREEN | FOREGROUND_BLUE,
                FOREGROUND_GREEN | FOREGROUND_INTENSITY,                  // Bright green
                FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright yellow
                FOREGROUND_RED | FOREGROUND_INTENSITY                     // Bright red
        };
#else
#define COLOR_WHITE "\x1B[0m"
    const char* TYPE_COLORS[_TYPE_COUNT] = {
        "\x1B[36m",
        "\x1B[32m",
        "\x1B[33m",
        "\x1B[31m",
    };
#endif

#ifdef __ANDROID__
    const android_LogPriority TYPE_PRIORITIES[_TYPE_COUNT] = {
        ANDROID_LOG_DEBUG,
        ANDROID_LOG_INFO,
        ANDROID_LOG_WARN,
        ANDROID_LOG_ERROR
    };
#endif

    std::string normalizeFormatString(std::string_view fmt) {
        std::string out;
        out.reserve(fmt.size());
        size_t nextArgument = 0;

        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] == '\\' && i + 1 < fmt.size()) {
                const char escaped = fmt[++i];
                if (escaped == '{' || escaped == '}') { out += escaped; }
                out += escaped;
                continue;
            }
            if (fmt[i] == '}') {
                out += "}}";
                continue;
            }
            if (fmt[i] != '{') {
                out += fmt[i];
                continue;
            }
            const size_t closingBrace = fmt.find('}', i + 1);
            if (closingBrace == std::string_view::npos) {
                out += "{{";
                continue;
            }

            const auto field = fmt.substr(i + 1, closingBrace - i - 1);
            size_t argument = nextArgument;
            if (!field.empty()) {
                const auto result = std::from_chars(field.data(), field.data() + field.size(), argument);
                if (result.ec != std::errc{} || result.ptr != field.data() + field.size()) {
                    out += "{{";
                    out += field;
                    out += "}}";
                    i = closingBrace;
                    continue;
                }
            }
            nextArgument = argument + 1;
            out += '{';
            out += std::to_string(argument);
            out += '}';
            i = closingBrace;
        }
        return out;
    }

    void __log__(Type type, const std::string& out) {
        // Get output stream depending on type
        FILE* outStream = (type == TYPE_ERROR) ? stderr : stdout;

        // Get time
        auto now = std::chrono::system_clock::now();
        auto nowt = std::chrono::system_clock::to_time_t(now);
        long long msec = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

        // Write to output
        {
            std::lock_guard<std::mutex> lck(outMtx);
            auto nowc = std::localtime(&nowt); // This is now threadsafe
#if defined(_WIN32)
            // Get output handle and return if invalid
            int wOutStream = (type == TYPE_ERROR) ? STD_ERROR_HANDLE  : STD_OUTPUT_HANDLE;
            HANDLE conHndl = GetStdHandle(wOutStream);
            if (!conHndl || conHndl == INVALID_HANDLE_VALUE) { return; }

            static CONSOLE_SCREEN_BUFFER_INFO console_info { 0 };
            static WORD bg = 0;
            if (console_info.wAttributes == 0) {
                GetConsoleScreenBufferInfo (conHndl, &console_info);
                bg = (console_info.wAttributes & ~7);
            }


            // Print beginning of log line
            SetConsoleTextAttribute(conHndl, bg | COLOR_WHITE);
            fprintf(outStream, "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [", nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, 0);

            // Switch color to the log color, print log type and 
            SetConsoleTextAttribute(conHndl, bg | TYPE_COLORS[type]);
            fputs(TYPE_STR[type], outStream);
            

            // Switch back to default color and print rest of log string
            SetConsoleTextAttribute(conHndl, bg | COLOR_WHITE);
            fprintf(outStream, "] %s\n", out.c_str());
            fflush(outStream);
#elif defined(__ANDROID__)
            // Print format string
            __android_log_print(ANDROID_LOG_WARN, FLOG_ANDROID_TAG, "%s\n", out.c_str());
//            __android_log_print(TYPE_PRIORITIES[type], FLOG_ANDROID_TAG, COLOR_WHITE "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s%s" COLOR_WHITE "] %s\n",
//                    nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, 0, TYPE_COLORS[type], TYPE_STR[type], out.c_str());
#else
            // Print format string
            fprintf(outStream, COLOR_WHITE "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s%s" COLOR_WHITE "] %s\n",
                    nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, (int)(msec % 1000), TYPE_COLORS[type], TYPE_STR[type], out.c_str());
            fflush(outStream);
#endif

            if (performAdhocFileLogging == -1) {
                FILE *f = fopen(adhocLogFileName,"rt");
                if (f) {
                    fclose(f);
                    performAdhocFileLogging = 1;
                }
                else {
                    performAdhocFileLogging = 0;
                }
            }
            if (performAdhocFileLogging == 1) {
                FILE *f = fopen(adhocLogFileName,"at");
                fprintf(f, "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s" "] %s\n",
                    nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, (int)(msec % 1000), TYPE_STR[type], out.c_str());
                fflush(f);
                fclose(f);
            }
            if (memoryLogEnabled) {
                logRecords.emplace_back(LogRec{msec, type, out});
            }
        }
    }

}
