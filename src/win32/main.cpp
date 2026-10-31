/*
 * BeBox emulator for Windows - the application.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * BeBox [--profile NAME] [--rom-dir DIR] [--boot-rom IMAGE]
 *   [--model 66|133|hobbit] [--kernel FILE] [--ram MB] [--video s3|cirrus]
 *   [--scsiN-disk FILE] [--scsiN-cd FILE] [--scsiN-share FOLDER]
 *   [--ide0 FILE] [--floppy FILE] [--net] [--net-forward SPEC]
 *   [--power-on] [--log CATEGORY=LEVEL]
 *
 * sets up the machine as the Haiku application does (the settings are
 * kept; an empty FILE removes the device, or leaves the CD-ROM drive
 * empty). --log writes the emulator's messages to the console BeBox was
 * started from.
 */
#include "MainWindow.h"
#include "Settings.h"
#include "Util.h"

#include "core/Log.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using bebox::MachineConfig;

namespace {

// Applies the command line to the settings; false (with a message) for a
// bad one. 'powerOn' is set by --power-on.
bool
ApplyArguments(Settings& settings, const std::vector<std::string>& arguments,
	bool& powerOn)
{
	MachineConfig& machine = settings.machine;
	auto usage = [](const std::string& text) {
		ShowMessage(nullptr, Widen(text + "\n\nBeBox [--profile NAME] "
			"[--rom-dir DIR] [--boot-rom IMAGE] [--model 66|133|hobbit] "
			"[--kernel FILE] [--ram MB] [--video s3|cirrus] [--scsiN-disk FILE] "
			"[--scsiN-cd FILE] [--scsiN-share FOLDER] [--ide0 FILE] "
			"[--floppy FILE] [--net] [--net-forward SPEC] [--power-on] "
			"[--log CATEGORY=LEVEL]"), MB_ICONERROR);
		return false;
	};
	// --profile first: the other options change that profile
	for (size_t i = 0; i + 1 < arguments.size(); i++) {
		if (arguments[i] != "--profile")
			continue;
		const std::string& name = arguments[i + 1];
		if (Settings::ProfileExists(name))
			settings.SwitchProfile(name);
		else if (Settings::CheckProfileName(name).empty())
			settings.CopyProfile(name);
		else
			return usage("Bad profile name \"" + name + "\".");
	}
	for (size_t i = 0; i < arguments.size(); i++) {
		const std::string& option = arguments[i];
		bool hasValue = i + 1 < arguments.size();
		std::string value = hasValue ? arguments[i + 1] : "";
		if (option == "--power-on") {
			powerOn = true;
			continue;
		}
		if (option == "--net") {
			settings.ethernet = true;
			continue;
		}
		if (option.compare(0, 2, "--") != 0)
			return usage("Unknown argument \"" + option + "\".");
		if (!hasValue)
			return usage(option + " needs a value.");
		i++;
		if (option == "--profile")
			continue;
		if (option == "--rom-dir")
			settings.romDirectory = value;
		else if (option == "--boot-rom")
			settings.bootRomImage = value;
		else if (option == "--model") {
			if (value == "66")
				machine.model = MachineConfig::kBeBox66;
			else if (value == "133")
				machine.model = MachineConfig::kBeBox133;
			else if (value == "hobbit")
				machine.model = MachineConfig::kHobbit;
			else
				return usage("--model takes 66, 133 or hobbit.");
		} else if (option == "--kernel")
			machine.hobbitKernelPath = value;
		else if (option == "--ram") {
			int megabytes = atoi(value.c_str());
			if (megabytes < 4 || megabytes > 512)
				return usage("--ram takes 4 to 512 (MB).");
			machine.ramMB = megabytes;
		} else if (option == "--video") {
			if (value == "s3")
				machine.videoCard = MachineConfig::kVideoS3Vision864;
			else if (value == "cirrus")
				machine.videoCard = MachineConfig::kVideoCirrus5434;
			else if (value == "none")
				machine.videoCard = MachineConfig::kVideoNone;
			else
				return usage("--video takes s3, cirrus or none.");
		} else if (option == "--ide0" || option == "--ide1")
			machine.ideImage[option[5] - '0'] = value;
		else if (option == "--floppy")
			machine.floppyImage = value;
		else if (option == "--net-forward") {
			settings.ethernet = true;
			settings.portForwards = value;
		} else if (option == "--log") {
			// to the console BeBox was started from
			if (AttachConsole(ATTACH_PARENT_PROCESS))
				freopen("CONOUT$", "w", stderr);
			if (!bebox::Log::Configure(value.c_str()))
				return usage("Unknown log category in \"" + value + "\".");
		} else if (option.size() > 7 && option.compare(0, 6, "--scsi") == 0
			&& option[6] >= '0' && option[6] <= '6' && option[7] == '-') {
			MachineConfig::ScsiTarget& target = machine.scsi[option[6] - '0'];
			std::string kind = option.substr(8);
			if (kind == "disk")
				target.kind = MachineConfig::ScsiTarget::kDisk;
			else if (kind == "cd")
				target.kind = MachineConfig::ScsiTarget::kCdRom;
			else if (kind == "share")
				target.kind = MachineConfig::ScsiTarget::kSharedFolder;
			else
				return usage("Unknown option " + option + ".");
			target.image = value;
		} else
			return usage("Unknown option " + option + ".");
	}
	Settings::DropEmptyDevices(machine);
	settings.Save();
	return true;
}

}	// namespace


int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED
		| COINIT_DISABLE_OLE1DDE);
	INITCOMMONCONTROLSEX controls = {};
	controls.dwSize = sizeof(controls);
	controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES
		| ICC_TAB_CLASSES;
	InitCommonControlsEx(&controls);
	// the emulator's threads must keep pace: timers at 1 ms
	timeBeginPeriod(1);

	Settings settings;
	settings.Load();

	int count = 0;
	wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
	std::vector<std::string> arguments;
	for (int i = 1; i < count; i++)
		arguments.push_back(Narrow(wide[i]));
	LocalFree(wide);
	bool powerOn = false;
	if (!ApplyArguments(settings, arguments, powerOn))
		return 2;

	MainWindow::RegisterClass(instance);
	int result = 0;
	{
		MainWindow window(settings);
		if (!window.Create(showCommand))
			return 1;
		if (powerOn)
			window.PowerOn();
		MSG message;
		while (GetMessageW(&message, nullptr, 0, 0) > 0) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		result = (int)message.wParam;
	}
	timeEndPeriod(1);
	CoUninitialize();
	return result;
}
