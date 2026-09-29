#pragma once

// File Transfer's choices, in the list's order (NetworkModeSelectionActivity: Join Network, Create Hotspot, USB
// Drive). Pure; tests: test/home_menu (NetworkModes).

#include <cstddef>

enum class NetworkMode { JOIN_NETWORK, CREATE_HOTSPOT, USB_DRIVE };

inline constexpr NetworkMode kNetworkModes[] = {NetworkMode::JOIN_NETWORK, NetworkMode::CREATE_HOTSPOT,
                                                NetworkMode::USB_DRIVE};
inline constexpr size_t kNetworkModeCount = sizeof(kNetworkModes) / sizeof(kNetworkModes[0]);
