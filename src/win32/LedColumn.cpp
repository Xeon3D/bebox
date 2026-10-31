/*
 * BeBox emulator for Windows - a front panel light pipe.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "LedColumn.h"

#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

const int kSlots = 16;
const int kLoadLeds = 15;

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


void
DrawLedColumn(HDC dc, const RECT& rect, bool withDisk, float level,
	bool power, float disk, int style)
{
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	if (width <= 0 || height <= 0)
		return;
	bool redGreen = style == kLedStyleRedGreen;
	Color load = redGreen ? kGreen : kLime;
	Color colors[kSlots];
	float brightness[kSlots];
	for (int i = 0; i < kSlots; i++) {
		if (i == 0) {
			colors[i] = withDisk ? (redGreen ? kRed : kAmber) : load;
			brightness[i] = withDisk ? disk : (power ? 1.0f : 0.0f);
		} else {
			int led = i - 1;
			// the top three go red in the red and green look
			colors[i] = redGreen && led >= kLoadLeds - 3 ? kRed : load;
			brightness[i] = power ? level - led : 0;
		}
		brightness[i] = std::max(0.0f, std::min(1.0f, brightness[i]));
	}

	// The pipe: a frame of black plastic, the lit strip inside.
	const int border = 3;
	int pipeWidth = std::max(1, width - 2 * border);
	int rows = std::max(1, height - 2 * border);
	std::vector<DWORD> pixels((size_t)pipeWidth * rows);
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
		for (int x = 0; x < pipeWidth; x++) {
			// a little darker towards the pipe's sides
			float edge = 1.0f - 0.35f * std::pow(std::fabs(
				(x + 0.5f) / pipeWidth * 2 - 1), 2.0f);
			BYTE r = (BYTE)std::min(255.0f, light.red * edge);
			BYTE g = (BYTE)std::min(255.0f, light.green * edge);
			BYTE b = (BYTE)std::min(255.0f, light.blue * edge);
			pixels[(size_t)y * pipeWidth + x] = (r << 16) | (g << 8) | b;
		}
	}

	HBRUSH frame = CreateSolidBrush(RGB(12, 12, 14));
	FillRect(dc, &rect, frame);
	DeleteObject(frame);
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = pipeWidth;
	info.bmiHeader.biHeight = -rows;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	SetDIBitsToDevice(dc, rect.left + border, rect.top + border, pipeWidth,
		rows, 0, 0, 0, rows, pixels.data(), &info, DIB_RGB_COLORS);
}
