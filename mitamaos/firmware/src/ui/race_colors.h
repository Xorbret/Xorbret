#pragma once
// One colour per demon race, shared by Proxima's sighting list and the
// Compendium so a race looks the same everywhere.
#include "mitama_ui.h"
#include "../detect/signatures.h"

namespace MUI {
inline uint16_t raceColor(uint8_t race) {
    switch (race) {
        case Sig::R_VILE:   return YELLOW;
        case Sig::R_NIGHT:  return PURPLE;
        case Sig::R_FAIRY:  return MAGENTA;
        case Sig::R_AVIAN:
        case Sig::R_HERALD: return CYAN;
        case Sig::R_FOUL:   return BONES;
        case Sig::R_JAKI:   return GREEN;
        default:            return RED;
    }
}
} // namespace MUI
