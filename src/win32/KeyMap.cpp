/*
 * BeBox emulator for Windows - keyboard codes.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "KeyMap.h"

#include "core/host/KeyCodes.h"

#include <windows.h>

using bebox::u32;


u32
WindowsKeyToSet2(unsigned int virtualKey, long lParam)
{
	unsigned int scanCode = (lParam >> 16) & 0xff;
	bool extended = (lParam & (1 << 24)) != 0;
	// Windows gives Pause and Num Lock the same scan code (45, Num Lock
	// with the extended flag): the virtual key tells them apart.
	switch (virtualKey) {
		case VK_PAUSE:
			return 0xe11477;
		case VK_NUMLOCK:
			return 0x77;
		case VK_SNAPSHOT:
			return 0xe07c;
	}
	// Some keyboards and remote sessions report no scan code.
	if (scanCode == 0) {
		scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
		extended = (scanCode & 0xff00) == 0xe000;
		scanCode &= 0xff;
	}
	return bebox::Set1ToSet2(scanCode, extended);
}
