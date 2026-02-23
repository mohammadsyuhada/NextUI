#ifndef __MODULE_PLAYER_H__
#define __MODULE_PLAYER_H__

#include <SDL2/SDL.h>
#include "module_common.h"

// Run the local video file browser and player module
// Handles: File browser for VIDEO_ROOT, launching ffplay for selected video
ModuleExitReason PlayerModule_run(SDL_Surface* screen);

#endif
