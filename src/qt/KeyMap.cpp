/*
 * BeBox emulator for Qt - keyboard codes.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "KeyMap.h"

#include "core/host/KeyCodes.h"

#include <QGuiApplication>
#include <QKeyEvent>

using bebox::u32;

namespace {

struct NativeKey {
	u32 native;
	u32 set2;
};

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS) && !defined(Q_OS_HAIKU)
// Linux input event codes past the ones equal to set 1 (1-83).
const NativeKey kEvdevKeys[] = {
	{ 86, 0x61 },		// the ISO key (\|)
	{ 87, 0x78 },		// F11
	{ 88, 0x07 },		// F12
	{ 89, 0x51 },		// Ro
	{ 92, 0x64 },		// Henkan
	{ 93, 0x13 },		// Katakana/Hiragana
	{ 94, 0x67 },		// Muhenkan
	{ 96, 0xe05a },		// keypad Enter
	{ 97, 0xe014 },		// right Control
	{ 98, 0xe04a },		// keypad /
	{ 99, 0xe07c },		// Print Screen
	{ 100, 0xe011 },	// right Alt
	{ 102, 0xe06c },	// Home
	{ 103, 0xe075 },	// Up
	{ 104, 0xe07d },	// Page Up
	{ 105, 0xe06b },	// Left
	{ 106, 0xe074 },	// Right
	{ 107, 0xe069 },	// End
	{ 108, 0xe072 },	// Down
	{ 109, 0xe07a },	// Page Down
	{ 110, 0xe070 },	// Insert
	{ 111, 0xe071 },	// Delete
	{ 119, 0xe11477 },	// Pause
	{ 124, 0x6a },		// Yen
	{ 125, 0xe01f },	// left Super (Windows)
	{ 126, 0xe027 },	// right Super
	{ 127, 0xe02f }		// Menu
};
#endif

#ifdef Q_OS_MACOS
// macOS virtual key codes (kVK_...): positions on the Mac keyboard. Option
// is where Alt is, Command where the Windows keys are.
const NativeKey kMacKeys[] = {
	{ 0x00, 0x1c }, { 0x01, 0x1b }, { 0x02, 0x23 }, { 0x03, 0x2b },	// A S D F
	{ 0x04, 0x33 }, { 0x05, 0x34 }, { 0x06, 0x1a }, { 0x07, 0x22 },	// H G Z X
	{ 0x08, 0x21 }, { 0x09, 0x2a }, { 0x0a, 0x61 }, { 0x0b, 0x32 },	// C V § B
	{ 0x0c, 0x15 }, { 0x0d, 0x1d }, { 0x0e, 0x24 }, { 0x0f, 0x2d },	// Q W E R
	{ 0x10, 0x35 }, { 0x11, 0x2c }, { 0x12, 0x16 }, { 0x13, 0x1e },	// Y T 1 2
	{ 0x14, 0x26 }, { 0x15, 0x25 }, { 0x16, 0x36 }, { 0x17, 0x2e },	// 3 4 6 5
	{ 0x18, 0x55 }, { 0x19, 0x46 }, { 0x1a, 0x3d }, { 0x1b, 0x4e },	// = 9 7 -
	{ 0x1c, 0x3e }, { 0x1d, 0x45 }, { 0x1e, 0x5b }, { 0x1f, 0x44 },	// 8 0 ] O
	{ 0x20, 0x3c }, { 0x21, 0x54 }, { 0x22, 0x43 }, { 0x23, 0x4d },	// U [ I P
	{ 0x24, 0x5a }, { 0x25, 0x4b }, { 0x26, 0x3b }, { 0x27, 0x52 },	// Return L J '
	{ 0x28, 0x42 }, { 0x29, 0x4c }, { 0x2a, 0x5d }, { 0x2b, 0x41 },	// K ; \ ,
	{ 0x2c, 0x4a }, { 0x2d, 0x31 }, { 0x2e, 0x3a }, { 0x2f, 0x49 },	// / N M .
	{ 0x30, 0x0d }, { 0x31, 0x29 }, { 0x32, 0x0e }, { 0x33, 0x66 },	// Tab Space ` Backspace
	{ 0x35, 0x76 },									// Escape
	{ 0x36, 0xe027 }, { 0x37, 0xe01f },				// right, left Command
	{ 0x38, 0x12 }, { 0x39, 0x58 },					// Shift, Caps Lock
	{ 0x3a, 0x11 }, { 0x3b, 0x14 },					// Option, Control
	{ 0x3c, 0x59 }, { 0x3d, 0xe011 }, { 0x3e, 0xe014 },	// right Shift, Option, Control
	{ 0x41, 0x71 }, { 0x43, 0x7c }, { 0x45, 0x79 },	// keypad . * +
	{ 0x47, 0x77 },									// Clear: Num Lock
	{ 0x4b, 0xe04a }, { 0x4c, 0xe05a }, { 0x4e, 0x7b },	// keypad / Enter -
	{ 0x52, 0x70 }, { 0x53, 0x69 }, { 0x54, 0x72 }, { 0x55, 0x7a },	// keypad 0-3
	{ 0x56, 0x6b }, { 0x57, 0x73 }, { 0x58, 0x74 }, { 0x59, 0x6c },	// keypad 4-7
	{ 0x5b, 0x75 }, { 0x5c, 0x7d },					// keypad 8, 9
	{ 0x5d, 0x6a }, { 0x5e, 0x51 },					// Yen, Ro
	{ 0x60, 0x03 }, { 0x61, 0x0b }, { 0x62, 0x83 }, { 0x63, 0x04 },	// F5 F6 F7 F3
	{ 0x64, 0x0a }, { 0x65, 0x01 }, { 0x67, 0x78 },	// F8 F9 F11
	{ 0x69, 0xe07c }, { 0x6b, 0x7e }, { 0x71, 0xe11477 },	// F13-F15: PrtSc ScrLk Pause
	{ 0x6d, 0x09 }, { 0x6f, 0x07 },					// F10 F12
	{ 0x72, 0xe070 }, { 0x73, 0xe06c }, { 0x74, 0xe07d },	// Help: Insert, Home, PgUp
	{ 0x75, 0xe071 }, { 0x76, 0x0c }, { 0x77, 0xe069 },	// Delete, F4, End
	{ 0x78, 0x06 }, { 0x79, 0xe07a }, { 0x7a, 0x05 },	// F2, PgDn, F1
	{ 0x7b, 0xe06b }, { 0x7c, 0xe074 }, { 0x7d, 0xe072 }, { 0x7e, 0xe075 }	// arrows
};
#endif


#if !defined(Q_OS_WIN) && !defined(Q_OS_HAIKU)
u32
Lookup(const NativeKey* keys, size_t count, u32 native)
{
	for (size_t i = 0; i < count; i++) {
		if (keys[i].native == native)
			return keys[i].set2;
	}
	return 0;
}
#endif


// Keys a system gave no usable native code for: by their meaning.
u32
FromQtKey(const QKeyEvent* event)
{
	struct QtKey {
		int key;
		const char* name;
	};
	static const QtKey kKeys[] = {
		{ Qt::Key_Escape, "Escape" }, { Qt::Key_Tab, "Tab" },
		{ Qt::Key_Backspace, "Backspace" }, { Qt::Key_Return, "Enter" },
		{ Qt::Key_Enter, "Enter" }, { Qt::Key_Insert, "Insert" },
		{ Qt::Key_Delete, "Delete" }, { Qt::Key_Home, "Home" },
		{ Qt::Key_End, "End" }, { Qt::Key_PageUp, "PageUp" },
		{ Qt::Key_PageDown, "PageDown" }, { Qt::Key_Left, "Left" },
		{ Qt::Key_Right, "Right" }, { Qt::Key_Up, "Up" },
		{ Qt::Key_Down, "Down" }, { Qt::Key_Shift, "LeftShift" },
		{ Qt::Key_Control, "LeftControl" }, { Qt::Key_Alt, "LeftAlt" },
		{ Qt::Key_Space, "Space" }, { Qt::Key_CapsLock, "CapsLock" },
		{ Qt::Key_F1, "F1" }, { Qt::Key_F2, "F2" }, { Qt::Key_F3, "F3" },
		{ Qt::Key_F4, "F4" }, { Qt::Key_F5, "F5" }, { Qt::Key_F6, "F6" },
		{ Qt::Key_F7, "F7" }, { Qt::Key_F8, "F8" }, { Qt::Key_F9, "F9" },
		{ Qt::Key_F10, "F10" }, { Qt::Key_F11, "F11" },
		{ Qt::Key_F12, "F12" }
	};
	for (const QtKey& key : kKeys) {
		if (key.key == event->key())
			return bebox::KeyNameToSet2(key.name);
	}
	QString text = event->text();
	if (text.size() == 1 && text[0].unicode() < 0x80) {
		bool shifted;
		return bebox::AsciiToSet2((char)text[0].toLower().unicode(),
			shifted);
	}
	return 0;
}

}	// namespace


u32
KeyEventToSet2(const QKeyEvent* event)
{
	u32 code = 0;
#if defined(Q_OS_WIN)
	// set 1; Qt adds the E0 prefix as 0x100. Pause and Num Lock have the
	// same scan code: the virtual key tells them apart.
	switch (event->nativeVirtualKey()) {
		case 0x13:		// VK_PAUSE
			return 0xe11477;
		case 0x90:		// VK_NUMLOCK
			return 0x77;
		case 0x2c:		// VK_SNAPSHOT
			return 0xe07c;
	}
	u32 scanCode = event->nativeScanCode();
	code = bebox::Set1ToSet2(scanCode & 0xff, (scanCode & 0x100) != 0);
#elif defined(Q_OS_MACOS)
	code = Lookup(kMacKeys, sizeof(kMacKeys) / sizeof(kMacKeys[0]),
		event->nativeVirtualKey());
#elif defined(Q_OS_HAIKU)
	code = bebox::HaikuKeyToSet2(event->nativeScanCode());
#else
	// X11 and Wayland: evdev codes + 8; the first 83 are set 1's
	QString platform = QGuiApplication::platformName();
	if (platform == "xcb" || platform.startsWith("wayland")) {
		u32 evdev = event->nativeScanCode() - 8;
		if (evdev >= 1 && evdev <= 83)
			code = bebox::Set1ToSet2(evdev, false);
		else {
			code = Lookup(kEvdevKeys, sizeof(kEvdevKeys)
				/ sizeof(kEvdevKeys[0]), evdev);
		}
	}
#endif
	if (code == 0)
		code = FromQtKey(event);
	return code;
}
