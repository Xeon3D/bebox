/*
 * BeBox emulator for Windows - the emulated display.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "ScreenView.h"

#include "core/board/Machine.h"
#include "core/host/EmulatorThread.h"
#include "core/isa/Kbc8042.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const wchar_t* const kClassName = L"BeBoxScreen";
const UINT kMsgFrame = WM_APP + 1;
const UINT_PTR kHintTimer = 1;
const UINT kHintDuration = 3000;

}	// namespace


ScreenView::ScreenView(bebox::EmulatorThread& emulator)
	:
	fEmulator(emulator),
	fWindow(nullptr),
	fStagingWidth(640),
	fStagingHeight(480),
	fDirtyTop(0),
	fDirtyBottom(480),
	fFramePending(false),
	fFrameWidth(640),
	fFrameHeight(480),
	fScale(1),
	fFullScreen(false),
	fFullScreenScale(kFullScreenFill),
	fPowered(false),
	fHintTimer(0),
	fCaptured(false),
	fMouseSpeed(100),
	fSwapButtons(false),
	fCaptureOnClick(true),
	fButtons(0),
	fRemainderX(0),
	fRemainderY(0),
	fHaveAbsolute(false),
	fLastAbsoluteX(0),
	fLastAbsoluteY(0)
{
	fStaging.assign(640 * 480, 0);
	fFrame.assign(640 * 480, 0);
}


ScreenView::~ScreenView()
{
	SetMouseCaptured(false);
}


void
ScreenView::RegisterClass(HINSTANCE instance)
{
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = _WindowProc;
	windowClass.hInstance = instance;
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.lpszClassName = kClassName;
	RegisterClassExW(&windowClass);
}


HWND
ScreenView::Create(HWND parent)
{
	fWindow = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE
		| WS_CLIPSIBLINGS, 0, 0, 640, 480, parent, nullptr,
		GetModuleHandleW(nullptr), this);
	return fWindow;
}


// Emulation thread.
void
ScreenView::FrameReady(const bebox::u32* pixels, int width, int height,
	int stride, int top, int bottom)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (width != fStagingWidth || height != fStagingHeight) {
			fStaging.resize((size_t)width * height);
			fStagingWidth = width;
			fStagingHeight = height;
			top = 0;
			bottom = height;
		}
		for (int y = top; y < bottom; y++) {
			memcpy(&fStaging[(size_t)y * width], pixels + (size_t)y * stride,
				width * 4);
		}
		fDirtyTop = std::min(fDirtyTop, top);
		fDirtyBottom = std::max(fDirtyBottom, bottom);
	}
	if (!fFramePending.exchange(true) && fWindow != nullptr)
		PostMessageW(fWindow, kMsgFrame, 0, 0);
}


void
ScreenView::FrameSize(int& width, int& height) const
{
	width = fFrameWidth;
	height = fFrameHeight;
}


void
ScreenView::SetScale(int scale)
{
	fScale = scale;
	InvalidateRect(fWindow, nullptr, FALSE);
}


void
ScreenView::SetFullScreen(bool fullScreen, int scale)
{
	fFullScreen = fullScreen;
	fFullScreenScale = scale;
	InvalidateRect(fWindow, nullptr, FALSE);
}


void
ScreenView::SetPowered(bool powered)
{
	fPowered = powered;
	if (powered)
		return;
	SetMouseCaptured(false);
	{
		// the emulation thread has stopped: nothing refills the frame
		std::lock_guard<std::mutex> lock(fLock);
		std::fill(fStaging.begin(), fStaging.end(), 0);
		fDirtyTop = 0;
		fDirtyBottom = fStagingHeight;
	}
	_TakeFrame();
	InvalidateRect(fWindow, nullptr, FALSE);
}


void
ScreenView::ShowHint(const std::wstring& text)
{
	fHint = text;
	if (fHintTimer != 0)
		KillTimer(fWindow, fHintTimer);
	fHintTimer = text.empty() ? 0
		: SetTimer(fWindow, kHintTimer, kHintDuration, nullptr);
	InvalidateRect(fWindow, nullptr, FALSE);
}


bool
ScreenView::CopyFrame(std::vector<bebox::u32>& pixels, int& width,
	int& height) const
{
	if (fFrame.empty())
		return false;
	pixels = fFrame;
	width = fFrameWidth;
	height = fFrameHeight;
	return true;
}


// The staging buffer's changed lines into the frame drawn.
void
ScreenView::_TakeFrame()
{
	int top, bottom;
	bool resized;
	{
		std::lock_guard<std::mutex> lock(fLock);
		resized = fStagingWidth != fFrameWidth
			|| fStagingHeight != fFrameHeight;
		if (resized) {
			fFrameWidth = fStagingWidth;
			fFrameHeight = fStagingHeight;
			fFrame.resize((size_t)fFrameWidth * fFrameHeight);
			fDirtyTop = 0;
			fDirtyBottom = fFrameHeight;
		}
		top = std::max(0, fDirtyTop);
		bottom = std::min(fFrameHeight, fDirtyBottom);
		fDirtyTop = fFrameHeight;
		fDirtyBottom = 0;
		if (top < bottom) {
			memcpy(&fFrame[(size_t)top * fFrameWidth],
				&fStaging[(size_t)top * fFrameWidth],
				(size_t)(bottom - top) * fFrameWidth * 4);
		}
	}
	if (resized) {
		InvalidateRect(fWindow, nullptr, FALSE);
		PostMessageW(GetParent(fWindow), kMsgScreenResized, fFrameWidth,
			fFrameHeight);
		return;
	}
	if (top >= bottom)
		return;
	// the changed lines as scaled, a line more for the filter
	RECT destination = _DestinationRect();
	double scale = (destination.bottom - destination.top)
		/ (double)fFrameHeight;
	RECT dirty = destination;
	dirty.top = destination.top + (LONG)std::floor((top - 1) * scale);
	dirty.bottom = destination.top + (LONG)std::ceil((bottom + 1) * scale);
	IntersectRect(&dirty, &dirty, &destination);
	InvalidateRect(fWindow, &dirty, FALSE);
}


RECT
ScreenView::_DestinationRect() const
{
	RECT bounds;
	GetClientRect(fWindow, &bounds);
	double width = fFrameWidth;
	double height = fFrameHeight;
	double fit = std::min(bounds.right / width, bounds.bottom / height);
	double scale;
	if (!fFullScreen)
		scale = fScale == 0 ? fit : fScale;
	else if (fFullScreenScale == kFullScreenWhole)
		scale = std::max(1.0, std::floor(fit));
	else if (fFullScreenScale == kFullScreenActual)
		scale = 1;
	else
		scale = fit;
	// a picture larger than the window fits it
	if (scale > fit)
		scale = fit;
	LONG w = (LONG)std::lround(width * scale);
	LONG h = (LONG)std::lround(height * scale);
	RECT rect;
	rect.left = (bounds.right - w) / 2;
	rect.top = (bounds.bottom - h) / 2;
	rect.right = rect.left + w;
	rect.bottom = rect.top + h;
	return rect;
}


void
ScreenView::_Paint()
{
	PAINTSTRUCT paint;
	HDC dc = BeginPaint(fWindow, &paint);
	RECT bounds;
	GetClientRect(fWindow, &bounds);
	RECT destination = _DestinationRect();
	// black around the picture
	HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
	RECT band;
	SetRect(&band, 0, 0, bounds.right, destination.top);
	FillRect(dc, &band, black);
	SetRect(&band, 0, destination.bottom, bounds.right, bounds.bottom);
	FillRect(dc, &band, black);
	SetRect(&band, 0, destination.top, destination.left, destination.bottom);
	FillRect(dc, &band, black);
	SetRect(&band, destination.right, destination.top, bounds.right,
		destination.bottom);
	FillRect(dc, &band, black);

	int w = destination.right - destination.left;
	int h = destination.bottom - destination.top;
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = fFrameWidth;
	info.bmiHeader.biHeight = -fFrameHeight;		// top down
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	// whole multiples stay sharp, other sizes are filtered
	bool whole = w % fFrameWidth == 0 && h % fFrameHeight == 0;
	SetStretchBltMode(dc, whole ? COLORONCOLOR : HALFTONE);
	SetBrushOrgEx(dc, 0, 0, nullptr);
	StretchDIBits(dc, destination.left, destination.top, w, h, 0, 0,
		fFrameWidth, fFrameHeight, fFrame.data(), &info, DIB_RGB_COLORS,
		SRCCOPY);

	if (!fHint.empty()) {
		HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		LOGFONTW logFont;
		GetObjectW(font, sizeof(logFont), &logFont);
		logFont.lfWeight = FW_BOLD;
		logFont.lfHeight = -16;
		HFONT bold = CreateFontIndirectW(&logFont);
		HGDIOBJ old = SelectObject(dc, bold);
		SIZE size;
		GetTextExtentPoint32W(dc, fHint.c_str(), (int)fHint.size(), &size);
		RECT box;
		box.left = (bounds.right - size.cx) / 2 - 12;
		box.top = 24;
		box.right = box.left + size.cx + 24;
		box.bottom = box.top + size.cy + 16;
		HBRUSH dark = CreateSolidBrush(RGB(32, 32, 32));
		HGDIOBJ oldBrush = SelectObject(dc, dark);
		HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
		RoundRect(dc, box.left, box.top, box.right, box.bottom, 12, 12);
		SelectObject(dc, oldPen);
		SelectObject(dc, oldBrush);
		DeleteObject(dark);
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, RGB(255, 255, 255));
		TextOutW(dc, box.left + 12, box.top + 8, fHint.c_str(),
			(int)fHint.size());
		SelectObject(dc, old);
		DeleteObject(bold);
	}
	EndPaint(fWindow, &paint);
}


// #pragma mark - mouse


void
ScreenView::SetMouseOptions(int speed, bool swapButtons, bool captureOnClick)
{
	fMouseSpeed = std::max(10, std::min(1000, speed));
	fSwapButtons = swapButtons;
	fCaptureOnClick = captureOnClick;
}


void
ScreenView::SetMouseCaptured(bool captured)
{
	if (captured && !fPowered)
		return;
	if (captured == fCaptured)
		return;
	fCaptured = captured;
	RAWINPUTDEVICE device = {};
	device.usUsagePage = 0x01;		// generic desktop
	device.usUsage = 0x02;			// mouse
	if (captured) {
		device.dwFlags = 0;
		device.hwndTarget = fWindow;
		RegisterRawInputDevices(&device, 1, sizeof(device));
		fHaveAbsolute = false;
		fRemainderX = fRemainderY = 0;
		SetCapture(fWindow);
		_ClipCursor();
		while (ShowCursor(FALSE) >= 0)
			;
	} else {
		device.dwFlags = RIDEV_REMOVE;
		device.hwndTarget = nullptr;
		RegisterRawInputDevices(&device, 1, sizeof(device));
		ClipCursor(nullptr);
		if (GetCapture() == fWindow)
			ReleaseCapture();
		while (ShowCursor(TRUE) < 0)
			;
		fButtons = 0;
		_SendMouse(0, 0, 0);
	}
	PostMessageW(GetParent(fWindow), kMsgCaptureChanged, captured, 0);
}


void
ScreenView::_ClipCursor()
{
	RECT rect;
	GetClientRect(fWindow, &rect);
	MapWindowPoints(fWindow, nullptr, (POINT*)&rect, 2);
	// a small area in the middle: the pointer never reaches the edges
	LONG x = (rect.left + rect.right) / 2;
	LONG y = (rect.top + rect.bottom) / 2;
	RECT clip = { x - 1, y - 1, x + 1, y + 1 };
	ClipCursor(&clip);
}


void
ScreenView::_SendMouse(int dx, int dy, int dz)
{
	bebox::u8 buttons = fButtons;
	fEmulator.Post([dx, dy, dz, buttons](bebox::Machine& machine) {
		machine.Keyboard().MouseMoved(dx, dy, dz, buttons);
	});
}


// Raw mouse input: relative motion (a mouse or touchpad), or positions
// (remote desktops, tablets) whose changes are the motion.
void
ScreenView::_RawInput(HRAWINPUT handle)
{
	RAWINPUT input;
	UINT size = sizeof(input);
	if (GetRawInputData(handle, RID_INPUT, &input, &size,
			sizeof(RAWINPUTHEADER)) == (UINT)-1
		|| input.header.dwType != RIM_TYPEMOUSE)
		return;
	const RAWMOUSE& mouse = input.data.mouse;
	float moveX = 0;
	float moveY = 0;
	if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
		bool virtualDesktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
		int width = GetSystemMetrics(virtualDesktop ? SM_CXVIRTUALSCREEN
			: SM_CXSCREEN);
		int height = GetSystemMetrics(virtualDesktop ? SM_CYVIRTUALSCREEN
			: SM_CYSCREEN);
		LONG x = (LONG)(mouse.lLastX / 65535.0 * width);
		LONG y = (LONG)(mouse.lLastY / 65535.0 * height);
		if (fHaveAbsolute) {
			moveX = (float)(x - fLastAbsoluteX);
			moveY = (float)(y - fLastAbsoluteY);
		}
		fHaveAbsolute = true;
		fLastAbsoluteX = x;
		fLastAbsoluteY = y;
	} else {
		moveX = (float)mouse.lLastX;
		moveY = (float)mouse.lLastY;
	}

	bebox::u8 primary = fSwapButtons ? 2 : 1;
	bebox::u8 secondary = fSwapButtons ? 1 : 2;
	USHORT flags = mouse.usButtonFlags;
	bebox::u8 buttons = fButtons;
	if ((flags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0)
		buttons |= primary;
	if ((flags & RI_MOUSE_LEFT_BUTTON_UP) != 0)
		buttons &= ~primary;
	if ((flags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0)
		buttons |= secondary;
	if ((flags & RI_MOUSE_RIGHT_BUTTON_UP) != 0)
		buttons &= ~secondary;
	if ((flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0)
		buttons |= 4;
	if ((flags & RI_MOUSE_MIDDLE_BUTTON_UP) != 0)
		buttons &= ~4;
	int dz = 0;
	if ((flags & RI_MOUSE_WHEEL) != 0) {
		// IntelliMouse: positive is towards the user (scrolling down)
		dz = -(SHORT)mouse.usButtonData / WHEEL_DELTA;
	}

	moveX = moveX * fMouseSpeed / 100.0f + fRemainderX;
	moveY = moveY * fMouseSpeed / 100.0f + fRemainderY;
	int dx = (int)moveX;
	int dy = (int)moveY;
	fRemainderX = moveX - dx;
	fRemainderY = moveY - dy;
	if (dx != 0 || dy != 0 || dz != 0 || buttons != fButtons) {
		fButtons = buttons;
		// PS/2: positive y is up
		_SendMouse(dx, -dy, dz);
	}
}


LRESULT CALLBACK
ScreenView::_WindowProc(HWND window, UINT message, WPARAM wParam,
	LPARAM lParam)
{
	ScreenView* view = (ScreenView*)GetWindowLongPtrW(window, GWLP_USERDATA);
	if (message == WM_NCCREATE) {
		CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
		view = (ScreenView*)create->lpCreateParams;
		view->fWindow = window;
		SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)view);
	}
	if (view == nullptr)
		return DefWindowProcW(window, message, wParam, lParam);
	return view->_Message(message, wParam, lParam);
}


LRESULT
ScreenView::_Message(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
		case WM_PAINT:
			_Paint();
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_SIZE:
			InvalidateRect(fWindow, nullptr, FALSE);
			if (fCaptured)
				_ClipCursor();
			return 0;
		case kMsgFrame:
			fFramePending = false;
			_TakeFrame();
			return 0;
		case WM_TIMER:
			if (wParam == kHintTimer) {
				KillTimer(fWindow, kHintTimer);
				fHintTimer = 0;
				fHint.clear();
				InvalidateRect(fWindow, nullptr, FALSE);
			}
			return 0;
		case WM_SETFOCUS:
			// the keyboard stays with the main window
			SetFocus(GetParent(fWindow));
			return 0;
		case WM_LBUTTONDOWN:
			if (!fCaptured && fCaptureOnClick && fPowered)
				SetMouseCaptured(true);
			return 0;
		case WM_INPUT:
			if (fCaptured)
				_RawInput((HRAWINPUT)lParam);
			break;
		case WM_SETCURSOR:
			if (fCaptured) {
				SetCursor(nullptr);
				return TRUE;
			}
			break;
		case WM_CAPTURECHANGED:
			// another window took the mouse (a system dialog, Alt+Tab)
			if (fCaptured && (HWND)lParam != fWindow)
				SetMouseCaptured(false);
			return 0;
	}
	return DefWindowProcW(fWindow, message, wParam, lParam);
}
