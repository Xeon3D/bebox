/*
 * BeBox emulator - keyboard codes.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * Maps Haiku/BeOS key codes (layout independent key positions) to AT scan
 * code set 2 make codes as sent by the BeBox's AT keyboard. Extended keys
 * have 0xe000 set (E0 prefix); Pause is 0xe11477.
 */
#ifndef BEBOX_KEY_CODES_H
#define BEBOX_KEY_CODES_H

#include "../Types.h"

namespace bebox {

// Returns 0 for keys without an AT equivalent.
u32 HaikuKeyToSet2(u32 haikuKey);

// AT scan code set 1 (what Windows reports, and Linux's evdev codes for
// most keys) to set 2; 'extended': the E0 prefix. 0 for keys without one.
u32 Set1ToSet2(u32 scanCode, bool extended);

// Key names for scripts ("Enter", "A", "F1", "Up", ...); 0 if unknown.
u32 KeyNameToSet2(const char* name);

// The key typing an ASCII character on a US keyboard ('\n' is Enter, '\t'
// Tab); 'shifted' tells whether Shift is needed. 0 if there is none.
u32 AsciiToSet2(char c, bool& shifted);

}	// namespace bebox

#endif	// BEBOX_KEY_CODES_H
