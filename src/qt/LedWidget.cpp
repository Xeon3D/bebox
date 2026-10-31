/*
 * BeBox emulator for Qt - a front panel light pipe.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "LedWidget.h"

#include "frontend/Settings.h"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

const int kSlots = 16;
const int kLoadLeds = 15;
const int kWidth = 14;

struct Color {
	float red, green, blue;
};

const Color kLime = { 150, 255, 40 };
const Color kGreen = { 40, 230, 60 };
const Color kAmber = { 255, 170, 0 };
const Color kRed = { 255, 40, 30 };
// The pipe itself, dark smoky acrylic.
const Color kAcrylic = { 10, 12, 10 };
// An unlit LED still shows faintly through the pipe.
const float kUnlit = 0.10f;


// The light of an LED at 'distance' LED spacings from it: a bright spot
// with soft edges and a faint glow further along.
float
LightSpread(float distance)
{
	distance = std::fabs(distance);
	float spot = std::exp(-std::pow(distance / 0.46f, 3.0f));
	float glow = std::exp(-(distance / 0.7f) * (distance / 0.7f));
	return 0.85f * spot + 0.15f * glow;
}

}	// namespace


LedWidget::LedWidget(bool withDisk, QWidget* parent)
	:
	QWidget(parent),
	fWithDisk(withDisk),
	fLevel(0),
	fPower(false),
	fDisk(0),
	fStyle(kLedStyleAuthentic)
{
	setAttribute(Qt::WA_OpaquePaintEvent);
	setFixedWidth(kWidth);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}


QSize
LedWidget::sizeHint() const
{
	return QSize(kWidth, 480);
}


void
LedWidget::SetLevels(float level, bool power, float disk)
{
	if (std::fabs(level - fLevel) < 0.01f && power == fPower
		&& std::fabs(disk - fDisk) < 0.01f)
		return;
	fLevel = level;
	fPower = power;
	fDisk = disk;
	update();
}


void
LedWidget::SetStyle(int style)
{
	fStyle = style;
	update();
}


void
LedWidget::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	painter.fillRect(rect(), QColor(12, 12, 14));
	double ratio = devicePixelRatioF();
	const int border = 3;
	int pipeWidth = std::max(1, (int)std::lround((width() - 2 * border)
		* ratio));
	int rows = std::max(1, (int)std::lround((height() - 2 * border)
		* ratio));

	bool redGreen = fStyle == kLedStyleRedGreen;
	Color load = redGreen ? kGreen : kLime;
	Color colors[kSlots];
	float brightness[kSlots];
	for (int i = 0; i < kSlots; i++) {
		if (i == 0) {
			colors[i] = fWithDisk ? (redGreen ? kRed : kAmber) : load;
			brightness[i] = fWithDisk ? fDisk : (fPower ? 1.0f : 0.0f);
		} else {
			int led = i - 1;
			// the top three go red in the red and green look
			colors[i] = redGreen && led >= kLoadLeds - 3 ? kRed : load;
			brightness[i] = fPower ? fLevel - led : 0;
		}
		brightness[i] = std::max(0.0f, std::min(1.0f, brightness[i]));
	}

	QImage pipe(pipeWidth, rows, QImage::Format_RGB32);
	float slot = rows / (float)kSlots;
	for (int y = 0; y < rows; y++) {
		// in LED spacings from the bottom of the pipe
		float position = (rows - y - 0.5f) / slot;
		Color light = kAcrylic;
		for (int i = 0; i < kSlots; i++) {
			float distance = position - (i + 0.5f);
			if (std::fabs(distance) > 2.5f)
				continue;
			float amount = LightSpread(distance)
				* (kUnlit + (1 - kUnlit) * brightness[i]);
			light.red += colors[i].red * amount;
			light.green += colors[i].green * amount;
			light.blue += colors[i].blue * amount;
		}
		QRgb* line = (QRgb*)pipe.scanLine(y);
		for (int x = 0; x < pipeWidth; x++) {
			// a little darker towards the pipe's sides
			float edge = 1.0f - 0.35f * std::pow(std::fabs(
				(x + 0.5f) / pipeWidth * 2 - 1), 2.0f);
			line[x] = qRgb((int)std::min(255.0f, light.red * edge),
				(int)std::min(255.0f, light.green * edge),
				(int)std::min(255.0f, light.blue * edge));
		}
	}
	pipe.setDevicePixelRatio(ratio);
	painter.drawImage(QPointF(border, border), pipe);
}
