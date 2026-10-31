/*
 * BeBox emulator for Windows - a serial port's console window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * A SerialBackend: what the BeBox sends shows in a read only text box
 * (a terminal of sorts: CR, LF, backspace and tab), and what is typed in
 * the window goes back to it. Transmit() and Receive() run on the
 * emulation thread; the window lives on the UI thread.
 */
#ifndef WIN32_SERIAL_CONSOLE_H
#define WIN32_SERIAL_CONSOLE_H

#include "core/host/SerialBackend.h"

#include <windows.h>

#include <deque>
#include <mutex>
#include <string>

class SerialConsole : public bebox::SerialBackend {
public:
								SerialConsole(int port);
	virtual						~SerialConsole();

			void				Show(HWND owner);
			HWND				Window() const { return fWindow; }
			void				Clear();

	virtual	void				Transmit(bebox::u8 byte);
	virtual	int					Receive();

	static	void				RegisterClass(HINSTANCE instance);

private:
	static	LRESULT CALLBACK	_WindowProc(HWND window, UINT message,
									WPARAM wParam, LPARAM lParam);
	static	LRESULT CALLBACK	_EditProc(HWND window, UINT message,
									WPARAM wParam, LPARAM lParam,
									UINT_PTR id, DWORD_PTR data);
			void				_Flush();

			int					fPort;
			HWND				fWindow;
			HWND				fEdit;
			HFONT				fFont;
			std::mutex			fLock;
			std::string			fPending;		// output not shown yet
			bool				fFlushPosted;
			std::deque<bebox::u8> fInput;
			std::wstring		fText;			// what the box shows
			size_t				fLineStart;		// of the last line in fText
};

#endif	// WIN32_SERIAL_CONSOLE_H
