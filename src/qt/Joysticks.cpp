/*
 * BeBox emulator for Qt - host joysticks for the game ports, through SDL2.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "Joysticks.h"

#ifdef HAVE_SDL2
#	define SDL_MAIN_HANDLED
#	include <SDL.h>
#endif


Joysticks::Joysticks()
	:
	fAvailable(false)
{
#ifdef HAVE_SDL2
	SDL_SetMainReady();
	// the window has the focus: the sticks are read without events
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	// SIGINT and SIGTERM stay Qt's (SDL would make them SDL_QUIT events
	// nobody reads, and BeBox would not quit)
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	fAvailable = SDL_Init(SDL_INIT_JOYSTICK) == 0;
#endif
}


Joysticks::~Joysticks()
{
	Close();
#ifdef HAVE_SDL2
	if (fAvailable)
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
#endif
}


std::vector<std::string>
Joysticks::Names()
{
	std::vector<std::string> names;
#ifdef HAVE_SDL2
	if (!fAvailable)
		return names;
	SDL_JoystickUpdate();
	int count = SDL_NumJoysticks();
	for (int i = 0; i < count; i++) {
		const char* name = SDL_JoystickNameForIndex(i);
		names.push_back(name != nullptr ? name : "Game controller");
	}
#endif
	return names;
}


void
Joysticks::Open(const std::vector<int>& indices)
{
	Close();
#ifdef HAVE_SDL2
	for (int index : indices) {
		SDL_Joystick* joystick = nullptr;
		if (fAvailable && index >= 0 && index < SDL_NumJoysticks())
			joystick = SDL_JoystickOpen(index);
		fOpen.push_back(joystick);
	}
#else
	fOpen.assign(indices.size(), nullptr);
#endif
}


void
Joysticks::Close()
{
#ifdef HAVE_SDL2
	for (void* joystick : fOpen) {
		if (joystick != nullptr)
			SDL_JoystickClose((SDL_Joystick*)joystick);
	}
#endif
	fOpen.clear();
}


bool
Joysticks::Poll(int slot, JoystickState& state)
{
#ifdef HAVE_SDL2
	if (slot < 0 || slot >= (int)fOpen.size() || fOpen[slot] == nullptr)
		return false;
	if (slot == 0 || fOpen[0] == nullptr)
		SDL_JoystickUpdate();
	SDL_Joystick* joystick = (SDL_Joystick*)fOpen[slot];
	// -32768..32767 to the 12-bit converter's range
	int x = SDL_JoystickNumAxes(joystick) > 0
		? SDL_JoystickGetAxis(joystick, 0) : 0;
	int y = SDL_JoystickNumAxes(joystick) > 1
		? SDL_JoystickGetAxis(joystick, 1) : 0;
	state.x = (bebox::u16)((x + 32768) >> 4);
	state.y = (bebox::u16)((y + 32768) >> 4);
	state.button1 = SDL_JoystickGetButton(joystick, 0) != 0;
	state.button2 = SDL_JoystickGetButton(joystick, 1) != 0;
	return true;
#else
	return false;
#endif
}
