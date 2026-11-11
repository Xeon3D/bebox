/*
 * BeBox emulator - the applications' command line.
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
 * empty).
 */
#ifndef BEBOX_FRONTEND_COMMAND_LINE_H
#define BEBOX_FRONTEND_COMMAND_LINE_H

#include "Settings.h"

#include <string>
#include <vector>

extern const char* const kCommandLineUsage;

struct CommandLine {
	bool		powerOn = false;
	// --log given: the front end shows the messages (a console)
	bool		logging = false;
};

// Applies the arguments (without the program's name) to the settings and
// saves them; false with 'error' for a bad one.
bool ApplyCommandLine(Settings& settings,
	const std::vector<std::string>& arguments, CommandLine& result,
	std::string& error);

#endif	// BEBOX_FRONTEND_COMMAND_LINE_H
