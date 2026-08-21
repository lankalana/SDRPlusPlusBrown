#pragma once

#include <cstdint>

namespace dsp::protocol::lora {

inline void normalizedSyncWord(uint16_t syncWord, int& first, int& second) {
    if (syncWord <= 0xFFu) {
        first = (syncWord >> 4u) & 0x0Fu;
        second = syncWord & 0x0Fu;
    }
    else {
        // SX126x stores each on-air sync nibble in the high half of a register
        // byte and reserves the low nibble (for example private sync 0x1424).
        first = (syncWord >> 12u) & 0x0Fu;
        second = (syncWord >> 4u) & 0x0Fu;
    }
}

inline bool matchSyncWord(int firstSyncSymbol, int secondSyncSymbol, uint16_t configuredSyncWord) {
    int expectedFirst = 0;
    int expectedSecond = 0;
    normalizedSyncWord(configuredSyncWord, expectedFirst, expectedSecond);
    return firstSyncSymbol == expectedFirst && secondSyncSymbol == expectedSecond;
}

inline int firstSyncBin(uint16_t syncWord, int bins) {
    int first = 0;
    int second = 0;
    normalizedSyncWord(syncWord, first, second);
    return (first << 3u) % bins;
}

inline int secondSyncBin(uint16_t syncWord, int bins) {
    int first = 0;
    int second = 0;
    normalizedSyncWord(syncWord, first, second);
    return ((second << 3u) + bins / 2) % bins;
}

inline int syncNibbleFromBin(int bin, int bins, bool secondSymbol) {
    if (secondSymbol) { bin -= bins / 2; }
    bin %= bins;
    if (bin < 0) { bin += bins; }
    return ((bin + 4) / 8) & 0x0F;
}

}
