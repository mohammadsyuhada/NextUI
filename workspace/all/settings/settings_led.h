#ifndef SETTINGS_LED_H
#define SETTINGS_LED_H

#include "settings_menu.h"

// Create and return the LED Control settings page
SettingsPage* led_page_create(void);

// Destroy the LED Control settings page
void led_page_destroy(SettingsPage* page);

#endif // SETTINGS_LED_H
