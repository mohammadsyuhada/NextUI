#define _GNU_SOURCE
#include "wget_fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "defines.h"
#include "api.h"

// Path to bundled wget binary (relative to pak root, which is the working directory)
#define WGET_BIN SHARED_BIN_PATH "/wget"

int wget_fetch(const char* url, uint8_t* buffer, int buffer_size) {
	if (!url || !buffer || buffer_size <= 0) {
		LOG_error("[WgetFetch] Invalid parameters\n");
		return -1;
	}

	char tmpfile[128];
	snprintf(tmpfile, sizeof(tmpfile), "/tmp/wget_%d.tmp", getpid());

	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
			 WGET_BIN " --no-check-certificate -q -T 15 -t 2"
					  " -O '%s' '%s' 2>/dev/null",
			 tmpfile, url);

	int ret = system(cmd);

	if (ret != 0) {
		struct stat st;
		if (stat(tmpfile, &st) != 0 || st.st_size == 0) {
			LOG_error("[WgetFetch] Failed to fetch: %s (exit=%d)\n", url, ret);
			unlink(tmpfile);
			return -1;
		}
	}

	FILE* fp = fopen(tmpfile, "rb");
	if (!fp) {
		LOG_error("[WgetFetch] Failed to open temp file for: %s\n", url);
		unlink(tmpfile);
		return -1;
	}

	int total = fread(buffer, 1, buffer_size - 1, fp);
	fclose(fp);
	unlink(tmpfile);

	if (total <= 0) {
		LOG_error("[WgetFetch] Empty response for: %s\n", url);
		return -1;
	}

	return total;
}

int wget_download_file(const char* url, const char* filepath,
					   volatile int* progress_pct, volatile bool* should_stop) {
	if (!url || !filepath) {
		LOG_error("[WgetFetch] download: invalid parameters\n");
		return -1;
	}

	if (progress_pct)
		*progress_pct = 0;

	// Step 1: Get content-length via wget --spider
	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
			 WGET_BIN " --no-check-certificate --spider -S -T 10 -t 1"
					  " '%s' 2>&1 | grep -i 'Content-Length' | tail -1 | awk '{print $2}'",
			 url);

	long total_size = 0;
	FILE* pipe = popen(cmd, "r");
	if (pipe) {
		char size_buf[32];
		if (fgets(size_buf, sizeof(size_buf), pipe)) {
			total_size = atol(size_buf);
		}
		pclose(pipe);
	}

	if (should_stop && *should_stop)
		return -1;

	// Step 2: Start wget download in background with completion marker
	char done_marker[512];
	snprintf(done_marker, sizeof(done_marker), "%s.done", filepath);

	unlink(done_marker);

	snprintf(cmd, sizeof(cmd),
			 "(" WGET_BIN " --no-check-certificate -q -T 30 -t 2"
			 " -O '%s' '%s' 2>/dev/null; touch '%s') &",
			 filepath, url, done_marker);
	system(cmd);

	// Step 3: Poll file size for progress
	int result = -1;
	while (!(should_stop && *should_stop)) {
		if (access(done_marker, F_OK) == 0) {
			break;
		}

		struct stat st;
		if (stat(filepath, &st) == 0 && progress_pct && total_size > 0) {
			int pct = (int)((st.st_size * 100) / total_size);
			if (pct > 99)
				pct = 99;
			*progress_pct = pct;
		}

		usleep(200000); // 200ms polling interval
	}

	// Step 4: Handle cancellation
	if (should_stop && *should_stop) {
		snprintf(cmd, sizeof(cmd), "pkill -f 'wget.*%s' 2>/dev/null", filepath);
		system(cmd);
		unlink(done_marker);
		unlink(filepath);
		return -1;
	}

	// Step 5: Verify download
	unlink(done_marker);

	struct stat st;
	if (stat(filepath, &st) == 0 && st.st_size > 0) {
		result = (int)st.st_size;
		if (progress_pct)
			*progress_pct = 100;
	} else {
		LOG_error("[WgetFetch] download failed: %s\n", url);
		unlink(filepath);
	}

	return result;
}
