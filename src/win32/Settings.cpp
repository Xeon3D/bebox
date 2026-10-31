/*
 * BeBox emulator for Windows - persistent settings and machine profiles.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "Settings.h"

#include "Util.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>

using bebox::MachineConfig;

namespace {

const char* const kDefaultProfile = "Default";

typedef std::map<std::string, std::string> KeyValues;


bool
ReadKeyValues(const std::string& path, KeyValues& values)
{
	FILE* file = _wfopen(Widen(path).c_str(), L"rb");
	if (file == nullptr)
		return false;
	char line[4096];
	while (fgets(line, sizeof(line), file) != nullptr) {
		size_t length = strlen(line);
		while (length > 0 && (line[length - 1] == '\n'
				|| line[length - 1] == '\r'))
			line[--length] = '\0';
		char* equals = strchr(line, '=');
		if (line[0] == '#' || equals == nullptr)
			continue;
		*equals = '\0';
		values[line] = equals + 1;
	}
	fclose(file);
	return true;
}


bool
WriteKeyValues(const std::string& path, const KeyValues& values)
{
	std::string temporary = path + ".new";
	FILE* file = _wfopen(Widen(temporary).c_str(), L"wb");
	if (file == nullptr)
		return false;
	bool ok = true;
	for (const auto& entry : values) {
		ok &= fprintf(file, "%s=%s\n", entry.first.c_str(),
			entry.second.c_str()) > 0;
	}
	ok = fclose(file) == 0 && ok;
	return ok && MoveFileExW(Widen(temporary).c_str(), Widen(path).c_str(),
		MOVEFILE_REPLACE_EXISTING);
}


int
GetInt(const KeyValues& values, const char* key, int fallback)
{
	auto found = values.find(key);
	return found == values.end() || found->second.empty() ? fallback
		: atoi(found->second.c_str());
}


bool
GetBool(const KeyValues& values, const char* key, bool fallback)
{
	auto found = values.find(key);
	return found == values.end() || found->second.empty() ? fallback
		: found->second == "1" || found->second == "true";
}


std::string
GetString(const KeyValues& values, const char* key,
	const std::string& fallback = "")
{
	auto found = values.find(key);
	return found == values.end() ? fallback : found->second;
}


bool
CopyFileIfExists(const std::string& from, const std::string& to)
{
	if (!FileExists(from))
		return true;
	return CopyFileW(Widen(from).c_str(), Widen(to).c_str(), FALSE) != 0;
}


bool
RemoveTree(const std::string& path)
{
	std::wstring wide = Widen(path);
	wide.push_back(L'\0');		// double terminated list
	SHFILEOPSTRUCTW operation = {};
	operation.wFunc = FO_DELETE;
	operation.pFrom = wide.c_str();
	operation.fFlags = FOF_NO_UI;
	return SHFileOperationW(&operation) == 0;
}

}	// namespace


Settings::Settings()
	:
	profile(kDefaultProfile),
	scale(1),
	fullScreenScale(0),
	showLeds(true),
	ledStyle(kLedStyleAuthentic),
	hostKey(kHostKeyRightControl),
	mouseSpeed(100),
	swapMouseButtons(false),
	captureOnClick(true),
	windowFrame{ 0, 0, 0, 0 }
{
	_ResetProfile();
}


std::string
Settings::Directory()
{
	return AppDataDirectory();
}


std::string
Settings::ProfilesDirectory()
{
	std::string directory = Directory() + "\\Profiles";
	CreateDirectories(directory);
	return directory;
}


std::string
Settings::ProfileDirectory() const
{
	return ProfilesDirectory() + "\\" + profile;
}


std::string
Settings::FlashPath() const
{
	return ProfileDirectory() + "\\flash.bin";
}


std::string
Settings::NvramPath() const
{
	return ProfileDirectory() + "\\nvram.bin";
}


void
Settings::_ResetProfile()
{
	machine = MachineConfig();
	machine.threadPerCpu = true;
	romDirectory = Directory() + "\\ROMs";
	bootRomImage.clear();
	speedLimit = 100;
	soundEnabled = true;
	soundVolume = 100;
	for (int i = 0; i < 4; i++) {
		serial[i].clear();
		joystick[i].clear();
	}
	serial[3] = "console";		// the kernel's debug output
	ethernet = false;
	ethernetPort = 0x300;
	ethernetIrq = 5;
	// a locally made up address after the NE2000 clone prefix
	std::random_device random;
	char mac[32];
	snprintf(mac, sizeof(mac), "00:00:e8:%02x:%02x:%02x", random() & 0xff,
		random() & 0xff, random() & 0xff);
	macAddress = mac;
	portForwards.clear();
}


bool
Settings::Load()
{
	KeyValues values;
	ReadKeyValues(Directory() + "\\settings.ini", values);
	profile = GetString(values, "profile", kDefaultProfile);
	if (!CheckProfileName(profile).empty() && !ProfileExists(profile))
		profile = kDefaultProfile;
	scale = GetInt(values, "scale", 1);
	if (scale < 0 || scale > 2)
		scale = 1;
	fullScreenScale = GetInt(values, "full_screen_scale", 0);
	showLeds = GetBool(values, "show_leds", true);
	ledStyle = GetInt(values, "led_style", kLedStyleAuthentic);
	hostKey = GetInt(values, "host_key", kHostKeyRightControl);
	mouseSpeed = GetInt(values, "mouse_speed", 100);
	swapMouseButtons = GetBool(values, "swap_mouse_buttons", false);
	captureOnClick = GetBool(values, "capture_on_click", true);
	windowFrame.left = GetInt(values, "window_left", 0);
	windowFrame.top = GetInt(values, "window_top", 0);
	windowFrame.right = GetInt(values, "window_right", 0);
	windowFrame.bottom = GetInt(values, "window_bottom", 0);
	return _LoadProfile();
}


bool
Settings::Save() const
{
	KeyValues values;
	values["profile"] = profile;
	values["scale"] = std::to_string(scale);
	values["full_screen_scale"] = std::to_string(fullScreenScale);
	values["show_leds"] = showLeds ? "1" : "0";
	values["led_style"] = std::to_string(ledStyle);
	values["host_key"] = std::to_string(hostKey);
	values["mouse_speed"] = std::to_string(mouseSpeed);
	values["swap_mouse_buttons"] = swapMouseButtons ? "1" : "0";
	values["capture_on_click"] = captureOnClick ? "1" : "0";
	values["window_left"] = std::to_string(windowFrame.left);
	values["window_top"] = std::to_string(windowFrame.top);
	values["window_right"] = std::to_string(windowFrame.right);
	values["window_bottom"] = std::to_string(windowFrame.bottom);
	bool ok = WriteKeyValues(Directory() + "\\settings.ini", values);
	return _SaveProfile() && ok;
}


bool
Settings::_LoadProfile()
{
	_ResetProfile();
	KeyValues values;
	if (!ReadKeyValues(ProfileDirectory() + "\\settings.ini", values))
		return false;
	int model = GetInt(values, "model", 66);
	machine.model = model == 133 ? MachineConfig::kBeBox133
		: model == 25 ? MachineConfig::kHobbit : MachineConfig::kBeBox66;
	machine.ramMB = GetInt(values, "ram", machine.ramMB);
	machine.cpuCount = GetInt(values, "cpus", machine.cpuCount);
	machine.boardRevision = GetInt(values, "board_revision",
		machine.boardRevision);
	machine.ioCardRevision = GetInt(values, "io_card_revision",
		machine.ioCardRevision);
	machine.videoCard = (MachineConfig::VideoCard)GetInt(values, "video",
		machine.videoCard);
	machine.vramMB = GetInt(values, "vram", machine.vramMB);
	machine.quantumUs = GetInt(values, "quantum", machine.quantumUs);
	machine.cpiPercent = GetInt(values, "cpi_percent", machine.cpiPercent);
	machine.threadPerCpu = GetBool(values, "thread_per_cpu",
		machine.threadPerCpu);
	machine.hobbitKernelPath = GetString(values, "hobbit_kernel");
	for (int i = 0; i < 2; i++) {
		std::string key = "ide" + std::to_string(i);
		machine.ideImage[i] = GetString(values, key.c_str());
		machine.ideReadOnly[i] = GetBool(values, (key + "_read_only").c_str(),
			false);
	}
	for (int id = 0; id < 7; id++) {
		std::string key = "scsi" + std::to_string(id);
		MachineConfig::ScsiTarget& target = machine.scsi[id];
		target.kind = (MachineConfig::ScsiTarget::Kind)GetInt(values,
			(key + "_kind").c_str(), MachineConfig::ScsiTarget::kNone);
		target.image = GetString(values, (key + "_image").c_str());
		target.readOnly = GetBool(values, (key + "_read_only").c_str(),
			false);
	}
	machine.floppyImage = GetString(values, "floppy");
	machine.floppyWriteProtected = GetBool(values, "floppy_write_protected",
		false);
	DropEmptyDevices(machine);
	romDirectory = GetString(values, "rom_directory", romDirectory);
	bootRomImage = GetString(values, "boot_rom_image");
	speedLimit = GetInt(values, "speed_limit", speedLimit);
	soundEnabled = GetBool(values, "sound", soundEnabled);
	soundVolume = GetInt(values, "sound_volume", soundVolume);
	for (int i = 0; i < 4; i++) {
		std::string key = "serial" + std::to_string(i + 1);
		serial[i] = GetString(values, key.c_str(), serial[i]);
		key = "joystick" + std::to_string(i + 1);
		joystick[i] = GetString(values, key.c_str());
	}
	ethernet = GetBool(values, "ethernet", ethernet);
	ethernetPort = GetInt(values, "ethernet_port", ethernetPort);
	ethernetIrq = GetInt(values, "ethernet_irq", ethernetIrq);
	macAddress = GetString(values, "mac_address", macAddress);
	portForwards = GetString(values, "port_forwards");
	return true;
}


bool
Settings::_SaveProfile() const
{
	std::string directory = ProfileDirectory();
	CreateDirectories(directory);
	KeyValues values;
	values["model"] = machine.model == MachineConfig::kBeBox133 ? "133"
		: machine.model == MachineConfig::kHobbit ? "25" : "66";
	values["ram"] = std::to_string(machine.ramMB);
	values["cpus"] = std::to_string(machine.cpuCount);
	values["board_revision"] = std::to_string(machine.boardRevision);
	values["io_card_revision"] = std::to_string(machine.ioCardRevision);
	values["video"] = std::to_string(machine.videoCard);
	values["vram"] = std::to_string(machine.vramMB);
	values["quantum"] = std::to_string(machine.quantumUs);
	values["cpi_percent"] = std::to_string(machine.cpiPercent);
	values["thread_per_cpu"] = machine.threadPerCpu ? "1" : "0";
	values["hobbit_kernel"] = machine.hobbitKernelPath;
	for (int i = 0; i < 2; i++) {
		std::string key = "ide" + std::to_string(i);
		values[key] = machine.ideImage[i];
		values[key + "_read_only"] = machine.ideReadOnly[i] ? "1" : "0";
	}
	for (int id = 0; id < 7; id++) {
		std::string key = "scsi" + std::to_string(id);
		const MachineConfig::ScsiTarget& target = machine.scsi[id];
		values[key + "_kind"] = std::to_string(target.kind);
		values[key + "_image"] = target.image;
		values[key + "_read_only"] = target.readOnly ? "1" : "0";
	}
	values["floppy"] = machine.floppyImage;
	values["floppy_write_protected"] = machine.floppyWriteProtected
		? "1" : "0";
	values["rom_directory"] = romDirectory;
	values["boot_rom_image"] = bootRomImage;
	values["speed_limit"] = std::to_string(speedLimit);
	values["sound"] = soundEnabled ? "1" : "0";
	values["sound_volume"] = std::to_string(soundVolume);
	for (int i = 0; i < 4; i++) {
		values["serial" + std::to_string(i + 1)] = serial[i];
		values["joystick" + std::to_string(i + 1)] = joystick[i];
	}
	values["ethernet"] = ethernet ? "1" : "0";
	values["ethernet_port"] = std::to_string(ethernetPort);
	values["ethernet_irq"] = std::to_string(ethernetIrq);
	values["mac_address"] = macAddress;
	values["port_forwards"] = portForwards;
	return WriteKeyValues(directory + "\\settings.ini", values);
}


MachineConfig
Settings::MachineConfigForRun() const
{
	MachineConfig config = machine;
	DropEmptyDevices(config);
	config.nubRomPath = romDirectory + "\\bootnub.rom";
	config.mainRomPath = bootRomImage.empty()
		? romDirectory + "\\bootmain.rom" : bootRomImage;
	config.flashPath = FlashPath();
	config.nvramPath = NvramPath();
	config.ethernet = ethernet;
	config.ethernetPort = (bebox::u32)ethernetPort;
	config.ethernetIrq = ethernetIrq;
	unsigned int mac[6];
	if (sscanf(macAddress.c_str(), "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1],
			&mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
		for (int i = 0; i < 6; i++)
			config.ethernetMac[i] = (bebox::u8)mac[i];
	}
	return config;
}


static std::string
Trimmed(const std::string& text)
{
	size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	size_t last = text.find_last_not_of(" \t");
	return text.substr(first, last - first + 1);
}


void
Settings::DropEmptyDevices(MachineConfig& machine)
{
	for (int id = 0; id < 7; id++) {
		MachineConfig::ScsiTarget& target = machine.scsi[id];
		if (Trimmed(target.image).empty()) {
			// a CD-ROM drive may be empty
			if (target.kind != MachineConfig::ScsiTarget::kCdRom)
				target.kind = MachineConfig::ScsiTarget::kNone;
			target.image.clear();
		}
	}
	for (int i = 0; i < 2; i++) {
		if (Trimmed(machine.ideImage[i]).empty())
			machine.ideImage[i].clear();
	}
	if (Trimmed(machine.floppyImage).empty())
		machine.floppyImage.clear();
}


std::vector<std::string>
Settings::GetProfiles()
{
	std::vector<std::string> names;
	WIN32_FIND_DATAW entry;
	HANDLE find = FindFirstFileW(Widen(ProfilesDirectory() + "\\*").c_str(),
		&entry);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
				&& wcscmp(entry.cFileName, L".") != 0
				&& wcscmp(entry.cFileName, L"..") != 0)
				names.push_back(Narrow(entry.cFileName));
		} while (FindNextFileW(find, &entry));
		FindClose(find);
	}
	if (names.empty())
		names.push_back(kDefaultProfile);
	std::sort(names.begin(), names.end(),
		[](const std::string& a, const std::string& b) {
			return _stricmp(a.c_str(), b.c_str()) < 0;
		});
	return names;
}


bool
Settings::ProfileExists(const std::string& name)
{
	return DirectoryExists(ProfilesDirectory() + "\\" + name);
}


std::string
Settings::CheckProfileName(const std::string& name)
{
	std::string trimmed = Trimmed(name);
	if (trimmed.empty())
		return "The name is empty.";
	if (trimmed != name || name.back() == '.')
		return "The name cannot start or end with a space or end with a "
			"period.";
	if (name.find_first_of("\\/:*?\"<>|") != std::string::npos)
		return "The name cannot contain \\ / : * ? \" < > or |.";
	if (ProfileExists(name))
		return "There is a profile of that name already.";
	return "";
}


bool
Settings::SwitchProfile(const std::string& name)
{
	_SaveProfile();
	profile = name;
	_LoadProfile();
	return Save();
}


bool
Settings::CopyProfile(const std::string& name)
{
	std::string from = ProfileDirectory();
	std::string to = ProfilesDirectory() + "\\" + name;
	if (!CreateDirectories(to))
		return false;
	bool ok = CopyFileIfExists(from + "\\flash.bin", to + "\\flash.bin")
		&& CopyFileIfExists(from + "\\nvram.bin", to + "\\nvram.bin");
	profile = name;
	return Save() && ok;
}


bool
Settings::RenameProfile(const std::string& name)
{
	std::string from = ProfileDirectory();
	std::string to = ProfilesDirectory() + "\\" + name;
	if (!MoveFileW(Widen(from).c_str(), Widen(to).c_str()))
		return false;
	profile = name;
	return Save();
}


bool
Settings::DeleteProfile(const std::string& name)
{
	return RemoveTree(ProfilesDirectory() + "\\" + name);
}
