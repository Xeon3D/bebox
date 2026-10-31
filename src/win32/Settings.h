/*
 * BeBox emulator for Windows - persistent settings and machine profiles.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * In %APPDATA%\BeBox: "settings.ini" holds what all profiles share
 * (keyboard and mouse, the LED look, the window) and the current
 * profile's name; each profile is a folder in Profiles\ with the
 * machine's own settings.ini, boot ROM flash (flash.bin), NVRAM
 * (nvram.bin) and shared folder volumes. The files are lines of
 * "key=value" in UTF-8.
 */
#ifndef WIN32_SETTINGS_H
#define WIN32_SETTINGS_H

#include "core/board/MachineConfig.h"

#include <windows.h>

#include <string>
#include <vector>

// Keys that release the mouse (and are not passed to the BeBox).
enum HostKey {
	kHostKeyRightControl = 0,
	kHostKeyRightAlt = 1,
	kHostKeyRightWindows = 2,
	kHostKeyMenu = 3
};

enum LedStyle {
	kLedStyleAuthentic = 0,		// lime and amber
	kLedStyleRedGreen = 1
};

class Settings {
public:
								Settings();

			// The shared settings and the current profile.
			bool				Load();
			bool				Save() const;

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
			// "host:COMn", "file:PATH".
			std::string			serial[4];
			// Host joysticks (winmm joystick numbers, "0"-"15") on the
			// BeBox's four game port sticks; "" = none.
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
			RECT				windowFrame;	// empty: not placed yet

private:
			void				_ResetProfile();
			bool				_LoadProfile();
			bool				_SaveProfile() const;
};

#endif	// WIN32_SETTINGS_H
