#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

// Initialize keyboard module
// Call this before using Keyboard_open()
void Keyboard_init(void);

// Open keyboard for text input
// Returns allocated string that caller must free, or NULL if cancelled
char* Keyboard_open(const char* prompt);

#endif // __KEYBOARD_H__
