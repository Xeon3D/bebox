/*
 * BeBox emulator for Qt - the application, on Windows, Linux, macOS and
 * Haiku.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The command line is the other applications' (frontend/CommandLine.h).
 * The settings are where each system keeps them: %APPDATA%\BeBox (shared
 * with the Windows application), ~/.config/BeBox, ~/Library/Application
 * Support/BeBox, ~/config/settings/BeBox (shared with the Haiku one's
 * profiles: flash and NVRAM).
 */
#include "MainWindow.h"

#include "frontend/CommandLine.h"
#include "frontend/Settings.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#	include <windows.h>
#	include <cstdio>
#endif

#include <cstdlib>
#include <cstring>


static std::string
SettingsDirectory()
{
#if defined(Q_OS_MACOS)
	QString base = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation);		// Application Support
	return QDir(base).filePath("BeBox").toStdString();
#elif defined(Q_OS_HAIKU)
	QString base = QStandardPaths::writableLocation(
		QStandardPaths::GenericConfigLocation);	// ~/config/settings
	return QDir(base).filePath("BeBox").toStdString();
#else
	// Windows: %APPDATA%\BeBox; elsewhere $XDG_CONFIG_HOME/BeBox
	return Settings::Directory();
#endif
}


int
main(int argc, char** argv)
{
#if defined(Q_OS_LINUX)
	// On Wayland, Qt cannot move the pointer (capturing the mouse needs
	// it), so X11 through XWayland when there is one (else Wayland after
	// all), unless the user
	// chose a platform (QT_QPA_PLATFORM or -platform).
	bool platformChosen = getenv("QT_QPA_PLATFORM") != nullptr;
	for (int i = 1; i < argc; i++)
		platformChosen |= strcmp(argv[i], "-platform") == 0;
	if (!platformChosen && getenv("WAYLAND_DISPLAY") != nullptr
		&& getenv("DISPLAY") != nullptr) {
		setenv("QT_QPA_PLATFORM", "xcb;wayland", 1);
	}
#endif

	QApplication application(argc, argv);
	application.setApplicationName("BeBox");
	application.setApplicationVersion("1.2");
	QIcon icon;
	for (int size : { 16, 24, 32, 48, 64 })
		icon.addFile(QString(":/icons/machine-%1.png").arg(size),
			QSize(size, size));
	application.setWindowIcon(icon);
#ifdef Q_OS_WIN
	// the emulator's threads must keep pace: timers at 1 ms
	timeBeginPeriod(1);
#endif

	Settings::SetDirectory(SettingsDirectory());
	Settings settings;
	settings.Load();

	std::vector<std::string> arguments;
	QStringList list = application.arguments();
	for (int i = 1; i < list.size(); i++) {
		arguments.push_back(list[i].toStdString());
#ifdef Q_OS_WIN
		// to the console BeBox was started from
		if (list[i] == "--log" && AttachConsole(ATTACH_PARENT_PROCESS))
			freopen("CONOUT$", "w", stderr);
#endif
	}
	CommandLine commandLine;
	std::string error;
	if (!ApplyCommandLine(settings, arguments, commandLine, error)) {
		QMessageBox::critical(nullptr, "BeBox", QString::fromStdString(error
			+ "\n\n" + kCommandLineUsage));
		return 2;
	}

	int result;
	{
		MainWindow window(settings);
		window.show();
		if (commandLine.powerOn)
			window.PowerOn();
		result = application.exec();
	}
#ifdef Q_OS_WIN
	timeEndPeriod(1);
#endif
	return result;
}
