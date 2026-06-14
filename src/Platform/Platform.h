#pragma once
// platform/Platform.h — umbrella header for the platform-abstraction boundary
// (Linux port L1). Include this to pull in the full boundary surface.
//
// See PORTING_LINUX.md §4. The app links against these interfaces; Windows and
// Linux each provide implementations under platform/win/ and platform/linux/.
// L1 lands the boundary + the portable utilities (Text, Crypto, Paths) and wraps
// existing Windows code; later phases add the Linux impls.

#include "platform/Text.h"
#include "platform/Crypto.h"
#include "platform/Paths.h"
#include "platform/Net.h"
#include "platform/Window.h"
#include "platform/Renderer2D.h"
#include "platform/AudioIO.h"

namespace platform {

// True on the platform the binary was built for. Handy for the few spots that
// must branch at runtime rather than compile time.
inline constexpr bool is_windows() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

} // namespace platform
