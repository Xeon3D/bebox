/*
 * BeBox emulator for Windows - keyboard codes.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * Windows reports key positions as scan code set 1 (with the E0 prefix as
 * the "extended" flag); the BeBox's AT keyboard sends set 2. The codes
 * returned are the core's (KeyCodes.h): extended keys have 0xe000 set,
 * Pause is 0xe11477.
 */
#ifndef WIN32_KEY_MAP_H
#define WIN32_KEY_MAP_H

#include "core/Types.h"

// The set 2 code of a key from its WM_KEYDOWN/WM_KEYUP message (virtual
// key and lParam); 0 for keys without an AT equivalent.
bebox::u32 WindowsKeyToSet2(unsigned int virtualKey, long lParam);

// The set 2 code of a set 1 scan code (extended: E0 prefix); 0 if none.
bebox::u32 Set1ToSet2(unsigned int scanCode, bool extended);

#endif	// WIN32_KEY_MAP_H
