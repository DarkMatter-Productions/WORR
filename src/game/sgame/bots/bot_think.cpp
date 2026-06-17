/*Copyright (c) 2024 ZeniMax Media Inc.
Licensed under the GNU General Public License 2.0.

bot_think.cpp implementation.*/

#include "../g_local.hpp"
#include "bot_runtime.hpp"
#include "bot_think.hpp"

/*
================
Bot_BeginFrame
================
*/
void Bot_BeginFrame( gentity_t * bot ) {
	(void)bot;
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

}

/*
================
Bot_EndFrame
================
*/
void Bot_EndFrame( gentity_t * bot ) {
	(void)bot;
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

}
