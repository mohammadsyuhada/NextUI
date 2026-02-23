#include "keyboard.h"
#include "vp_defines.h"
#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char keyboard_path[512] = "";
static int keyboard_initialized = 0;

void Keyboard_init(void) {
	if (keyboard_initialized)
		return;

	// Keyboard binary is in the pak's bin folder
	// Pak directory is current working directory (launch.sh sets cwd to pak folder)
	snprintf(keyboard_path, sizeof(keyboard_path), "./bin/keyboard");
	chmod(keyboard_path, 0755);

	keyboard_initialized = 1;
}

char* Keyboard_open(const char* prompt) {
	(void)prompt; // Not used with external keyboard

	// Lazy init if not already initialized
	if (!keyboard_initialized) {
		Keyboard_init();
	}

	if (access(keyboard_path, X_OK) != 0) {
		LOG_error("Keyboard binary not found: %s\n", keyboard_path);
		return NULL;
	}

	// Use the same custom font as the rest of the application
	const char* font_path = APP_RES_PATH "/fonts/font.ttf";

	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "%s \"%s\" 2>/dev/null", keyboard_path, font_path);

	FILE* pipe = popen(cmd, "r");
	if (!pipe) {
		return NULL;
	}

	char* result = malloc(512);
	if (result) {
		result[0] = '\0';
		if (fgets(result, 512, pipe)) {
			char* nl = strchr(result, '\n');
			if (nl)
				*nl = '\0';
		}

		// If empty, user cancelled
		if (result[0] == '\0') {
			free(result);
			result = NULL;
		}
	}

	pclose(pipe);
	return result;
}
