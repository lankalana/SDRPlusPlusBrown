#pragma once

#include <cstdint>

namespace dsp::protocol::lora {

inline void normalizedSyncWord(uint16_t syncWord, int& first, int& second) {
    if (syncWord <= 0xFFu) {
        first = (syncWord >> 4u) & 0x0Fu;
        second = syncWord & 0x0Fu;
    }
    else {
        first = (syncWord >> 8u) & 0x0Fu;
        second = syncWord & 0x0Fu;
    }
}

inline bool matchSyncWord(int firstSyncSymbol, int secondSyncSymbol, uint16_t configuredSyncWord) {
    int expectedFirst = 0;
    int expectedSecond = 0;
    normalizedSyncWord(configuredSyncWord, expectedFirst, expectedSecond);
    return firstSyncSymbol == expectedFirst && secondSyncSymbol == expectedSecond;
}

}
