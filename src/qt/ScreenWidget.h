/*
 * BeBox emulator for Qt - the emulated display.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The machine's VideoSink: frames arrive on the emulation thread into a
 * staging buffer, the widget copies the changed lines and repaints them.
 * Scales are in device pixels, so 100% stays sharp on high density
 * screens; whole multiples are drawn sharp, other sizes filtered.
 *
 * The captured mouse: the pointer is hidden and grabbed, and its motion
 * goes to the BeBox's PS/2 mouse; it goes back to the widget's middle
 * now and then so that it never sticks at an edge.
 */
#ifndef QT_SCREEN_WIDGET_H
#define QT_SCREEN_WIDGET_H

#include "core/host/VideoSink.h"

#include <QImage>
#include <QPoint>
#include <QWidget>

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace bebox {
	class EmulatorThread;
}

enum FullScreenScale {
	kFullScreenFill = 0,		// as large as fits
	kFullScreenWhole = 1,		// the largest whole multiple
	kFullScreenActual = 2		// 1:1
};

class ScreenWidget : public QWidget, public bebox::VideoSink {
	Q_OBJECT
public:
								ScreenWidget(bebox::EmulatorThread& emulator,
									QWidget* parent = nullptr);
	virtual						~ScreenWidget();

	virtual	void				FrameReady(const bebox::u32* pixels,
									int width, int height, int stride,
									int top, int bottom) override;

			QSize				FrameSize() const;
			// The widget's size for the picture at 'scale', in the
			// widget's (logical) pixels.
			QSize				SizeForScale(int scale) const;
			void				SetScale(int scale);	// 0: fit
			void				SetFullScreen(bool fullScreen, int scale);
			void				SetPowered(bool powered);
			void				ShowHint(const QString& text);

			void				SetMouseCaptured(bool captured);
			bool				IsMouseCaptured() const { return fCaptured; }
			void				SetMouseOptions(int speed, bool swapButtons,
									bool captureOnClick);

			QImage				CopyFrame() const { return fFrame.copy(); }

signals:
			void				FrameResized();
			void				CaptureChanged(bool captured);

protected:
	virtual	void				paintEvent(QPaintEvent* event) override;
	virtual	void				mousePressEvent(QMouseEvent* event) override;
	virtual	void				mouseReleaseEvent(QMouseEvent* event)
									override;
	virtual	void				mouseMoveEvent(QMouseEvent* event) override;
	virtual	void				wheelEvent(QWheelEvent* event) override;
	virtual	void				focusOutEvent(QFocusEvent* event) override;
	virtual	bool				focusNextPrevChild(bool next) override
									{ return false; }

private:
			void				_TakeFrame();
			QRectF				_DestinationRect() const;
			void				_SendMouse(int dx, int dy, int dz);
			void				_UpdateButtons(Qt::MouseButtons buttons);
			QPoint				_Center() const;

			bebox::EmulatorThread& fEmulator;

			// written on the emulation thread
	mutable	std::mutex			fLock;
			std::vector<bebox::u32> fStaging;
			int					fStagingWidth;
			int					fStagingHeight;
			int					fDirtyTop;
			int					fDirtyBottom;
			std::atomic<bool>	fFramePending;

			QImage				fFrame;
			int					fScale;
			bool				fFullScreen;
			int					fFullScreenScale;
			bool				fPowered;
			QString				fHint;
			int					fHintGeneration;

			bool				fCaptured;
			int					fMouseSpeed;
			bool				fSwapButtons;
			bool				fCaptureOnClick;
			bebox::u8			fButtons;		// the PS/2 mouse's
			QPoint				fLastPosition;
			bool				fWarped;
			float				fRemainderX;
			float				fRemainderY;
			int					fWheelRemainder;
};

#endif	// QT_SCREEN_WIDGET_H
