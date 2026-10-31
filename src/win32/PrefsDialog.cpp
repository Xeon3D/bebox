/*
 * BeBox emulator for Windows - the preferences.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "PrefsDialog.h"

#include "Util.h"
#include "resource.h"

#include "core/storage/BfsReader.h"
#include "core/storage/DiskImage.h"
#include "core/storage/Qcow2Image.h"

#include <commctrl.h>
#include <mmsystem.h>
#include <prsht.h>

#include <cstdio>
#include <cstring>

using bebox::MachineConfig;

namespace {

const char* const kBootRomPath = "/beos/etc/bebox_bootmain.image";

const wchar_t* const kDiskFilters = L"Disk images (*.img, *.qcow2, *.hdf, "
	L"*.raw, *.dsk)\0*.img;*.qcow2;*.hdf;*.raw;*.dsk\0All files\0*.*\0";
const wchar_t* const kCdFilters = L"CD images (*.iso, *.cue, *.toast, "
	L"*.bin)\0*.iso;*.cue;*.toast;*.bin\0All files\0*.*\0";
const wchar_t* const kFloppyFilters = L"Floppy images (*.img, *.ima, "
	L"*.dsk)\0*.img;*.ima;*.dsk\0All files\0*.*\0";
const wchar_t* const kAllFilters = L"All files\0*.*\0";

const char* const kSerialKinds[] = { "", "console", "tcp-listen:",
	"tcp-connect:", "host:", "file:" };
const wchar_t* const kSerialLabels[] = { L"Not connected",
	L"Console window", L"TCP server", L"TCP client", L"Host serial port",
	L"Log to file" };
const int kSerialKindCount = 6;

enum {
	kPageMachine,
	kPageRoms,
	kPageDisplay,
	kPageStorage,
	kPageSound,
	kPageInput,
	kPageGamePorts,
	kPageNetwork,
	kPageSerial,
	kPageAdvanced,
	kPageCount
};

struct PrefsState {
	Settings*	settings;
	Settings	work;
	bool		running;
	bool		initialized[kPageCount];
};


// #pragma mark - combo boxes


void
AddChoices(HWND dialog, int id, const wchar_t* const* labels,
	const int* values, int count, int selected)
{
	HWND combo = GetDlgItem(dialog, id);
	SendMessageW(combo, CB_RESETCONTENT, 0, 0);
	int selection = 0;
	for (int i = 0; i < count; i++) {
		LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0,
			(LPARAM)labels[i]);
		SendMessageW(combo, CB_SETITEMDATA, index, values[i]);
		if (values[i] == selected)
			selection = (int)index;
	}
	SendMessageW(combo, CB_SETCURSEL, selection, 0);
}


int
SelectedValue(HWND dialog, int id, int fallback = 0)
{
	HWND combo = GetDlgItem(dialog, id);
	LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
	if (index == CB_ERR)
		return fallback;
	return (int)SendMessageW(combo, CB_GETITEMDATA, index, 0);
}


bool
Checked(HWND dialog, int id)
{
	return IsDlgButtonChecked(dialog, id) == BST_CHECKED;
}


void
SetChecked(HWND dialog, int id, bool checked)
{
	CheckDlgButton(dialog, id, checked ? BST_CHECKED : BST_UNCHECKED);
}


std::string
ItemText(HWND dialog, int id)
{
	return WindowTextUtf8(GetDlgItem(dialog, id));
}


void
SetItemText(HWND dialog, int id, const std::string& text)
{
	SetWindowTextUtf8(GetDlgItem(dialog, id), text);
}


PrefsState*
StateOf(HWND dialog, UINT message, LPARAM lParam)
{
	if (message == WM_INITDIALOG) {
		PROPSHEETPAGEW* page = (PROPSHEETPAGEW*)lParam;
		SetWindowLongPtrW(dialog, DWLP_USER, page->lParam);
		return (PrefsState*)page->lParam;
	}
	return (PrefsState*)GetWindowLongPtrW(dialog, DWLP_USER);
}


// PSN_APPLY: the page's controls into the working settings.
bool
IsApply(UINT message, LPARAM lParam)
{
	return message == WM_NOTIFY
		&& ((NMHDR*)lParam)->code == PSN_APPLY;
}


// #pragma mark - Machine


void
FillMemorySizes(HWND dialog, bool hobbit, int current)
{
	static const int kBeBoxSizes[] = { 8, 16, 24, 32, 48, 64, 96, 128, 192,
		256, 384, 512 };
	static const int kHobbitSizes[] = { 4, 8, 16, 20, 32 };
	const int* sizes = hobbit ? kHobbitSizes : kBeBoxSizes;
	int count = hobbit ? 5 : 12;
	int selected = sizes[0];
	for (int i = 0; i < count; i++) {
		if (sizes[i] <= current)
			selected = sizes[i];
	}
	std::wstring labels[12];
	const wchar_t* pointers[12];
	for (int i = 0; i < count; i++) {
		labels[i] = Format(L"%d MB", sizes[i]);
		pointers[i] = labels[i].c_str();
	}
	AddChoices(dialog, IDC_MEMORY, pointers, sizes, count, selected);
}


void
UpdateMachineControls(HWND dialog, PrefsState* state)
{
	bool hobbit = SelectedValue(dialog, IDC_MODEL) == 25;
	EnableWindow(GetDlgItem(dialog, IDC_BOARD), !hobbit);
	EnableWindow(GetDlgItem(dialog, IDC_IO_CARD), !hobbit);
	std::wstring note = hobbit
		? L"The Hobbit prototype takes the memory sizes its kernel knows "
			L"(4 to 32 MB). Motherboard and I/O card are BeBox options."
		: L"The speed is the processors' speed: BeOS's clock keeps real time "
			L"at any setting.";
	if (state->running)
		note += L"\r\n\r\nChanges to the hardware restart the machine.";
	SetDlgItemTextW(dialog, IDC_MACHINE_NOTE, note.c_str());
}


INT_PTR CALLBACK
MachinePage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	MachineConfig& machine = work.machine;
	switch (message) {
		case WM_INITDIALOG:
		{
			state->initialized[kPageMachine] = true;
			static const wchar_t* const kModels[] = {
				L"Dual603-66 (2 × PowerPC 603, 66 MHz)",
				L"Dual603e-133 (2 × PowerPC 603e, 133 MHz)",
				L"Hobbit prototype (2 × AT&T 92010, 25 MHz)" };
			static const int kModelValues[] = { 66, 133, 25 };
			AddChoices(dialog, IDC_MODEL, kModels, kModelValues, 3,
				machine.model == MachineConfig::kBeBox133 ? 133
					: machine.model == MachineConfig::kHobbit ? 25 : 66);
			FillMemorySizes(dialog, machine.model == MachineConfig::kHobbit,
				machine.ramMB);
			static const wchar_t* const kCpus[] = { L"Two (as built)",
				L"One (CPU 1 held in reset)" };
			static const int kCpuValues[] = { 2, 1 };
			AddChoices(dialog, IDC_CPUS, kCpus, kCpuValues, 2,
				machine.cpuCount);
			static const wchar_t* const kBoards[] = {
				L"Revision 5 (128 KB boot ROM flash)",
				L"Revision 6 (512 KB flash)", L"Revision 3" };
			static const int kBoardValues[] = { 5, 6, 3 };
			AddChoices(dialog, IDC_BOARD, kBoards, kBoardValues, 3,
				machine.boardRevision);
			static const wchar_t* const kIoCards[] = { L"Revision 5",
				L"Revision 4" };
			static const int kIoCardValues[] = { 5, 4 };
			AddChoices(dialog, IDC_IO_CARD, kIoCards, kIoCardValues, 2,
				machine.ioCardRevision);
			static const wchar_t* const kSpeeds[] = { L"50%",
				L"100% (a real BeBox)", L"200%", L"As fast as possible" };
			static const int kSpeedValues[] = { 50, 100, 200, 0 };
			AddChoices(dialog, IDC_SPEED, kSpeeds, kSpeedValues, 4,
				work.speedLimit);
			UpdateMachineControls(dialog, state);
			return TRUE;
		}
		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_MODEL
				&& HIWORD(wParam) == CBN_SELCHANGE) {
				FillMemorySizes(dialog, SelectedValue(dialog, IDC_MODEL) == 25,
					SelectedValue(dialog, IDC_MEMORY, 32));
				UpdateMachineControls(dialog, state);
			}
			return TRUE;
	}
	if (IsApply(message, lParam)) {
		int model = SelectedValue(dialog, IDC_MODEL, 66);
		machine.model = model == 133 ? MachineConfig::kBeBox133
			: model == 25 ? MachineConfig::kHobbit : MachineConfig::kBeBox66;
		machine.ramMB = SelectedValue(dialog, IDC_MEMORY, 32);
		machine.cpuCount = SelectedValue(dialog, IDC_CPUS, 2);
		machine.boardRevision = SelectedValue(dialog, IDC_BOARD, 6);
		machine.ioCardRevision = SelectedValue(dialog, IDC_IO_CARD, 5);
		work.speedLimit = SelectedValue(dialog, IDC_SPEED, 100);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - ROM & NVRAM


// Takes a boot ROM update image as it is; from a BeOS CD (or disk) image,
// copies /beos/etc/bebox_bootmain.image into the settings directory.
// The path to use, or "" (an error was shown).
std::string
BootRomChosen(HWND dialog, const std::string& path)
{
	char header[12] = {};
	FILE* file = _wfopen(Widen(path).c_str(), L"rb");
	if (file != nullptr) {
		fread(header, 1, sizeof(header), file);
		fclose(file);
	}
	if (memcmp(header, "Joy!peff", 8) == 0
		|| (memcmp(header, "BOOT", 4) == 0
			&& memcmp(header + 4, "Joy!peff", 8) == 0))
		return path;

	std::vector<bebox::u8> data;
	std::string error;
	if (!bebox::BfsReader::ExtractFromImage(path, kBootRomPath, data,
			error)) {
		ShowMessage(dialog, Widen(Leaf(path) + " is neither a boot ROM image "
			"nor a BeOS CD image with " + kBootRomPath + " (" + error + ")."),
			MB_ICONERROR);
		return "";
	}
	// e.g. "BeOS Release 3 PowerPC.iso" -> "BeOS Release 3 PowerPC.rom"
	std::string name = Leaf(path);
	size_t dot = name.rfind('.');
	if (dot != std::string::npos && dot > 0)
		name.resize(dot);
	std::string directory = Settings::Directory() + "\\BootROMs";
	CreateDirectories(directory);
	std::string target = directory + "\\" + name + ".rom";
	FILE* output = _wfopen(Widen(target).c_str(), L"wb");
	bool ok = output != nullptr
		&& fwrite(data.data(), 1, data.size(), output) == data.size();
	if (output != nullptr)
		ok = fclose(output) == 0 && ok;
	if (!ok) {
		ShowMessage(dialog, Widen("Cannot write " + target + "."),
			MB_ICONERROR);
		return "";
	}
	return target;
}


INT_PTR CALLBACK
RomsPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	switch (message) {
		case WM_INITDIALOG:
			state->initialized[kPageRoms] = true;
			SetItemText(dialog, IDC_ROM_DIR, work.romDirectory);
			SetItemText(dialog, IDC_BOOT_ROM, work.bootRomImage);
			SetItemText(dialog, IDC_HOBBIT_KERNEL,
				work.machine.hobbitKernelPath);
			EnableWindow(GetDlgItem(dialog, IDC_RESET_FLASH), !state->running);
			EnableWindow(GetDlgItem(dialog, IDC_CLEAR_NVRAM), !state->running);
			return TRUE;
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDC_ROM_DIR_CHOOSE:
				{
					std::string folder = ChooseFolder(dialog,
						L"BeBox: ROM folder", ItemText(dialog, IDC_ROM_DIR));
					if (!folder.empty())
						SetItemText(dialog, IDC_ROM_DIR, folder);
					break;
				}
				case IDC_BOOT_ROM_CHOOSE:
				{
					std::string path = ChooseOpenFile(dialog,
						L"BeBox: Boot ROM image (or a BeOS CD image)",
						L"Boot ROM and CD images (*.image, *.rom, *.iso, "
						L"*.cue, *.toast)\0*.image;*.rom;*.iso;*.cue;*.toast"
						L"\0All files\0*.*\0",
						ItemText(dialog, IDC_BOOT_ROM));
					if (!path.empty()) {
						path = BootRomChosen(dialog, path);
						if (!path.empty())
							SetItemText(dialog, IDC_BOOT_ROM, path);
					}
					break;
				}
				case IDC_HOBBIT_KERNEL_CHOOSE:
				{
					std::string path = ChooseOpenFile(dialog,
						L"BeBox: Hobbit kernel", kAllFilters,
						ItemText(dialog, IDC_HOBBIT_KERNEL));
					if (!path.empty())
						SetItemText(dialog, IDC_HOBBIT_KERNEL, path);
					break;
				}
				case IDC_RESET_FLASH:
					if (AskYesNo(dialog, L"Reset the boot ROM flash to the "
							L"ROM files? Boot ROM updates written by BeOS are "
							L"lost.")) {
						DeleteFileW(Widen(work.FlashPath()).c_str());
					}
					break;
				case IDC_CLEAR_NVRAM:
					if (AskYesNo(dialog, L"Clear the NVRAM? The boot "
							L"preferences (boot disk) are lost.")) {
						DeleteFileW(Widen(work.NvramPath()).c_str());
					}
					break;
			}
			return TRUE;
	}
	if (IsApply(message, lParam)) {
		work.romDirectory = ItemText(dialog, IDC_ROM_DIR);
		work.bootRomImage = ItemText(dialog, IDC_BOOT_ROM);
		work.machine.hobbitKernelPath = ItemText(dialog, IDC_HOBBIT_KERNEL);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - Display


INT_PTR CALLBACK
DisplayPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	if (message == WM_INITDIALOG) {
		state->initialized[kPageDisplay] = true;
		static const wchar_t* const kVideo[] = {
			L"Number Nine #9 GXE64 (S3 Vision864)",
			L"Cirrus Logic CL-GD5434", L"None" };
		static const int kVideoValues[] = { MachineConfig::kVideoS3Vision864,
			MachineConfig::kVideoCirrus5434, MachineConfig::kVideoNone };
		AddChoices(dialog, IDC_VIDEO, kVideo, kVideoValues, 3,
			work.machine.videoCard);
		static const wchar_t* const kVram[] = { L"1 MB", L"2 MB", L"4 MB" };
		static const int kVramValues[] = { 1, 2, 4 };
		AddChoices(dialog, IDC_VRAM, kVram, kVramValues, 3,
			work.machine.vramMB);
		static const wchar_t* const kStyles[] = {
			L"Authentic (lime and amber)", L"Red and green" };
		static const int kStyleValues[] = { kLedStyleAuthentic,
			kLedStyleRedGreen };
		AddChoices(dialog, IDC_LED_STYLE, kStyles, kStyleValues, 2,
			work.ledStyle);
		SetChecked(dialog, IDC_SHOW_LEDS, work.showLeds);
		return TRUE;
	}
	if (IsApply(message, lParam)) {
		work.machine.videoCard = (MachineConfig::VideoCard)SelectedValue(
			dialog, IDC_VIDEO, MachineConfig::kVideoS3Vision864);
		work.machine.vramMB = SelectedValue(dialog, IDC_VRAM, 2);
		work.ledStyle = SelectedValue(dialog, IDC_LED_STYLE);
		work.showLeds = Checked(dialog, IDC_SHOW_LEDS);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - Storage


int IdePath(int i) { return IDC_IDE_PATH + i * 4; }
int ScsiKind(int id) { return IDC_SCSI_KIND + id * 4; }
int ScsiPath(int id) { return IDC_SCSI_PATH + id * 4; }
int ScsiReadOnly(int id) { return IDC_SCSI_READ_ONLY + id * 4; }


void
UpdateScsiRow(HWND dialog, int id)
{
	int kind = SelectedValue(dialog, ScsiKind(id));
	bool none = kind == MachineConfig::ScsiTarget::kNone;
	EnableWindow(GetDlgItem(dialog, ScsiPath(id)), !none);
	EnableWindow(GetDlgItem(dialog, ScsiPath(id) + 1), !none);
	// discs and shared folders: never written / written back on shutdown
	EnableWindow(GetDlgItem(dialog, ScsiReadOnly(id)),
		kind == MachineConfig::ScsiTarget::kDisk);
}


INT_PTR CALLBACK
StoragePage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	MachineConfig& machine = state->work.machine;
	switch (message) {
		case WM_INITDIALOG:
		{
			state->initialized[kPageStorage] = true;
			for (int i = 0; i < 2; i++) {
				SetItemText(dialog, IdePath(i), machine.ideImage[i]);
				SetChecked(dialog, IdePath(i) + 2, machine.ideReadOnly[i]);
			}
			static const wchar_t* const kKinds[] = { L"None", L"Disk",
				L"CD-ROM", L"Shared folder" };
			static const int kKindValues[] = {
				MachineConfig::ScsiTarget::kNone,
				MachineConfig::ScsiTarget::kDisk,
				MachineConfig::ScsiTarget::kCdRom,
				MachineConfig::ScsiTarget::kSharedFolder };
			for (int id = 0; id < 7; id++) {
				const MachineConfig::ScsiTarget& target = machine.scsi[id];
				AddChoices(dialog, ScsiKind(id), kKinds, kKindValues, 4,
					target.kind);
				SetItemText(dialog, ScsiPath(id), target.image);
				SetChecked(dialog, ScsiReadOnly(id), target.readOnly);
				UpdateScsiRow(dialog, id);
			}
			SetItemText(dialog, IDC_FLOPPY_PATH, machine.floppyImage);
			SetChecked(dialog, IDC_FLOPPY_PROTECT,
				machine.floppyWriteProtected);
			return TRUE;
		}
		case WM_COMMAND:
		{
			int control = LOWORD(wParam);
			if (control >= IDC_SCSI_KIND && control < IDC_SCSI_KIND + 28) {
				int id = (control - IDC_SCSI_KIND) / 4;
				int column = (control - IDC_SCSI_KIND) % 4;
				if (column == 0 && HIWORD(wParam) == CBN_SELCHANGE)
					UpdateScsiRow(dialog, id);
				else if (column == 2) {
					int kind = SelectedValue(dialog, ScsiKind(id));
					std::string current = ItemText(dialog, ScsiPath(id));
					std::wstring title = Format(L"BeBox: SCSI ID %d", id);
					std::string path;
					if (kind == MachineConfig::ScsiTarget::kSharedFolder)
						path = ChooseFolder(dialog, title.c_str(), current);
					else {
						path = ChooseOpenFile(dialog, title.c_str(),
							kind == MachineConfig::ScsiTarget::kCdRom
								? kCdFilters : kDiskFilters, current);
					}
					if (!path.empty())
						SetItemText(dialog, ScsiPath(id), path);
				}
				return TRUE;
			}
			if (control == IDC_IDE_CHOOSE || control == IDC_IDE_CHOOSE + 4) {
				int i = (control - IDC_IDE_CHOOSE) / 4;
				std::string path = ChooseOpenFile(dialog, i == 0
					? L"BeBox: IDE master" : L"BeBox: IDE slave",
					kDiskFilters, ItemText(dialog, IdePath(i)));
				if (!path.empty())
					SetItemText(dialog, IdePath(i), path);
			} else if (control == IDC_FLOPPY_CHOOSE) {
				std::string path = ChooseOpenFile(dialog, L"BeBox: Floppy",
					kFloppyFilters, ItemText(dialog, IDC_FLOPPY_PATH));
				if (!path.empty())
					SetItemText(dialog, IDC_FLOPPY_PATH, path);
			} else if (control == IDC_NEW_DISK) {
				std::string path = ShowNewDiskDialog(dialog,
					Settings::Directory());
				if (path.empty())
					break;
				// on the first free SCSI ID
				for (int id = 0; id < 7; id++) {
					if (SelectedValue(dialog, ScsiKind(id))
							!= MachineConfig::ScsiTarget::kNone)
						continue;
					SendDlgItemMessageW(dialog, ScsiKind(id), CB_SETCURSEL,
						1, 0);
					SetItemText(dialog, ScsiPath(id), path);
					UpdateScsiRow(dialog, id);
					ShowMessage(dialog, Format(L"The new disk is on SCSI ID "
						L"%d.", id));
					break;
				}
			}
			return TRUE;
		}
	}
	if (IsApply(message, lParam)) {
		for (int i = 0; i < 2; i++) {
			machine.ideImage[i] = ItemText(dialog, IdePath(i));
			machine.ideReadOnly[i] = Checked(dialog, IdePath(i) + 2);
		}
		for (int id = 0; id < 7; id++) {
			MachineConfig::ScsiTarget& target = machine.scsi[id];
			target.kind = (MachineConfig::ScsiTarget::Kind)SelectedValue(
				dialog, ScsiKind(id));
			target.image = ItemText(dialog, ScsiPath(id));
			target.readOnly = Checked(dialog, ScsiReadOnly(id));
		}
		machine.floppyImage = ItemText(dialog, IDC_FLOPPY_PATH);
		machine.floppyWriteProtected = Checked(dialog, IDC_FLOPPY_PROTECT);
		Settings::DropEmptyDevices(machine);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - Sound, keyboard and mouse


INT_PTR CALLBACK
SoundPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	if (message == WM_INITDIALOG) {
		state->initialized[kPageSound] = true;
		SetChecked(dialog, IDC_SOUND_ENABLED, work.soundEnabled);
		HWND volume = GetDlgItem(dialog, IDC_SOUND_VOLUME);
		SendMessageW(volume, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
		SendMessageW(volume, TBM_SETPOS, TRUE, work.soundVolume);
		return TRUE;
	}
	if (IsApply(message, lParam)) {
		work.soundEnabled = Checked(dialog, IDC_SOUND_ENABLED);
		work.soundVolume = (int)SendDlgItemMessageW(dialog, IDC_SOUND_VOLUME,
			TBM_GETPOS, 0, 0);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


INT_PTR CALLBACK
InputPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	if (message == WM_INITDIALOG) {
		state->initialized[kPageInput] = true;
		static const wchar_t* const kKeys[] = { L"Right Control",
			L"Right Alt", L"Right Windows key", L"Menu key" };
		static const int kKeyValues[] = { kHostKeyRightControl,
			kHostKeyRightAlt, kHostKeyRightWindows, kHostKeyMenu };
		AddChoices(dialog, IDC_HOST_KEY, kKeys, kKeyValues, 4, work.hostKey);
		HWND speed = GetDlgItem(dialog, IDC_MOUSE_SPEED);
		SendMessageW(speed, TBM_SETRANGE, TRUE, MAKELONG(25, 400));
		SendMessageW(speed, TBM_SETPOS, TRUE, work.mouseSpeed);
		SetChecked(dialog, IDC_SWAP_BUTTONS, work.swapMouseButtons);
		SetChecked(dialog, IDC_CAPTURE_ON_CLICK, work.captureOnClick);
		return TRUE;
	}
	if (IsApply(message, lParam)) {
		work.hostKey = SelectedValue(dialog, IDC_HOST_KEY);
		work.mouseSpeed = (int)SendDlgItemMessageW(dialog, IDC_MOUSE_SPEED,
			TBM_GETPOS, 0, 0);
		work.swapMouseButtons = Checked(dialog, IDC_SWAP_BUTTONS);
		work.captureOnClick = Checked(dialog, IDC_CAPTURE_ON_CLICK);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - Game ports


INT_PTR CALLBACK
GamePortsPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	if (message == WM_INITDIALOG) {
		state->initialized[kPageGamePorts] = true;
		// the joysticks connected now (winmm numbers them 0-15)
		std::vector<std::wstring> labels;
		std::vector<int> values;
		labels.push_back(L"None");
		values.push_back(-1);
		UINT count = joyGetNumDevs();
		for (UINT i = 0; i < count && i < 16; i++) {
			JOYINFOEX info = {};
			info.dwSize = sizeof(info);
			info.dwFlags = JOY_RETURNALL;
			if (joyGetPosEx(i, &info) != JOYERR_NOERROR)
				continue;
			JOYCAPSW caps = {};
			joyGetDevCapsW(i, &caps, sizeof(caps));
			labels.push_back(Format(L"%u: %ls", i + 1, caps.szPname[0] != 0
				? caps.szPname : L"Game controller"));
			values.push_back((int)i);
		}
		std::vector<const wchar_t*> pointers;
		for (const std::wstring& label : labels)
			pointers.push_back(label.c_str());
		for (int stick = 0; stick < 4; stick++) {
			int selected = work.joystick[stick].empty() ? -1
				: atoi(work.joystick[stick].c_str());
			AddChoices(dialog, IDC_STICK + stick, pointers.data(),
				values.data(), (int)values.size(), selected);
		}
		return TRUE;
	}
	if (IsApply(message, lParam)) {
		for (int stick = 0; stick < 4; stick++) {
			int value = SelectedValue(dialog, IDC_STICK + stick, -1);
			work.joystick[stick] = value < 0 ? "" : std::to_string(value);
		}
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - Network


void
UpdateNetworkControls(HWND dialog)
{
	bool on = Checked(dialog, IDC_ETHERNET);
	for (int id : { IDC_ETHERNET_PORT, IDC_ETHERNET_IRQ, IDC_MAC_ADDRESS,
			IDC_PORT_FORWARDS })
		EnableWindow(GetDlgItem(dialog, id), on);
}


INT_PTR CALLBACK
NetworkPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	switch (message) {
		case WM_INITDIALOG:
		{
			state->initialized[kPageNetwork] = true;
			SetChecked(dialog, IDC_ETHERNET, work.ethernet);
			static const wchar_t* const kPorts[] = { L"220", L"240", L"280",
				L"2A0", L"2C0", L"300", L"320", L"340", L"3C0" };
			static const int kPortValues[] = { 0x220, 0x240, 0x280, 0x2a0,
				0x2c0, 0x300, 0x320, 0x340, 0x3c0 };
			AddChoices(dialog, IDC_ETHERNET_PORT, kPorts, kPortValues, 9,
				work.ethernetPort);
			static const wchar_t* const kIrqs[] = { L"5", L"9", L"10", L"11",
				L"15" };
			static const int kIrqValues[] = { 5, 9, 10, 11, 15 };
			AddChoices(dialog, IDC_ETHERNET_IRQ, kIrqs, kIrqValues, 5,
				work.ethernetIrq);
			SetItemText(dialog, IDC_MAC_ADDRESS, work.macAddress);
			SetItemText(dialog, IDC_PORT_FORWARDS, work.portForwards);
			UpdateNetworkControls(dialog);
			return TRUE;
		}
		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_ETHERNET)
				UpdateNetworkControls(dialog);
			return TRUE;
		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == PSN_KILLACTIVE) {
				// the MAC address must be one
				unsigned int mac[6];
				char tail;
				std::string text = ItemText(dialog, IDC_MAC_ADDRESS);
				bool bad = sscanf(text.c_str(), "%x:%x:%x:%x:%x:%x%c",
					&mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5],
					&tail) != 6;
				for (int i = 0; !bad && i < 6; i++)
					bad = mac[i] > 0xff;
				if (bad) {
					ShowMessage(dialog, L"The MAC address is six hexadecimal "
						L"bytes, e.g. 00:00:e8:12:34:56.", MB_ICONERROR);
				}
				SetWindowLongPtrW(dialog, DWLP_MSGRESULT, bad);
				return TRUE;
			}
			break;
	}
	if (IsApply(message, lParam)) {
		work.ethernet = Checked(dialog, IDC_ETHERNET);
		work.ethernetPort = SelectedValue(dialog, IDC_ETHERNET_PORT, 0x300);
		work.ethernetIrq = SelectedValue(dialog, IDC_ETHERNET_IRQ, 5);
		work.macAddress = ItemText(dialog, IDC_MAC_ADDRESS);
		work.portForwards = ItemText(dialog, IDC_PORT_FORWARDS);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - Serial ports


void
UpdateSerialControls(HWND dialog)
{
	for (int i = 0; i < 4; i++) {
		int kind = SelectedValue(dialog, IDC_SERIAL_KIND + i * 2);
		EnableWindow(GetDlgItem(dialog, IDC_SERIAL_TARGET + i * 2), kind >= 2);
	}
}


INT_PTR CALLBACK
SerialPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	Settings& work = state->work;
	switch (message) {
		case WM_INITDIALOG:
		{
			state->initialized[kPageSerial] = true;
			static const int kValues[] = { 0, 1, 2, 3, 4, 5 };
			for (int i = 0; i < 4; i++) {
				const std::string& destination = work.serial[i];
				int kind = 0;
				for (int k = 1; k < kSerialKindCount; k++) {
					size_t length = strlen(kSerialKinds[k]);
					if (k == 1 ? destination == kSerialKinds[k]
							: destination.compare(0, length, kSerialKinds[k])
								== 0)
						kind = k;
				}
				AddChoices(dialog, IDC_SERIAL_KIND + i * 2, kSerialLabels,
					kValues, kSerialKindCount, kind);
				SetItemText(dialog, IDC_SERIAL_TARGET + i * 2, kind >= 2
					? destination.substr(strlen(kSerialKinds[kind])) : "");
			}
			UpdateSerialControls(dialog);
			return TRUE;
		}
		case WM_COMMAND:
			if (HIWORD(wParam) == CBN_SELCHANGE)
				UpdateSerialControls(dialog);
			return TRUE;
	}
	if (IsApply(message, lParam)) {
		for (int i = 0; i < 4; i++) {
			int kind = SelectedValue(dialog, IDC_SERIAL_KIND + i * 2);
			std::string destination = kSerialKinds[kind];
			if (kind >= 2)
				destination += ItemText(dialog, IDC_SERIAL_TARGET + i * 2);
			work.serial[i] = destination;
		}
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - Advanced


INT_PTR CALLBACK
AdvancedPage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	PrefsState* state = StateOf(dialog, message, lParam);
	if (state == nullptr)
		return FALSE;
	MachineConfig& machine = state->work.machine;
	if (message == WM_INITDIALOG) {
		state->initialized[kPageAdvanced] = true;
		static const wchar_t* const kQuanta[] = { L"1 µs", L"5 µs",
			L"10 µs (default)", L"50 µs" };
		static const int kQuantumValues[] = { 1, 5, 10, 50 };
		AddChoices(dialog, IDC_QUANTUM, kQuanta, kQuantumValues, 4,
			machine.quantumUs);
		static const wchar_t* const kCpis[] = { L"0.5", L"1.0 (default)",
			L"1.5", L"2.0" };
		static const int kCpiValues[] = { 50, 100, 150, 200 };
		AddChoices(dialog, IDC_CPI, kCpis, kCpiValues, 4, machine.cpiPercent);
		SetChecked(dialog, IDC_THREADS, machine.threadPerCpu);
		return TRUE;
	}
	if (IsApply(message, lParam)) {
		machine.quantumUs = SelectedValue(dialog, IDC_QUANTUM, 10);
		machine.cpiPercent = SelectedValue(dialog, IDC_CPI, 100);
		machine.threadPerCpu = Checked(dialog, IDC_THREADS);
		SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	return FALSE;
}


// #pragma mark - New disk and profile name dialogs


struct NewDiskState {
	std::string directory;
	std::string path;
};


INT_PTR CALLBACK
NewDiskProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	NewDiskState* state;
	if (message == WM_INITDIALOG) {
		SetWindowLongPtrW(dialog, DWLP_USER, lParam);
		state = (NewDiskState*)lParam;
		SetItemText(dialog, IDC_DISK_PATH, state->directory + "\\Disk.qcow2");
		static const wchar_t* const kSizes[] = { L"100 MB", L"250 MB",
			L"500 MB", L"1 GB", L"2 GB", L"4 GB", L"8 GB" };
		static const int kSizeValues[] = { 100, 250, 500, 1024, 2048, 4096,
			8192 };
		AddChoices(dialog, IDC_DISK_SIZE, kSizes, kSizeValues, 7, 1024);
		static const wchar_t* const kFormats[] = {
			L"QCOW2 (grows as it fills)", L"Raw (its full size at once)" };
		static const int kFormatValues[] = { 0, 1 };
		AddChoices(dialog, IDC_DISK_FORMAT, kFormats, kFormatValues, 2, 0);
		return TRUE;
	}
	state = (NewDiskState*)GetWindowLongPtrW(dialog, DWLP_USER);
	if (message != WM_COMMAND || state == nullptr)
		return FALSE;
	switch (LOWORD(wParam)) {
		case IDC_DISK_CHOOSE:
		{
			bool raw = SelectedValue(dialog, IDC_DISK_FORMAT) == 1;
			std::string path = ChooseSaveFile(dialog, L"BeBox: New disk image",
				raw ? L"Raw disk images (*.img)\0*.img\0"
					: L"QCOW2 disk images (*.qcow2)\0*.qcow2\0",
				raw ? L"img" : L"qcow2", ItemText(dialog, IDC_DISK_PATH));
			if (!path.empty())
				SetItemText(dialog, IDC_DISK_PATH, path);
			return TRUE;
		}
		case IDOK:
		{
			std::string path = ItemText(dialog, IDC_DISK_PATH);
			if (path.empty())
				return TRUE;
			if (FileExists(path)) {
				ShowMessage(dialog, Widen(path + " exists already."),
					MB_ICONERROR);
				return TRUE;
			}
			bebox::u64 size = (bebox::u64)SelectedValue(dialog, IDC_DISK_SIZE,
				1024) << 20;
			std::string error;
			bool ok = SelectedValue(dialog, IDC_DISK_FORMAT) == 1
				? bebox::RawImage::Create(path, size, error)
				: bebox::Qcow2Image::Create(path, size, error);
			if (!ok) {
				ShowMessage(dialog, Widen("The disk image could not be "
					"created: " + error), MB_ICONERROR);
				return TRUE;
			}
			state->path = path;
			EndDialog(dialog, IDOK);
			return TRUE;
		}
		case IDCANCEL:
			EndDialog(dialog, IDCANCEL);
			return TRUE;
	}
	return FALSE;
}


struct NameState {
	const wchar_t* title;
	const wchar_t* prompt;
	std::string name;
};


INT_PTR CALLBACK
NameProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	NameState* state;
	if (message == WM_INITDIALOG) {
		SetWindowLongPtrW(dialog, DWLP_USER, lParam);
		state = (NameState*)lParam;
		SetWindowTextW(dialog, state->title);
		SetDlgItemTextW(dialog, IDC_PROFILE_PROMPT, state->prompt);
		SetItemText(dialog, IDC_PROFILE_NAME, state->name);
		SendDlgItemMessageW(dialog, IDC_PROFILE_NAME, EM_SETSEL, 0, -1);
		return TRUE;
	}
	state = (NameState*)GetWindowLongPtrW(dialog, DWLP_USER);
	if (message != WM_COMMAND || state == nullptr)
		return FALSE;
	if (LOWORD(wParam) == IDOK) {
		std::string name = ItemText(dialog, IDC_PROFILE_NAME);
		std::string problem = Settings::CheckProfileName(name);
		if (!problem.empty()) {
			ShowMessage(dialog, Widen(problem), MB_ICONERROR);
			return TRUE;
		}
		state->name = name;
		EndDialog(dialog, IDOK);
		return TRUE;
	}
	if (LOWORD(wParam) == IDCANCEL) {
		EndDialog(dialog, IDCANCEL);
		return TRUE;
	}
	return FALSE;
}


// The parts of the machine a running one cannot change.
bool
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

}	// namespace


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


bool
ShowPreferences(HWND owner, Settings& settings, bool running)
{
	PrefsState state;
	state.settings = &settings;
	state.work = settings;
	state.running = running;
	for (bool& initialized : state.initialized)
		initialized = false;

	struct PageInfo {
		int dialog;
		DLGPROC proc;
	};
	static const PageInfo kPages[kPageCount] = {
		{ IDD_PREFS_MACHINE, MachinePage },
		{ IDD_PREFS_ROMS, RomsPage },
		{ IDD_PREFS_DISPLAY, DisplayPage },
		{ IDD_PREFS_STORAGE, StoragePage },
		{ IDD_PREFS_SOUND, SoundPage },
		{ IDD_PREFS_INPUT, InputPage },
		{ IDD_PREFS_GAMEPORTS, GamePortsPage },
		{ IDD_PREFS_NETWORK, NetworkPage },
		{ IDD_PREFS_SERIAL, SerialPage },
		{ IDD_PREFS_ADVANCED, AdvancedPage }
	};
	PROPSHEETPAGEW pages[kPageCount] = {};
	for (int i = 0; i < kPageCount; i++) {
		pages[i].dwSize = sizeof(PROPSHEETPAGEW);
		pages[i].hInstance = GetModuleHandleW(nullptr);
		pages[i].pszTemplate = MAKEINTRESOURCEW(kPages[i].dialog);
		pages[i].pfnDlgProc = kPages[i].proc;
		pages[i].lParam = (LPARAM)&state;
	}
	std::wstring title = Widen("BeBox preferences: " + settings.profile);
	PROPSHEETHEADERW header = {};
	header.dwSize = sizeof(header);
	header.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP
		| PSH_USEICONID;
	header.hwndParent = owner;
	header.hInstance = GetModuleHandleW(nullptr);
	header.pszIcon = MAKEINTRESOURCEW(IDI_BEBOX);
	header.pszCaption = title.c_str();
	header.nPages = kPageCount;
	header.ppsp = pages;
	if (PropertySheetW(&header) <= 0)
		return false;
	settings = state.work;
	return true;
}


std::string
ShowNewDiskDialog(HWND owner, const std::string& directory)
{
	NewDiskState state;
	state.directory = directory;
	if (DialogBoxParamW(GetModuleHandleW(nullptr),
			MAKEINTRESOURCEW(IDD_NEW_DISK), owner, NewDiskProc,
			(LPARAM)&state) != IDOK)
		return "";
	return state.path;
}


std::string
AskProfileName(HWND owner, const wchar_t* title, const wchar_t* prompt,
	const std::string& initial)
{
	NameState state;
	state.title = title;
	state.prompt = prompt;
	state.name = initial;
	if (DialogBoxParamW(GetModuleHandleW(nullptr),
			MAKEINTRESOURCEW(IDD_PROFILE_NAME), owner, NameProc,
			(LPARAM)&state) != IDOK)
		return "";
	return state.name;
}
