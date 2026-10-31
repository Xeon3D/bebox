/*
 * BeBox emulator for Qt - the preferences.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "PrefsDialog.h"

#include "core/storage/DiskImage.h"
#include "core/storage/Qcow2Image.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cstring>

using bebox::MachineConfig;

namespace {

const char* const kSerialKinds[] = { "", "console", "tcp-listen:",
	"tcp-connect:", "host:", "file:" };
const int kSerialKindCount = 6;

const char* const kDiskFilters = "Disk images (*.img *.qcow2 *.hdf *.raw "
	"*.dsk);;All files (*)";
const char* const kCdFilters = "CD images (*.iso *.cue *.toast *.bin);;"
	"All files (*)";
const char* const kFloppyFilters = "Floppy images (*.img *.ima *.dsk);;"
	"All files (*)";


QString
Q(const std::string& text)
{
	return QString::fromStdString(text);
}


std::string
S(const QString& text)
{
	return text.trimmed().toStdString();
}


QComboBox*
Choices(const std::vector<std::pair<QString, int> >& choices, int selected)
{
	QComboBox* combo = new QComboBox;
	for (const auto& choice : choices) {
		combo->addItem(choice.first, choice.second);
		if (choice.second == selected)
			combo->setCurrentIndex(combo->count() - 1);
	}
	return combo;
}


int
Value(QComboBox* combo, int fallback = 0)
{
	QVariant data = combo->currentData();
	return data.isValid() ? data.toInt() : fallback;
}


QLabel*
Note(const QString& text)
{
	QLabel* label = new QLabel(text);
	label->setWordWrap(true);
	label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	QPalette palette = label->palette();
	palette.setColor(QPalette::WindowText,
		palette.color(QPalette::PlaceholderText));
	label->setPalette(palette);
	return label;
}


// A page's form: label and field rows, notes under them.
QWidget*
FormPage(QFormLayout*& form)
{
	QWidget* page = new QWidget;
	QVBoxLayout* layout = new QVBoxLayout(page);
	form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	layout->addLayout(form);
	return page;
}


void
FinishPage(QWidget* page)
{
	((QVBoxLayout*)page->layout())->addStretch(1);
}


// A path field with a button that chooses a file or folder.
QWidget*
PathField(QLineEdit*& edit, const QString& text, std::function<void()> choose,
	QPushButton** button = nullptr)
{
	QWidget* field = new QWidget;
	QHBoxLayout* layout = new QHBoxLayout(field);
	layout->setContentsMargins(0, 0, 0, 0);
	edit = new QLineEdit(text);
	edit->setClearButtonEnabled(true);
	QPushButton* browse = new QPushButton(QObject::tr("Choose…"));
	QObject::connect(browse, &QPushButton::clicked, choose);
	layout->addWidget(edit, 1);
	layout->addWidget(browse);
	if (button != nullptr)
		*button = browse;
	return field;
}

}	// namespace


QIcon
PageIcon(const char* name)
{
	QIcon icon;
	for (int size : { 16, 24, 32, 48, 64 })
		icon.addFile(QString(":/icons/%1-%2.png").arg(name).arg(size),
			QSize(size, size));
	return icon;
}


PrefsDialog::PrefsDialog(const Settings& settings, bool running,
	const std::vector<std::string>& joysticks, QWidget* parent)
	:
	QDialog(parent),
	fWork(settings),
	fRunning(running)
{
	setWindowTitle(tr("BeBox preferences: %1").arg(Q(settings.profile)));

	fSidebar = new QListWidget;
	fSidebar->setIconSize(QSize(24, 24));
	fSidebar->setUniformItemSizes(true);
	fSidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	fSidebar->setFixedWidth(190);

	fHeaderIcon = new QLabel;
	fHeaderTitle = new QLabel;
	QFont title = fHeaderTitle->font();
	title.setBold(true);
	title.setPointSizeF(title.pointSizeF() * 1.15);
	fHeaderTitle->setFont(title);
	QHBoxLayout* header = new QHBoxLayout;
	header->setContentsMargins(2, 2, 2, 2);
	header->addWidget(fHeaderIcon);
	header->addWidget(fHeaderTitle, 1);

	fPages = new QStackedWidget;
	QVBoxLayout* right = new QVBoxLayout;
	right->setContentsMargins(0, 0, 0, 0);
	right->addLayout(header);
	right->addWidget(fPages, 1);

	QHBoxLayout* body = new QHBoxLayout;
	body->addWidget(fSidebar);
	body->addLayout(right, 1);

	QFrame* line = new QFrame;
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
		| QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addLayout(body, 1);
	layout->addWidget(line);
	layout->addWidget(buttons);

	_AddPage("machine", tr("Machine"), _MachinePage());
	_AddPage("rom", tr("ROM & NVRAM"), _RomPage());
	_AddPage("display", tr("Display"), _DisplayPage());
	_AddPage("input", tr("Input devices"), _InputPage(joysticks));
	_AddPage("sound", tr("Sound"), _SoundPage());
	_AddPage("network", tr("Network"), _NetworkPage());
	_AddPage("ports", tr("Ports"), _PortsPage());
	_AddPage("storage", tr("Storage"), _StoragePage());

	connect(fSidebar, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0)
			return;
		fPages->setCurrentIndex(row);
		QListWidgetItem* item = fSidebar->item(row);
		fHeaderIcon->setPixmap(item->icon().pixmap(QSize(24, 24)));
		fHeaderTitle->setText(item->text());
	});
	fSidebar->setCurrentRow(0);
	resize(minimumSizeHint().expandedTo(QSize(760, 500)));
}


void
PrefsDialog::_AddPage(const char* icon, const QString& name, QWidget* page)
{
	QListWidgetItem* item = new QListWidgetItem(PageIcon(icon), name);
	item->setSizeHint(QSize(-1, fontMetrics().height() * 2 + 4));
	fSidebar->addItem(item);
	// pages without tabs get the tabs' frame
	if (qobject_cast<QTabWidget*>(page) == nullptr) {
		QFrame* frame = new QFrame;
		frame->setFrameShape(QFrame::StyledPanel);
		QVBoxLayout* layout = new QVBoxLayout(frame);
		layout->addWidget(page);
		page = frame;
	}
	fPages->addWidget(page);
}


QWidget*
PrefsDialog::_Tabs(const std::vector<std::pair<const char*,
	std::pair<QString, QWidget*> > >& tabs)
{
	QTabWidget* widget = new QTabWidget;
	widget->setIconSize(QSize(16, 16));
	for (const auto& tab : tabs)
		widget->addTab(tab.second.second, PageIcon(tab.first),
			tab.second.first);
	return widget;
}


// #pragma mark - Machine


QWidget*
PrefsDialog::_MachinePage()
{
	MachineConfig& machine = fWork.machine;
	QFormLayout* form;

	QWidget* general = FormPage(form);
	fModel = Choices({
		{ tr("Dual603-66 (2 × PowerPC 603, 66 MHz)"), 66 },
		{ tr("Dual603e-133 (2 × PowerPC 603e, 133 MHz)"), 133 },
		{ tr("Hobbit prototype (2 × AT&T 92010, 25 MHz)"), 25 } },
		machine.model == MachineConfig::kBeBox133 ? 133
			: machine.model == MachineConfig::kHobbit ? 25 : 66);
	fMemory = new QComboBox;
	fCpus = Choices({ { tr("Two (as built)"), 2 },
		{ tr("One (CPU 1 held in reset)"), 1 } }, machine.cpuCount);
	fSpeed = Choices({ { tr("50%"), 50 }, { tr("100% (a real BeBox)"), 100 },
		{ tr("200%"), 200 }, { tr("As fast as possible"), 0 } },
		fWork.speedLimit);
	form->addRow(tr("Model:"), fModel);
	form->addRow(tr("Memory:"), fMemory);
	form->addRow(tr("Processors:"), fCpus);
	form->addRow(tr("Speed:"), fSpeed);
	fMachineNote = Note("");
	general->layout()->addWidget(fMachineNote);
	FinishPage(general);

	QWidget* boards = FormPage(form);
	fBoard = Choices({ { tr("Revision 5 (128 KB boot ROM flash)"), 5 },
		{ tr("Revision 6 (512 KB flash)"), 6 }, { tr("Revision 3"), 3 } },
		machine.boardRevision);
	fIoCard = Choices({ { tr("Revision 5"), 5 }, { tr("Revision 4"), 4 } },
		machine.ioCardRevision);
	form->addRow(tr("Motherboard:"), fBoard);
	form->addRow(tr("I/O card:"), fIoCard);
	boards->layout()->addWidget(Note(tr("Motherboard and I/O card are "
		"BeBox options; the Hobbit prototype has neither.")));
	FinishPage(boards);

	QWidget* performance = FormPage(form);
	fQuantum = Choices({ { tr("1 µs"), 1 }, { tr("5 µs"), 5 },
		{ tr("10 µs (default)"), 10 }, { tr("50 µs"), 50 } },
		machine.quantumUs);
	fCpi = Choices({ { "0.5", 50 }, { tr("1.0 (default)"), 100 },
		{ "1.5", 150 }, { "2.0", 200 } }, machine.cpiPercent);
	fThreads = new QCheckBox(tr("Run each processor on a host core of its "
		"own"));
	fThreads->setChecked(machine.threadPerCpu);
	form->addRow(tr("CPU time slice:"), fQuantum);
	form->addRow(tr("Cycles per instruction:"), fCpi);
	form->addRow(fThreads);
	performance->layout()->addWidget(Note(tr("The processors run in slices "
		"of emulated time, the devices in between: shorter slices are "
		"closer to the real machine and slower. On cores of their own, "
		"both processors run at the same time: up to nearly twice as fast "
		"when BeOS keeps both busy.")));
	FinishPage(performance);

	_FillMemory(machine.model == MachineConfig::kHobbit, machine.ramMB);
	connect(fModel, &QComboBox::currentIndexChanged, this, [this]() {
		_FillMemory(Value(fModel) == 25, Value(fMemory, 32));
		_UpdateMachineControls();
	});
	_UpdateMachineControls();

	return _Tabs({ { "machine", { tr("Machine"), general } },
		{ "board", { tr("Boards"), boards } },
		{ "performance", { tr("Performance"), performance } } });
}


void
PrefsDialog::_FillMemory(bool hobbit, int current)
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
	fMemory->clear();
	for (int i = 0; i < count; i++) {
		fMemory->addItem(tr("%1 MB").arg(sizes[i]), sizes[i]);
		if (sizes[i] == selected)
			fMemory->setCurrentIndex(i);
	}
}


void
PrefsDialog::_UpdateMachineControls()
{
	bool hobbit = Value(fModel) == 25;
	fBoard->setEnabled(!hobbit);
	fIoCard->setEnabled(!hobbit);
	QString note = hobbit
		? tr("The Hobbit prototype takes the memory sizes its kernel knows "
			"(4 to 32 MB).")
		: tr("The speed is the processors' speed: BeOS's clock keeps real "
			"time at any setting.");
	if (fRunning)
		note += "\n\n" + tr("Changes to the hardware restart the machine.");
	fMachineNote->setText(note);
}


// #pragma mark - ROM & NVRAM


QWidget*
PrefsDialog::_RomPage()
{
	QFormLayout* form;
	QWidget* page = FormPage(form);
	form->addRow(tr("ROM folder:"), PathField(fRomDirectory,
		Q(fWork.romDirectory), [this]() {
			QString folder = QFileDialog::getExistingDirectory(this,
				tr("ROM folder"), fRomDirectory->text());
			if (!folder.isEmpty())
				fRomDirectory->setText(QDir::toNativeSeparators(folder));
		}));
	form->addRow(tr("Boot ROM image:"), PathField(fBootRom,
		Q(fWork.bootRomImage), [this]() { _ChooseBootRom(); }));
	form->addRow(tr("Hobbit kernel:"), PathField(fHobbitKernel,
		Q(fWork.machine.hobbitKernelPath), [this]() {
			QString path = QFileDialog::getOpenFileName(this,
				tr("Hobbit kernel"), fHobbitKernel->text());
			if (!path.isEmpty())
				fHobbitKernel->setText(QDir::toNativeSeparators(path));
		}));
	page->layout()->addWidget(Note(tr("The folder holds bootnub.rom and "
		"bootmain.rom from a BeBox. A boot ROM image replaces bootmain.rom: "
		"a ROM update image (bebox_bootmain.image), or a PowerPC BeOS CD "
		"image (PR2, R3, R4.5) to copy it from.")));

	QHBoxLayout* buttons = new QHBoxLayout;
	QPushButton* resetFlash = new QPushButton(tr("Reset boot ROM flash"));
	QPushButton* clearNvram = new QPushButton(tr("Clear NVRAM"));
	resetFlash->setEnabled(!fRunning);
	clearNvram->setEnabled(!fRunning);
	connect(resetFlash, &QPushButton::clicked, this, [this]() {
		if (QMessageBox::question(this, tr("BeBox"), tr("Reset the boot ROM "
				"flash to the ROM files? Boot ROM updates written by BeOS "
				"are lost.")) == QMessageBox::Yes)
			QFile::remove(Q(fWork.FlashPath()));
	});
	connect(clearNvram, &QPushButton::clicked, this, [this]() {
		if (QMessageBox::question(this, tr("BeBox"), tr("Clear the NVRAM? "
				"The boot preferences (boot disk) are lost."))
				== QMessageBox::Yes)
			QFile::remove(Q(fWork.NvramPath()));
	});
	buttons->addWidget(resetFlash);
	buttons->addWidget(clearNvram);
	buttons->addStretch(1);
	((QVBoxLayout*)page->layout())->addLayout(buttons);
	page->layout()->addWidget(Note(tr("The flash holds the boot ROM (and "
		"what BeOS's ROM updater wrote to it); the NVRAM the boot "
		"preferences. Both are kept per profile. The Hobbit prototype has "
		"no boot ROM here: BeBox starts its kernel itself.")));
	FinishPage(page);
	return page;
}


void
PrefsDialog::_ChooseBootRom()
{
	QString path = QFileDialog::getOpenFileName(this,
		tr("Boot ROM image (or a BeOS CD image)"), fBootRom->text(),
		tr("Boot ROM and CD images (*.image *.rom *.iso *.cue *.toast);;"
			"All files (*)"));
	if (path.isEmpty())
		return;
	std::string error;
	std::string result = BootRomFromFile(path.toStdString(), error);
	if (result.empty()) {
		QMessageBox::warning(this, tr("BeBox"), Q(error));
		return;
	}
	fBootRom->setText(QDir::toNativeSeparators(Q(result)));
}


// #pragma mark - Display


QWidget*
PrefsDialog::_DisplayPage()
{
	QFormLayout* form;
	QWidget* video = FormPage(form);
	fVideo = Choices({
		{ tr("Number Nine #9 GXE64 (S3 Vision864)"),
			MachineConfig::kVideoS3Vision864 },
		{ tr("Cirrus Logic CL-GD5434"), MachineConfig::kVideoCirrus5434 },
		{ tr("None"), MachineConfig::kVideoNone } },
		fWork.machine.videoCard);
	fVram = Choices({ { tr("1 MB"), 1 }, { tr("2 MB"), 2 }, { tr("4 MB"), 4 } },
		fWork.machine.vramMB);
	form->addRow(tr("Video card:"), fVideo);
	form->addRow(tr("Video memory:"), fVram);
	FinishPage(video);

	QWidget* panel = FormPage(form);
	fLedStyle = Choices({ { tr("Authentic (lime and amber)"),
		kLedStyleAuthentic }, { tr("Red and green"), kLedStyleRedGreen } },
		fWork.ledStyle);
	fShowLeds = new QCheckBox(tr("Show the front panel LEDs"));
	fShowLeds->setChecked(fWork.showLeds);
	form->addRow(tr("LED colors:"), fLedStyle);
	form->addRow(fShowLeds);
	panel->layout()->addWidget(Note(tr("The LEDs are the same in all "
		"profiles.")));
	FinishPage(panel);

	return _Tabs({ { "display", { tr("Video"), video } },
		{ "leds", { tr("Front panel"), panel } } });
}


// #pragma mark - Input devices


QWidget*
PrefsDialog::_InputPage(const std::vector<std::string>& joysticks)
{
	QFormLayout* form;
	QWidget* keyboard = FormPage(form);
#ifdef Q_OS_MACOS
	fHostKey = Choices({ { tr("Right Control"), kHostKeyRightControl },
		{ tr("Right Option"), kHostKeyRightAlt },
		{ tr("Right Command"), kHostKeyRightWindows } }, fWork.hostKey);
#else
	fHostKey = Choices({ { tr("Right Control"), kHostKeyRightControl },
		{ tr("Right Alt"), kHostKeyRightAlt },
		{ tr("Right Windows key"), kHostKeyRightWindows },
		{ tr("Menu key"), kHostKeyMenu } }, fWork.hostKey);
#endif
	fMouseSpeed = new QSlider(Qt::Horizontal);
	fMouseSpeed->setRange(25, 400);
	fMouseSpeed->setValue(fWork.mouseSpeed);
	fSwapButtons = new QCheckBox(tr("Swap the left and right mouse buttons"));
	fSwapButtons->setChecked(fWork.swapMouseButtons);
	fCaptureOnClick = new QCheckBox(tr("Clicking the screen captures the "
		"mouse"));
	fCaptureOnClick->setChecked(fWork.captureOnClick);
	form->addRow(tr("Mouse release key:"), fHostKey);
	form->addRow(tr("Mouse speed:"), fMouseSpeed);
	form->addRow(fSwapButtons);
	form->addRow(fCaptureOnClick);
	keyboard->layout()->addWidget(Note(tr("While the window is active, all "
		"keys go to the BeBox except the release key. The release key also "
		"captures the mouse; with F it switches to full screen and "
		"back.")));
	FinishPage(keyboard);

	QWidget* sticks = FormPage(form);
	std::vector<std::pair<QString, int> > choices = { { tr("None"), -1 } };
	for (size_t i = 0; i < joysticks.size(); i++)
		choices.push_back({ QString("%1: %2").arg(i + 1)
			.arg(Q(joysticks[i])), (int)i });
	for (int i = 0; i < 4; i++) {
		int selected = fWork.joystick[i].empty() ? -1
			: atoi(fWork.joystick[i].c_str());
		fSticks[i] = Choices(choices, selected);
		form->addRow(tr("Stick %1:").arg(i + 1), fSticks[i]);
	}
	sticks->layout()->addWidget(Note(joysticks.empty()
		? tr("No joysticks or game controllers are connected to this "
			"computer.")
		: tr("Sticks 1 and 2 are on the upper game port, 3 and 4 on the "
			"lower one.")));
	FinishPage(sticks);

	return _Tabs({ { "keyboard", { tr("Keyboard && mouse"), keyboard } },
		{ "joystick", { tr("Game ports"), sticks } } });
}


// #pragma mark - Sound, network, ports


QWidget*
PrefsDialog::_SoundPage()
{
	QFormLayout* form;
	QWidget* page = FormPage(form);
	fSound = new QCheckBox(tr("Play the sound output (CS4231A codec)"));
	fSound->setChecked(fWork.soundEnabled);
	fVolume = new QSlider(Qt::Horizontal);
	fVolume->setRange(0, 100);
	fVolume->setValue(fWork.soundVolume);
	form->addRow(fSound);
	form->addRow(tr("Volume:"), fVolume);
#ifndef HAVE_QT_MULTIMEDIA
	page->layout()->addWidget(Note(tr("This build of BeBox has no sound "
		"(built without Qt Multimedia).")));
#endif
	FinishPage(page);
	return page;
}


QWidget*
PrefsDialog::_NetworkPage()
{
	QFormLayout* form;
	QWidget* page = FormPage(form);
	fEthernet = new QCheckBox(tr("Ethernet card (NE2000 compatible, ISA)"));
	fEthernet->setChecked(fWork.ethernet);
	fEthernetPort = Choices({ { "220", 0x220 }, { "240", 0x240 },
		{ "280", 0x280 }, { "2A0", 0x2a0 }, { "2C0", 0x2c0 },
		{ "300", 0x300 }, { "320", 0x320 }, { "340", 0x340 },
		{ "3C0", 0x3c0 } }, fWork.ethernetPort);
	fEthernetIrq = Choices({ { "5", 5 }, { "9", 9 }, { "10", 10 },
		{ "11", 11 }, { "15", 15 } }, fWork.ethernetIrq);
	fMacAddress = new QLineEdit(Q(fWork.macAddress));
	fMacAddress->setInputMask("HH:HH:HH:HH:HH:HH");
	fPortForwards = new QLineEdit(Q(fWork.portForwards));
	fPortForwards->setPlaceholderText("tcp:2121:21,tcp:8080:80");
	form->addRow(fEthernet);
	form->addRow(tr("I/O port:"), fEthernetPort);
	form->addRow(tr("IRQ:"), fEthernetIrq);
	form->addRow(tr("MAC address:"), fMacAddress);
	form->addRow(tr("Forwarded host ports:"), fPortForwards);
	page->layout()->addWidget(Note(tr("The BeBox is on a private network "
		"behind this computer (NAT): in BeOS's Network preferences, add a "
		"Novell NE2000 interface at the port and IRQ above. IP 10.0.2.15, "
		"mask 255.255.255.0, gateway 10.0.2.2, DNS 10.0.2.3 (or DHCP). The "
		"host is 10.0.2.2. Forwarded host ports reach the BeBox.")));
	auto update = [this]() {
		bool on = fEthernet->isChecked();
		for (QWidget* field : { (QWidget*)fEthernetPort,
				(QWidget*)fEthernetIrq, (QWidget*)fMacAddress,
				(QWidget*)fPortForwards })
			field->setEnabled(on);
	};
	connect(fEthernet, &QCheckBox::toggled, this, update);
	update();
	FinishPage(page);
	return page;
}


QWidget*
PrefsDialog::_PortsPage()
{
	QFormLayout* form;
	QWidget* page = FormPage(form);
#if defined(Q_OS_WIN)
	QString device = tr("COM1");
#elif defined(Q_OS_MACOS)
	QString device = tr("/dev/cu.usbserial");
#elif defined(Q_OS_HAIKU)
	QString device = tr("/dev/ports/usb0");
#else
	QString device = tr("/dev/ttyUSB0");
#endif
	const QString hints[kSerialKindCount] = { "", "",
		tr("Port, or address:port (e.g. 2001)"),
		tr("Host:port (e.g. localhost:2001)"), tr("Device (e.g. %1)")
			.arg(device), tr("File path") };
	for (int i = 0; i < 4; i++) {
		const std::string& destination = fWork.serial[i];
		int kind = 0;
		for (int k = 1; k < kSerialKindCount; k++) {
			size_t length = strlen(kSerialKinds[k]);
			if (k == 1 ? destination == kSerialKinds[k]
					: destination.compare(0, length, kSerialKinds[k]) == 0)
				kind = k;
		}
		fSerialKind[i] = Choices({ { tr("Not connected"), 0 },
			{ tr("Console window"), 1 }, { tr("TCP server"), 2 },
			{ tr("TCP client"), 3 }, { tr("Host serial port"), 4 },
			{ tr("Log to file"), 5 } }, kind);
		fSerialTarget[i] = new QLineEdit(kind >= 2
			? Q(destination.substr(strlen(kSerialKinds[kind]))) : "");
		QWidget* row = new QWidget;
		QHBoxLayout* layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(fSerialKind[i]);
		layout->addWidget(fSerialTarget[i], 1);
		form->addRow(i == 3 ? tr("Serial 4 (debug):")
			: tr("Serial %1:").arg(i + 1), row);
		auto update = [this, i, hints]() {
			int kind = Value(fSerialKind[i]);
			fSerialTarget[i]->setEnabled(kind >= 2);
			fSerialTarget[i]->setPlaceholderText(hints[kind]);
		};
		connect(fSerialKind[i], &QComboBox::currentIndexChanged, this,
			update);
		update();
	}
	page->layout()->addWidget(Note(tr("Serial 4 carries the kernel's debug "
		"output (Machine > Restart with debug output).")));
	FinishPage(page);
	return page;
}


// #pragma mark - Storage


QWidget*
PrefsDialog::_StoragePage()
{
	MachineConfig& machine = fWork.machine;

	// SCSI: kind, image, read only per ID
	QWidget* scsi = new QWidget;
	QVBoxLayout* scsiLayout = new QVBoxLayout(scsi);
	QGridLayout* grid = new QGridLayout;
	grid->setColumnStretch(2, 1);
	for (int id = 0; id < 7; id++) {
		const MachineConfig::ScsiTarget& target = machine.scsi[id];
		fScsiKind[id] = Choices({ { tr("None"),
				MachineConfig::ScsiTarget::kNone },
			{ tr("Disk"), MachineConfig::ScsiTarget::kDisk },
			{ tr("CD-ROM"), MachineConfig::ScsiTarget::kCdRom },
			{ tr("Shared folder"),
				MachineConfig::ScsiTarget::kSharedFolder } }, target.kind);
		QWidget* path = PathField(fScsiPath[id], Q(target.image), [this, id]() {
			int kind = Value(fScsiKind[id]);
			QString title = tr("SCSI ID %1").arg(id);
			QString chosen = kind == MachineConfig::ScsiTarget::kSharedFolder
				? QFileDialog::getExistingDirectory(this, title,
					fScsiPath[id]->text())
				: QFileDialog::getOpenFileName(this, title,
					fScsiPath[id]->text(), kind
						== MachineConfig::ScsiTarget::kCdRom
						? tr(kCdFilters) : tr(kDiskFilters));
			if (!chosen.isEmpty())
				fScsiPath[id]->setText(QDir::toNativeSeparators(chosen));
		}, &fScsiChoose[id]);
		fScsiReadOnly[id] = new QCheckBox(tr("Read only"));
		fScsiReadOnly[id]->setChecked(target.readOnly);
		grid->addWidget(new QLabel(tr("ID %1:").arg(id)), id, 0);
		grid->addWidget(fScsiKind[id], id, 1);
		grid->addWidget(path, id, 2);
		grid->addWidget(fScsiReadOnly[id], id, 3);
		connect(fScsiKind[id], &QComboBox::currentIndexChanged, this,
			[this, id]() { _UpdateScsiRow(id); });
		_UpdateScsiRow(id);
	}
	scsiLayout->addLayout(grid);
	QHBoxLayout* newDisk = new QHBoxLayout;
	QPushButton* create = new QPushButton(tr("New disk image…"));
	connect(create, &QPushButton::clicked, this, [this]() { _NewDisk(); });
	newDisk->addWidget(create);
	newDisk->addStretch(1);
	scsiLayout->addLayout(newDisk);
	scsiLayout->addWidget(Note(tr("Disk images: raw, or QCOW2 (grows as it "
		"fills). Shared folder: a folder of this computer BeOS sees as a "
		"disk (Advanced Access and later); BeOS's changes come back to the "
		"folder when it shuts down.")));
	scsiLayout->addStretch(1);

	// IDE: master and slave
	QFormLayout* form;
	QWidget* ide = FormPage(form);
	for (int i = 0; i < 2; i++) {
		QWidget* row = new QWidget;
		QHBoxLayout* layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(PathField(fIdePath[i], Q(machine.ideImage[i]),
			[this, i]() {
				QString path = QFileDialog::getOpenFileName(this, i == 0
					? tr("IDE master") : tr("IDE slave"), fIdePath[i]->text(),
					tr(kDiskFilters));
				if (!path.isEmpty())
					fIdePath[i]->setText(QDir::toNativeSeparators(path));
			}), 1);
		fIdeReadOnly[i] = new QCheckBox(tr("Read only"));
		fIdeReadOnly[i]->setChecked(machine.ideReadOnly[i]);
		layout->addWidget(fIdeReadOnly[i]);
		form->addRow(i == 0 ? tr("Master:") : tr("Slave:"), row);
	}
	FinishPage(ide);

	// the floppy drive
	QWidget* floppy = FormPage(form);
	form->addRow(tr("Disk:"), PathField(fFloppyPath, Q(machine.floppyImage),
		[this]() {
			QString path = QFileDialog::getOpenFileName(this, tr("Floppy"),
				fFloppyPath->text(), tr(kFloppyFilters));
			if (!path.isEmpty())
				fFloppyPath->setText(QDir::toNativeSeparators(path));
		}));
	fFloppyProtect = new QCheckBox(tr("Write protect"));
	fFloppyProtect->setChecked(machine.floppyWriteProtected);
	form->addRow(fFloppyProtect);
	floppy->layout()->addWidget(Note(tr("The floppy and the CDs can also "
		"change while the machine runs (Media menu, or drop an image on the "
		"window).")));
	FinishPage(floppy);

	return _Tabs({ { "scsi", { tr("SCSI"), scsi } },
		{ "ide", { tr("IDE"), ide } },
		{ "floppy", { tr("Floppy"), floppy } } });
}


void
PrefsDialog::_UpdateScsiRow(int id)
{
	int kind = Value(fScsiKind[id]);
	bool none = kind == MachineConfig::ScsiTarget::kNone;
	fScsiPath[id]->setEnabled(!none);
	fScsiChoose[id]->setEnabled(!none);
	// discs and shared folders: never written / written back on shutdown
	fScsiReadOnly[id]->setEnabled(kind == MachineConfig::ScsiTarget::kDisk);
}


void
PrefsDialog::_NewDisk()
{
	QString path = AskNewDisk(this, Q(Settings::Directory()));
	if (path.isEmpty())
		return;
	// on the first free SCSI ID
	for (int id = 0; id < 7; id++) {
		if (Value(fScsiKind[id]) != MachineConfig::ScsiTarget::kNone)
			continue;
		fScsiKind[id]->setCurrentIndex(1);
		fScsiPath[id]->setText(QDir::toNativeSeparators(path));
		QMessageBox::information(this, tr("BeBox"),
			tr("The new disk is on SCSI ID %1.").arg(id));
		return;
	}
	QMessageBox::information(this, tr("BeBox"), tr("The new disk is %1; all "
		"SCSI IDs are taken.").arg(QDir::toNativeSeparators(path)));
}


// #pragma mark - OK


// OK, Cancel and the close box: not while anything the dialog opened (a
// message, a file chooser) is still open.
void
PrefsDialog::done(int result)
{
	if (QApplication::activeModalWidget() != this)
		return;
	QDialog::done(result);
}


void
PrefsDialog::accept()
{
	// the MAC address must be one (the mask makes it hexadecimal)
	if (fEthernet->isChecked() && !fMacAddress->hasAcceptableInput()) {
		fSidebar->setCurrentRow(5);
		QMessageBox::warning(this, tr("BeBox"), tr("The MAC address is six "
			"hexadecimal bytes, e.g. 00:00:e8:12:34:56."));
		return;
	}

	MachineConfig& machine = fWork.machine;
	int model = Value(fModel, 66);
	machine.model = model == 133 ? MachineConfig::kBeBox133
		: model == 25 ? MachineConfig::kHobbit : MachineConfig::kBeBox66;
	machine.ramMB = Value(fMemory, 32);
	machine.cpuCount = Value(fCpus, 2);
	fWork.speedLimit = Value(fSpeed, 100);
	machine.boardRevision = Value(fBoard, 6);
	machine.ioCardRevision = Value(fIoCard, 5);
	machine.quantumUs = Value(fQuantum, 10);
	machine.cpiPercent = Value(fCpi, 100);
	machine.threadPerCpu = fThreads->isChecked();

	fWork.romDirectory = S(fRomDirectory->text());
	fWork.bootRomImage = S(fBootRom->text());
	machine.hobbitKernelPath = S(fHobbitKernel->text());

	machine.videoCard = (MachineConfig::VideoCard)Value(fVideo,
		MachineConfig::kVideoS3Vision864);
	machine.vramMB = Value(fVram, 2);
	fWork.ledStyle = Value(fLedStyle);
	fWork.showLeds = fShowLeds->isChecked();

	fWork.hostKey = Value(fHostKey);
	fWork.mouseSpeed = fMouseSpeed->value();
	fWork.swapMouseButtons = fSwapButtons->isChecked();
	fWork.captureOnClick = fCaptureOnClick->isChecked();
	for (int i = 0; i < 4; i++) {
		int stick = Value(fSticks[i], -1);
		fWork.joystick[i] = stick < 0 ? "" : std::to_string(stick);
	}

	fWork.soundEnabled = fSound->isChecked();
	fWork.soundVolume = fVolume->value();

	fWork.ethernet = fEthernet->isChecked();
	fWork.ethernetPort = Value(fEthernetPort, 0x300);
	fWork.ethernetIrq = Value(fEthernetIrq, 5);
	fWork.macAddress = S(fMacAddress->text().toLower());
	fWork.portForwards = S(fPortForwards->text());

	for (int i = 0; i < 4; i++) {
		int kind = Value(fSerialKind[i]);
		std::string destination = kSerialKinds[kind];
		if (kind >= 2)
			destination += S(fSerialTarget[i]->text());
		fWork.serial[i] = destination;
	}

	for (int i = 0; i < 2; i++) {
		machine.ideImage[i] = S(fIdePath[i]->text());
		machine.ideReadOnly[i] = fIdeReadOnly[i]->isChecked();
	}
	for (int id = 0; id < 7; id++) {
		MachineConfig::ScsiTarget& target = machine.scsi[id];
		target.kind = (MachineConfig::ScsiTarget::Kind)Value(fScsiKind[id]);
		target.image = S(fScsiPath[id]->text());
		target.readOnly = fScsiReadOnly[id]->isChecked();
	}
	machine.floppyImage = S(fFloppyPath->text());
	machine.floppyWriteProtected = fFloppyProtect->isChecked();
	Settings::DropEmptyDevices(machine);
	QDialog::accept();
}


// #pragma mark - new disk images


QString
AskNewDisk(QWidget* parent, const QString& directory)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("New disk image"));
	QFormLayout* form = new QFormLayout;
	QLineEdit* path;
	QWidget* pathField = PathField(path, QDir::toNativeSeparators(
		directory + "/Disk.qcow2"), [&]() {
			QString chosen = QFileDialog::getSaveFileName(&dialog,
				QObject::tr("New disk image"), path->text(),
				QObject::tr("QCOW2 disk images (*.qcow2);;Raw disk images "
					"(*.img)"));
			if (!chosen.isEmpty())
				path->setText(QDir::toNativeSeparators(chosen));
		});
	QComboBox* size = Choices({ { "100 MB", 100 }, { "250 MB", 250 },
		{ "500 MB", 500 }, { "1 GB", 1024 }, { "2 GB", 2048 },
		{ "4 GB", 4096 }, { "8 GB", 8192 } }, 1024);
	QComboBox* format = Choices({
		{ QObject::tr("QCOW2 (grows as it fills)"), 0 },
		{ QObject::tr("Raw (its full size at once)"), 1 } }, 0);
	form->addRow(QObject::tr("File:"), pathField);
	form->addRow(QObject::tr("Size:"), size);
	form->addRow(QObject::tr("Format:"), format);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
		| QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Create"));
	QVBoxLayout* layout = new QVBoxLayout(&dialog);
	layout->addLayout(form);
	layout->addWidget(buttons);
	dialog.setMinimumWidth(480);

	QString created;
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
		&QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		QString file = path->text().trimmed();
		if (file.isEmpty())
			return;
		if (QFileInfo::exists(file)) {
			QMessageBox::warning(&dialog, QObject::tr("BeBox"),
				QObject::tr("%1 exists already.").arg(file));
			return;
		}
		bebox::u64 bytes = (bebox::u64)Value(size, 1024) << 20;
		std::string error;
		bool ok = Value(format) == 1
			? bebox::RawImage::Create(file.toStdString(), bytes, error)
			: bebox::Qcow2Image::Create(file.toStdString(), bytes, error);
		if (!ok) {
			QMessageBox::warning(&dialog, QObject::tr("BeBox"),
				QObject::tr("The disk image could not be created: %1")
					.arg(Q(error)));
			return;
		}
		created = file;
		dialog.accept();
	});
	dialog.exec();
	return created;
}
