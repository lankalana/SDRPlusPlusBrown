#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>
#include <cmath>

inline void coalesceSplit(std::vector<std::string> &x) {
    std::erase(x, "");
}

template <typename Callback>
inline void splitString(std::string_view str, std::string_view separators, Callback&& callback) {
    if (str.empty()) {
        return;
    }

    std::size_t start = 0;
    while (true) {
        const auto end = str.find_first_of(separators, start);
        if (end == std::string_view::npos) { break; }
        callback(std::string(str.substr(start, end - start)));
        start = end + 1;
    }
    if (start < str.size()) {
        callback(std::string(str.substr(start)));
    }
}



inline void splitStringV(const std::string & str, const char *sep, std::vector<std::string> &dest) {
    dest.clear();
    splitString(str,sep,[&](const std::string &str) {
        dest.emplace_back(str);
    });
}

inline std::string joinStringV(std::string_view sep, const std::vector<std::string> &src) {
    std::string ret;
    if (src.empty()) {
        return ret;
    }

    std::size_t size = sep.size() * (src.size() - 1);
    for (const auto& item : src) {
        size += item.size();
    }
    ret.reserve(size);

    bool first = true;
    for(const auto& q : src) {
        if (!first){
            ret += sep;
        }
        ret += q;
        first = false;
    }
    return ret;
}


inline void removeSubstrings(std::string& input, const std::string& substring) {
    size_t pos = input.find(substring);
    while (pos != std::string::npos) {
        input.erase(pos, substring.length());
        pos = input.find(substring, pos);
    }
}


inline void replaceSubstrings(std::string& input, const std::string& substring, const std::string& replacement) {
    size_t pos = input.find(substring);
    while (pos != std::string::npos) {
        input.replace(pos, substring.length(), replacement);
        pos = input.find(substring, pos + replacement.length());
    }
}


inline void trimString(std::string & line) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c); };
    const auto begin = std::find_if_not(line.begin(), line.end(), isSpace);
    const auto end = std::find_if_not(line.rbegin(), line.rend(), isSpace).base();
    line = begin < end ? std::string(begin, end) : std::string{};
}

namespace percentile {


    template<typename T>
    T kthSmallest(T *arr, int l, int r, int k)
    {
        if (k > 0 && k <= r - l + 1) {
            T* nth = arr + l + k - 1;
            std::nth_element(arr + l, nth, arr + r + 1);
            return *nth;
        }
        return arr[0];
    }


    template<typename T>
    T percentile(std::vector<T>& arr, double p) {
        int n = arr.size();
        if (n == 0) {
            return 0;
        }
        double k = (n - 1) * p;
        return kthSmallest(arr.data(), 0, n-1, (int)k);
    }

    template<typename T>
    T percentile_sampling(std::vector<T>& arr, double p) {
        int n = arr.size();
        if (n == 0) {
            return 0;
        }
        int targetPoints = 100;
        T *data = arr.data();
        std::vector<T> sampled;
        if (n > 2 * targetPoints) {
            // sample array
            float step = (n - 1) / (float)targetPoints;
            sampled.resize(targetPoints);
            for(int z=0; z<targetPoints; z++) {
                sampled[z] = data[(int)std::floor(step * z)];
            }
            data = sampled.data();
            n = sampled.size();
        }
        double k = (n - 1) * p;
        return kthSmallest(data, 0, n-1, (int)k);
    }


}
