/*
 * BeBox emulator for Qt - the emulated display.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "ScreenWidget.h"

#include "core/board/Machine.h"
#include "core/host/EmulatorThread.h"
#include "core/isa/Kbc8042.h"

#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>


ScreenWidget::ScreenWidget(bebox::EmulatorThread& emulator, QWidget* parent)
	:
	QWidget(parent),
	fEmulator(emulator),
	fStagingWidth(640),
	fStagingHeight(480),
	fDirtyTop(0),
	fDirtyBottom(480),
	fFramePending(false),
	fFrame(640, 480, QImage::Format_RGB32),
	fScale(1),
	fFullScreen(false),
	fFullScreenScale(kFullScreenFill),
	fPowered(false),
	fHintGeneration(0),
	fCaptured(false),
	fMouseSpeed(100),
	fSwapButtons(false),
	fCaptureOnClick(true),
	fButtons(0),
	fWarped(false),
	fRemainderX(0),
	fRemainderY(0),
	fWheelRemainder(0)
{
	fStaging.assign(640 * 480, 0);
	fFrame.fill(Qt::black);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}


ScreenWidget::~ScreenWidget()
{
	SetMouseCaptured(false);
}


// Emulation thread.
void
ScreenWidget::FrameReady(const bebox::u32* pixels, int width, int height,
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
	if (!fFramePending.exchange(true))
		QMetaObject::invokeMethod(this, [this]() { _TakeFrame(); },
			Qt::QueuedConnection);
}


QSize
ScreenWidget::FrameSize() const
{
	return fFrame.size();
}


QSize
ScreenWidget::SizeForScale(int scale) const
{
	double ratio = devicePixelRatioF();
	return QSize((int)std::ceil(fFrame.width() * scale / ratio),
		(int)std::ceil(fFrame.height() * scale / ratio));
}


void
ScreenWidget::SetScale(int scale)
{
	fScale = scale;
	update();
}


void
ScreenWidget::SetFullScreen(bool fullScreen, int scale)
{
	fFullScreen = fullScreen;
	fFullScreenScale = scale;
	update();
}


void
ScreenWidget::SetPowered(bool powered)
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
	update();
}


void
ScreenWidget::ShowHint(const QString& text)
{
	fHint = text;
	int generation = ++fHintGeneration;
	if (!text.isEmpty()) {
		QTimer::singleShot(3000, this, [this, generation]() {
			if (generation == fHintGeneration) {
				fHint.clear();
				update();
			}
		});
	}
	update();
}


// The staging buffer's changed lines into the frame drawn.
void
ScreenWidget::_TakeFrame()
{
	fFramePending = false;
	int top, bottom;
	bool resized;
	{
		std::lock_guard<std::mutex> lock(fLock);
		resized = fStagingWidth != fFrame.width()
			|| fStagingHeight != fFrame.height();
		if (resized) {
			fFrame = QImage(fStagingWidth, fStagingHeight,
				QImage::Format_RGB32);
			fDirtyTop = 0;
			fDirtyBottom = fStagingHeight;
		}
		top = std::max(0, fDirtyTop);
		bottom = std::min(fStagingHeight, fDirtyBottom);
		fDirtyTop = fStagingHeight;
		fDirtyBottom = 0;
		// the core's pixels are 0x00RRGGBB; RGB32 wants 0xff in the top
		// byte (copied as is to a backing store with alpha, as on Wayland,
		// 0x00 lets the desktop show through)
		for (int y = top; y < bottom; y++) {
			const uint32_t* from = &fStaging[(size_t)y * fStagingWidth];
			QRgb* to = (QRgb*)fFrame.scanLine(y);
			for (int x = 0; x < fStagingWidth; x++)
				to[x] = from[x] | 0xff000000u;
		}
	}
	if (resized) {
		update();
		emit FrameResized();
		return;
	}
	if (top >= bottom)
		return;
	// the changed lines as scaled, a line more for the filter
	QRectF destination = _DestinationRect();
	double scale = destination.height() / fFrame.height();
	QRectF dirty(destination.left(), destination.top() + (top - 1) * scale,
		destination.width(), (bottom - top + 2) * scale);
	update(dirty.toAlignedRect() & destination.toAlignedRect());
}


QRectF
ScreenWidget::_DestinationRect() const
{
	// in device pixels, then back
	double ratio = devicePixelRatioF();
	double width = fFrame.width();
	double height = fFrame.height();
	double boundsWidth = this->width() * ratio;
	double boundsHeight = this->height() * ratio;
	double fit = std::min(boundsWidth / width, boundsHeight / height);
	double scale;
	if (!fFullScreen)
		scale = fScale == 0 ? fit : fScale;
	else if (fFullScreenScale == kFullScreenWhole)
		scale = std::max(1.0, std::floor(fit));
	else if (fFullScreenScale == kFullScreenActual)
		scale = 1;
	else
		scale = fit;
	// a picture larger than the widget fits it
	if (scale > fit)
		scale = fit;
	double w = std::round(width * scale);
	double h = std::round(height * scale);
	double left = std::floor((boundsWidth - w) / 2);
	double top = std::floor((boundsHeight - h) / 2);
	return QRectF(left / ratio, top / ratio, w / ratio, h / ratio);
}


void
ScreenWidget::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	QRectF destination = _DestinationRect();
	QPainterPath background;
	background.addRect(rect());
	QPainterPath picture;
	picture.addRect(destination);
	painter.fillPath(background.subtracted(picture), Qt::black);
	// whole multiples stay sharp, other sizes are filtered
	double ratio = devicePixelRatioF();
	double scale = destination.width() * ratio / fFrame.width();
	bool whole = std::fabs(scale - std::round(scale)) < 0.001;
	painter.setRenderHint(QPainter::SmoothPixmapTransform, !whole);
	painter.drawImage(destination, fFrame);

	if (!fHint.isEmpty()) {
		QFont font = painter.font();
		font.setBold(true);
		font.setPointSizeF(font.pointSizeF() * 1.2);
		painter.setFont(font);
		QFontMetrics metrics(font);
		QRect text = metrics.boundingRect(fHint);
		QRect box(0, 0, text.width() + 24, text.height() + 16);
		box.moveCenter(QPoint(width() / 2, 24 + box.height() / 2));
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, 190));
		painter.drawRoundedRect(box, 6, 6);
		painter.setPen(Qt::white);
		painter.drawText(box, Qt::AlignCenter, fHint);
	}
}


// #pragma mark - mouse


void
ScreenWidget::SetMouseOptions(int speed, bool swapButtons,
	bool captureOnClick)
{
	fMouseSpeed = std::max(10, std::min(1000, speed));
	fSwapButtons = swapButtons;
	fCaptureOnClick = captureOnClick;
}


QPoint
ScreenWidget::_Center() const
{
	return mapToGlobal(QPoint(width() / 2, height() / 2));
}


void
ScreenWidget::SetMouseCaptured(bool captured)
{
	if (captured && !fPowered)
		return;
	if (captured == fCaptured)
		return;
	fCaptured = captured;
	if (captured) {
		setCursor(Qt::BlankCursor);
		grabMouse();
		grabKeyboard();
		fRemainderX = fRemainderY = 0;
		fWarped = true;
		QCursor::setPos(_Center());
		fLastPosition = _Center();
	} else {
		releaseKeyboard();
		releaseMouse();
		unsetCursor();
		fButtons = 0;
		_SendMouse(0, 0, 0);
	}
	emit CaptureChanged(captured);
}


void
ScreenWidget::_SendMouse(int dx, int dy, int dz)
{
	bebox::u8 buttons = fButtons;
	fEmulator.Post([dx, dy, dz, buttons](bebox::Machine& machine) {
		machine.Keyboard().MouseMoved(dx, dy, dz, buttons);
	});
}


void
ScreenWidget::_UpdateButtons(Qt::MouseButtons buttons)
{
	bebox::u8 primary = fSwapButtons ? 2 : 1;
	bebox::u8 secondary = fSwapButtons ? 1 : 2;
	fButtons = ((buttons & Qt::LeftButton) ? primary : 0)
		| ((buttons & Qt::RightButton) ? secondary : 0)
		| ((buttons & Qt::MiddleButton) ? 4 : 0);
}


void
ScreenWidget::mousePressEvent(QMouseEvent* event)
{
	setFocus();
	if (!fCaptured) {
		if (fCaptureOnClick && fPowered && event->button() == Qt::LeftButton)
			SetMouseCaptured(true);
		return;
	}
	_UpdateButtons(event->buttons());
	_SendMouse(0, 0, 0);
}


void
ScreenWidget::mouseReleaseEvent(QMouseEvent* event)
{
	if (!fCaptured)
		return;
	_UpdateButtons(event->buttons());
	_SendMouse(0, 0, 0);
}


// Motion of the captured pointer, from one position to the next; the
// pointer goes back to the middle now and then (the move that makes is
// no motion).
void
ScreenWidget::mouseMoveEvent(QMouseEvent* event)
{
	if (!fCaptured)
		return;
	QPoint position = event->globalPosition().toPoint();
	if (fWarped && position == _Center()) {
		fWarped = false;
		fLastPosition = position;
		return;
	}
	float moveX = (position.x() - fLastPosition.x()) * fMouseSpeed / 100.0f
		+ fRemainderX;
	float moveY = (position.y() - fLastPosition.y()) * fMouseSpeed / 100.0f
		+ fRemainderY;
	fLastPosition = position;
	int dx = (int)moveX;
	int dy = (int)moveY;
	fRemainderX = moveX - dx;
	fRemainderY = moveY - dy;
	_UpdateButtons(event->buttons());
	if (dx != 0 || dy != 0) {
		// PS/2: positive y is up
		_SendMouse(dx, -dy, 0);
	}
	QPoint center = _Center();
	if (std::abs(position.x() - center.x()) > width() / 4
		|| std::abs(position.y() - center.y()) > height() / 4) {
		fWarped = true;
		QCursor::setPos(center);
		fLastPosition = center;
	}
}


void
ScreenWidget::wheelEvent(QWheelEvent* event)
{
	if (!fCaptured)
		return;
	// IntelliMouse: positive is towards the user (scrolling down)
	fWheelRemainder += event->angleDelta().y();
	int steps = fWheelRemainder / 120;
	fWheelRemainder -= steps * 120;
	if (steps != 0)
		_SendMouse(0, 0, -steps);
}


void
ScreenWidget::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
}
