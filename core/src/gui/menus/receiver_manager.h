#pragma once
#include <string>

namespace receiver_manager_menu {
    void init();
    void draw(void* ctx);

    // Compact "[ RX2 v ]" selector for the main top bar. Returns true if the selection changed.
    bool drawSelector(float width);

    // "NFM  12.5 kHz" style summary of the active receiver, empty if there is none.
    std::string activeReceiverSummary();

    // Short mode name ("NFM", "USB", ...) for a receiver, or "" if it has no radio interface.
    std::string modeName(const std::string& receiverName);
}
