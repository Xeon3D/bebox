/*
 * BeBox emulator for Windows - the application.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The command line is the other applications' (frontend/CommandLine.h);
 * --log writes the emulator's messages to the console BeBox was started
 * from.
 */
#include "MainWindow.h"
#include "Util.h"

#include "frontend/CommandLine.h"
#include "frontend/Settings.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdio>


int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED
		| COINIT_DISABLE_OLE1DDE);
	INITCOMMONCONTROLSEX controls = {};
	controls.dwSize = sizeof(controls);
	controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES
		| ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&controls);
	// the emulator's threads must keep pace: timers at 1 ms
	timeBeginPeriod(1);

	Settings::SetDirectory(AppDataDirectory());
	Settings settings;
	settings.Load();

	int count = 0;
	wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
	std::vector<std::string> arguments;
	for (int i = 1; i < count; i++) {
		arguments.push_back(Narrow(wide[i]));
		// to the console BeBox was started from
		if (wcscmp(wide[i], L"--log") == 0
			&& AttachConsole(ATTACH_PARENT_PROCESS))
			freopen("CONOUT$", "w", stderr);
	}
	LocalFree(wide);
	CommandLine commandLine;
	std::string error;
	if (!ApplyCommandLine(settings, arguments, commandLine, error)) {
		ShowMessage(nullptr, Widen(error + "\n\n" + kCommandLineUsage),
			MB_ICONERROR);
		return 2;
	}

	MainWindow::RegisterClass(instance);
	int result = 0;
	{
		MainWindow window(settings);
		if (!window.Create(showCommand))
			return 1;
		if (commandLine.powerOn)
			window.PowerOn();
		MSG message;
		while (GetMessageW(&message, nullptr, 0, 0) > 0) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		result = (int)message.wParam;
	}
	timeEndPeriod(1);
	CoUninitialize();
	return result;
}
