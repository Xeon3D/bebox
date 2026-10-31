/*
 * BeBox emulator for Windows - the preferences.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * A sidebar of sections (Machine, ROM & NVRAM, Display, Input devices,
 * Sound, Network, Ports, Storage), each with its icon and name above its
 * pages, on tabs where it has more than one. It edits a copy of the
 * settings; the caller takes the result and decides what it means for a
 * running machine.
 */
#ifndef WIN32_PREFS_DIALOG_H
#define WIN32_PREFS_DIALOG_H

#include "frontend/Settings.h"

#include <windows.h>

// Shows the preferences for 'settings' (changed in place on OK). 'running':
// the machine is on (hardware changes will restart it; the flash and NVRAM
// cannot be reset). Returns whether OK was chosen.
bool ShowPreferences(HWND owner, Settings& settings, bool running);

// Asks for a new disk image and creates it; its path, or "" if canceled.
std::string ShowNewDiskDialog(HWND owner, const std::string& directory);

// Asks for a profile name ('prompt' above the field); "" if canceled.
std::string AskProfileName(HWND owner, const wchar_t* title,
	const wchar_t* prompt, const std::string& initial);

#endif	// WIN32_PREFS_DIALOG_H
