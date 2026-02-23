#ifndef __UI_MAIN_H__
#define __UI_MAIN_H__

#include "api.h"
#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

// Render the main menu
void render_menu(SDL_Surface* screen, IndicatorType show_setting, int menu_selected,
				 char* toast_message, uint32_t toast_time);

// Render confirmation dialog overlay (title + optional content + "A: Yes  B: No")
void render_confirmation_dialog(SDL_Surface* screen, const char* content, const char* title);

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, int app_state);

// Check if menu scroll needs continuous redraw
bool menu_needs_scroll_redraw(void);

#endif
