/*
 * BeBox emulator for Qt - the machine's window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The screen between the two front panel LED columns, a menu bar and a
 * status line (the profile, the model, the speed; the floppy, CDs and
 * disk light). Keys go to the BeBox's keyboard while the window is active
 * and the machine is on, except the mouse release key ("host key"),
 * which with F switches to full screen and back.
 */
#ifndef QT_MAIN_WINDOW_H
#define QT_MAIN_WINDOW_H

#include "Joysticks.h"

#include "frontend/JobQueue.h"
#include "frontend/Settings.h"

#include "core/Log.h"
#include "core/host/EmulatorThread.h"

#include <QMainWindow>

#include <memory>
#include <set>
#include <string>
#include <vector>

class LedWidget;
class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QTimer;
class AudioOutput;
class ScreenWidget;
class SerialConsole;

namespace bebox {
	class CdImage;
	class HostAttributes;
	class NetworkBackend;
	class SerialBackend;
	class SharedFolder;
}

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
								MainWindow(Settings& settings);
	virtual						~MainWindow();

			bool				PowerOn();
			void				PowerOff();

protected:
	virtual	bool				eventFilter(QObject* watched, QEvent* event)
									override;
	virtual	void				closeEvent(QCloseEvent* event) override;
	virtual	void				changeEvent(QEvent* event) override;
	virtual	void				dragEnterEvent(QDragEnterEvent* event)
									override;
	virtual	void				dropEvent(QDropEvent* event) override;

private:
			void				_BuildMenus();
			void				_BuildProfileMenu();
			void				_BuildMediaMenu();
			void				_UpdateMenus();

			bool				_StartMachine(const bebox::MachineConfig&
									config);
			void				_PrepareSharedFolders(
									const bebox::MachineConfig& config);
			void				_SharedFoldersReady(int generation);
			void				_SyncSharedFolders(bool interactive);
			void				_ApplySettings();
			void				_ShowPreferences();
			void				_SwitchProfile(const std::string& name,
									bool create);
			void				_NewProfile();
			void				_RenameProfile();
			void				_DeleteProfile();
			void				_Reset();
			void				_RestartWith(bool debug);
			void				_HoldKeyDuringBoot(bebox::u32 set2,
									double seconds);
			void				_PressKeyCombination(
									const std::vector<const char*>& keys);
			void				_ShowProblem(const QString& text);
	static	void				_LogSink(bebox::LogCategory category,
									bebox::LogLevel level,
									const char* message, void* cookie);

			void				_InsertFloppy(const std::string& path);
			void				_EjectFloppy();
			std::vector<int>	_CdDrives() const;
			bool				_InsertCd(int id, const std::string& path);
			void				_EjectCd(int id);
			void				_ChangeCd(int id,
									std::unique_ptr<bebox::CdImage> image);
			void				_CdEjected(int id, const std::string& path);

			bool				_ShowLeds() const;
			void				_FitWindow();
			void				_SetFullScreen(bool fullScreen);
			void				_UpdateStatus();
			void				_SaveScreenshot();
			void				_ShowConsole(int port);
			void				_About();

			bool				_HandleKey(QKeyEvent* event);
			bool				_HandleKeyCode(bebox::u32 code, bool down,
									bool repeat, bool fullScreenKey);
#ifdef Q_OS_HAIKU
			bool				_HandleHaikuKeys(QKeyEvent* event);
#endif
			void				_SendKey(bebox::u32 code, bool down);
			void				_ReleaseAllKeys();
			bebox::u32			_HostKeyCode() const;
			QString				_HostKeyName() const;
			void				_SetSystemKeyHook(bool hook);
			void				_PollJoysticks();

			Settings&			fSettings;
			bebox::EmulatorThread fEmulator;
			ScreenWidget*		fScreen;
			LedWidget*			fLeftLeds;
			LedWidget*			fRightLeds;
			AudioOutput*			fAudio;
			SerialConsole*		fConsoles[4];
			Joysticks			fJoysticks;
			std::vector<std::unique_ptr<bebox::SerialBackend> >
								fSerialBackends;
			std::unique_ptr<bebox::NetworkBackend> fNetwork;
			std::unique_ptr<bebox::HostAttributes> fHostAttributes;
			std::vector<std::unique_ptr<bebox::SharedFolder> >
								fSharedFolders;
			JobQueue			fFolderJobs;
			int					fFolderStarts;
			int					fFolderSyncs;
			struct FolderStart;
			std::shared_ptr<FolderStart> fFolderStart;

			QMenu*				fProfileMenu;
			QMenu*				fMediaMenu;
			QAction*			fPowerAction;
			QAction*			fPauseAction;
			QAction*			fCtrlAltDeleteAction;
			QAction*			fInterruptAction;
			QAction*			fFullScreenAction;
			QAction*			fFullScreenScaleActions[3];
			QAction*			fScaleActions[3];
			QAction*			fShowLedsAction;
			QAction*			fLedStyleActions[2];
			QLabel*				fStatus;
			QPushButton*		fMedia;
			QTimer*				fStatusTimer;
			QTimer*				fJoystickTimer;

			bool				fPowered;
			bool				fStarting;
			bool				fFullScreen;
			bool				fWasMaximized;

			std::set<bebox::u32> fKeysDown;
			bool				fHostKeyDown;
			bool				fHostComboUsed;
#ifdef Q_OS_HAIKU
			// Haiku's key states (key_info) as last seen; the key last
			// pressed, for its auto-repeat
			unsigned char		fHaikuKeys[16];
			bebox::u32			fHaikuLastKey;
#endif

			QString				fProblem;
			qint64				fProblemTime;
			std::set<QString>	fProblemsAlerted;
};

#endif	// QT_MAIN_WINDOW_H
