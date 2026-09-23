#pragma once
#include <string>

/**
 * A "receiver" is a radio module instance together with its VFO. Ownership tells the automatic
 * reception subsystem which receivers it is allowed to touch: it may create, retune, delete and
 * record AUTOMATIC receivers, and must never modify MANUAL ones.
 */
enum class ReceiverOwner {
    MANUAL,
    AUTOMATIC
};

namespace receivers {
    // Manual receivers created from the Receivers panel are named RX1, RX2, ...
    // Automatic receivers are named AUTO1, AUTO2, ...
    // Ownership is derived from the name so it survives restarts without a separate mapping.
    inline constexpr const char* MANUAL_PREFIX = "RX";
    inline constexpr const char* AUTO_PREFIX = "AUTO";

    ReceiverOwner ownerFromName(const std::string& name);

    std::string makeName(ReceiverOwner owner, int index);

    // Lowest-numbered name of the given kind that isn't already used by a module instance or VFO.
    std::string nextFreeName(ReceiverOwner owner);

    // True if a module instance or VFO already uses this name.
    bool nameTaken(const std::string& name);
}
