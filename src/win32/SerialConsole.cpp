/*
 * BeBox emulator for Windows - a serial port's console window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "SerialConsole.h"

#include "Util.h"

#include <commctrl.h>

namespace {

const wchar_t* const kClassName = L"BeBoxSerialConsole";
const UINT kMsgFlush = WM_APP + 1;
// The box keeps the last 256 K characters.
const size_t kMaxText = 256 * 1024;

}	// namespace


SerialConsole::SerialConsole(int port)
	:
	fPort(port),
	fWindow(nullptr),
	fEdit(nullptr),
	fFont(nullptr),
	fFlushPosted(false),
	fLineStart(0)
{
}


SerialConsole::~SerialConsole()
{
	if (fWindow != nullptr)
		DestroyWindow(fWindow);
	if (fFont != nullptr)
		DeleteObject(fFont);
}


void
SerialConsole::RegisterClass(HINSTANCE instance)
{
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = _WindowProc;
	windowClass.hInstance = instance;
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszClassName = kClassName;
	RegisterClassExW(&windowClass);
}


void
SerialConsole::Show(HWND owner)
{
	if (fWindow == nullptr) {
		std::wstring title = Format(L"BeBox serial port %d", fPort + 1);
		fWindow = CreateWindowExW(0, kClassName, title.c_str(),
			WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 480,
			owner, nullptr, GetModuleHandleW(nullptr), this);
		fEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE
				| ES_AUTOVSCROLL | ES_READONLY,
			0, 0, 0, 0, fWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
		fFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
		SendMessageW(fEdit, WM_SETFONT, (WPARAM)fFont, TRUE);
		SendMessageW(fEdit, EM_SETLIMITTEXT, 0, 0);
		SetWindowSubclass(fEdit, _EditProc, 1, (DWORD_PTR)this);
		RECT client;
		GetClientRect(fWindow, &client);
		MoveWindow(fEdit, 0, 0, client.right, client.bottom, TRUE);
		{
			std::lock_guard<std::mutex> lock(fLock);
			fFlushPosted = true;
		}
		_Flush();
	}
	ShowWindow(fWindow, SW_SHOWNORMAL);
	SetForegroundWindow(fWindow);
	SetFocus(fEdit);
}


void
SerialConsole::Clear()
{
	std::lock_guard<std::mutex> lock(fLock);
	fPending.clear();
	fInput.clear();
	fText.clear();
	fLineStart = 0;
	if (fEdit != nullptr)
		SetWindowTextW(fEdit, L"");
}


// Emulation thread.
void
SerialConsole::Transmit(bebox::u8 byte)
{
	std::lock_guard<std::mutex> lock(fLock);
	if (fPending.size() < kMaxText)
		fPending.push_back((char)byte);
	if (!fFlushPosted && fWindow != nullptr) {
		fFlushPosted = true;
		PostMessageW(fWindow, kMsgFlush, 0, 0);
	}
}


// Emulation thread.
int
SerialConsole::Receive()
{
	std::lock_guard<std::mutex> lock(fLock);
	if (fInput.empty())
		return -1;
	bebox::u8 byte = fInput.front();
	fInput.pop_front();
	return byte;
}


// The output so far into the box, as a simple terminal would show it.
void
SerialConsole::_Flush()
{
	std::string pending;
	{
		std::lock_guard<std::mutex> lock(fLock);
		pending.swap(fPending);
		fFlushPosted = false;
	}
	if (fEdit == nullptr)
		return;
	bool changed = false;
	for (char c : pending) {
		unsigned char byte = (unsigned char)c;
		switch (byte) {
			case '\n':
				fText += L"\r\n";
				fLineStart = fText.size();
				break;
			case '\r':
				// a following line feed ends the line; anything else
				// overwrites it (below)
				fText.push_back(L'\r');
				break;
			case '\b':
				if (fText.size() > fLineStart)
					fText.pop_back();
				break;
			case '\t':
				do {
					fText.push_back(L' ');
				} while ((fText.size() - fLineStart) % 8 != 0);
				break;
			default:
				if (byte < 0x20 || byte == 0x7f)
					continue;
				// Latin-1 like a terminal of the time
				fText.push_back((wchar_t)byte);
				break;
		}
		// a carriage return not followed by a line feed: back to the
		// line's start
		if (byte != '\r' && byte != '\n' && fText.size() >= 2
			&& fText[fText.size() - 2] == L'\r') {
			wchar_t last = fText.back();
			fText.resize(fLineStart);
			fText.push_back(last);
		}
		changed = true;
	}
	if (!changed)
		return;
	if (fText.size() > kMaxText) {
		size_t cut = fText.find(L"\r\n", fText.size() - kMaxText);
		if (cut != std::wstring::npos) {
			fText.erase(0, cut + 2);
			fLineStart -= std::min(fLineStart, cut + 2);
		}
	}
	// the text as a terminal shows it: no stray carriage returns
	std::wstring shown;
	shown.reserve(fText.size());
	for (size_t i = 0; i < fText.size(); i++) {
		if (fText[i] == L'\r' && (i + 1 >= fText.size()
				|| fText[i + 1] != L'\n'))
			continue;
		shown.push_back(fText[i]);
	}
	SendMessageW(fEdit, WM_SETREDRAW, FALSE, 0);
	SetWindowTextW(fEdit, shown.c_str());
	int length = GetWindowTextLengthW(fEdit);
	SendMessageW(fEdit, EM_SETSEL, length, length);
	SendMessageW(fEdit, EM_SCROLLCARET, 0, 0);
	SendMessageW(fEdit, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(fEdit, nullptr, TRUE);
}


LRESULT CALLBACK
SerialConsole::_WindowProc(HWND window, UINT message, WPARAM wParam,
	LPARAM lParam)
{
	SerialConsole* console
		= (SerialConsole*)GetWindowLongPtrW(window, GWLP_USERDATA);
	switch (message) {
		case WM_NCCREATE:
		{
			CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
			SetWindowLongPtrW(window, GWLP_USERDATA,
				(LONG_PTR)create->lpCreateParams);
			break;
		}
		case WM_SIZE:
			if (console != nullptr && console->fEdit != nullptr)
				MoveWindow(console->fEdit, 0, 0, LOWORD(lParam),
					HIWORD(lParam), TRUE);
			return 0;
		case WM_SETFOCUS:
			if (console != nullptr && console->fEdit != nullptr)
				SetFocus(console->fEdit);
			return 0;
		case WM_CLOSE:
			// hidden, not destroyed: the port stays connected
			ShowWindow(window, SW_HIDE);
			return 0;
		case kMsgFlush:
			if (console != nullptr)
				console->_Flush();
			return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}


// Typed characters go to the BeBox instead of into the box.
LRESULT CALLBACK
SerialConsole::_EditProc(HWND window, UINT message, WPARAM wParam,
	LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
	SerialConsole* console = (SerialConsole*)data;
	switch (message) {
		case WM_CHAR:
		{
			// Ctrl+C copies when there is a selection
			if (wParam == 3) {
				DWORD start = 0, end = 0;
				SendMessageW(window, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
				if (start != end)
					break;
			}
			if (wParam > 0xff)
				return 0;
			std::lock_guard<std::mutex> lock(console->fLock);
			console->fInput.push_back((bebox::u8)wParam);
			return 0;
		}
		case WM_PASTE:
		{
			if (!OpenClipboard(window))
				return 0;
			HANDLE data = GetClipboardData(CF_UNICODETEXT);
			if (data != nullptr) {
				const wchar_t* text = (const wchar_t*)GlobalLock(data);
				if (text != nullptr) {
					std::lock_guard<std::mutex> lock(console->fLock);
					for (; *text != 0; text++) {
						if (*text == L'\n')
							continue;
						if (*text <= 0xff)
							console->fInput.push_back((bebox::u8)*text);
					}
					GlobalUnlock(data);
				}
			}
			CloseClipboard();
			return 0;
		}
	}
	return DefSubclassProc(window, message, wParam, lParam);
}
