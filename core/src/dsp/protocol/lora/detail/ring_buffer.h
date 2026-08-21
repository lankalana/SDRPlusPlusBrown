#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace dsp::protocol::lora::detail {

template <class T>
class RingBuffer {
public:
    void reserve(std::size_t capacity) {
        storage.resize(capacity);
        clear();
    }

    void clear() {
        head = 0;
        used = 0;
    }

    void push(const T* values, std::size_t count) {
        if (storage.empty() || !values) { return; }
        for (std::size_t i = 0; i < count; i++) {
            storage[(head + used) % storage.size()] = values[i];
            if (used < storage.size()) { used++; }
            else { head = (head + 1) % storage.size(); }
        }
    }

    std::size_t size() const { return used; }
    std::size_t capacity() const { return storage.size(); }

    const T& operator[](std::size_t index) const {
        return storage[(head + index) % storage.size()];
    }

private:
    std::vector<T> storage;
    std::size_t head = 0;
    std::size_t used = 0;
};

}
