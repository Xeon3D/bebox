/*
 * BeBox emulator for Qt - keyboard codes.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The key's position from its native code, which differs on each system:
 * scan code set 1 on Windows, evdev codes (+8) with X11 and Wayland, the
 * virtual key codes (kVK_...) on macOS, Haiku's key codes on Haiku. Keys
 * without one fall back to their Qt key. The result is the core's set 2
 * code (KeyCodes.h): extended keys have 0xe000 set, Pause is 0xe11477.
 */
#ifndef QT_KEY_MAP_H
#define QT_KEY_MAP_H

#include "core/Types.h"

class QKeyEvent;

// 0 for keys without an AT equivalent.
bebox::u32 KeyEventToSet2(const QKeyEvent* event);

#endif	// QT_KEY_MAP_H
