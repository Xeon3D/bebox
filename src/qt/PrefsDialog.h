/*
 * BeBox emulator for Qt - the preferences.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * A sidebar of pages (Machine, ROM & NVRAM, Display, Input devices,
 * Sound, Network, Ports, Storage), each page under a header with its icon
 * and name, the larger ones in tabs. It edits a copy of the settings; the
 * caller takes the result and decides what it means for a running machine.
 */
#ifndef QT_PREFS_DIALOG_H
#define QT_PREFS_DIALOG_H

#include "frontend/Settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTabWidget;

// An icon of the preferences (and the window's menus): "machine",
// "storage", ... in all its sizes.
QIcon PageIcon(const char* name);

class PrefsDialog : public QDialog {
	Q_OBJECT
public:
								PrefsDialog(const Settings& settings,
									bool running,
									const std::vector<std::string>& joysticks,
									QWidget* parent = nullptr);

			// The settings as edited (after accept).
			const Settings&		Result() const { return fWork; }

	virtual	void				accept() override;
	virtual	void				done(int result) override;

private:
			void				_AddPage(const char* icon, const QString& name,
									QWidget* page);
			QWidget*			_Tabs(const std::vector<std::pair<const char*,
									std::pair<QString, QWidget*> > >& tabs);

			QWidget*			_MachinePage();
			QWidget*			_RomPage();
			QWidget*			_DisplayPage();
			QWidget*			_InputPage(
									const std::vector<std::string>& joysticks);
			QWidget*			_SoundPage();
			QWidget*			_NetworkPage();
			QWidget*			_PortsPage();
			QWidget*			_StoragePage();

			void				_FillMemory(bool hobbit, int current);
			void				_UpdateMachineControls();
			void				_UpdateScsiRow(int id);
			void				_ChooseBootRom();
			void				_NewDisk();

			Settings			fWork;
			bool				fRunning;

			QListWidget*		fSidebar;
			QLabel*				fHeaderIcon;
			QLabel*				fHeaderTitle;
			QStackedWidget*		fPages;

			// Machine
			QComboBox*			fModel;
			QComboBox*			fMemory;
			QComboBox*			fCpus;
			QComboBox*			fSpeed;
			QComboBox*			fBoard;
			QComboBox*			fIoCard;
			QComboBox*			fQuantum;
			QComboBox*			fCpi;
			QCheckBox*			fThreads;
			QLabel*				fMachineNote;
			// ROM & NVRAM
			QLineEdit*			fRomDirectory;
			QLineEdit*			fBootRom;
			QLineEdit*			fHobbitKernel;
			// Display
			QComboBox*			fVideo;
			QComboBox*			fVram;
			QComboBox*			fLedStyle;
			QCheckBox*			fShowLeds;
			// Input devices
			QComboBox*			fHostKey;
			QSlider*			fMouseSpeed;
			QCheckBox*			fSwapButtons;
			QCheckBox*			fCaptureOnClick;
			QComboBox*			fSticks[4];
			// Sound
			QCheckBox*			fSound;
			QSlider*			fVolume;
			// Network
			QCheckBox*			fEthernet;
			QComboBox*			fEthernetPort;
			QComboBox*			fEthernetIrq;
			QLineEdit*			fMacAddress;
			QLineEdit*			fPortForwards;
			// Ports
			QComboBox*			fSerialKind[4];
			QLineEdit*			fSerialTarget[4];
			// Storage
			QLineEdit*			fIdePath[2];
			QCheckBox*			fIdeReadOnly[2];
			QComboBox*			fScsiKind[7];
			QLineEdit*			fScsiPath[7];
			QPushButton*		fScsiChoose[7];
			QCheckBox*			fScsiReadOnly[7];
			QLineEdit*			fFloppyPath;
			QCheckBox*			fFloppyProtect;
};

// Asks for a new disk image and creates it; its path, or "" if canceled.
QString AskNewDisk(QWidget* parent, const QString& directory);

#endif	// QT_PREFS_DIALOG_H
