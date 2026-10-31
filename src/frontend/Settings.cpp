/*
 * BeBox emulator - persistent settings and machine profiles, for the
 * Windows front end.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "Settings.h"

#include "core/storage/BfsReader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

using bebox::MachineConfig;

namespace fs = std::filesystem;

namespace {

const char* const kDefaultProfile = "Default";
const char* const kBootRomPath = "/beos/etc/bebox_bootmain.image";

std::string sDirectory;

typedef std::map<std::string, std::string> KeyValues;


fs::path
Path(const std::string& utf8)
{
	return fs::u8path(utf8);
}


std::string
Utf8(const fs::path& path)
{
	return path.u8string();
}


bool
ReadKeyValues(const std::string& path, KeyValues& values)
{
	std::ifstream file(Path(path), std::ios::binary);
	if (!file)
		return false;
	std::string line;
	while (std::getline(file, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
			line.pop_back();
		size_t equals = line.find('=');
		if (line.empty() || line[0] == '#' || equals == std::string::npos)
			continue;
		values[line.substr(0, equals)] = line.substr(equals + 1);
	}
	return true;
}


// Written to a new file that then replaces the old one.
bool
WriteKeyValues(const std::string& path, const KeyValues& values)
{
	std::string temporary = path + ".new";
	{
		std::ofstream file(Path(temporary), std::ios::binary
			| std::ios::trunc);
		if (!file)
			return false;
		for (const auto& entry : values)
			file << entry.first << '=' << entry.second << '\n';
		file.flush();
		if (!file)
			return false;
	}
	std::error_code error;
	fs::rename(Path(temporary), Path(path), error);
	return !error;
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
	std::error_code error;
	if (!fs::exists(Path(from), error))
		return true;
	return fs::copy_file(Path(from), Path(to),
		fs::copy_options::overwrite_existing, error);
}


std::string
Trimmed(const std::string& text)
{
	size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	size_t last = text.find_last_not_of(" \t");
	return text.substr(first, last - first + 1);
}


std::string
Lowered(std::string text)
{
	for (char& c : text)
		c = (char)tolower((unsigned char)c);
	return text;
}


std::string
DefaultDirectory()
{
#ifdef _WIN32
	const char* base = getenv("APPDATA");
	return std::string(base != nullptr ? base : ".") + "\\BeBox";
#else
	const char* config = getenv("XDG_CONFIG_HOME");
	if (config != nullptr && config[0] != '\0')
		return std::string(config) + "/BeBox";
	const char* home = getenv("HOME");
	return std::string(home != nullptr ? home : ".") + "/.config/BeBox";
#endif
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
	captureOnClick(true)
{
	_ResetProfile();
}


void
Settings::SetDirectory(const std::string& directory)
{
	sDirectory = directory;
}


std::string
Settings::Directory()
{
	if (sDirectory.empty())
		sDirectory = DefaultDirectory();
	std::error_code error;
	fs::create_directories(Path(sDirectory), error);
	return sDirectory;
}


std::string
Settings::ProfilesDirectory()
{
	std::string directory = Utf8(Path(Directory()) / "Profiles");
	std::error_code error;
	fs::create_directories(Path(directory), error);
	return directory;
}


std::string
Settings::ProfileDirectory() const
{
	return Utf8(Path(ProfilesDirectory()) / Path(profile));
}


std::string
Settings::FlashPath() const
{
	return Utf8(Path(ProfileDirectory()) / "flash.bin");
}


std::string
Settings::NvramPath() const
{
	return Utf8(Path(ProfileDirectory()) / "nvram.bin");
}


void
Settings::_ResetProfile()
{
	machine = MachineConfig();
	machine.threadPerCpu = true;
	romDirectory = Utf8(Path(Directory()) / "ROMs");
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
	ReadKeyValues(Utf8(Path(Directory()) / "settings.ini"), values);
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
	bool ok = WriteKeyValues(Utf8(Path(Directory()) / "settings.ini"),
		values);
	return _SaveProfile() && ok;
}


bool
Settings::_LoadProfile()
{
	_ResetProfile();
	KeyValues values;
	if (!ReadKeyValues(Utf8(Path(ProfileDirectory()) / "settings.ini"),
			values))
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
	std::error_code error;
	fs::create_directories(Path(directory), error);
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
	return WriteKeyValues(Utf8(Path(directory) / "settings.ini"), values);
}


MachineConfig
Settings::MachineConfigForRun() const
{
	MachineConfig config = machine;
	DropEmptyDevices(config);
	config.nubRomPath = Utf8(Path(romDirectory) / "bootnub.rom");
	config.mainRomPath = bootRomImage.empty()
		? Utf8(Path(romDirectory) / "bootmain.rom") : bootRomImage;
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
	std::error_code error;
	for (const fs::directory_entry& entry
			: fs::directory_iterator(Path(ProfilesDirectory()), error)) {
		if (entry.is_directory(error))
			names.push_back(Utf8(entry.path().filename()));
	}
	if (names.empty())
		names.push_back(kDefaultProfile);
	std::sort(names.begin(), names.end(),
		[](const std::string& a, const std::string& b) {
			return Lowered(a) < Lowered(b);
		});
	return names;
}


bool
Settings::ProfileExists(const std::string& name)
{
	std::error_code error;
	return fs::is_directory(Path(ProfilesDirectory()) / Path(name), error);
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
	// what one of the systems does not take in a folder name
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
	fs::path to = Path(ProfilesDirectory()) / Path(name);
	std::error_code error;
	fs::create_directories(to, error);
	if (error)
		return false;
	bool ok = CopyFileIfExists(Utf8(Path(from) / "flash.bin"),
			Utf8(to / "flash.bin"))
		&& CopyFileIfExists(Utf8(Path(from) / "nvram.bin"),
			Utf8(to / "nvram.bin"));
	profile = name;
	return Save() && ok;
}


bool
Settings::RenameProfile(const std::string& name)
{
	std::error_code error;
	fs::rename(Path(ProfileDirectory()),
		Path(ProfilesDirectory()) / Path(name), error);
	if (error)
		return false;
	profile = name;
	return Save();
}


bool
Settings::DeleteProfile(const std::string& name)
{
	fs::path folder = Path(ProfilesDirectory()) / Path(name);
	std::error_code error;
	// shared folder volumes may have been left write protected
	for (const fs::directory_entry& entry
			: fs::recursive_directory_iterator(folder, error)) {
		fs::permissions(entry.path(), fs::perms::owner_write,
			fs::perm_options::add, error);
	}
	fs::remove_all(folder, error);
	return !error;
}


// #pragma mark -


// The parts of the machine a running one cannot change.
static bool
SameHardware(const MachineConfig& a, const MachineConfig& b)
{
	if (a.model != b.model || a.ramMB != b.ramMB || a.cpuCount != b.cpuCount
		|| a.boardRevision != b.boardRevision
		|| a.ioCardRevision != b.ioCardRevision
		|| a.videoCard != b.videoCard || a.vramMB != b.vramMB
		|| a.quantumUs != b.quantumUs || a.cpiPercent != b.cpiPercent
		|| a.threadPerCpu != b.threadPerCpu
		|| a.hobbitKernelPath != b.hobbitKernelPath
		|| a.nubRomPath != b.nubRomPath || a.mainRomPath != b.mainRomPath
		|| a.ethernet != b.ethernet || a.ethernetPort != b.ethernetPort
		|| a.ethernetIrq != b.ethernetIrq
		|| memcmp(a.ethernetMac, b.ethernetMac, 6) != 0)
		return false;
	for (int i = 0; i < 2; i++) {
		if (a.ideImage[i] != b.ideImage[i]
			|| a.ideReadOnly[i] != b.ideReadOnly[i])
			return false;
	}
	for (int id = 0; id < 7; id++) {
		const MachineConfig::ScsiTarget& x = a.scsi[id];
		const MachineConfig::ScsiTarget& y = b.scsi[id];
		if (x.kind != y.kind)
			return false;
		// a disc goes into the running machine
		if (x.kind != MachineConfig::ScsiTarget::kCdRom
			&& (x.image != y.image || x.readOnly != y.readOnly))
			return false;
	}
	return true;
}


bool
NeedsRestart(const Settings& old, const Settings& now)
{
	if (!SameHardware(old.MachineConfigForRun(), now.MachineConfigForRun())
		|| old.portForwards != now.portForwards)
		return true;
	for (int i = 0; i < 4; i++) {
		if (old.serial[i] != now.serial[i])
			return true;
	}
	return false;
}


std::string
BootRomFromFile(const std::string& path, std::string& error)
{
	char header[12] = {};
	{
		std::ifstream file(Path(path), std::ios::binary);
		file.read(header, sizeof(header));
	}
	if (memcmp(header, "Joy!peff", 8) == 0
		|| (memcmp(header, "BOOT", 4) == 0
			&& memcmp(header + 4, "Joy!peff", 8) == 0))
		return path;

	std::vector<bebox::u8> data;
	std::string reason;
	if (!bebox::BfsReader::ExtractFromImage(path, kBootRomPath, data,
			reason)) {
		error = Utf8(Path(path).filename()) + " is neither a boot ROM image "
			"nor a BeOS CD image with " + kBootRomPath + " (" + reason + ").";
		return "";
	}
	// e.g. "BeOS Release 3 PowerPC.iso" -> "BeOS Release 3 PowerPC.rom"
	fs::path directory = Path(Settings::Directory()) / "BootROMs";
	std::error_code ignored;
	fs::create_directories(directory, ignored);
	fs::path target = directory / Path(path).stem();
	target += ".rom";
	std::ofstream output(target, std::ios::binary | std::ios::trunc);
	output.write((const char*)data.data(), data.size());
	output.flush();
	if (!output) {
		error = "Cannot write " + Utf8(target) + ".";
		return "";
	}
	return Utf8(target);
}
