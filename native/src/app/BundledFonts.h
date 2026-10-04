#pragma once

namespace FlappedEar {

// Registers Sora and JetBrains Mono (SIL OFL 1.1), the fonts of FlappedEar
// Telemetry's design language that the editor uses through qml/Theme.js.
// Returns false if any bundled font could not be loaded.
bool registerBundledFonts();

} // namespace FlappedEar
