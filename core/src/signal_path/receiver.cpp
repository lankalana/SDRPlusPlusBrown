#include <signal_path/receiver.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <cctype>
#include <cstring>

namespace receivers {
    // Matches prefix followed by at least one digit and nothing else, so that a user instance
    // called e.g. "AUTOMATIC" isn't mistaken for an automatic receiver.
    static bool isIndexedName(const std::string& name, const char* prefix) {
        size_t plen = strlen(prefix);
        if (name.size() <= plen) { return false; }
        if (name.compare(0, plen, prefix) != 0) { return false; }
        for (size_t i = plen; i < name.size(); i++) {
            if (!isdigit((unsigned char)name[i])) { return false; }
        }
        return true;
    }

    ReceiverOwner ownerFromName(const std::string& name) {
        return isIndexedName(name, AUTO_PREFIX) ? ReceiverOwner::AUTOMATIC : ReceiverOwner::MANUAL;
    }

    std::string makeName(ReceiverOwner owner, int index) {
        return std::string((owner == ReceiverOwner::AUTOMATIC) ? AUTO_PREFIX : MANUAL_PREFIX) + std::to_string(index);
    }

    bool nameTaken(const std::string& name) {
        if (core::moduleManager.instances.find(name) != core::moduleManager.instances.end()) { return true; }
        return sigpath::vfoManager.vfoExists(name);
    }

    std::string nextFreeName(ReceiverOwner owner) {
        for (int i = 1;; i++) {
            std::string name = makeName(owner, i);
            if (!nameTaken(name)) { return name; }
        }
    }
}
