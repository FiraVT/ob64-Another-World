// The liberation rule (R6 in docs/anotherworld/design-field-controls.md): what
// happens when a unit enters a town. The field overlay shows it on the cards
// and the move card (field_view.cpp); the auto commander picks units by it
// (auto_commander.cpp), so the two never disagree.
//
// `unit` is the unit's alignment, the average of its members' character byte
// +0x1B. `town` is the town's own alignment byte, location record +0x02
// (0x801951EC + i*0x24): 0xFF cannot be taken, bit 0x80 adds 40 to the gap.
// A gap of 31 or more captures; the game adds 0-7 at random, so a gap within 7
// of the line may go either way.
#pragma once

#include <cstdint>
#include <cstdlib>

namespace ogre::liberation {

enum class Verdict { Liberates, MayCapture, Captures };

inline Verdict verdict(int unit, uint8_t town) {
    const int gap = std::abs(unit - (town & 0x7F)) + ((town & 0x80) ? 40 : 0);
    if (gap >= 31) {
        return Verdict::Captures;
    }
    return gap + 7 < 31 ? Verdict::Liberates : Verdict::MayCapture;
}

}  // namespace ogre::liberation

