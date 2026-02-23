#ifndef GAMESWITCHER_H
#define GAMESWITCHER_H

#include "sdl.h"
#include <stdbool.h>

typedef struct {
	bool dirty;
	bool folderbgchanged;
	bool startgame;
	int screen;
	int gsanimdir;
} GameSwitcherResult;

void GameSwitcher_init(void);
int GameSwitcher_shouldStartInSwitcher(void);
void GameSwitcher_resetSelection(void);
int GameSwitcher_getSelected(void);
const char* GameSwitcher_getSelectedName(void);
GameSwitcherResult GameSwitcher_handleInput(unsigned long now);
void GameSwitcher_render(int lastScreen, SDL_Surface* blackBG, int gsanimdir);

#endif // GAMESWITCHER_H
