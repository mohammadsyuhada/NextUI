#ifndef WGET_FETCH_H
#define WGET_FETCH_H

#include <stdint.h>
#include <stdbool.h>

// Fetch URL content into memory buffer using wget
int wget_fetch(const char* url, uint8_t* buffer, int buffer_size);

// Download URL to file with progress reporting and cancellation
int wget_download_file(const char* url, const char* filepath,
					   volatile int* progress_pct, volatile bool* should_stop);

#endif // WGET_FETCH_H
