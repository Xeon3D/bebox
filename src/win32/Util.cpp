/*
 * BeBox emulator for Windows - small helpers.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "Util.h"

#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdarg>
#include <cwchar>

const wchar_t* const kAppName = L"BeBox";


std::wstring
Widen(const std::string& text)
{
	if (text.empty())
		return std::wstring();
	int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
		(int)text.size(), nullptr, 0);
	std::wstring result(length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &result[0],
		length);
	return result;
}


std::string
Narrow(const std::wstring& text)
{
	if (text.empty())
		return std::string();
	int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
		(int)text.size(), nullptr, 0, nullptr, nullptr);
	std::string result(length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &result[0],
		length, nullptr, nullptr);
	return result;
}


std::wstring
Format(const wchar_t* format, ...)
{
	wchar_t buffer[2048];
	va_list arguments;
	va_start(arguments, format);
	_vsnwprintf(buffer, sizeof(buffer) / sizeof(buffer[0]) - 1, format,
		arguments);
	va_end(arguments);
	buffer[sizeof(buffer) / sizeof(buffer[0]) - 1] = 0;
	return buffer;
}


std::string
Leaf(const std::string& path)
{
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


bool
FileExists(const std::string& path)
{
	DWORD attributes = GetFileAttributesW(Widen(path).c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}


bool
DirectoryExists(const std::string& path)
{
	DWORD attributes = GetFileAttributesW(Widen(path).c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}


bool
CreateDirectories(const std::string& path)
{
	if (path.empty() || DirectoryExists(path))
		return true;
	size_t slash = path.find_last_of("/\\");
	if (slash != std::string::npos && slash > 0
		&& path[slash - 1] != ':')
		CreateDirectories(path.substr(0, slash));
	return CreateDirectoryW(Widen(path).c_str(), nullptr) != 0
		|| GetLastError() == ERROR_ALREADY_EXISTS;
}


std::string
AppDataDirectory()
{
	wchar_t* folder = nullptr;
	std::string path;
	if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder)
			== S_OK) {
		path = Narrow(folder) + "\\BeBox";
		CoTaskMemFree(folder);
	} else
		path = "BeBox";
	CreateDirectories(path);
	return path;
}


void
ShowMessage(HWND owner, const std::wstring& text, UINT icon)
{
	MessageBoxW(owner, text.c_str(), kAppName, MB_OK | icon);
}


bool
AskYesNo(HWND owner, const std::wstring& text)
{
	return MessageBoxW(owner, text.c_str(), kAppName,
		MB_YESNO | MB_ICONQUESTION) == IDYES;
}


static std::string
FileDialog(HWND owner, const wchar_t* title, const wchar_t* filters,
	const wchar_t* defaultExtension, const std::string& initial, bool save)
{
	wchar_t file[MAX_PATH * 4] = L"";
	std::wstring directory;
	if (!initial.empty()) {
		std::wstring wide = Widen(initial);
		if (DirectoryExists(initial))
			directory = wide;
		else {
			size_t slash = wide.find_last_of(L"/\\");
			if (slash != std::wstring::npos)
				directory = wide.substr(0, slash);
			wcsncpy(file, wide.c_str() + (slash == std::wstring::npos ? 0
				: slash + 1), sizeof(file) / sizeof(file[0]) - 1);
		}
	}
	OPENFILENAMEW dialog = {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = owner;
	dialog.lpstrFilter = filters;
	dialog.lpstrFile = file;
	dialog.nMaxFile = sizeof(file) / sizeof(file[0]);
	dialog.lpstrTitle = title;
	dialog.lpstrDefExt = defaultExtension;
	dialog.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
	dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY
		| (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	BOOL ok = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
	return ok ? Narrow(file) : std::string();
}


std::string
ChooseOpenFile(HWND owner, const wchar_t* title, const wchar_t* filters,
	const std::string& initial)
{
	return FileDialog(owner, title, filters, nullptr, initial, false);
}


std::string
ChooseSaveFile(HWND owner, const wchar_t* title, const wchar_t* filters,
	const wchar_t* defaultExtension, const std::string& initial)
{
	return FileDialog(owner, title, filters, defaultExtension, initial, true);
}


std::string
ChooseFolder(HWND owner, const wchar_t* title, const std::string& initial)
{
	std::string result;
	IFileOpenDialog* dialog = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
		return result;
	DWORD options = 0;
	dialog->GetOptions(&options);
	dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
	dialog->SetTitle(title);
	if (!initial.empty() && DirectoryExists(initial)) {
		IShellItem* folder = nullptr;
		if (SUCCEEDED(SHCreateItemFromParsingName(Widen(initial).c_str(),
				nullptr, IID_PPV_ARGS(&folder)))) {
			dialog->SetFolder(folder);
			folder->Release();
		}
	}
	if (SUCCEEDED(dialog->Show(owner))) {
		IShellItem* item = nullptr;
		if (SUCCEEDED(dialog->GetResult(&item))) {
			wchar_t* path = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
				result = Narrow(path);
				CoTaskMemFree(path);
			}
			item->Release();
		}
	}
	dialog->Release();
	return result;
}


std::string
WindowTextUtf8(HWND window)
{
	int length = GetWindowTextLengthW(window);
	std::wstring text(length + 1, L'\0');
	GetWindowTextW(window, &text[0], length + 1);
	text.resize(length);
	return Narrow(text);
}


void
SetWindowTextUtf8(HWND window, const std::string& text)
{
	SetWindowTextW(window, Widen(text).c_str());
}
