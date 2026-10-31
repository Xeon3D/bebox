/*
 * BeBox emulator for Windows - the machine's window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The screen between the two front panel LED columns, a menu bar and a
 * status line (the profile, the model, the speed; the floppy, CDs and
 * disk light). Keys go to the BeBox's keyboard while the window is
 * active and the machine is on, except the mouse release key ("host
 * key"), which with F switches to full screen and back.
 */
#ifndef WIN32_MAIN_WINDOW_H
#define WIN32_MAIN_WINDOW_H

#include "ScreenView.h"
#include "SerialConsole.h"
#include "Settings.h"
#include "WinAudio.h"

#include "core/Log.h"
#include "core/host/EmulatorThread.h"

#include <windows.h>
#include <shellapi.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace bebox {
	class CdImage;
	class NetworkBackend;
	class SerialBackend;
	class SharedFolder;
}

// Runs jobs one after the other on a thread of its own (the shared
// folders' copies, which take seconds with a big folder).
class JobQueue {
public:
								JobQueue();
								~JobQueue();

			void				Add(std::function<void()> job);
			// Waits until all jobs ran.
			void				Drain();

private:
			void				_Run();

			std::mutex			fLock;
			std::condition_variable fCondition;
			std::deque<std::function<void()> > fJobs;
			bool				fBusy;
			bool				fQuit;
			std::thread			fThread;
};


class MainWindow {
public:
								MainWindow(Settings& settings);
								~MainWindow();

	static	void				RegisterClass(HINSTANCE instance);
			bool				Create(int showCommand);
			HWND				Window() const { return fWindow; }

			bool				PowerOn();
			void				PowerOff();

private:
	static	LRESULT CALLBACK	_WindowProc(HWND window, UINT message,
									WPARAM wParam, LPARAM lParam);
			LRESULT				_Message(UINT message, WPARAM wParam,
									LPARAM lParam);
			void				_Command(int command);

			// menus
			void				_BuildMenus();
			void				_BuildProfileMenu();
			void				_BuildMediaMenu();
			void				_UpdateMenus();

			// machine
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
			void				_HoldKeyDuringBoot(bebox::u32 set2,
									double seconds);
			void				_PressKeyCombination(
									const std::vector<const char*>& keys);
			void				_ShowProblem(const std::string& text);
	static	void				_LogSink(bebox::LogCategory category,
									bebox::LogLevel level,
									const char* message, void* cookie);

			// media
			void				_InsertFloppy(const std::string& path);
			void				_EjectFloppy();
			std::vector<int>	_CdDrives() const;
			bool				_InsertCd(int id, const std::string& path);
			void				_EjectCd(int id);
			void				_ChangeCd(int id,
									std::unique_ptr<bebox::CdImage> image);
			void				_DroppedFiles(HDROP drop);

			// window
			void				_Layout();
			int					_LedWidth() const;
			bool				_ShowLeds() const;
			void				_FitWindow();
			void				_SetFullScreen(bool fullScreen);
			void				_PaintLeds(HDC dc);
			void				_UpdateStatus();
			void				_StatusClicked(int part);
			void				_SaveScreenshot();
			void				_ShowConsole(int port);
			void				_About();

			// keyboard
			bool				_HandleKey(UINT message, WPARAM wParam,
									LPARAM lParam);
			void				_SendKey(bebox::u32 code, bool down);
			void				_ReleaseAllKeys();
			bebox::u32			_HostKeyCode() const;
			const wchar_t*		_HostKeyName() const;
			void				_SetKeyboardHook(bool hook);
	static	LRESULT CALLBACK	_KeyboardHook(int code, WPARAM wParam,
									LPARAM lParam);

			// joysticks
			void				_PollJoysticks();

			Settings&			fSettings;
			bebox::EmulatorThread fEmulator;
			ScreenView			fScreen;
			WinAudio			fAudio;
			SerialConsole*		fConsoles[4];
			std::vector<std::unique_ptr<bebox::SerialBackend> >
								fSerialBackends;
			std::unique_ptr<bebox::NetworkBackend> fNetwork;
			std::vector<std::unique_ptr<bebox::SharedFolder> >
								fSharedFolders;
			JobQueue			fFolderJobs;
			int					fFolderStarts;
			int					fFolderSyncs;
			struct FolderStart;
			std::shared_ptr<FolderStart> fFolderStart;

			HWND				fWindow;
			HWND				fStatus;
			HMENU				fMenuBar;
			HMENU				fMachineMenu;
			HMENU				fProfileMenu;
			HMENU				fMediaMenu;
			HMENU				fViewMenu;
			HMENU				fToolsMenu;

			bool				fPowered;
			bool				fStarting;
			bool				fFullScreen;
			WINDOWPLACEMENT		fWindowedPlacement;
			LONG_PTR			fWindowedStyle;

			std::set<bebox::u32> fKeysDown;
			bool				fHostKeyDown;
			bool				fHostComboUsed;
			HHOOK				fKeyboardHook;

			// host joysticks on the game ports: winmm device and range
			struct Stick {
				int				device;		// -1: none
				unsigned int	minX, maxX, minY, maxY;
			};
			Stick				fSticks[4];

			std::string			fProblem;
			ULONGLONG			fProblemTime;
			std::set<std::string> fProblemsAlerted;
			std::wstring		fStatusText;
			std::wstring		fMediaText;
			float				fLeds[2];
			float				fDisk;
};

#endif	// WIN32_MAIN_WINDOW_H
