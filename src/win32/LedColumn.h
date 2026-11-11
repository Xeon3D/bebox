/*
 * BeBox emulator for Windows - a front panel light pipe.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * Sixteen LEDs behind a strip of dark acrylic: from the bottom, the power
 * LED (left column) or the disk LED (right column), then fifteen LEDs of
 * the CPU load bar graph. Each LED's light spreads along the pipe and
 * adds up with its neighbours'.
 */
#ifndef WIN32_LED_COLUMN_H
#define WIN32_LED_COLUMN_H

#include <windows.h>

const int kLedColumnWidth = 14;

// Draws a column into 'rect': 'level' 0-15 of the load bar, 'disk' the
// disk LED's duty cycle (0-1, the right column only).
void DrawLedColumn(HDC dc, const RECT& rect, bool withDisk, float level,
	bool power, float disk, int style);

#endif	// WIN32_LED_COLUMN_H
