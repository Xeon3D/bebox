/*
 * BeBox emulator for Windows - small helpers.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The core takes UTF-8 paths (the manifest makes UTF-8 the process's code
 * page); the window code uses the wide Windows API and converts.
 */
#ifndef WIN32_UTIL_H
#define WIN32_UTIL_H

#include <windows.h>

#include <string>
#include <vector>

extern const wchar_t* const kAppName;

std::wstring Widen(const std::string& text);
std::string Narrow(const std::wstring& text);

// printf into a wide string.
std::wstring Format(const wchar_t* format, ...);

// The last path component.
std::string Leaf(const std::string& path);
bool FileExists(const std::string& path);
bool DirectoryExists(const std::string& path);
// mkdir -p
bool CreateDirectories(const std::string& path);

// %APPDATA%\BeBox (made if missing).
std::string AppDataDirectory();

void ShowMessage(HWND owner, const std::wstring& text,
	UINT icon = MB_ICONINFORMATION);
bool AskYesNo(HWND owner, const std::wstring& text);

// File dialogs; filters as "Name\0*.ext;*.ext\0...\0\0". Empty: canceled.
std::string ChooseOpenFile(HWND owner, const wchar_t* title,
	const wchar_t* filters, const std::string& initial = "");
std::string ChooseSaveFile(HWND owner, const wchar_t* title,
	const wchar_t* filters, const wchar_t* defaultExtension,
	const std::string& initial = "");
std::string ChooseFolder(HWND owner, const wchar_t* title,
	const std::string& initial = "");

// The text of a window (an edit control), as UTF-8.
std::string WindowTextUtf8(HWND window);
void SetWindowTextUtf8(HWND window, const std::string& text);

#endif	// WIN32_UTIL_H
