/*
 * BeBox emulator for Windows - the emulated display.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * A child window and the machine's VideoSink: frames arrive on the
 * emulation thread into a staging buffer, the window thread copies the
 * changed lines and redraws them. Whole multiples of the picture's size
 * stay sharp, other sizes are filtered.
 *
 * The captured mouse: the pointer is hidden and held in the window, and
 * the mouse's own motion (Raw Input) goes to the BeBox's PS/2 mouse.
 */
#ifndef WIN32_SCREEN_VIEW_H
#define WIN32_SCREEN_VIEW_H

#include "core/host/VideoSink.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace bebox {
	class EmulatorThread;
}

// Messages to the parent window.
const UINT kMsgScreenResized = WM_APP + 20;		// the picture's size
const UINT kMsgCaptureChanged = WM_APP + 21;

enum FullScreenScale {
	kFullScreenFill = 0,		// as large as fits
	kFullScreenWhole = 1,		// the largest whole multiple
	kFullScreenActual = 2		// 1:1
};

class ScreenView : public bebox::VideoSink {
public:
								ScreenView(bebox::EmulatorThread& emulator);
	virtual						~ScreenView();

	static	void				RegisterClass(HINSTANCE instance);
			HWND				Create(HWND parent);
			HWND				Window() const { return fWindow; }

	virtual	void				FrameReady(const bebox::u32* pixels,
									int width, int height, int stride,
									int top, int bottom);

			void				FrameSize(int& width, int& height) const;
			void				SetScale(int scale);	// 0: fit
			void				SetFullScreen(bool fullScreen, int scale);
			void				SetPowered(bool powered);
			void				ShowHint(const std::wstring& text);

			void				SetMouseCaptured(bool captured);
			bool				IsMouseCaptured() const { return fCaptured; }
			void				SetMouseOptions(int speed, bool swapButtons,
									bool captureOnClick);

			// The last frame (0x00RRGGBB); false if there is none.
			bool				CopyFrame(std::vector<bebox::u32>& pixels,
									int& width, int& height) const;

private:
	static	LRESULT CALLBACK	_WindowProc(HWND window, UINT message,
									WPARAM wParam, LPARAM lParam);
			LRESULT				_Message(UINT message, WPARAM wParam,
									LPARAM lParam);
			void				_TakeFrame();
			RECT				_DestinationRect() const;
			void				_Paint();
			void				_RawInput(HRAWINPUT input);
			void				_SendMouse(int dx, int dy, int dz);
			void				_ClipCursor();

			bebox::EmulatorThread& fEmulator;
			HWND				fWindow;

			// written on the emulation thread
	mutable	std::mutex			fLock;
			std::vector<bebox::u32> fStaging;
			int					fStagingWidth;
			int					fStagingHeight;
			int					fDirtyTop;
			int					fDirtyBottom;
			std::atomic<bool>	fFramePending;

			// window thread
			std::vector<bebox::u32> fFrame;
			int					fFrameWidth;
			int					fFrameHeight;
			int					fScale;
			bool				fFullScreen;
			int					fFullScreenScale;
			bool				fPowered;
			std::wstring		fHint;
			UINT_PTR			fHintTimer;

			bool				fCaptured;
			int					fMouseSpeed;
			bool				fSwapButtons;
			bool				fCaptureOnClick;
			bebox::u8			fButtons;		// the PS/2 mouse's
			float				fRemainderX;
			float				fRemainderY;
			bool				fHaveAbsolute;
			LONG				fLastAbsoluteX;
			LONG				fLastAbsoluteY;
};

#endif	// WIN32_SCREEN_VIEW_H
