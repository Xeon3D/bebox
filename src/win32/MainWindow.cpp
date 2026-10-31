/*
 * BeBox emulator for Windows - the machine's window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "MainWindow.h"

#include "KeyMap.h"
#include "LedColumn.h"
#include "PrefsDialog.h"
#include "Util.h"
#include "resource.h"

#include "core/board/Machine.h"
#include "core/debug/PngWriter.h"
#include "core/host/KeyCodes.h"
#include "core/host/NetworkBackend.h"
#include "core/host/SerialBackends.h"
#include "core/isa/Fdc82077.h"
#include "core/isa/JoystickAdc.h"
#include "core/isa/Kbc8042.h"
#include "core/pci/ScsiDevice.h"
#include "core/storage/CdImage.h"
#include "core/storage/DiskImage.h"
#include "core/storage/SharedFolder.h"

#include <commctrl.h>
#include <mmsystem.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <ctime>

using bebox::MachineConfig;

namespace {

const wchar_t* const kClassName = L"BeBoxMachineWindow";
const wchar_t* const kDash = L"  —  ";

enum {
	IDM_POWER = 200,
	IDM_RESET,
	IDM_INTERRUPT,
	IDM_CTRL_ALT_DELETE,
	IDM_BOOT_MENU,
	IDM_BOOT_DEBUG,
	IDM_PAUSE,
	IDM_PREFERENCES,
	IDM_ABOUT,
	IDM_QUIT,
	IDM_NEW_PROFILE,
	IDM_RENAME_PROFILE,
	IDM_DELETE_PROFILE,
	IDM_INSERT_FLOPPY,
	IDM_EJECT_FLOPPY,
	IDM_FULL_SCREEN,
	IDM_FULL_SCREEN_FILL,
	IDM_FULL_SCREEN_WHOLE,
	IDM_FULL_SCREEN_ACTUAL,
	IDM_SCALE_1,
	IDM_SCALE_2,
	IDM_SCALE_FIT,
	IDM_SHOW_LEDS,
	IDM_LEDS_AUTHENTIC,
	IDM_LEDS_RED_GREEN,
	IDM_SCREENSHOT,
	IDM_PROFILE_FIRST = 1000,		// + profile index
	IDM_PROFILE_LAST = 1099,
	IDM_CD_INSERT = 1200,			// + SCSI ID
	IDM_CD_EJECT = 1210,			// + SCSI ID
	IDM_CONSOLE = 1300				// + port
};

// Messages from other threads (and the keyboard hook).
const UINT kMsgProblem = WM_APP + 40;			// lParam: std::string*
const UINT kMsgCdEjected = WM_APP + 41;			// wParam: ID, lParam: path
const UINT kMsgFoldersReady = WM_APP + 42;		// wParam: generation
const UINT kMsgFoldersSynced = WM_APP + 43;		// lParam: report or null
const UINT kMsgHookKey = WM_APP + 44;

const UINT_PTR kStatusTimer = 1;
const UINT_PTR kJoystickTimer = 2;

// The host key, as set 2 codes.
const bebox::u32 kHostKeyCodes[] = { 0xe014, 0xe011, 0xe027, 0xe02f };
const wchar_t* const kHostKeyNames[] = { L"Right Ctrl", L"Right Alt",
	L"Right Windows", L"Menu" };

const wchar_t* const kFloppyFilters = L"Floppy images (*.img, *.ima, "
	L"*.dsk)\0*.img;*.ima;*.dsk\0All files\0*.*\0";
const wchar_t* const kCdFilters = L"CD images (*.iso, *.cue, *.toast, "
	L"*.bin)\0*.iso;*.cue;*.toast;*.bin\0All files\0*.*\0";

MainWindow* sHookTarget = nullptr;


bool
HasExtension(const std::string& path, const char* const* extensions)
{
	size_t dot = path.rfind('.');
	if (dot == std::string::npos)
		return false;
	std::string extension = path.substr(dot + 1);
	for (const char* const* known = extensions; *known != nullptr; known++) {
		if (_stricmp(extension.c_str(), *known) == 0)
			return true;
	}
	return false;
}

}	// namespace


// #pragma mark - MainWindow


struct MainWindow::FolderStart {
	int generation = 0;
	MachineConfig config;		// the volumes' images in place
	std::string profileDirectory;
	std::vector<std::unique_ptr<bebox::SharedFolder> > folders;
	std::string messages;
};


MainWindow::MainWindow(Settings& settings)
	:
	fSettings(settings),
	fScreen(fEmulator),
	fFolderStarts(0),
	fFolderSyncs(0),
	fWindow(nullptr),
	fStatus(nullptr),
	fMenuBar(nullptr),
	fMachineMenu(nullptr),
	fProfileMenu(nullptr),
	fMediaMenu(nullptr),
	fViewMenu(nullptr),
	fToolsMenu(nullptr),
	fPowered(false),
	fStarting(false),
	fFullScreen(false),
	fWindowedStyle(0),
	fHostKeyDown(false),
	fHostComboUsed(false),
	fKeyboardHook(nullptr),
	fProblemTime(0),
	fLeds{ 0, 0 },
	fDisk(0)
{
	for (int i = 0; i < 4; i++)
		fConsoles[i] = new SerialConsole(i);
	for (Stick& stick : fSticks)
		stick.device = -1;
	fWindowedPlacement.length = sizeof(fWindowedPlacement);
	bebox::Log::SetSink(&MainWindow::_LogSink, this);
}


MainWindow::~MainWindow()
{
	bebox::Log::SetSink(nullptr, nullptr);
	_SetKeyboardHook(false);
	for (SerialConsole* console : fConsoles)
		delete console;
}


void
MainWindow::RegisterClass(HINSTANCE instance)
{
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = _WindowProc;
	windowClass.hInstance = instance;
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_BEBOX));
	windowClass.hIconSm = (HICON)LoadImageW(instance,
		MAKEINTRESOURCEW(IDI_BEBOX), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	windowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	windowClass.lpszClassName = kClassName;
	RegisterClassExW(&windowClass);
	ScreenView::RegisterClass(instance);
	SerialConsole::RegisterClass(instance);
}


bool
MainWindow::Create(int showCommand)
{
	_BuildMenus();
	fWindow = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, kAppName,
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
		800, 600, nullptr, fMenuBar, GetModuleHandleW(nullptr), this);
	if (fWindow == nullptr)
		return false;
	const WindowFrame& saved = fSettings.windowFrame;
	RECT frame = { saved.left, saved.top, saved.right, saved.bottom };
	if (saved.IsValid()) {
		// where it was, if that is still on a screen
		HMONITOR monitor = MonitorFromRect(&frame, MONITOR_DEFAULTTONULL);
		if (monitor != nullptr) {
			SetWindowPos(fWindow, nullptr, frame.left, frame.top,
				frame.right - frame.left, frame.bottom - frame.top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
	_FitWindow();
	ShowWindow(fWindow, showCommand);
	UpdateWindow(fWindow);
	SetTimer(fWindow, kStatusTimer, 100, nullptr);
	_UpdateMenus();
	_UpdateStatus();
	return true;
}


// #pragma mark - menus


void
MainWindow::_BuildMenus()
{
	fMenuBar = CreateMenu();

	fMachineMenu = CreatePopupMenu();
	AppendMenuW(fMachineMenu, MF_STRING, IDM_POWER, L"&Power on");
	AppendMenuW(fMachineMenu, MF_STRING, IDM_RESET,
		L"&Reset (both switches)");
	AppendMenuW(fMachineMenu, MF_STRING, IDM_INTERRUPT,
		L"&Interrupt switch");
	AppendMenuW(fMachineMenu, MF_STRING, IDM_CTRL_ALT_DELETE,
		L"Send Ctrl+Alt+&Delete");
	AppendMenuW(fMachineMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(fMachineMenu, MF_STRING, IDM_BOOT_MENU,
		L"Restart with &boot menu");
	AppendMenuW(fMachineMenu, MF_STRING, IDM_BOOT_DEBUG,
		L"Restart with debu&g output (serial 4)");
	AppendMenuW(fMachineMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(fMachineMenu, MF_STRING, IDM_PAUSE, L"P&ause");
	AppendMenuW(fMachineMenu, MF_SEPARATOR, 0, nullptr);
	fProfileMenu = CreatePopupMenu();
	AppendMenuW(fMachineMenu, MF_POPUP, (UINT_PTR)fProfileMenu,
		L"Pro&file");
	AppendMenuW(fMachineMenu, MF_STRING, IDM_PREFERENCES,
		L"Pr&eferences...");
	AppendMenuW(fMachineMenu, MF_STRING, IDM_ABOUT, L"About &BeBox...");
	AppendMenuW(fMachineMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(fMachineMenu, MF_STRING, IDM_QUIT, L"&Quit");
	AppendMenuW(fMenuBar, MF_POPUP, (UINT_PTR)fMachineMenu, L"&Machine");

	fMediaMenu = CreatePopupMenu();
	AppendMenuW(fMenuBar, MF_POPUP, (UINT_PTR)fMediaMenu, L"M&edia");

	fViewMenu = CreatePopupMenu();
	AppendMenuW(fViewMenu, MF_STRING, IDM_FULL_SCREEN, L"&Full screen");
	HMENU fullScreen = CreatePopupMenu();
	AppendMenuW(fullScreen, MF_STRING, IDM_FULL_SCREEN_FILL,
		L"&Fill the screen");
	AppendMenuW(fullScreen, MF_STRING, IDM_FULL_SCREEN_WHOLE,
		L"&Whole multiples");
	AppendMenuW(fullScreen, MF_STRING, IDM_FULL_SCREEN_ACTUAL,
		L"&Actual size");
	AppendMenuW(fViewMenu, MF_POPUP, (UINT_PTR)fullScreen,
		L"Full screen &scaling");
	AppendMenuW(fViewMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(fViewMenu, MF_STRING, IDM_SCALE_1, L"&100%");
	AppendMenuW(fViewMenu, MF_STRING, IDM_SCALE_2, L"&200%");
	AppendMenuW(fViewMenu, MF_STRING, IDM_SCALE_FIT, L"Fit &window");
	AppendMenuW(fViewMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(fViewMenu, MF_STRING, IDM_SHOW_LEDS,
		L"Show front panel &LEDs");
	HMENU styles = CreatePopupMenu();
	AppendMenuW(styles, MF_STRING, IDM_LEDS_AUTHENTIC,
		L"&Authentic (lime and amber)");
	AppendMenuW(styles, MF_STRING, IDM_LEDS_RED_GREEN, L"&Red and green");
	AppendMenuW(fViewMenu, MF_POPUP, (UINT_PTR)styles, L"LED &colors");
	AppendMenuW(fMenuBar, MF_POPUP, (UINT_PTR)fViewMenu, L"&View");

	fToolsMenu = CreatePopupMenu();
	HMENU consoles = CreatePopupMenu();
	for (int i = 0; i < 4; i++) {
		AppendMenuW(consoles, MF_STRING, IDM_CONSOLE + i,
			Format(L"Serial &%d", i + 1).c_str());
	}
	AppendMenuW(fToolsMenu, MF_POPUP, (UINT_PTR)consoles,
		L"Serial &console");
	AppendMenuW(fToolsMenu, MF_STRING, IDM_SCREENSHOT,
		L"Save &screenshot...");
	AppendMenuW(fMenuBar, MF_POPUP, (UINT_PTR)fToolsMenu, L"&Tools");

	_BuildProfileMenu();
	_BuildMediaMenu();
}


void
MainWindow::_BuildProfileMenu()
{
	while (GetMenuItemCount(fProfileMenu) > 0)
		DeleteMenu(fProfileMenu, 0, MF_BYPOSITION);
	std::vector<std::string> names = Settings::GetProfiles();
	for (size_t i = 0; i < names.size() && i <= IDM_PROFILE_LAST
			- IDM_PROFILE_FIRST; i++) {
		UINT flags = MF_STRING | (names[i] == fSettings.profile
			? MF_CHECKED : 0);
		AppendMenuW(fProfileMenu, flags, IDM_PROFILE_FIRST + i,
			Widen(names[i]).c_str());
	}
	AppendMenuW(fProfileMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(fProfileMenu, MF_STRING, IDM_NEW_PROFILE,
		L"&New profile...");
	AppendMenuW(fProfileMenu, MF_STRING, IDM_RENAME_PROFILE,
		Widen("&Rename \"" + fSettings.profile + "\"...").c_str());
	AppendMenuW(fProfileMenu, MF_STRING | (names.size() > 1 ? 0
		: MF_GRAYED), IDM_DELETE_PROFILE,
		Widen("&Delete \"" + fSettings.profile + "\"...").c_str());
}


void
MainWindow::_BuildMediaMenu()
{
	while (GetMenuItemCount(fMediaMenu) > 0)
		DeleteMenu(fMediaMenu, 0, MF_BYPOSITION);
	// both models have a floppy drive
	AppendMenuW(fMediaMenu, MF_STRING, IDM_INSERT_FLOPPY,
		L"Insert &floppy...");
	AppendMenuW(fMediaMenu, MF_STRING
		| (fSettings.machine.floppyImage.empty() ? MF_GRAYED : 0),
		IDM_EJECT_FLOPPY, L"&Eject floppy");
	std::vector<int> drives = _CdDrives();
	if (!drives.empty())
		AppendMenuW(fMediaMenu, MF_SEPARATOR, 0, nullptr);
	for (int id : drives) {
		const std::string& image = fSettings.machine.scsi[id].image;
		std::wstring insert = drives.size() == 1 ? L"Insert &CD..."
			: Format(L"Insert CD (SCSI ID %d)...", id);
		std::wstring eject = image.empty()
			? (drives.size() == 1 ? std::wstring(L"Eject CD")
				: Format(L"Eject CD (SCSI ID %d)", id))
			: Format(L"Eject \"%ls\"", Widen(Leaf(image)).c_str());
		AppendMenuW(fMediaMenu, MF_STRING, IDM_CD_INSERT + id,
			insert.c_str());
		AppendMenuW(fMediaMenu, MF_STRING | (image.empty() ? MF_GRAYED : 0),
			IDM_CD_EJECT + id, eject.c_str());
	}
	if (drives.empty()) {
		AppendMenuW(fMediaMenu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(fMediaMenu, MF_STRING | MF_GRAYED, 0,
			L"No CD-ROM drive (Preferences > Storage)");
	}
}


void
MainWindow::_UpdateMenus()
{
	bool on = fPowered || fStarting;
	MENUITEMINFOW item = {};
	item.cbSize = sizeof(item);
	item.fMask = MIIM_STRING;
	item.dwTypeData = (LPWSTR)(on ? L"&Power off" : L"&Power on");
	SetMenuItemInfoW(fMachineMenu, IDM_POWER, FALSE, &item);
	CheckMenuItem(fMachineMenu, IDM_PAUSE,
		fEmulator.IsPaused() ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(fMachineMenu, IDM_CTRL_ALT_DELETE,
		fPowered ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(fMachineMenu, IDM_INTERRUPT,
		fPowered ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(fMachineMenu, IDM_PAUSE, fPowered ? MF_ENABLED : MF_GRAYED);

	std::wstring fullScreen = Format(L"&Full screen\t%ls+F", _HostKeyName());
	item.dwTypeData = (LPWSTR)fullScreen.c_str();
	SetMenuItemInfoW(fViewMenu, IDM_FULL_SCREEN, FALSE, &item);
	CheckMenuItem(fViewMenu, IDM_FULL_SCREEN,
		fFullScreen ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuRadioItem(fViewMenu, IDM_FULL_SCREEN_FILL,
		IDM_FULL_SCREEN_ACTUAL, IDM_FULL_SCREEN_FILL
			+ std::max(0, std::min(2, fSettings.fullScreenScale)),
		MF_BYCOMMAND);
	CheckMenuRadioItem(fViewMenu, IDM_SCALE_1, IDM_SCALE_FIT,
		fSettings.scale == 0 ? IDM_SCALE_FIT
			: fSettings.scale == 2 ? IDM_SCALE_2 : IDM_SCALE_1,
		MF_BYCOMMAND);
	CheckMenuItem(fViewMenu, IDM_SHOW_LEDS,
		fSettings.showLeds ? MF_CHECKED : MF_UNCHECKED);
	// the Hobbit prototype's kernel drives no front panel lights
	EnableMenuItem(fViewMenu, IDM_SHOW_LEDS,
		fSettings.machine.model != MachineConfig::kHobbit
			? MF_ENABLED : MF_GRAYED);
	CheckMenuRadioItem(fViewMenu, IDM_LEDS_AUTHENTIC, IDM_LEDS_RED_GREEN,
		fSettings.ledStyle == kLedStyleRedGreen ? IDM_LEDS_RED_GREEN
			: IDM_LEDS_AUTHENTIC, MF_BYCOMMAND);
	_BuildMediaMenu();
	if (fWindow != nullptr && !fFullScreen)
		DrawMenuBar(fWindow);
}


// #pragma mark - power


bool
MainWindow::PowerOn()
{
	if (fPowered || fStarting)
		return true;
	// what was posted while the machine was off: it starts from the
	// settings
	fEmulator.DiscardCommands();
	fProblemsAlerted.clear();
	MachineConfig config = fSettings.MachineConfigForRun();
	for (const MachineConfig::ScsiTarget& target : config.scsi) {
		if (target.kind == MachineConfig::ScsiTarget::kSharedFolder) {
			fStarting = true;
			_PrepareSharedFolders(config);
			_UpdateMenus();
			_UpdateStatus();
			return true;
		}
	}
	return _StartMachine(config);
}


bool
MainWindow::_StartMachine(const MachineConfig& config)
{
	// serial ports
	fSerialBackends.clear();
	bebox::SerialBackend* serial[4] = { nullptr, nullptr, nullptr, nullptr };
	for (int i = 0; i < 4; i++) {
		const std::string& destination = fSettings.serial[i];
		if (destination == "console") {
			fConsoles[i]->Clear();
			serial[i] = fConsoles[i];
		} else if (!destination.empty()) {
			std::string error;
			std::unique_ptr<bebox::SerialBackend> backend
				= bebox::CreateSerialBackend(destination, error);
			if (!backend) {
				ShowMessage(fWindow, Widen("Serial port "
					+ std::to_string(i + 1) + " is not connected: " + error),
					MB_ICONWARNING);
				continue;
			}
			serial[i] = backend.get();
			fSerialBackends.push_back(std::move(backend));
		}
	}
	// Ethernet: the user mode network (NAT)
	fNetwork.reset();
	if (fSettings.ethernet) {
		bebox::UserNetworkConfig networkConfig;
		std::string error;
		if (bebox::ParsePortForwards(fSettings.portForwards,
				networkConfig.forwards, error))
			fNetwork = bebox::CreateUserNetwork(networkConfig, error);
		if (!fNetwork) {
			ShowMessage(fWindow, Widen("The Ethernet card is not connected: "
				+ error), MB_ICONWARNING);
		}
	}
	bebox::NetworkBackend* network = fNetwork.get();
	WinAudio* audio = nullptr;
	if (fSettings.soundEnabled) {
		fAudio.SetVolume(fSettings.soundVolume / 100.0f);
		if (fAudio.Start())
			audio = &fAudio;
	}
	ScreenView* screen = &fScreen;
	HWND window = fWindow;
	std::string error;
	if (!fEmulator.Start(config, error,
			[screen, audio, serial, network, window](bebox::Machine& machine) {
				machine.SetVideoSink(screen);
				machine.SetAudioSink(audio);
				if (network != nullptr)
					machine.SetNetworkBackend(network);
				for (int i = 0; i < 4; i++) {
					if (serial[i] != nullptr)
						machine.SetSerialBackend(i, serial[i]);
				}
				// BeOS ejects a disc: the drive shows empty
				for (int id = 0; id < 7; id++) {
					bebox::ScsiCd* cd = machine.CdRom(id);
					if (cd == nullptr)
						continue;
					cd->SetEjectCallback([window, id](const std::string& path) {
						PostMessageW(window, kMsgCdEjected, id,
							(LPARAM)new std::string(path));
					});
				}
			})) {
		fAudio.Stop();
		fSerialBackends.clear();
		fNetwork.reset();
		// nothing ran on the shared folders' volumes
		fSharedFolders.clear();
		std::string hint = fSettings.machine.model == MachineConfig::kHobbit
			? "Check the disk images (and the Hobbit kernel) in the "
				"preferences."
			: "Check the ROM folder and the disk images in the preferences "
				"(Machine > Preferences > ROM & NVRAM).";
		ShowMessage(fWindow, Widen("The machine could not be started:\n\n"
			+ error + "\n\n" + hint), MB_ICONERROR);
		return false;
	}
	fEmulator.SetSpeedLimit(fSettings.speedLimit);
	fPowered = true;
	fScreen.SetPowered(true);
	// joysticks: their ranges, polled at 50 Hz
	for (int i = 0; i < 4; i++) {
		Stick& stick = fSticks[i];
		stick.device = fSettings.joystick[i].empty() ? -1
			: atoi(fSettings.joystick[i].c_str());
		JOYCAPSW caps = {};
		if (stick.device >= 0 && joyGetDevCapsW(stick.device, &caps,
				sizeof(caps)) == JOYERR_NOERROR) {
			stick.minX = caps.wXmin;
			stick.maxX = std::max(caps.wXmax, caps.wXmin + 1);
			stick.minY = caps.wYmin;
			stick.maxY = std::max(caps.wYmax, caps.wYmin + 1);
		} else
			stick.device = -1;
	}
	SetTimer(fWindow, kJoystickTimer, 20, nullptr);
	_UpdateMenus();
	_UpdateStatus();
	SetFocus(fWindow);
	return true;
}


void
MainWindow::PowerOff()
{
	if (fStarting) {
		// the shared folders are still being copied: the machine does not
		// start (_SharedFoldersReady drops them)
		fStarting = false;
		fEmulator.DiscardCommands();
		_UpdateMenus();
		_UpdateStatus();
		return;
	}
	if (!fPowered)
		return;
	fEmulator.Stop();
	KillTimer(fWindow, kJoystickTimer);
	fScreen.SetPowered(false);
	fAudio.Stop();
	fSerialBackends.clear();
	fNetwork.reset();
	_SyncSharedFolders(true);
	fPowered = false;
	_ReleaseAllKeys();
	fLeds[0] = fLeds[1] = fDisk = 0;
	InvalidateRect(fWindow, nullptr, FALSE);
	_UpdateMenus();
	_UpdateStatus();
}


// Shared folders: a BFS volume per folder, built in the profile's folder
// at power on, BeOS's changes back to the folder at power off. The
// copies run on fFolderJobs one after the other (a folder goes back
// before it is copied again); the machine starts when its folders are
// ready.
void
MainWindow::_PrepareSharedFolders(const MachineConfig& config)
{
	std::shared_ptr<FolderStart> start(new FolderStart);
	start->generation = ++fFolderStarts;
	start->config = config;
	start->profileDirectory = fSettings.ProfileDirectory();
	fFolderStart = start;
	HWND window = fWindow;
	fFolderJobs.Add([start, window]() {
		for (int id = 0; id < 7; id++) {
			MachineConfig::ScsiTarget& target = start->config.scsi[id];
			if (target.kind != MachineConfig::ScsiTarget::kSharedFolder)
				continue;
			std::string image = start->profileDirectory + "\\shared-scsi"
				+ std::to_string(id) + ".qcow2";
			std::unique_ptr<bebox::SharedFolder> share(
				new bebox::SharedFolder(target.image, image));
			std::string report;
			std::string error;
			if (!share->Prepare(report, error)) {
				start->messages += error + "\n\n";
				target.kind = MachineConfig::ScsiTarget::kNone;
				continue;
			}
			if (!report.empty())
				start->messages += report + "\n\n";
			target.image = image;
			target.readOnly = false;
			start->folders.push_back(std::move(share));
		}
		PostMessageW(window, kMsgFoldersReady, start->generation, 0);
	});
}


void
MainWindow::_SharedFoldersReady(int generation)
{
	std::shared_ptr<FolderStart> start = fFolderStart;
	if (!start || generation != start->generation)
		return;
	fFolderStart.reset();
	// powered off meanwhile: nothing ran on the volumes, they are dropped
	if (!fStarting)
		return;
	fStarting = false;
	fSharedFolders = std::move(start->folders);
	if (!start->messages.empty()) {
		while (!start->messages.empty() && start->messages.back() == '\n')
			start->messages.pop_back();
		ShowMessage(fWindow, Widen(start->messages));
	}
	// the floppy and CDs changed meanwhile go in
	MachineConfig& config = start->config;
	config.floppyImage = fSettings.machine.floppyImage;
	config.floppyWriteProtected = fSettings.machine.floppyWriteProtected;
	for (int id = 0; id < 7; id++) {
		if (config.scsi[id].kind == MachineConfig::ScsiTarget::kCdRom
			&& fSettings.machine.scsi[id].kind
				== MachineConfig::ScsiTarget::kCdRom)
			config.scsi[id].image = fSettings.machine.scsi[id].image;
	}
	_StartMachine(config);
	_UpdateMenus();
	_UpdateStatus();
}


// BeOS's changes go back to the folders (the machine has stopped).
void
MainWindow::_SyncSharedFolders(bool interactive)
{
	if (fSharedFolders.empty())
		return;
	auto folders = std::make_shared<
		std::vector<std::unique_ptr<bebox::SharedFolder> > >(
			std::move(fSharedFolders));
	fSharedFolders.clear();
	fFolderSyncs++;
	HWND window = fWindow;
	fFolderJobs.Add([folders, interactive, window]() {
		std::string messages;
		for (auto& share : *folders) {
			std::string report;
			std::string error;
			if (!share->Sync(report, error))
				messages += error + "\n\n";
			else if (!report.empty())
				messages += report + "\n\n";
		}
		folders->clear();
		while (!messages.empty() && messages.back() == '\n')
			messages.pop_back();
		PostMessageW(window, kMsgFoldersSynced, 0, interactive
			&& !messages.empty() ? (LPARAM)new std::string(messages) : 0);
	});
}


// #pragma mark - settings and profiles


void
MainWindow::_ApplySettings()
{
	fEmulator.SetSpeedLimit(fSettings.speedLimit);
	fAudio.SetVolume(fSettings.soundVolume / 100.0f);
	fScreen.SetScale(fSettings.scale);
	fScreen.SetMouseOptions(fSettings.mouseSpeed, fSettings.swapMouseButtons,
		fSettings.captureOnClick);
	_BuildProfileMenu();
	_UpdateMenus();
	_Layout();
	_FitWindow();
	_UpdateStatus();
	InvalidateRect(fWindow, nullptr, FALSE);
}


void
MainWindow::_ShowPreferences()
{
	bool running = fPowered || fStarting;
	if (fScreen.IsMouseCaptured())
		fScreen.SetMouseCaptured(false);
	Settings old = fSettings;
	if (!ShowPreferences(fWindow, fSettings, running))
		return;
	fSettings.Save();
	bool restart = running && NeedsRestart(old, fSettings);
	if (running && !restart && fPowered) {
		// the floppy and the CDs change while the machine runs
		const MachineConfig& now = fSettings.machine;
		if (now.floppyImage != old.machine.floppyImage
			|| now.floppyWriteProtected != old.machine.floppyWriteProtected) {
			if (now.floppyImage.empty())
				_EjectFloppy();
			else
				_InsertFloppy(now.floppyImage);
		}
		for (int id = 0; id < 7; id++) {
			if (now.scsi[id].kind != MachineConfig::ScsiTarget::kCdRom
				|| now.scsi[id].image == old.machine.scsi[id].image)
				continue;
			if (now.scsi[id].image.empty())
				_EjectCd(id);
			else if (!_InsertCd(id, now.scsi[id].image))
				fSettings.machine.scsi[id].image.clear();
		}
		if (fSettings.soundEnabled != old.soundEnabled) {
			if (fSettings.soundEnabled) {
				fAudio.Start();
				fEmulator.Post([this](bebox::Machine& machine) {
					machine.SetAudioSink(&fAudio);
				});
			} else {
				fEmulator.Post([](bebox::Machine& machine) {
					machine.SetAudioSink(nullptr);
				});
			}
		}
		for (int i = 0; i < 4; i++) {
			if (fSettings.joystick[i] != old.joystick[i]) {
				// picked up at the next power on; until then, no stick
				fSticks[i].device = -1;
			}
		}
	}
	_ApplySettings();
	if (restart) {
		PowerOff();
		PowerOn();
	}
}


void
MainWindow::_SwitchProfile(const std::string& name, bool create)
{
	if (name == fSettings.profile && !create)
		return;
	if (create) {
		// a copy of the running machine keeps running, with the copy's
		// flash and NVRAM files from now on
		fSettings.CopyProfile(name);
		if (fPowered) {
			std::string flash = fSettings.FlashPath();
			std::string nvram = fSettings.NvramPath();
			fEmulator.Post([flash, nvram](bebox::Machine& machine) {
				machine.SetPersistentPaths(flash, nvram);
			});
		}
		_ApplySettings();
		return;
	}
	bool wasOn = fPowered || fStarting;
	PowerOff();
	fSettings.SwitchProfile(name);
	_ApplySettings();
	if (wasOn)
		PowerOn();
}


void
MainWindow::_NewProfile()
{
	std::string name = AskProfileName(fWindow, L"New profile",
		Widen("A copy of \"" + fSettings.profile + "\" named:").c_str(), "");
	if (!name.empty())
		_SwitchProfile(name, true);
}


void
MainWindow::_RenameProfile()
{
	std::string name = AskProfileName(fWindow, L"Rename profile",
		L"New name:", fSettings.profile);
	if (name.empty() || name == fSettings.profile)
		return;
	if (!fSettings.RenameProfile(name)) {
		ShowMessage(fWindow, L"The profile could not be renamed.",
			MB_ICONERROR);
		return;
	}
	if (fPowered) {
		std::string flash = fSettings.FlashPath();
		std::string nvram = fSettings.NvramPath();
		fEmulator.Post([flash, nvram](bebox::Machine& machine) {
			machine.SetPersistentPaths(flash, nvram);
		});
	}
	_ApplySettings();
}


void
MainWindow::_DeleteProfile()
{
	std::vector<std::string> names = Settings::GetProfiles();
	if (names.size() <= 1)
		return;
	if (!AskYesNo(fWindow, Widen("Delete the profile \"" + fSettings.profile
			+ "\" with its boot ROM flash, NVRAM and shared folder volumes? "
			"Disk images stay where they are.")))
		return;
	std::string victim = fSettings.profile;
	std::string next = names[0] == victim ? names[1] : names[0];
	PowerOff();
	fFolderJobs.Drain();
	fSettings.SwitchProfile(next);
	if (!Settings::DeleteProfile(victim)) {
		ShowMessage(fWindow, Widen("The profile \"" + victim + "\" could not "
			"be deleted completely."), MB_ICONWARNING);
	}
	_ApplySettings();
}


// #pragma mark - commands


void
MainWindow::_Command(int command)
{
	if (command >= IDM_PROFILE_FIRST && command <= IDM_PROFILE_LAST) {
		std::vector<std::string> names = Settings::GetProfiles();
		size_t index = command - IDM_PROFILE_FIRST;
		if (index < names.size())
			_SwitchProfile(names[index], false);
		return;
	}
	if (command >= IDM_CD_INSERT && command < IDM_CD_INSERT + 7) {
		int id = command - IDM_CD_INSERT;
		std::string path = ChooseOpenFile(fWindow,
			Format(L"BeBox: CD-ROM (SCSI ID %d)", id).c_str(), kCdFilters,
			fSettings.machine.scsi[id].image);
		if (!path.empty() && _InsertCd(id, path))
			fSettings.Save();
		return;
	}
	if (command >= IDM_CD_EJECT && command < IDM_CD_EJECT + 7) {
		_EjectCd(command - IDM_CD_EJECT);
		fSettings.Save();
		return;
	}
	if (command >= IDM_CONSOLE && command < IDM_CONSOLE + 4) {
		_ShowConsole(command - IDM_CONSOLE);
		return;
	}
	switch (command) {
		case IDM_POWER:
			if (fPowered || fStarting)
				PowerOff();
			else
				PowerOn();
			break;
		case IDM_RESET:
			if (!fPowered) {
				PowerOn();
				break;
			}
			_ReleaseAllKeys();
			fEmulator.Post([](bebox::Machine& machine) {
				machine.PowerOn();
			});
			break;
		case IDM_INTERRUPT:
			fEmulator.Post([](bebox::Machine& machine) {
				machine.PressInterruptSwitch();
			});
			break;
		case IDM_CTRL_ALT_DELETE:
			// BeOS R3 and later show their Team Monitor (twice: restart)
			if (fPowered)
				_PressKeyCombination({ "LeftControl", "LeftAlt", "Delete" });
			break;
		case IDM_BOOT_MENU:
		case IDM_BOOT_DEBUG:
		{
			if (!fPowered && !PowerOn())
				break;
			_ReleaseAllKeys();
			// F1 sends the kernel's debug output to serial port 4; it goes
			// up when the boot ROM starts writing there.
			bool debug = command == IDM_BOOT_DEBUG;
			fEmulator.Post([debug](bebox::Machine& machine) {
				machine.PowerOn();
				if (debug)
					machine.HoldDebugOutputKey();
			});
			if (debug) {
				if (fSettings.serial[3] == "console")
					_ShowConsole(3);
				break;
			}
			// The boot menu: DR7/DR8 ROMs: any key; PR and later: left
			// Shift, down when the ROM looks at the keyboard (about 6 s
			// after power on with 32 MB; the memory test before takes
			// 1/16 s per MB more at 66 MHz, half that at 133 MHz).
			const MachineConfig& config = fSettings.machine;
			double memoryTest = config.ramMB > 32
				? (config.ramMB - 32) / (config.model
					== MachineConfig::kBeBox133 ? 32.0 : 16.0) : 0;
			_HoldKeyDuringBoot(0x12, 8 + memoryTest);
			break;
		}
		case IDM_PAUSE:
			fEmulator.SetPaused(!fEmulator.IsPaused());
			_UpdateMenus();
			_UpdateStatus();
			break;
		case IDM_PREFERENCES:
			_ShowPreferences();
			break;
		case IDM_ABOUT:
			_About();
			break;
		case IDM_QUIT:
			PostMessageW(fWindow, WM_CLOSE, 0, 0);
			break;
		case IDM_NEW_PROFILE:
			_NewProfile();
			break;
		case IDM_RENAME_PROFILE:
			_RenameProfile();
			break;
		case IDM_DELETE_PROFILE:
			_DeleteProfile();
			break;
		case IDM_INSERT_FLOPPY:
		{
			std::string path = ChooseOpenFile(fWindow, L"BeBox: Floppy",
				kFloppyFilters, fSettings.machine.floppyImage);
			if (!path.empty()) {
				_InsertFloppy(path);
				fSettings.Save();
			}
			break;
		}
		case IDM_EJECT_FLOPPY:
			_EjectFloppy();
			fSettings.Save();
			break;
		case IDM_FULL_SCREEN:
			_SetFullScreen(!fFullScreen);
			break;
		case IDM_FULL_SCREEN_FILL:
		case IDM_FULL_SCREEN_WHOLE:
		case IDM_FULL_SCREEN_ACTUAL:
			fSettings.fullScreenScale = command - IDM_FULL_SCREEN_FILL;
			fScreen.SetFullScreen(fFullScreen, fSettings.fullScreenScale);
			_UpdateMenus();
			fSettings.Save();
			break;
		case IDM_SCALE_1:
		case IDM_SCALE_2:
		case IDM_SCALE_FIT:
			fSettings.scale = command == IDM_SCALE_FIT ? 0
				: command == IDM_SCALE_2 ? 2 : 1;
			fScreen.SetScale(fSettings.scale);
			_FitWindow();
			_UpdateMenus();
			fSettings.Save();
			break;
		case IDM_SHOW_LEDS:
			fSettings.showLeds = !fSettings.showLeds;
			_Layout();
			_FitWindow();
			_UpdateMenus();
			fSettings.Save();
			break;
		case IDM_LEDS_AUTHENTIC:
		case IDM_LEDS_RED_GREEN:
			fSettings.ledStyle = command == IDM_LEDS_RED_GREEN
				? kLedStyleRedGreen : kLedStyleAuthentic;
			InvalidateRect(fWindow, nullptr, FALSE);
			_UpdateMenus();
			fSettings.Save();
			break;
		case IDM_SCREENSHOT:
			_SaveScreenshot();
			break;
	}
}


void
MainWindow::_HoldKeyDuringBoot(bebox::u32 set2, double seconds)
{
	// The boot ROM checks the keyboard while the logo is shown; the hold
	// is timed in emulated time (the keyboard repeats the key meanwhile).
	fEmulator.Post([set2, seconds](bebox::Machine& machine) {
		machine.Keyboard().HoldKey(set2,
			(bebox::u64)(seconds * machine.TicksPerSecond()));
	});
}


// Presses the keys in order and releases them in reverse, 50 ms apart.
void
MainWindow::_PressKeyCombination(const std::vector<const char*>& keys)
{
	std::vector<std::pair<bebox::u32, bool> > events;
	for (const char* key : keys)
		events.push_back(std::make_pair(bebox::KeyNameToSet2(key), true));
	for (auto key = keys.rbegin(); key != keys.rend(); key++)
		events.push_back(std::make_pair(bebox::KeyNameToSet2(*key), false));
	fEmulator.Post([events](bebox::Machine& machine) {
		machine.Keyboard().TypeKeys(events,
			machine.MicrosecondsToTicks(50000));
	});
}


// Any thread: errors of the storage and the host (a disk image, the flash
// or NVRAM file that cannot be written) are shown in the window too.
void
MainWindow::_LogSink(bebox::LogCategory category, bebox::LogLevel level,
	const char* message, void* cookie)
{
	bebox::Log::DefaultSink(category, level, message, nullptr);
	MainWindow* window = (MainWindow*)cookie;
	if (level == bebox::kLogError && (category == bebox::kLogStorage
			|| category == bebox::kLogHost) && window->fWindow != nullptr) {
		PostMessageW(window->fWindow, kMsgProblem, 0,
			(LPARAM)new std::string(message));
	}
}


// In the status line for 30 s, in a message box the first time since
// power on.
void
MainWindow::_ShowProblem(const std::string& text)
{
	fProblem = text;
	fProblemTime = GetTickCount64();
	_UpdateStatus();
	if (!fProblemsAlerted.insert(text).second || text.empty())
		return;
	std::string alert = text;
	alert[0] = (char)toupper((unsigned char)alert[0]);
	ShowMessage(fWindow, Widen(alert + "."), MB_ICONWARNING);
}


// #pragma mark - media


void
MainWindow::_InsertFloppy(const std::string& path)
{
	fSettings.machine.floppyImage = path;
	bool writeProtected = fSettings.machine.floppyWriteProtected;
	fEmulator.Post([path, writeProtected](bebox::Machine& machine) {
		std::string error;
		std::unique_ptr<bebox::DiskImage> image
			= bebox::DiskImage::Open(path, writeProtected, error);
		if (image && machine.Floppy() != nullptr)
			machine.Floppy()->InsertDisk(machine.FloppyUnit(),
				std::move(image), writeProtected, error);
	});
	_UpdateMenus();
	_UpdateStatus();
}


void
MainWindow::_EjectFloppy()
{
	fSettings.machine.floppyImage.clear();
	fEmulator.Post([](bebox::Machine& machine) {
		if (machine.Floppy() != nullptr)
			machine.Floppy()->EjectDisk(machine.FloppyUnit());
	});
	_UpdateMenus();
	_UpdateStatus();
}


std::vector<int>
MainWindow::_CdDrives() const
{
	std::vector<int> drives;
	for (int id = 0; id < 7; id++) {
		if (fSettings.machine.scsi[id].kind == MachineConfig::ScsiTarget::kCdRom)
			drives.push_back(id);
	}
	return drives;
}


bool
MainWindow::_InsertCd(int id, const std::string& path)
{
	std::string error;
	std::unique_ptr<bebox::CdImage> image = bebox::CdImage::Open(path, error);
	if (!image) {
		ShowMessage(fWindow, Widen("The CD image could not be opened: "
			+ error), MB_ICONERROR);
		return false;
	}
	fSettings.machine.scsi[id].image = path;
	_ChangeCd(id, std::move(image));
	return true;
}


void
MainWindow::_EjectCd(int id)
{
	fSettings.machine.scsi[id].image.clear();
	_ChangeCd(id, nullptr);
}


// Into the running machine: a disc already in comes out first, the new
// one goes in a few seconds later (ScsiCd::Change()).
void
MainWindow::_ChangeCd(int id, std::unique_ptr<bebox::CdImage> image)
{
	if (fPowered) {
		// commands are copied: share the image
		auto disc = std::make_shared<std::unique_ptr<bebox::CdImage> >(
			std::move(image));
		fEmulator.Post([id, disc](bebox::Machine& machine) {
			if (bebox::ScsiCd* cd = machine.CdRom(id))
				cd->Change(std::move(*disc));
		});
	}
	_UpdateMenus();
	_UpdateStatus();
}


// Files dropped on the window: CD images into the first CD-ROM drive (an
// empty one if there is), floppy images into the floppy drive.
void
MainWindow::_DroppedFiles(HDROP drop)
{
	static const char* const kCdExtensions[] = { "iso", "cue", "toast",
		"bin", nullptr };
	static const char* const kFloppyExtensions[] = { "img", "ima", "dsk",
		nullptr };
	UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
	for (UINT i = 0; i < count; i++) {
		wchar_t file[MAX_PATH * 4];
		if (DragQueryFileW(drop, i, file, sizeof(file) / sizeof(file[0]))
				== 0)
			continue;
		std::string path = Narrow(file);
		if (HasExtension(path, kCdExtensions)) {
			std::vector<int> drives = _CdDrives();
			if (drives.empty()) {
				ShowMessage(fWindow, L"There is no CD-ROM drive: give a SCSI "
					L"ID one in Machine > Preferences > Storage.");
				continue;
			}
			int target = drives[0];
			for (int id : drives) {
				if (fSettings.machine.scsi[id].image.empty()) {
					target = id;
					break;
				}
			}
			_InsertCd(target, path);
		} else if (HasExtension(path, kFloppyExtensions)) {
			WIN32_FILE_ATTRIBUTE_DATA data;
			if (GetFileAttributesExW(file, GetFileExInfoStandard, &data)
				&& data.nFileSizeHigh == 0
				&& data.nFileSizeLow <= 2949120)
				_InsertFloppy(path);
			else {
				ShowMessage(fWindow, Widen(Leaf(path) + " is too large for a "
					"floppy: disk images go on a SCSI ID (Preferences > "
					"Storage)."));
			}
		}
	}
	DragFinish(drop);
	fSettings.Save();
}


// #pragma mark - window


int
MainWindow::_LedWidth() const
{
	UINT dpi = fWindow != nullptr ? GetDpiForWindow(fWindow) : 96;
	return MulDiv(kLedColumnWidth, dpi, 96);
}


bool
MainWindow::_ShowLeds() const
{
	return fSettings.showLeds
		&& fSettings.machine.model != MachineConfig::kHobbit;
}


void
MainWindow::_Layout()
{
	if (fWindow == nullptr || fScreen.Window() == nullptr)
		return;
	RECT client;
	GetClientRect(fWindow, &client);
	int statusHeight = 0;
	if (fStatus != nullptr && IsWindowVisible(fStatus)) {
		SendMessageW(fStatus, WM_SIZE, 0, 0);
		RECT status;
		GetWindowRect(fStatus, &status);
		statusHeight = status.bottom - status.top;
		// the media part: as wide as its text
		HDC dc = GetDC(fStatus);
		HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(fStatus,
			WM_GETFONT, 0, 0));
		SIZE size = { 0, 0 };
		std::wstring media = fMediaText.empty() ? L"Floppy: empty"
			: fMediaText;
		GetTextExtentPoint32W(dc, media.c_str(), (int)media.size(), &size);
		SelectObject(dc, old);
		ReleaseDC(fStatus, dc);
		int parts[2] = { std::max(80, (int)(client.right - size.cx - 32)), -1 };
		SendMessageW(fStatus, SB_SETPARTS, 2, (LPARAM)parts);
	}
	int led = _ShowLeds() ? _LedWidth() : 0;
	MoveWindow(fScreen.Window(), led, 0, std::max(0,
		(int)client.right - 2 * led), std::max(0, (int)client.bottom
			- statusHeight), TRUE);
	InvalidateRect(fWindow, nullptr, FALSE);
}


// The window as large as the picture at the chosen scale (not for "fit
// window" and full screen).
void
MainWindow::_FitWindow()
{
	if (fFullScreen || fSettings.scale == 0 || fWindow == nullptr
		|| IsZoomed(fWindow))
		return;
	int width, height;
	fScreen.FrameSize(width, height);
	int led = _ShowLeds() ? _LedWidth() : 0;
	int statusHeight = 0;
	if (fStatus != nullptr) {
		RECT status;
		GetWindowRect(fStatus, &status);
		statusHeight = status.bottom - status.top;
	}
	RECT rect = { 0, 0, width * fSettings.scale + 2 * led,
		height * fSettings.scale + statusHeight };
	UINT dpi = GetDpiForWindow(fWindow);
	AdjustWindowRectExForDpi(&rect, (DWORD)GetWindowLongPtrW(fWindow,
		GWL_STYLE), TRUE, (DWORD)GetWindowLongPtrW(fWindow, GWL_EXSTYLE), dpi);
	// a menu bar too narrow for its items wraps: add the lines it takes
	RECT client;
	GetClientRect(fWindow, &client);
	SetWindowPos(fWindow, nullptr, 0, 0, rect.right - rect.left,
		rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	RECT now;
	GetClientRect(fWindow, &now);
	int missing = height * fSettings.scale + statusHeight - now.bottom;
	if (missing > 0) {
		RECT window;
		GetWindowRect(fWindow, &window);
		SetWindowPos(fWindow, nullptr, 0, 0, window.right - window.left,
			window.bottom - window.top + missing, SWP_NOMOVE | SWP_NOZORDER
				| SWP_NOACTIVATE);
	}
}


// Full screen: no border, menu bar or status line; the screen (and the
// LEDs, if shown) fill the display. The window comes back after.
void
MainWindow::_SetFullScreen(bool fullScreen)
{
	if (fullScreen == fFullScreen)
		return;
	fFullScreen = fullScreen;
	if (fullScreen) {
		fWindowedPlacement.length = sizeof(fWindowedPlacement);
		GetWindowPlacement(fWindow, &fWindowedPlacement);
		fWindowedStyle = GetWindowLongPtrW(fWindow, GWL_STYLE);
		MONITORINFO monitor = {};
		monitor.cbSize = sizeof(monitor);
		GetMonitorInfoW(MonitorFromWindow(fWindow, MONITOR_DEFAULTTONEAREST),
			&monitor);
		SetMenu(fWindow, nullptr);
		ShowWindow(fStatus, SW_HIDE);
		SetWindowLongPtrW(fWindow, GWL_STYLE, (fWindowedStyle
			& ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
		const RECT& frame = monitor.rcMonitor;
		SetWindowPos(fWindow, HWND_TOP, frame.left, frame.top,
			frame.right - frame.left, frame.bottom - frame.top,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		fScreen.SetFullScreen(true, fSettings.fullScreenScale);
		fScreen.ShowHint(Format(L"%ls+F leaves full screen",
			_HostKeyName()));
	} else {
		SetWindowLongPtrW(fWindow, GWL_STYLE, fWindowedStyle);
		SetMenu(fWindow, fMenuBar);
		ShowWindow(fStatus, SW_SHOW);
		SetWindowPlacement(fWindow, &fWindowedPlacement);
		SetWindowPos(fWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE
			| SWP_NOZORDER | SWP_FRAMECHANGED);
		fScreen.SetFullScreen(false, fSettings.fullScreenScale);
		fScreen.ShowHint(L"");
		// the BeBox may have changed its resolution meanwhile
		_Layout();
		_FitWindow();
	}
	_Layout();
	_UpdateMenus();
}


void
MainWindow::_PaintLeds(HDC dc)
{
	if (!_ShowLeds())
		return;
	RECT client;
	GetClientRect(fWindow, &client);
	int statusHeight = 0;
	if (IsWindowVisible(fStatus)) {
		RECT status;
		GetWindowRect(fStatus, &status);
		statusHeight = status.bottom - status.top;
	}
	int led = _LedWidth();
	int bottom = client.bottom - statusHeight;
	RECT left = { 0, 0, led, bottom };
	RECT right = { client.right - led, 0, client.right, bottom };
	bool on = fPowered;
	DrawLedColumn(dc, left, false, fLeds[0], on, 0, fSettings.ledStyle);
	DrawLedColumn(dc, right, true, fLeds[1], on, fDisk, fSettings.ledStyle);
}


void
MainWindow::_UpdateStatus()
{
	if (fStatus == nullptr)
		return;
	bool problem = !fProblem.empty()
		&& GetTickCount64() - fProblemTime < 30000;
	std::wstring profile = Widen(fSettings.profile);
	std::wstring text;
	if (fStarting) {
		text = profile + kDash + L"starting: copying the shared folder…";
	} else if (!fPowered) {
		text = profile + kDash + L"powered off";
		if (fFolderSyncs > 0) {
			text += kDash;
			text += L"copying BeOS's changes to the shared folder…";
		}
	} else {
		bebox::EmulatorThread::Status status = fEmulator.GetStatus();
		bool changed = std::fabs(status.leds[0] - fLeds[0]) > 0.01f
			|| std::fabs(status.leds[1] - fLeds[1]) > 0.01f
			|| std::fabs(status.disk - fDisk) > 0.01f;
		fLeds[0] = status.leds[0];
		fLeds[1] = status.leds[1];
		fDisk = status.disk;
		if (changed && _ShowLeds()) {
			HDC dc = GetDC(fWindow);
			_PaintLeds(dc);
			ReleaseDC(fWindow, dc);
		}
		const wchar_t* model = fSettings.machine.model
				== MachineConfig::kBeBox133 ? L"BeBox Dual603e-133"
			: fSettings.machine.model == MachineConfig::kHobbit
				? L"Hobbit BeBox" : L"BeBox Dual603-66";
		text = profile + kDash + Format(L"%ls, %u MB", model,
			(unsigned)fSettings.machine.ramMB) + kDash
			+ Format(L"%.0f%%, %.0f MIPS", status.speedPercent, status.mips);
		if (status.paused)
			text += L"  (paused)";
		if (problem)
			;	// instead of the mouse hint
		else if (fScreen.IsMouseCaptured())
			text += kDash + std::wstring(_HostKeyName()) + L" releases the mouse";
		else if (fSettings.captureOnClick)
			text += kDash + std::wstring(L"click the screen to capture the "
				L"mouse");
		else
			text += kDash + std::wstring(_HostKeyName())
				+ L" captures the mouse";
	}
	if (problem)
		text += kDash + Widen(fProblem);
	if (text != fStatusText) {
		fStatusText = text;
		SendMessageW(fStatus, SB_SETTEXTW, 0, (LPARAM)fStatusText.c_str());
	}

	// media: what is inserted, the disk light
	const MachineConfig& machine = fSettings.machine;
	std::wstring media = L"Floppy: " + (machine.floppyImage.empty()
		? std::wstring(L"empty") : Widen(Leaf(machine.floppyImage)));
	for (int id : _CdDrives()) {
		const std::string& image = machine.scsi[id].image;
		media += Format(L"   CD %d: ", id) + (image.empty()
			? std::wstring(L"empty") : Widen(Leaf(image)));
	}
	bool diskActive = fPowered && fDisk > 0.05f;
	media += diskActive ? L"   ●" : L"   ○";
	if (media != fMediaText) {
		bool resize = media.size() != fMediaText.size();
		fMediaText = media;
		if (resize)
			_Layout();
		SendMessageW(fStatus, SB_SETTEXTW, 1, (LPARAM)fMediaText.c_str());
	}
}


// A click on the media part of the status line: the Media menu.
void
MainWindow::_StatusClicked(int part)
{
	if (part != 1)
		return;
	_BuildMediaMenu();
	POINT where;
	GetCursorPos(&where);
	TrackPopupMenu(fMediaMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, where.x,
		where.y, 0, fWindow, nullptr);
}


void
MainWindow::_SaveScreenshot()
{
	std::vector<bebox::u32> pixels;
	int width, height;
	if (!fScreen.CopyFrame(pixels, width, height))
		return;
	wchar_t* pictures = nullptr;
	std::string directory;
	if (SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures)
			== S_OK) {
		directory = Narrow(pictures);
		CoTaskMemFree(pictures);
	}
	time_t now = time(nullptr);
	struct tm parts;
	localtime_s(&parts, &now);
	char name[64];
	strftime(name, sizeof(name), "BeBox %Y-%m-%d %H%M%S.png", &parts);
	std::string path = ChooseSaveFile(fWindow, L"BeBox: Save screenshot",
		L"PNG images (*.png)\0*.png\0", L"png", directory + "\\" + name);
	if (path.empty())
		return;
	if (!bebox::WritePng(path, pixels.data(), width, height, width)) {
		ShowMessage(fWindow, Widen("Cannot write " + path + "."),
			MB_ICONERROR);
	}
}


void
MainWindow::_ShowConsole(int port)
{
	fConsoles[port]->Show(fWindow);
	if (fSettings.serial[port] != "console") {
		ShowMessage(fConsoles[port]->Window(), Format(L"Serial port %d is not "
			L"connected to its console window: choose \"Console window\" for "
			L"it in Preferences > Serial ports.", port + 1));
	}
}


void
MainWindow::_About()
{
	ShowMessage(fWindow, L"BeBox 1.2 for Windows\n\n"
		L"An emulator of Be Inc.'s BeBox, the dual-processor computer that "
		L"BeOS was born on: the Dual603-66 and Dual603e-133, and the Hobbit "
		L"prototypes before them.\n\n"
		L"Copyright 2026, the BeBox emulator authors. MIT License.\n"
		L"User mode networking: libslirp (BSD license).");
}


// #pragma mark - keyboard


bebox::u32
MainWindow::_HostKeyCode() const
{
	int index = fSettings.hostKey >= 0 && fSettings.hostKey < 4
		? fSettings.hostKey : kHostKeyRightControl;
	return kHostKeyCodes[index];
}


const wchar_t*
MainWindow::_HostKeyName() const
{
	int index = fSettings.hostKey >= 0 && fSettings.hostKey < 4
		? fSettings.hostKey : kHostKeyRightControl;
	return kHostKeyNames[index];
}


void
MainWindow::_SendKey(bebox::u32 code, bool down)
{
	if (code == 0)
		return;
	if (!down && fKeysDown.count(code) == 0)
		return;
	if (down)
		fKeysDown.insert(code);
	else
		fKeysDown.erase(code);
	fEmulator.Post([code, down](bebox::Machine& machine) {
		if (down)
			machine.Keyboard().KeyDown(code);
		else
			machine.Keyboard().KeyUp(code);
	});
}


void
MainWindow::_ReleaseAllKeys()
{
	std::set<bebox::u32> keys = fKeysDown;
	for (bebox::u32 code : keys)
		_SendKey(code, false);
	fHostKeyDown = false;
}


// A key message; true if it was used (not for Windows' own handling).
bool
MainWindow::_HandleKey(UINT message, WPARAM wParam, LPARAM lParam)
{
	bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
	bool repeat = down && (lParam & (1 << 30)) != 0;
	// AltGr: Windows sends a left Control of its own before the right Alt
	if (wParam == VK_CONTROL && (lParam & (1 << 24)) == 0) {
		MSG next;
		if (PeekMessageW(&next, fWindow, WM_KEYFIRST, WM_KEYLAST,
				PM_NOREMOVE)
			&& next.wParam == VK_MENU && (next.lParam & (1 << 24)) != 0
			&& next.time == (DWORD)GetMessageTime())
			return true;
	}
	bebox::u32 code = WindowsKeyToSet2((unsigned)wParam, (long)lParam);

	// the host key: releases or captures the mouse when it goes up alone
	if (code == _HostKeyCode()) {
		if (down) {
			if (!fHostKeyDown) {
				fHostKeyDown = true;
				fHostComboUsed = false;
			}
		} else {
			fHostKeyDown = false;
			if (!fHostComboUsed && fPowered)
				fScreen.SetMouseCaptured(!fScreen.IsMouseCaptured());
		}
		return true;
	}
	// with F: full screen, also while the machine is off
	if (fHostKeyDown && wParam == 'F') {
		fHostComboUsed = true;
		if (down && !repeat)
			_SetFullScreen(!fFullScreen);
		return true;
	}
	if (!fPowered)
		return false;
	// Print Screen: Windows only says when it goes up
	if (wParam == VK_SNAPSHOT) {
		if (!down) {
			_SendKey(code, true);
			_SendKey(code, false);
		}
		return true;
	}
	if (down) {
		// auto-repeat sends the make code again, like a real keyboard
		fKeysDown.erase(code);
		_SendKey(code, true);
	} else
		_SendKey(code, false);
	return true;
}


// While the mouse is captured, the keys Windows keeps for itself (the
// Windows keys, Alt+Tab, Alt+Esc, Ctrl+Esc) go to the BeBox too.
LRESULT CALLBACK
MainWindow::_KeyboardHook(int code, WPARAM wParam, LPARAM lParam)
{
	MainWindow* target = sHookTarget;
	if (code == HC_ACTION && target != nullptr
		&& target->fScreen.IsMouseCaptured()
		&& GetForegroundWindow() == target->fWindow) {
		KBDLLHOOKSTRUCT* key = (KBDLLHOOKSTRUCT*)lParam;
		bool alt = (key->flags & LLKHF_ALTDOWN) != 0;
		bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		DWORD vk = key->vkCode;
		if (vk == VK_LWIN || vk == VK_RWIN || (vk == VK_TAB && alt)
			|| (vk == VK_ESCAPE && (alt || control))) {
			bool up = (key->flags & LLKHF_UP) != 0;
			LPARAM keyParam = (LPARAM)((key->scanCode & 0xff) << 16)
				| ((key->flags & LLKHF_EXTENDED) != 0 ? (1 << 24) : 0)
				| (up ? (LPARAM)0xc0000000 : 0);
			PostMessageW(target->fWindow, kMsgHookKey,
				vk | (up ? 0x10000 : 0), keyParam);
			return 1;
		}
	}
	return CallNextHookEx(nullptr, code, wParam, lParam);
}


void
MainWindow::_SetKeyboardHook(bool hook)
{
	if (hook && fKeyboardHook == nullptr) {
		sHookTarget = this;
		fKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, _KeyboardHook,
			GetModuleHandleW(nullptr), 0);
	} else if (!hook && fKeyboardHook != nullptr) {
		UnhookWindowsHookEx(fKeyboardHook);
		fKeyboardHook = nullptr;
		sHookTarget = nullptr;
	}
}


// #pragma mark - joysticks


// Host joysticks feed the game ports while the machine runs, at 50 Hz.
void
MainWindow::_PollJoysticks()
{
	if (!fPowered)
		return;
	for (int i = 0; i < 4; i++) {
		const Stick& stick = fSticks[i];
		if (stick.device < 0)
			continue;
		JOYINFOEX info = {};
		info.dwSize = sizeof(info);
		info.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS;
		if (joyGetPosEx(stick.device, &info) != JOYERR_NOERROR)
			continue;
		// the host's range to the 12-bit converter's
		bebox::u16 x = (bebox::u16)std::min<DWORD>(4095, (DWORD)(
			(double)(info.dwXpos - stick.minX) * 4095
				/ (stick.maxX - stick.minX)));
		bebox::u16 y = (bebox::u16)std::min<DWORD>(4095, (DWORD)(
			(double)(info.dwYpos - stick.minY) * 4095
				/ (stick.maxY - stick.minY)));
		bool button1 = (info.dwButtons & 1) != 0;
		bool button2 = (info.dwButtons & 2) != 0;
		fEmulator.Post([i, x, y, button1, button2](bebox::Machine& machine) {
			if (machine.GamePorts() != nullptr)
				machine.GamePorts()->SetStick(i, x, y, button1, button2);
		});
	}
}


// #pragma mark - messages


LRESULT CALLBACK
MainWindow::_WindowProc(HWND window, UINT message, WPARAM wParam,
	LPARAM lParam)
{
	MainWindow* self = (MainWindow*)GetWindowLongPtrW(window, GWLP_USERDATA);
	if (message == WM_NCCREATE) {
		CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
		self = (MainWindow*)create->lpCreateParams;
		self->fWindow = window;
		SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)self);
	}
	if (self == nullptr)
		return DefWindowProcW(window, message, wParam, lParam);
	return self->_Message(message, wParam, lParam);
}


LRESULT
MainWindow::_Message(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
		case WM_CREATE:
			fScreen.Create(fWindow);
			fScreen.SetScale(fSettings.scale);
			fScreen.SetMouseOptions(fSettings.mouseSpeed,
				fSettings.swapMouseButtons, fSettings.captureOnClick);
			fStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
				WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, fWindow,
				nullptr, GetModuleHandleW(nullptr), nullptr);
			_Layout();
			return 0;

		case WM_SIZE:
			_Layout();
			return 0;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			PAINTSTRUCT paint;
			HDC dc = BeginPaint(fWindow, &paint);
			_PaintLeds(dc);
			EndPaint(fWindow, &paint);
			return 0;
		}

		case WM_DPICHANGED:
		{
			RECT* suggested = (RECT*)lParam;
			SetWindowPos(fWindow, nullptr, suggested->left, suggested->top,
				suggested->right - suggested->left,
				suggested->bottom - suggested->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			_Layout();
			return 0;
		}

		case WM_TIMER:
			if (wParam == kStatusTimer)
				_UpdateStatus();
			else if (wParam == kJoystickTimer)
				_PollJoysticks();
			return 0;

		case WM_COMMAND:
			_Command(LOWORD(wParam));
			return 0;

		case WM_INITMENUPOPUP:
			_UpdateMenus();
			return 0;

		case WM_NOTIFY:
		{
			NMHDR* header = (NMHDR*)lParam;
			if (header->hwndFrom == fStatus && header->code == NM_CLICK) {
				_StatusClicked((int)((NMMOUSE*)lParam)->dwItemSpec);
				return TRUE;
			}
			break;
		}

		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
			if (_HandleKey(message, wParam, lParam))
				return 0;
			break;

		case WM_CHAR:
		case WM_SYSCHAR:
		case WM_DEADCHAR:
		case WM_SYSDEADCHAR:
			// the keys went to the BeBox: no beeps, no menu mnemonics
			if (fPowered)
				return 0;
			break;

		case WM_SYSCOMMAND:
			// Alt and F10 are the BeBox's while it runs
			if ((wParam & 0xfff0) == SC_KEYMENU && fPowered && lParam == 0)
				return 0;
			break;

		case kMsgHookKey:
		{
			UINT vk = (UINT)(wParam & 0xffff);
			bool up = (wParam & 0x10000) != 0;
			_HandleKey(up ? WM_KEYUP : WM_KEYDOWN, vk, lParam);
			return 0;
		}

		case WM_ACTIVATE:
			if (LOWORD(wParam) == WA_INACTIVE) {
				fScreen.SetMouseCaptured(false);
				_ReleaseAllKeys();
			} else
				SetFocus(fWindow);
			return 0;

		case WM_KILLFOCUS:
			_ReleaseAllKeys();
			return 0;

		case kMsgScreenResized:
			_FitWindow();
			return 0;

		case kMsgCaptureChanged:
			_SetKeyboardHook(wParam != 0);
			_UpdateStatus();
			return 0;

		case kMsgProblem:
		{
			std::unique_ptr<std::string> text((std::string*)lParam);
			_ShowProblem(*text);
			return 0;
		}

		case kMsgCdEjected:
		{
			// by BeOS: the drive is empty unless another disc went in since
			std::unique_ptr<std::string> path((std::string*)lParam);
			int id = (int)wParam;
			if (id < 0 || id > 6)
				return 0;
			MachineConfig::ScsiTarget& target = fSettings.machine.scsi[id];
			if (target.kind == MachineConfig::ScsiTarget::kCdRom
				&& target.image == *path) {
				target.image.clear();
				fSettings.Save();
				_UpdateMenus();
				_UpdateStatus();
			}
			return 0;
		}

		case kMsgFoldersReady:
			_SharedFoldersReady((int)wParam);
			return 0;

		case kMsgFoldersSynced:
		{
			std::unique_ptr<std::string> report((std::string*)lParam);
			fFolderSyncs--;
			_UpdateStatus();
			if (report)
				ShowMessage(fWindow, Widen(*report));
			return 0;
		}

		case WM_DROPFILES:
			_DroppedFiles((HDROP)wParam);
			return 0;

		case WM_CLOSE:
		{
			PowerOff();
			// BeOS's changes to the shared folders go back before quitting
			if (fFolderSyncs > 0) {
				SendMessageW(fStatus, SB_SETTEXTW, 0, (LPARAM)L"Copying "
					L"BeOS's changes to the shared folder…");
				UpdateWindow(fStatus);
			}
			fFolderJobs.Drain();
			if (fFullScreen)
				_SetFullScreen(false);
			WINDOWPLACEMENT placement = {};
			placement.length = sizeof(placement);
			if (GetWindowPlacement(fWindow, &placement)) {
				const RECT& frame = placement.rcNormalPosition;
				fSettings.windowFrame = { (int)frame.left, (int)frame.top,
					(int)frame.right, (int)frame.bottom };
			}
			fSettings.Save();
			DestroyWindow(fWindow);
			return 0;
		}

		case WM_DESTROY:
			KillTimer(fWindow, kStatusTimer);
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProcW(fWindow, message, wParam, lParam);
}
