/*
 * BeBox emulator - the applications' command line.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "CommandLine.h"

#include "core/Log.h"

#include <cstdlib>

using bebox::MachineConfig;

const char* const kCommandLineUsage = "BeBox [--profile NAME] "
	"[--rom-dir DIR] [--boot-rom IMAGE] [--model 66|133|hobbit] "
	"[--kernel FILE] [--ram MB] [--video s3|cirrus] [--scsiN-disk FILE] "
	"[--scsiN-cd FILE] [--scsiN-share FOLDER] [--ide0 FILE] [--floppy FILE] "
	"[--net] [--net-forward SPEC] [--power-on] [--log CATEGORY=LEVEL]";


bool
ApplyCommandLine(Settings& settings, const std::vector<std::string>& arguments,
	CommandLine& result, std::string& error)
{
	MachineConfig& machine = settings.machine;
	// --profile first: the other options change that profile
	for (size_t i = 0; i + 1 < arguments.size(); i++) {
		if (arguments[i] != "--profile")
			continue;
		const std::string& name = arguments[i + 1];
		if (Settings::ProfileExists(name))
			settings.SwitchProfile(name);
		else if (Settings::CheckProfileName(name).empty())
			settings.CopyProfile(name);
		else {
			error = "Bad profile name \"" + name + "\".";
			return false;
		}
	}
	for (size_t i = 0; i < arguments.size(); i++) {
		const std::string& option = arguments[i];
		if (option == "--power-on") {
			result.powerOn = true;
			continue;
		}
		if (option == "--net") {
			settings.ethernet = true;
			continue;
		}
		if (option.compare(0, 2, "--") != 0) {
			error = "Unknown argument \"" + option + "\".";
			return false;
		}
		if (i + 1 >= arguments.size()) {
			error = option + " needs a value.";
			return false;
		}
		const std::string& value = arguments[++i];
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
			else {
				error = "--model takes 66, 133 or hobbit.";
				return false;
			}
		} else if (option == "--kernel")
			machine.hobbitKernelPath = value;
		else if (option == "--ram") {
			int megabytes = atoi(value.c_str());
			if (megabytes < 4 || megabytes > 512) {
				error = "--ram takes 4 to 512 (MB).";
				return false;
			}
			machine.ramMB = megabytes;
		} else if (option == "--video") {
			if (value == "s3")
				machine.videoCard = MachineConfig::kVideoS3Vision864;
			else if (value == "cirrus")
				machine.videoCard = MachineConfig::kVideoCirrus5434;
			else if (value == "none")
				machine.videoCard = MachineConfig::kVideoNone;
			else {
				error = "--video takes s3, cirrus or none.";
				return false;
			}
		} else if (option == "--ide0" || option == "--ide1")
			machine.ideImage[option[5] - '0'] = value;
		else if (option == "--floppy")
			machine.floppyImage = value;
		else if (option == "--net-forward") {
			settings.ethernet = true;
			settings.portForwards = value;
		} else if (option == "--log") {
			result.logging = true;
			if (!bebox::Log::Configure(value.c_str())) {
				error = "Unknown log category in \"" + value + "\".";
				return false;
			}
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
			else {
				error = "Unknown option " + option + ".";
				return false;
			}
			target.image = value;
		} else {
			error = "Unknown option " + option + ".";
			return false;
		}
	}
	Settings::DropEmptyDevices(machine);
	settings.Save();
	return true;
}
