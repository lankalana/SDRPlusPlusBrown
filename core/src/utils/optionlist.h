#pragma once
#include <algorithm>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include <stdexcept>

template <class K, class T>
class OptionList {
public:
    OptionList() { updateText(); }
    OptionList(const OptionList& other) : entries(other.entries) { updateText(); }
    OptionList(OptionList&& other) : entries(std::move(other.entries)) { updateText(); }

    OptionList& operator=(const OptionList& other) {
        entries = other.entries;
        updateText();
        return *this;
    }

    OptionList& operator=(OptionList&& other) {
        entries = std::move(other.entries);
        updateText();
        return *this;
    }

    void define(const K& key, const std::string& name, const T& value) {
        if (keyExists(key)) { throw std::runtime_error("Key already exists"); }
        if (nameExists(name)) { throw std::runtime_error("Name already exists"); }
        if (valueExists(value)) { throw std::runtime_error("Value already exists"); }
        entries.push_back({ key, name, value });
        updateText();
    }

    void define(const std::string& name, const T& value) {
        define(name, name, value);
    }

    void undefine(int id) {
        entries.erase(entries.begin() + id);
        updateText();
    }

    void undefineKey(const K& key) {
        undefine(keyId(key));
    }

    void undefineName(const std::string& name) {
        undefine(nameId(name));
    }

    void undefineValue(const T& value) {
        undefine(valueId(value));
    }

    void clear() {
        entries.clear();
        updateText();
    }

    int size() const {
        return static_cast<int>(entries.size());
    }

    bool empty() const {
        return entries.empty();
    }

    bool keyExists(const K& key) const {
        return std::ranges::find(entries, key, &Entry::key) != entries.end();
    }

    bool nameExists(const std::string& name) const {
        return std::ranges::find(entries, name, &Entry::name) != entries.end();
    }

    bool valueExists(const T& value) const {
        return std::ranges::find(entries, value, &Entry::value) != entries.end();
    }

    int keyId(const K& key) const {
        auto it = std::ranges::find(entries, key, &Entry::key);
        if (it == entries.end()) { throw std::runtime_error("Key doesn't exist"); }
        return static_cast<int>(std::distance(entries.begin(), it));
    }

    int nameId(const std::string& name) const {
        auto it = std::ranges::find(entries, name, &Entry::name);
        if (it == entries.end()) { throw std::runtime_error("Name doesn't exist"); }
        return static_cast<int>(std::distance(entries.begin(), it));
    }

    int valueId(const T& value) const {
        auto it = std::ranges::find(entries, value, &Entry::value);
        if (it == entries.end()) { throw std::runtime_error("Value doesn't exist"); }
        return static_cast<int>(std::distance(entries.begin(), it));
    }

    inline const K& key(int id) const {
        return entries[id].key;
    }

    inline const std::string& name(int id) const {
        return entries[id].name;
    }

    inline const T& value(int id) const {
        return entries[id].value;
    }

    inline const T& operator[](int& id) const {
        return entries[id].value;
    }

    const char* txt = nullptr;

private:
    struct Entry {
        K key;
        std::string name;
        T value;
    };

    void updateText() {
        _txt.clear();
        for (const auto& entry : entries) {
            _txt += entry.name;
            _txt += '\0';
        }
        txt = _txt.c_str();
    }

    std::vector<Entry> entries;
    std::string _txt;
};
