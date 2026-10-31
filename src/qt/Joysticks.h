/*
 * BeBox emulator for Qt - host joysticks for the game ports, through SDL2.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * Qt 6 has no game controller support; SDL2 has, on all the systems the
 * Qt front end runs on. Built without SDL2 there are no joysticks.
 */
#ifndef QT_JOYSTICKS_H
#define QT_JOYSTICKS_H

#include "core/Types.h"

#include <string>
#include <vector>

struct JoystickState {
	bebox::u16	x;			// 0-4095, 2048 centered
	bebox::u16	y;
	bool		button1;
	bool		button2;
};

class Joysticks {
public:
								Joysticks();
								~Joysticks();

			bool				IsAvailable() const { return fAvailable; }
			// The joysticks connected now: their names, by index.
			std::vector<std::string> Names();

			// Opens the joysticks of these indices (-1: none) for Poll().
			void				Open(const std::vector<int>& indices);
			void				Close();
			// Stick 'slot' as opened; false if it has none.
			bool				Poll(int slot, JoystickState& state);

private:
			bool				fAvailable;
			std::vector<void*>	fOpen;			// SDL_Joystick*
};

#endif	// QT_JOYSTICKS_H
