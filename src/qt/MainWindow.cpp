/*
 * BeBox emulator for Qt - the machine's window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "MainWindow.h"

#include "KeyMap.h"
#include "LedWidget.h"
#include "PrefsDialog.h"
#include "AudioOutput.h"
#include "ScreenWidget.h"
#include "SerialConsole.h"

#include "core/board/Machine.h"
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

#ifdef Q_OS_HAIKU
#	include "haiku/HaikuAttributes.h"
#endif
#ifdef Q_OS_WIN
#	include <windows.h>
#endif

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <cmath>

using bebox::MachineConfig;

namespace {

const QString kDash = QString::fromUtf8("  —  ");

// The host key, as set 2 codes.
const bebox::u32 kHostKeyCodes[] = { 0xe014, 0xe011, 0xe027, 0xe02f };

const char* const kFloppyFilters = "Floppy images (*.img *.ima *.dsk);;"
	"All files (*)";
const char* const kCdFilters = "CD images (*.iso *.cue *.toast *.bin);;"
	"All files (*)";


QString
Q(const std::string& text)
{
	return QString::fromStdString(text);
}


QString
LeafOf(const std::string& path)
{
	return QFileInfo(Q(path)).fileName();
}

#ifdef Q_OS_WIN
// While the mouse is captured, the keys Windows keeps for itself (the
// Windows keys, Alt+Tab, Alt+Esc, Ctrl+Esc) go to the BeBox too: a low
// level keyboard hook takes them and hands them to the window.
HHOOK sHook = nullptr;
std::function<void(bebox::u32, bool)> sHookTarget;


LRESULT CALLBACK
SystemKeyHook(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION && sHookTarget) {
		KBDLLHOOKSTRUCT* key = (KBDLLHOOKSTRUCT*)lParam;
		bool alt = (key->flags & LLKHF_ALTDOWN) != 0;
		bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		DWORD vk = key->vkCode;
		if (vk == VK_LWIN || vk == VK_RWIN || (vk == VK_TAB && alt)
			|| (vk == VK_ESCAPE && (alt || control))) {
			bool up = (key->flags & LLKHF_UP) != 0;
			bebox::u32 set2 = bebox::Set1ToSet2(key->scanCode & 0xff,
				(key->flags & LLKHF_EXTENDED) != 0);
			sHookTarget(set2, !up);
			return 1;
		}
	}
	return CallNextHookEx(nullptr, code, wParam, lParam);
}
#endif

}	// namespace


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
	fFolderStarts(0),
	fFolderSyncs(0),
	fPowered(false),
	fStarting(false),
	fFullScreen(false),
	fWasMaximized(false),
	fHostKeyDown(false),
	fHostComboUsed(false),
	fProblemTime(0)
{
	setWindowTitle("BeBox");
	setAcceptDrops(true);
#ifdef Q_OS_HAIKU
	fHostAttributes.reset(new HaikuAttributes);
#endif

	fScreen = new ScreenWidget(fEmulator);
	fLeftLeds = new LedWidget(false);
	fRightLeds = new LedWidget(true);
	QWidget* central = new QWidget;
	QHBoxLayout* layout = new QHBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(fLeftLeds);
	layout->addWidget(fScreen, 1);
	layout->addWidget(fRightLeds);
	central->setAutoFillBackground(true);
	QPalette palette = central->palette();
	palette.setColor(QPalette::Window, Qt::black);
	central->setPalette(palette);
	setCentralWidget(central);

	fAudio = new AudioOutput(this);
	for (int i = 0; i < 4; i++)
		fConsoles[i] = new SerialConsole(i, nullptr);

	fStatus = new QLabel;
	fStatus->setTextFormat(Qt::PlainText);
	fMedia = new QPushButton;
	fMedia->setFlat(true);
	fMedia->setToolTip(tr("Insert or eject a floppy or CD"));
	statusBar()->addWidget(fStatus, 1);
	statusBar()->addPermanentWidget(fMedia);

	_BuildMenus();
	connect(fMedia, &QPushButton::clicked, this, [this]() {
		_BuildMediaMenu();
		fMediaMenu->popup(fMedia->mapToGlobal(QPoint(0, 0))
			- QPoint(0, fMediaMenu->sizeHint().height()));
	});
	connect(fScreen, &ScreenWidget::FrameResized, this,
		[this]() { _FitWindow(); });
	connect(fScreen, &ScreenWidget::CaptureChanged, this,
		[this](bool captured) {
			_SetSystemKeyHook(captured);
			_UpdateStatus();
		});

	fStatusTimer = new QTimer(this);
	connect(fStatusTimer, &QTimer::timeout, this, [this]() {
		_UpdateStatus();
	});
	fStatusTimer->start(100);
	fJoystickTimer = new QTimer(this);
	connect(fJoystickTimer, &QTimer::timeout, this, [this]() {
		_PollJoysticks();
	});

	qApp->installEventFilter(this);
	bebox::Log::SetSink(&MainWindow::_LogSink, this);
	_ApplySettings();

	const WindowFrame& frame = fSettings.windowFrame;
	if (frame.IsValid()) {
		QRect rect(frame.left, frame.top, frame.right - frame.left,
			frame.bottom - frame.top);
		if (QGuiApplication::screenAt(rect.center()) != nullptr)
			setGeometry(rect);
	}
	_FitWindow();
}


MainWindow::~MainWindow()
{
	bebox::Log::SetSink(nullptr, nullptr);
	qApp->removeEventFilter(this);
	_SetSystemKeyHook(false);
	fEmulator.Stop();
	for (SerialConsole* console : fConsoles)
		delete console;
}


// #pragma mark - menus


void
MainWindow::_BuildMenus()
{
	QMenu* machine = menuBar()->addMenu(tr("&Machine"));
	fPowerAction = machine->addAction(tr("&Power on"), this, [this]() {
		if (fPowered || fStarting)
			PowerOff();
		else
			PowerOn();
	});
	machine->addAction(tr("&Reset (both switches)"), this,
		[this]() { _Reset(); });
	fInterruptAction = machine->addAction(tr("&Interrupt switch"), this,
		[this]() {
			fEmulator.Post([](bebox::Machine& machine) {
				machine.PressInterruptSwitch();
			});
		});
	fCtrlAltDeleteAction = machine->addAction(tr("Send Ctrl+Alt+&Delete"),
		this, [this]() {
			// BeOS R3 and later show their Team Monitor (twice: restart)
			if (fPowered)
				_PressKeyCombination({ "LeftControl", "LeftAlt", "Delete" });
		});
	machine->addSeparator();
	machine->addAction(tr("Restart with &boot menu"), this,
		[this]() { _RestartWith(false); });
	machine->addAction(tr("Restart with debu&g output (serial 4)"), this,
		[this]() { _RestartWith(true); });
	machine->addSeparator();
	fPauseAction = machine->addAction(tr("P&ause"), this, [this]() {
		fEmulator.SetPaused(!fEmulator.IsPaused());
		_UpdateMenus();
		_UpdateStatus();
	});
	fPauseAction->setCheckable(true);
	machine->addSeparator();
	fProfileMenu = machine->addMenu(tr("Pro&file"));
	QAction* preferences = machine->addAction(PageIcon("machine"),
		tr("Pr&eferences…"), this, [this]() { _ShowPreferences(); });
	preferences->setMenuRole(QAction::PreferencesRole);
	QAction* about = machine->addAction(tr("About &BeBox…"), this,
		[this]() { _About(); });
	about->setMenuRole(QAction::AboutRole);
	machine->addSeparator();
	QAction* quit = machine->addAction(tr("&Quit"), this,
		[this]() { close(); });
	quit->setMenuRole(QAction::QuitRole);

	fMediaMenu = menuBar()->addMenu(tr("M&edia"));
	connect(fMediaMenu, &QMenu::aboutToShow, this,
		[this]() { _BuildMediaMenu(); });

	QMenu* view = menuBar()->addMenu(tr("&View"));
	fFullScreenAction = view->addAction(tr("&Full screen"), this,
		[this]() { _SetFullScreen(!fFullScreen); });
	fFullScreenAction->setCheckable(true);
	QMenu* fullScreenScale = view->addMenu(tr("Full screen &scaling"));
	QActionGroup* fullScreenGroup = new QActionGroup(this);
	const char* fullScreenNames[] = { "&Fill the screen", "&Whole multiples",
		"&Actual size" };
	for (int i = 0; i < 3; i++) {
		fFullScreenScaleActions[i] = fullScreenScale->addAction(
			tr(fullScreenNames[i]), this, [this, i]() {
				fSettings.fullScreenScale = i;
				fScreen->SetFullScreen(fFullScreen, i);
				fSettings.Save();
			});
		fFullScreenScaleActions[i]->setCheckable(true);
		fullScreenGroup->addAction(fFullScreenScaleActions[i]);
	}
	view->addSeparator();
	QActionGroup* scaleGroup = new QActionGroup(this);
	const char* scaleNames[] = { "Fit &window", "&100%", "&200%" };
	for (int i = 0; i < 3; i++) {
		fScaleActions[i] = view->addAction(tr(scaleNames[i]), this,
			[this, i]() {
				fSettings.scale = i;
				fScreen->SetScale(i);
				_FitWindow();
				fSettings.Save();
			});
		fScaleActions[i]->setCheckable(true);
		scaleGroup->addAction(fScaleActions[i]);
	}
	// in the order of the menu: 100%, 200%, fit
	view->insertAction(fScaleActions[0], fScaleActions[1]);
	view->insertAction(fScaleActions[0], fScaleActions[2]);
	view->addSeparator();
	fShowLedsAction = view->addAction(tr("Show front panel &LEDs"), this,
		[this]() {
			fSettings.showLeds = !fSettings.showLeds;
			_ApplySettings();
			fSettings.Save();
		});
	fShowLedsAction->setCheckable(true);
	QMenu* styles = view->addMenu(tr("LED &colors"));
	QActionGroup* styleGroup = new QActionGroup(this);
	const char* styleNames[] = { "&Authentic (lime and amber)",
		"&Red and green" };
	for (int i = 0; i < 2; i++) {
		fLedStyleActions[i] = styles->addAction(tr(styleNames[i]), this,
			[this, i]() {
				fSettings.ledStyle = i;
				_ApplySettings();
				fSettings.Save();
			});
		fLedStyleActions[i]->setCheckable(true);
		styleGroup->addAction(fLedStyleActions[i]);
	}

	QMenu* tools = menuBar()->addMenu(tr("&Tools"));
	QMenu* consoles = tools->addMenu(PageIcon("serial"), tr("Serial "
		"&console"));
	for (int i = 0; i < 4; i++) {
		consoles->addAction(tr("Serial &%1").arg(i + 1), this,
			[this, i]() { _ShowConsole(i); });
	}
	tools->addAction(tr("Save &screenshot…"), this,
		[this]() { _SaveScreenshot(); });

	_BuildProfileMenu();
	_BuildMediaMenu();
}


void
MainWindow::_BuildProfileMenu()
{
	fProfileMenu->clear();
	std::vector<std::string> names = Settings::GetProfiles();
	QActionGroup* group = new QActionGroup(fProfileMenu);
	for (const std::string& name : names) {
		QAction* action = fProfileMenu->addAction(Q(name), this,
			[this, name]() { _SwitchProfile(name, false); });
		action->setCheckable(true);
		action->setChecked(name == fSettings.profile);
		group->addAction(action);
	}
	fProfileMenu->addSeparator();
	fProfileMenu->addAction(tr("&New profile…"), this,
		[this]() { _NewProfile(); });
	fProfileMenu->addAction(tr("&Rename \"%1\"…")
		.arg(Q(fSettings.profile)), this, [this]() { _RenameProfile(); });
	QAction* remove = fProfileMenu->addAction(tr("&Delete \"%1\"…")
		.arg(Q(fSettings.profile)), this, [this]() { _DeleteProfile(); });
	remove->setEnabled(names.size() > 1);
}


void
MainWindow::_BuildMediaMenu()
{
	fMediaMenu->clear();
	// both models have a floppy drive
	fMediaMenu->addAction(PageIcon("floppy"), tr("Insert &floppy…"),
		this, [this]() {
			QString path = QFileDialog::getOpenFileName(this, tr("Floppy"),
				Q(fSettings.machine.floppyImage), tr(kFloppyFilters));
			if (!path.isEmpty()) {
				_InsertFloppy(QDir::toNativeSeparators(path).toStdString());
				fSettings.Save();
			}
		});
	QAction* eject = fMediaMenu->addAction(tr("&Eject floppy"), this,
		[this]() {
			_EjectFloppy();
			fSettings.Save();
		});
	eject->setEnabled(!fSettings.machine.floppyImage.empty());
	std::vector<int> drives = _CdDrives();
	fMediaMenu->addSeparator();
	if (drives.empty()) {
		fMediaMenu->addAction(tr("No CD-ROM drive (Preferences > "
			"Storage)"))->setEnabled(false);
	}
	for (int id : drives) {
		const std::string& image = fSettings.machine.scsi[id].image;
		QString insert = drives.size() == 1 ? tr("Insert &CD…")
			: tr("Insert CD (SCSI ID %1)…").arg(id);
		fMediaMenu->addAction(PageIcon("scsi"), insert, this, [this, id]() {
			QString path = QFileDialog::getOpenFileName(this,
				tr("CD-ROM (SCSI ID %1)").arg(id),
				Q(fSettings.machine.scsi[id].image), tr(kCdFilters));
			if (!path.isEmpty()
				&& _InsertCd(id, QDir::toNativeSeparators(path)
					.toStdString()))
				fSettings.Save();
		});
		QString ejectText = image.empty()
			? (drives.size() == 1 ? tr("Eject CD")
				: tr("Eject CD (SCSI ID %1)").arg(id))
			: tr("Eject \"%1\"").arg(LeafOf(image));
		QAction* ejectCd = fMediaMenu->addAction(ejectText, this,
			[this, id]() {
				_EjectCd(id);
				fSettings.Save();
			});
		ejectCd->setEnabled(!image.empty());
	}
}


void
MainWindow::_UpdateMenus()
{
	bool on = fPowered || fStarting;
	fPowerAction->setText(on ? tr("&Power off") : tr("&Power on"));
	fPauseAction->setChecked(fEmulator.IsPaused());
	fPauseAction->setEnabled(fPowered);
	fCtrlAltDeleteAction->setEnabled(fPowered);
	fInterruptAction->setEnabled(fPowered);
	fFullScreenAction->setText(tr("&Full screen") + "\t" + _HostKeyName()
		+ "+F");
	fFullScreenAction->setChecked(fFullScreen);
	fFullScreenScaleActions[std::max(0, std::min(2,
		fSettings.fullScreenScale))]->setChecked(true);
	fScaleActions[std::max(0, std::min(2, fSettings.scale))]->setChecked(true);
	fShowLedsAction->setChecked(fSettings.showLeds);
	// the Hobbit prototype's kernel drives no front panel lights
	fShowLedsAction->setEnabled(fSettings.machine.model
		!= MachineConfig::kHobbit);
	fLedStyleActions[fSettings.ledStyle == kLedStyleRedGreen ? 1 : 0]
		->setChecked(true);
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
				QMessageBox::warning(this, "BeBox", tr("Serial port %1 is "
					"not connected: %2").arg(i + 1).arg(Q(error)));
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
			QMessageBox::warning(this, "BeBox", tr("The Ethernet card is not "
				"connected: %1").arg(Q(error)));
		}
	}
	bebox::NetworkBackend* network = fNetwork.get();
	AudioOutput* audio = nullptr;
	if (fSettings.soundEnabled) {
		fAudio->SetVolume(fSettings.soundVolume / 100.0f);
		if (fAudio->Start())
			audio = fAudio;
	}
	ScreenWidget* screen = fScreen;
	MainWindow* window = this;
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
					cd->SetEjectCallback(
						[window, id](const std::string& path) {
							QMetaObject::invokeMethod(window,
								[window, id, path]() {
									window->_CdEjected(id, path);
								}, Qt::QueuedConnection);
						});
				}
			})) {
		fAudio->Stop();
		fSerialBackends.clear();
		fNetwork.reset();
		// nothing ran on the shared folders' volumes
		fSharedFolders.clear();
		QString hint = fSettings.machine.model == MachineConfig::kHobbit
			? tr("Check the disk images (and the Hobbit kernel) in the "
				"preferences.")
			: tr("Check the ROM folder and the disk images in the "
				"preferences (Machine > Preferences > ROM & NVRAM).");
		QMessageBox::critical(this, "BeBox", tr("The machine could not be "
			"started:\n\n%1\n\n%2").arg(Q(error)).arg(hint));
		return false;
	}
	fEmulator.SetSpeedLimit(fSettings.speedLimit);
	fPowered = true;
	fScreen->SetPowered(true);
	std::vector<int> sticks;
	for (int i = 0; i < 4; i++)
		sticks.push_back(fSettings.joystick[i].empty() ? -1
			: atoi(fSettings.joystick[i].c_str()));
	fJoysticks.Open(sticks);
	fJoystickTimer->start(20);
	_UpdateMenus();
	_UpdateStatus();
	fScreen->setFocus();
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
	fJoystickTimer->stop();
	fJoysticks.Close();
	fScreen->SetPowered(false);
	fAudio->Stop();
	fSerialBackends.clear();
	fNetwork.reset();
	_SyncSharedFolders(true);
	fPowered = false;
	_ReleaseAllKeys();
	fLeftLeds->SetLevels(0, false, 0);
	fRightLeds->SetLevels(0, false, 0);
	_UpdateMenus();
	_UpdateStatus();
}


// Shared folders: a BFS volume per folder, built in the profile's folder
// at power on, BeOS's changes back to the folder at power off. The
// copies run on fFolderJobs one after the other; the machine starts when
// its folders are ready.
void
MainWindow::_PrepareSharedFolders(const MachineConfig& config)
{
	std::shared_ptr<FolderStart> start(new FolderStart);
	start->generation = ++fFolderStarts;
	start->config = config;
	start->profileDirectory = fSettings.ProfileDirectory();
	fFolderStart = start;
	bebox::HostAttributes* attributes = fHostAttributes.get();
	MainWindow* window = this;
	fFolderJobs.Add([start, attributes, window]() {
		for (int id = 0; id < 7; id++) {
			MachineConfig::ScsiTarget& target = start->config.scsi[id];
			if (target.kind != MachineConfig::ScsiTarget::kSharedFolder)
				continue;
			std::string image = (QDir(Q(start->profileDirectory))
				.filePath(QString("shared-scsi%1.qcow2").arg(id)))
				.toStdString();
			std::unique_ptr<bebox::SharedFolder> share(
				new bebox::SharedFolder(target.image, image, attributes));
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
		int generation = start->generation;
		QMetaObject::invokeMethod(window, [window, generation]() {
			window->_SharedFoldersReady(generation);
		}, Qt::QueuedConnection);
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
	if (!start->messages.empty())
		QMessageBox::information(this, "BeBox", Q(start->messages).trimmed());
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
	MainWindow* window = this;
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
		QMetaObject::invokeMethod(window, [window, interactive, messages]() {
			window->fFolderSyncs--;
			window->_UpdateStatus();
			if (interactive && !messages.empty())
				QMessageBox::information(window, "BeBox",
					Q(messages).trimmed());
		}, Qt::QueuedConnection);
	});
}


// #pragma mark - settings and profiles


void
MainWindow::_ApplySettings()
{
	fEmulator.SetSpeedLimit(fSettings.speedLimit);
	fAudio->SetVolume(fSettings.soundVolume / 100.0f);
	fScreen->SetScale(fSettings.scale);
	fScreen->SetMouseOptions(fSettings.mouseSpeed,
		fSettings.swapMouseButtons, fSettings.captureOnClick);
	fLeftLeds->SetStyle(fSettings.ledStyle);
	fRightLeds->SetStyle(fSettings.ledStyle);
	fLeftLeds->setVisible(_ShowLeds());
	fRightLeds->setVisible(_ShowLeds());
	_BuildProfileMenu();
	_UpdateMenus();
	_FitWindow();
	_UpdateStatus();
}


void
MainWindow::_ShowPreferences()
{
	bool running = fPowered || fStarting;
	fScreen->SetMouseCaptured(false);
	PrefsDialog dialog(fSettings, running, fJoysticks.Names(), this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	Settings old = fSettings;
	fSettings = dialog.Result();
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
			AudioOutput* audio = fSettings.soundEnabled && fAudio->Start()
				? fAudio : nullptr;
			fEmulator.Post([audio](bebox::Machine& machine) {
				machine.SetAudioSink(audio);
			});
			if (audio == nullptr)
				fAudio->Stop();
		}
		std::vector<int> sticks;
		for (int i = 0; i < 4; i++)
			sticks.push_back(fSettings.joystick[i].empty() ? -1
				: atoi(fSettings.joystick[i].c_str()));
		fJoysticks.Open(sticks);
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
	if (name == fSettings.profile && !create) {
		_BuildProfileMenu();
		return;
	}
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


// A name for a profile: asked until it is a usable one.
static std::string
AskProfileName(QWidget* parent, const QString& title, const QString& prompt,
	const std::string& initial)
{
	QString name = Q(initial);
	while (true) {
		bool ok = false;
		name = QInputDialog::getText(parent, title, prompt,
			QLineEdit::Normal, name, &ok);
		if (!ok)
			return "";
		std::string problem = Settings::CheckProfileName(name.toStdString());
		if (problem.empty() || name.toStdString() == initial)
			return name.toStdString();
		QMessageBox::warning(parent, "BeBox", Q(problem));
	}
}


void
MainWindow::_NewProfile()
{
	std::string name = AskProfileName(this, tr("New profile"),
		tr("A copy of \"%1\" named:").arg(Q(fSettings.profile)), "");
	if (!name.empty())
		_SwitchProfile(name, true);
}


void
MainWindow::_RenameProfile()
{
	std::string name = AskProfileName(this, tr("Rename profile"),
		tr("New name:"), fSettings.profile);
	if (name.empty() || name == fSettings.profile)
		return;
	if (!fSettings.RenameProfile(name)) {
		QMessageBox::warning(this, "BeBox", tr("The profile could not be "
			"renamed."));
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
	if (QMessageBox::question(this, "BeBox", tr("Delete the profile \"%1\" "
			"with its boot ROM flash, NVRAM and shared folder volumes? Disk "
			"images stay where they are.").arg(Q(fSettings.profile)))
			!= QMessageBox::Yes)
		return;
	std::string victim = fSettings.profile;
	std::string next = names[0] == victim ? names[1] : names[0];
	PowerOff();
	fFolderJobs.Drain();
	fSettings.SwitchProfile(next);
	if (!Settings::DeleteProfile(victim)) {
		QMessageBox::warning(this, "BeBox", tr("The profile \"%1\" could not "
			"be deleted completely.").arg(Q(victim)));
	}
	_ApplySettings();
}


// #pragma mark - machine commands


void
MainWindow::_Reset()
{
	if (!fPowered) {
		PowerOn();
		return;
	}
	_ReleaseAllKeys();
	fEmulator.Post([](bebox::Machine& machine) {
		machine.PowerOn();
	});
}


void
MainWindow::_RestartWith(bool debug)
{
	if (!fPowered && !PowerOn())
		return;
	_ReleaseAllKeys();
	// F1 sends the kernel's debug output to serial port 4; it goes up when
	// the boot ROM starts writing there.
	fEmulator.Post([debug](bebox::Machine& machine) {
		machine.PowerOn();
		if (debug)
			machine.HoldDebugOutputKey();
	});
	if (debug) {
		if (fSettings.serial[3] == "console")
			_ShowConsole(3);
		return;
	}
	// The boot menu: DR7/DR8 ROMs: any key; PR and later: left Shift, down
	// when the ROM looks at the keyboard (about 6 s after power on with
	// 32 MB; the memory test before takes 1/16 s per MB more at 66 MHz,
	// half that at 133 MHz).
	const MachineConfig& config = fSettings.machine;
	double memoryTest = config.ramMB > 32
		? (config.ramMB - 32) / (config.model == MachineConfig::kBeBox133
			? 32.0 : 16.0) : 0;
	_HoldKeyDuringBoot(0x12, 8 + memoryTest);
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
	if (level != bebox::kLogError || (category != bebox::kLogStorage
			&& category != bebox::kLogHost))
		return;
	MainWindow* window = (MainWindow*)cookie;
	QString text = QString::fromUtf8(message);
	QMetaObject::invokeMethod(window, [window, text]() {
		window->_ShowProblem(text);
	}, Qt::QueuedConnection);
}


// In the status line for 30 s, in a message box the first time since
// power on.
void
MainWindow::_ShowProblem(const QString& text)
{
	fProblem = text;
	fProblemTime = QDateTime::currentMSecsSinceEpoch();
	_UpdateStatus();
	if (text.isEmpty() || !fProblemsAlerted.insert(text).second)
		return;
	QString alert = text;
	alert[0] = alert[0].toUpper();
	QMessageBox::warning(this, "BeBox", alert + ".");
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
		QMessageBox::warning(this, "BeBox", tr("The CD image could not be "
			"opened: %1").arg(Q(error)));
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
	_UpdateStatus();
}


// By BeOS: the drive is empty unless another disc went in since.
void
MainWindow::_CdEjected(int id, const std::string& path)
{
	if (id < 0 || id > 6)
		return;
	MachineConfig::ScsiTarget& target = fSettings.machine.scsi[id];
	if (target.kind == MachineConfig::ScsiTarget::kCdRom
		&& target.image == path) {
		target.image.clear();
		fSettings.Save();
		_UpdateStatus();
	}
}


void
MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}


// Files dropped on the window: CD images into the first CD-ROM drive (an
// empty one if there is), floppy images into the floppy drive.
void
MainWindow::dropEvent(QDropEvent* event)
{
	static const QStringList kCdSuffixes = { "iso", "cue", "toast", "bin" };
	static const QStringList kFloppySuffixes = { "img", "ima", "dsk" };
	for (const QUrl& url : event->mimeData()->urls()) {
		if (!url.isLocalFile())
			continue;
		QFileInfo file(url.toLocalFile());
		std::string path = QDir::toNativeSeparators(file.absoluteFilePath())
			.toStdString();
		QString suffix = file.suffix().toLower();
		if (kCdSuffixes.contains(suffix)) {
			std::vector<int> drives = _CdDrives();
			if (drives.empty()) {
				QMessageBox::information(this, "BeBox", tr("There is no "
					"CD-ROM drive: give a SCSI ID one in Machine > "
					"Preferences > Storage."));
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
		} else if (kFloppySuffixes.contains(suffix)) {
			if (file.size() <= 2949120)
				_InsertFloppy(path);
			else {
				QMessageBox::information(this, "BeBox", tr("%1 is too large "
					"for a floppy: disk images go on a SCSI ID (Preferences "
					"> Storage).").arg(file.fileName()));
			}
		}
	}
	event->acceptProposedAction();
	fSettings.Save();
}


// #pragma mark - window


bool
MainWindow::_ShowLeds() const
{
	return fSettings.showLeds
		&& fSettings.machine.model != MachineConfig::kHobbit;
}


// The window as large as the picture at the chosen scale (not for "fit
// window", full screen or a maximized window).
void
MainWindow::_FitWindow()
{
	if (fFullScreen || fSettings.scale == 0 || isMaximized())
		return;
	QSize screen = fScreen->SizeForScale(fSettings.scale);
	int leds = _ShowLeds() ? fLeftLeds->width() + fRightLeds->width() : 0;
	int extra = menuBar()->sizeHint().height()
		+ statusBar()->sizeHint().height();
	resize(screen.width() + leds, screen.height() + extra);
}


// Full screen: no menu bar or status line; the screen (and the LEDs, if
// shown) fill the display. The window comes back after.
void
MainWindow::_SetFullScreen(bool fullScreen)
{
	if (fullScreen == fFullScreen)
		return;
	fFullScreen = fullScreen;
	if (fullScreen) {
		fWasMaximized = isMaximized();
		menuBar()->hide();
		statusBar()->hide();
		showFullScreen();
		fScreen->SetFullScreen(true, fSettings.fullScreenScale);
		fScreen->ShowHint(tr("%1+F leaves full screen").arg(_HostKeyName()));
	} else {
		menuBar()->show();
		statusBar()->show();
		if (fWasMaximized)
			showMaximized();
		else
			showNormal();
		fScreen->SetFullScreen(false, fSettings.fullScreenScale);
		fScreen->ShowHint("");
		// the BeBox may have changed its resolution meanwhile
		QTimer::singleShot(0, this, [this]() { _FitWindow(); });
	}
	_UpdateMenus();
}


void
MainWindow::_UpdateStatus()
{
	bool problem = !fProblem.isEmpty()
		&& QDateTime::currentMSecsSinceEpoch() - fProblemTime < 30000;
	QString profile = Q(fSettings.profile);
	QString text;
	float disk = 0;
	if (fStarting) {
		text = profile + kDash + tr("starting: copying the shared "
			"folder…");
	} else if (!fPowered) {
		text = profile + kDash + tr("powered off");
		if (fFolderSyncs > 0)
			text += kDash + tr("copying BeOS's changes to the shared "
				"folder…");
	} else {
		bebox::EmulatorThread::Status status = fEmulator.GetStatus();
		fLeftLeds->SetLevels(status.leds[0], true, 0);
		fRightLeds->SetLevels(status.leds[1], true, status.disk);
		disk = status.disk;
		QString model = fSettings.machine.model == MachineConfig::kBeBox133
			? "BeBox Dual603e-133"
			: fSettings.machine.model == MachineConfig::kHobbit
				? "Hobbit BeBox" : "BeBox Dual603-66";
		text = profile + kDash + tr("%1, %2 MB").arg(model)
			.arg(fSettings.machine.ramMB) + kDash + tr("%1%, %2 MIPS")
			.arg(status.speedPercent, 0, 'f', 0).arg(status.mips, 0, 'f', 0);
		if (status.paused)
			text += tr("  (paused)");
		if (problem)
			;	// instead of the mouse hint
		else if (fScreen->IsMouseCaptured())
			text += kDash + tr("%1 releases the mouse").arg(_HostKeyName());
		else if (fSettings.captureOnClick)
			text += kDash + tr("click the screen to capture the mouse");
		else
			text += kDash + tr("%1 captures the mouse").arg(_HostKeyName());
	}
	if (problem)
		text += kDash + fProblem;
	if (text != fStatus->text())
		fStatus->setText(text);

	// media: what is inserted, the disk light
	const MachineConfig& machine = fSettings.machine;
	QString media = tr("Floppy: %1").arg(machine.floppyImage.empty()
		? tr("empty") : LeafOf(machine.floppyImage));
	for (int id : _CdDrives()) {
		const std::string& image = machine.scsi[id].image;
		media += tr("   CD %1: %2").arg(id).arg(image.empty() ? tr("empty")
			: LeafOf(image));
	}
	media += fPowered && disk > 0.05f ? QString::fromUtf8("   ●")
		: QString::fromUtf8("   ○");
	if (media != fMedia->text())
		fMedia->setText(media);
}


void
MainWindow::_SaveScreenshot()
{
	QImage frame = fScreen->CopyFrame();
	QString directory = QStandardPaths::writableLocation(
		QStandardPaths::PicturesLocation);
	QString name = QDateTime::currentDateTime().toString(
		"'BeBox' yyyy-MM-dd HHmmss'.png'");
	QString path = QFileDialog::getSaveFileName(this, tr("Save screenshot"),
		QDir(directory).filePath(name), tr("PNG images (*.png)"));
	if (path.isEmpty())
		return;
	if (!frame.save(path, "PNG"))
		QMessageBox::warning(this, "BeBox", tr("Cannot write %1.").arg(path));
}


void
MainWindow::_ShowConsole(int port)
{
	fConsoles[port]->ShowWindow();
	if (fSettings.serial[port] != "console") {
		QMessageBox::information(fConsoles[port], "BeBox", tr("Serial port %1 "
			"is not connected to its console window: choose \"Console "
			"window\" for it in Preferences > Ports.").arg(port + 1));
	}
}


void
MainWindow::_About()
{
	QMessageBox about(this);
	about.setWindowTitle(tr("About BeBox"));
	about.setIconPixmap(windowIcon().pixmap(64, 64));
	about.setText(tr("<b>BeBox 1.2</b>"));
	about.setInformativeText(tr("An emulator of Be Inc.'s BeBox, the "
		"dual-processor computer that BeOS was born on: the Dual603-66 and "
		"Dual603e-133, and the Hobbit prototypes before them.<br><br>"
		"Copyright 2026, the BeBox emulator authors. MIT License.<br>"
		"Qt %1; user mode networking: libslirp (BSD license).")
		.arg(qVersion()));
	about.exec();
}


void
MainWindow::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
		fScreen->SetMouseCaptured(false);
		_ReleaseAllKeys();
	}
	QMainWindow::changeEvent(event);
}


void
MainWindow::closeEvent(QCloseEvent* event)
{
	PowerOff();
	// BeOS's changes to the shared folders go back before quitting
	if (fFolderSyncs > 0) {
		fStatus->setText(tr("Copying BeOS's changes to the shared "
			"folder…"));
		fStatus->repaint();
	}
	fFolderJobs.Drain();
	if (fFullScreen)
		_SetFullScreen(false);
	QRect frame = normalGeometry();
	fSettings.windowFrame = { frame.left(), frame.top(),
		frame.left() + frame.width(), frame.top() + frame.height() };
	fSettings.Save();
	for (SerialConsole* console : fConsoles)
		console->hide();
	event->accept();
}


// #pragma mark - keyboard


bebox::u32
MainWindow::_HostKeyCode() const
{
	int index = fSettings.hostKey >= 0 && fSettings.hostKey < 4
		? fSettings.hostKey : kHostKeyRightControl;
	return kHostKeyCodes[index];
}


QString
MainWindow::_HostKeyName() const
{
#ifdef Q_OS_MACOS
	static const char* const kNames[] = { "Right Control", "Right Option",
		"Right Command", "Menu" };
#else
	static const char* const kNames[] = { "Right Ctrl", "Right Alt",
		"Right Windows", "Menu" };
#endif
	int index = fSettings.hostKey >= 0 && fSettings.hostKey < 4
		? fSettings.hostKey : kHostKeyRightControl;
	return tr(kNames[index]);
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


// All key events of the window, before Qt's own use of them (menu
// mnemonics, Alt, focus changes): true if they went to the BeBox.
bool
MainWindow::eventFilter(QObject* watched, QEvent* event)
{
	QEvent::Type type = event->type();
	if (type != QEvent::KeyPress && type != QEvent::KeyRelease
		&& type != QEvent::ShortcutOverride)
		return false;
	QWidget* widget = qobject_cast<QWidget*>(watched);
	if (widget == nullptr || widget->window() != this || !isActiveWindow()
		|| QApplication::activeModalWidget() != nullptr)
		return false;
	QKeyEvent* key = (QKeyEvent*)event;
	if (type == QEvent::ShortcutOverride) {
		// no shortcuts while the BeBox has the keyboard
		if (fPowered) {
			event->accept();
			return true;
		}
		return false;
	}
	return _HandleKey(key);
}


bool
MainWindow::_HandleKey(QKeyEvent* event)
{
	bool down = event->type() == QEvent::KeyPress;
	// X11 sends a release before each repeated press: only the press counts
	if (!down && event->isAutoRepeat())
		return fPowered;
	bebox::u32 code = KeyEventToSet2(event);

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
				fScreen->SetMouseCaptured(!fScreen->IsMouseCaptured());
		}
		return true;
	}
	// with F: full screen, also while the machine is off
	if (fHostKeyDown && event->key() == Qt::Key_F) {
		fHostComboUsed = true;
		if (down && !event->isAutoRepeat())
			_SetFullScreen(!fFullScreen);
		return true;
	}
	if (!fPowered)
		return false;
	if (down) {
		// auto-repeat sends the make code again, like a real keyboard
		fKeysDown.erase(code);
		_SendKey(code, true);
	} else
		_SendKey(code, false);
	return true;
}


void
MainWindow::_SetSystemKeyHook(bool hook)
{
#ifdef Q_OS_WIN
	if (hook && sHook == nullptr) {
		sHookTarget = [this](bebox::u32 code, bool down) {
			QMetaObject::invokeMethod(this, [this, code, down]() {
				if (isActiveWindow() && fScreen->IsMouseCaptured()) {
					if (down)
						fKeysDown.erase(code);
					_SendKey(code, down);
				}
			}, Qt::QueuedConnection);
		};
		sHook = SetWindowsHookExW(WH_KEYBOARD_LL, SystemKeyHook,
			GetModuleHandleW(nullptr), 0);
	} else if (!hook && sHook != nullptr) {
		UnhookWindowsHookEx(sHook);
		sHook = nullptr;
		sHookTarget = nullptr;
	}
#else
	(void)hook;
#endif
}


// Host joysticks feed the game ports while the machine runs, at 50 Hz.
void
MainWindow::_PollJoysticks()
{
	if (!fPowered)
		return;
	for (int i = 0; i < 4; i++) {
		JoystickState state;
		if (!fJoysticks.Poll(i, state))
			continue;
		fEmulator.Post([i, state](bebox::Machine& machine) {
			if (machine.GamePorts() != nullptr)
				machine.GamePorts()->SetStick(i, state.x, state.y,
					state.button1, state.button2);
		});
	}
}
