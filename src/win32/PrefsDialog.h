/*
 * BeBox emulator for Windows - the preferences.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * A property sheet with the Haiku application's sections: Machine, ROM &
 * NVRAM, Display, Storage, Sound, Keyboard & mouse, Game ports, Network,
 * Serial ports and Advanced. It edits a copy of the settings; the caller
 * takes the result and decides what it means for a running machine.
 */
#ifndef WIN32_PREFS_DIALOG_H
#define WIN32_PREFS_DIALOG_H

#include "Settings.h"

#include <windows.h>

// Shows the preferences for 'settings' (changed in place on OK). 'running':
// the machine is on (hardware changes will restart it; the flash and NVRAM
// cannot be reset). Returns whether OK was chosen.
bool ShowPreferences(HWND owner, Settings& settings, bool running);

// Whether going from 'old' to 'now' needs the machine to start again: all
// but the floppy, the discs in the CD-ROM drives and what only the window
// uses (sound volume, speed limit, joysticks, mouse, LEDs).
bool NeedsRestart(const Settings& old, const Settings& now);

// Asks for a new disk image and creates it; its path, or "" if canceled.
std::string ShowNewDiskDialog(HWND owner, const std::string& directory);

// Asks for a profile name ('prompt' above the field); "" if canceled.
std::string AskProfileName(HWND owner, const wchar_t* title,
	const wchar_t* prompt, const std::string& initial);

#endif	// WIN32_PREFS_DIALOG_H
