#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Current settings
static struct {
} current_settings;

void Settings_init(void) {
}

void Settings_quit(void) {
	Settings_save();
}

void Settings_save(void) {
}
