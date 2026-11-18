#pragma once
#include <cstring>

// Printable ASCII without spaces; WPA2 passphrase length, not a raw PSK.
inline bool validWifiPassword(const char* password) {
    const size_t length = std::strlen(password);
    if (length < 8 || length > 63) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char value = static_cast<unsigned char>(password[i]);
        if (value < 33 || value > 126) return false;
    }
    return true;
}
