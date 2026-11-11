/*
 * BeBox emulator - persistent settings and machine profiles, for the
 * Windows and Qt front ends.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * In the settings folder (SetDirectory(): %APPDATA%\BeBox on Windows, the
 * config folder elsewhere): "settings.ini" holds what all profiles share
 * (keyboard and mouse, the LED look, the window) and the current
 * profile's name; each profile is a folder in Profiles/ with the
 * machine's own settings.ini, boot ROM flash (flash.bin), NVRAM
 * (nvram.bin) and shared folder volumes. The files are lines of
 * "key=value" in UTF-8; paths are UTF-8 too.
 */
#ifndef BEBOX_FRONTEND_SETTINGS_H
#define BEBOX_FRONTEND_SETTINGS_H

#include "core/board/MachineConfig.h"

#include <string>
#include <vector>

// Keys that release the mouse (and are not passed to the BeBox).
enum HostKey {
	kHostKeyRightControl = 0,
	kHostKeyRightAlt = 1,		// Right Option on a Mac
	kHostKeyRightWindows = 2,	// Right Command on a Mac
	kHostKeyMenu = 3
};

enum LedStyle {
	kLedStyleAuthentic = 0,		// lime and amber
	kLedStyleRedGreen = 1
};

struct WindowFrame {
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;

	bool IsValid() const { return right > left && bottom > top; }
};

class Settings {
public:
								Settings();

			// The shared settings and the current profile.
			bool				Load();
			bool				Save() const;

			// Where the settings live (made if missing); set before Load().
	static	void				SetDirectory(const std::string& directory);
	static	std::string			Directory();
	static	std::string			ProfilesDirectory();
			std::string			ProfileDirectory() const;
			// The machine's settings with the paths filled in.
			bebox::MachineConfig MachineConfigForRun() const;
			std::string			FlashPath() const;
			std::string			NvramPath() const;

	// Profiles
	static	std::vector<std::string> GetProfiles();
	static	bool				ProfileExists(const std::string& name);
			// Why a name cannot be used for a new profile ("" if it can).
	static	std::string			CheckProfileName(const std::string& name);
			// Saves the current profile and loads 'name'.
			bool				SwitchProfile(const std::string& name);
			// A copy of the current profile (settings, flash, NVRAM) named
			// 'name', which becomes the current one.
			bool				CopyProfile(const std::string& name);
			bool				RenameProfile(const std::string& name);
			// Deletes another profile (its folder).
	static	bool				DeleteProfile(const std::string& name);

			// A device kind set without an image would stop the machine
			// from starting: such devices become "none" (a CD-ROM drive
			// stays, empty).
	static	void				DropEmptyDevices(bebox::MachineConfig& machine);

			std::string			profile;

			// The profile's settings
			bebox::MachineConfig machine;
			std::string			romDirectory;
			// Main boot ROM: a ROM update image (bebox_bootmain.image of a
			// BeOS CD); empty = bootmain.rom of the ROM directory.
			std::string			bootRomImage;
			int					speedLimit;		// percent, 0 = unlimited
			bool				soundEnabled;
			int					soundVolume;	// percent
			// Serial destinations: "" (not connected), "console",
			// "tcp-listen:[ADDR:]PORT", "tcp-connect:HOST:PORT",
			// "host:DEVICE", "file:PATH".
			std::string			serial[4];
			// Host joysticks on the BeBox's four game port sticks (the
			// front end's device numbers); "" = none.
			std::string			joystick[4];
			bool				ethernet;
			int					ethernetPort;
			int					ethernetIrq;
			std::string			macAddress;
			std::string			portForwards;

			// Shared by all profiles
			int					scale;			// 1, 2, 0 = fit window
			int					fullScreenScale;	// 0 fill, 1 whole, 2 1:1
			bool				showLeds;
			int					ledStyle;		// LedStyle
			int					hostKey;		// HostKey
			int					mouseSpeed;		// percent
			bool				swapMouseButtons;
			bool				captureOnClick;
			WindowFrame			windowFrame;	// invalid: not placed yet

private:
			void				_ResetProfile();
			bool				_LoadProfile();
			bool				_SaveProfile() const;
};


// Whether going from 'old' to 'now' needs the machine to start again: all
// but the floppy, the discs in the CD-ROM drives and what only the window
// uses (sound volume, speed limit, joysticks, mouse, LEDs).
bool NeedsRestart(const Settings& old, const Settings& now);

// A file chosen as the boot ROM image: a ROM update image is used as it is;
// from a BeOS CD (or disk) image, /beos/etc/bebox_bootmain.image is copied
// into the settings folder's BootROMs. The path to use, or "" with 'error'.
std::string BootRomFromFile(const std::string& path, std::string& error);

#endif	// BEBOX_FRONTEND_SETTINGS_H
