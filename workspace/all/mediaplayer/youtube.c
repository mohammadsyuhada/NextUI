#include "youtube.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "vp_defines.h"
#include "api.h"
#include "parson/parson.h"

#define YTDLP_BIN SHARED_BIN_PATH "/yt-dlp"
#define WGET_BIN SHARED_BIN_PATH "/wget"

static YouTubeAsyncOp search_op;
static YouTubeAsyncOp resolve_op;
static YouTubeThumbDownloader thumb_dl;
static YouTubeChannelInfoOp channel_info_op;
static YouTubeUploadsOp uploads_op;

// Per-channel thumbnail downloader
static struct {
	YouTubeThumbDownloader dl;
	char channel_id[SUBS_MAX_ID];
} channel_thumb_dl;

// Single thumbnail retry state
static struct {
	pthread_t thread;
	char id[YT_MAX_ID];
	volatile bool running;
} thumb_retry;

void YouTube_init(void) {
	memset(&search_op, 0, sizeof(search_op));
	memset(&resolve_op, 0, sizeof(resolve_op));
	memset(&thumb_dl, 0, sizeof(thumb_dl));
	memset(&channel_info_op, 0, sizeof(channel_info_op));
	memset(&uploads_op, 0, sizeof(uploads_op));
	memset(&channel_thumb_dl, 0, sizeof(channel_thumb_dl));
	memset(&thumb_retry, 0, sizeof(thumb_retry));
}

// Check if a JPEG file on disk is complete (ends with FF D9)
static bool is_jpeg_complete(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	if (size < 4) {
		fclose(f);
		return false;
	}

	// Check JPEG end marker (FF D9)
	uint8_t tail[2];
	fseek(f, -2, SEEK_END);
	if (fread(tail, 1, 2, f) != 2) {
		fclose(f);
		return false;
	}
	fclose(f);

	return (tail[0] == 0xFF && tail[1] == 0xD9);
}

// Build thumbnail cache path for a video ID
bool YouTube_getThumbnailPath(const char* video_id, char* path_out, int size) {
	snprintf(path_out, size, APP_THUMBNAILS_DIR "/%s.jpg", video_id);
	struct stat st;
	if (stat(path_out, &st) != 0 || st.st_size == 0)
		return false;

	// Verify JPEG is complete; delete if truncated
	if (!is_jpeg_complete(path_out)) {
		unlink(path_out);
		return false;
	}
	return true;
}

// Clean thumbnail cache: delete files not in the current result set
static void thumb_cache_cleanup(YouTubeThumbDownloader* dl) {
	DIR* dir = opendir(APP_THUMBNAILS_DIR);
	if (!dir)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		// Extract video ID from filename (strip .jpg extension)
		char id[YT_MAX_ID];
		strncpy(id, entry->d_name, YT_MAX_ID - 1);
		id[YT_MAX_ID - 1] = '\0';
		char* dot = strrchr(id, '.');
		if (dot)
			*dot = '\0';

		// Check if this ID is in the current set
		bool keep = false;
		for (int i = 0; i < dl->count; i++) {
			if (strcmp(id, dl->ids[i]) == 0) {
				keep = true;
				break;
			}
		}

		if (!keep) {
			char path[512];
			snprintf(path, sizeof(path), APP_THUMBNAILS_DIR "/%s", entry->d_name);
			unlink(path);
		}
	}
	closedir(dir);
}

// Background thread: download thumbnails one-by-one
static void* thumb_download_thread(void* arg) {
	YouTubeThumbDownloader* dl = (YouTubeThumbDownloader*)arg;

	// Ensure thumbnails directory exists
	mkdir(SDCARD_PATH "/.cache", 0755);
	mkdir(APP_THUMBNAILS_DIR, 0755);

	// Purge stale thumbnails not in current result set
	thumb_cache_cleanup(dl);

	for (int i = 0; i < dl->count; i++) {
		if (dl->cancel)
			break;

		char path[512];
		// Skip if already cached and complete
		if (YouTube_getThumbnailPath(dl->ids[i], path, sizeof(path))) {
			dl->downloaded_count = i + 1;
			continue;
		}

		// Download thumbnail via wget, try multiple qualities
		char cmd[1024];
		const char* thumb_qualities[] = {"sddefault", "hqdefault", "mqdefault"};
		bool thumb_ok = false;

		for (int q = 0; q < 3 && !thumb_ok && !dl->cancel; q++) {
			snprintf(cmd, sizeof(cmd),
					 WGET_BIN " -q -T 10 --no-check-certificate -O '%s' "
							  "'https://i.ytimg.com/vi/%s/%s.jpg' 2>/dev/null",
					 path, dl->ids[i], thumb_qualities[q]);

			system(cmd);

			// Check result
			struct stat st;
			if (stat(path, &st) != 0 || st.st_size == 0) {
				unlink(path);
				continue;
			}

			if (is_jpeg_complete(path)) {
				thumb_ok = true;
			} else {
				unlink(path);
			}
		}

		if (!thumb_ok && !dl->cancel) {
			LOG_info("Thumb [%s] failed all qualities\n", dl->ids[i]);
		}

		dl->downloaded_count = i + 1;
	}

	dl->running = false;
	return NULL;
}

void YouTube_downloadThumbnails(YouTubeSearchResults* results) {
	YouTube_cancelThumbnails();

	memset(&thumb_dl, 0, sizeof(thumb_dl));
	thumb_dl.count = results->count;
	for (int i = 0; i < results->count; i++) {
		strncpy(thumb_dl.ids[i], results->items[i].id, YT_MAX_ID - 1);
	}
	thumb_dl.running = true;

	pthread_create(&thumb_dl.thread, NULL, thumb_download_thread, &thumb_dl);
}

void YouTube_cancelThumbnails(void) {
	if (thumb_dl.running) {
		thumb_dl.cancel = true;
		pthread_detach(thumb_dl.thread);
		thumb_dl.running = false;
	}
}

YouTubeThumbDownloader* YouTube_getThumbDownloader(void) {
	return &thumb_dl;
}

// Background thread: retry downloading a single thumbnail
static void* thumb_retry_thread(void* arg) {
	(void)arg;
	char path[512];
	snprintf(path, sizeof(path), APP_THUMBNAILS_DIR "/%s.jpg", thumb_retry.id);

	char cmd[1024];
	const char* thumb_qualities[] = {"sddefault", "hqdefault", "mqdefault"};

	for (int q = 0; q < 3; q++) {
		snprintf(cmd, sizeof(cmd),
				 WGET_BIN " -q -T 10 --no-check-certificate -O '%s' "
						  "'https://i.ytimg.com/vi/%s/%s.jpg' 2>/dev/null",
				 path, thumb_retry.id, thumb_qualities[q]);

		system(cmd);

		struct stat st;
		if (stat(path, &st) != 0 || st.st_size == 0) {
			unlink(path);
			continue;
		}

		if (is_jpeg_complete(path)) {
			break; // Success
		}
		unlink(path);
	}

	thumb_retry.running = false;
	return NULL;
}

bool YouTube_retryThumbnail(const char* video_id) {
	// Already retrying this ID
	if (thumb_retry.running && strcmp(thumb_retry.id, video_id) == 0) {
		return true;
	}

	// Wait for previous retry to finish
	if (thumb_retry.running) {
		return false; // Busy with another retry
	}

	// Join previous thread if any
	if (thumb_retry.id[0]) {
		pthread_join(thumb_retry.thread, NULL);
	}

	strncpy(thumb_retry.id, video_id, YT_MAX_ID - 1);
	thumb_retry.id[YT_MAX_ID - 1] = '\0';
	thumb_retry.running = true;

	pthread_create(&thumb_retry.thread, NULL, thumb_retry_thread, NULL);
	return true;
}

// Parse duration string like "123" (seconds) or "NA"
static int parse_duration(const char* str) {
	if (!str || str[0] == '\0' || strcmp(str, "NA") == 0)
		return -1;
	return atoi(str);
}

// URL-encode a query string for use in URLs
// Search via yt-dlp
// Returns number of results, or -1 on failure
static int search_via_ytdlp(YouTubeAsyncOp* op) {
	// Sanitize query - strip shell-dangerous characters
	char safe_query[1024];
	int j = 0;
	for (int i = 0; op->query[i] && j < (int)sizeof(safe_query) - 2; i++) {
		char c = op->query[i];
		if (c == '"' || c == '\'' || c == '`' || c == '$' ||
			c == '\\' || c == ';' || c == '&' || c == '|') {
			continue;
		}
		safe_query[j++] = c;
	}
	safe_query[j] = '\0';

	const char* temp_file = "/tmp/yt_video_search.txt";
	const char* temp_err = "/tmp/yt_video_search_err.txt";

	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
			 YTDLP_BIN " 'ytsearch%d:%s'"
					   " --flat-playlist --no-warnings --socket-timeout 15"
					   " --print '%%(id)s\t%%(title)s\t%%(channel)s\t%%(duration)s'"
					   " > %s 2> %s",
			 YT_MAX_RESULTS, safe_query, temp_file, temp_err);

	LOG_info("yt-dlp search: %s\n", cmd);

	int ret = system(cmd);

	if (op->cancel) {
		unlink(temp_file);
		unlink(temp_err);
		return -1;
	}

	if (ret != 0) {
		FILE* err = fopen(temp_err, "r");
		if (err) {
			char err_line[256];
			if (fgets(err_line, sizeof(err_line), err)) {
				char* nl = strchr(err_line, '\n');
				if (nl)
					*nl = '\0';
				LOG_error("yt-dlp error: %s\n", err_line);

				if (strstr(err_line, "name resolution") || strstr(err_line, "resolve")) {
					snprintf(op->error, sizeof(op->error), "Network error - check WiFi");
				} else if (strstr(err_line, "timed out") || strstr(err_line, "timeout")) {
					snprintf(op->error, sizeof(op->error), "Connection timed out");
				} else if (strstr(err_line, "SSL") || strstr(err_line, "certificate")) {
					snprintf(op->error, sizeof(op->error), "SSL error - update yt-dlp");
				} else {
					snprintf(op->error, sizeof(op->error), "Search failed (exit %d)", WEXITSTATUS(ret));
				}
			}
			fclose(err);
		}
	}

	FILE* f = fopen(temp_file, "r");
	if (!f) {
		unlink(temp_err);
		return -1;
	}

	char line[2048];
	int count = 0;

	while (fgets(line, sizeof(line), f) && count < YT_MAX_RESULTS) {
		if (op->cancel)
			break;

		int len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (len == 0)
			continue;

		char* id = line;
		char* title = strchr(id, '\t');
		if (!title)
			continue;
		*title++ = '\0';

		char* channel = strchr(title, '\t');
		if (!channel)
			continue;
		*channel++ = '\0';

		char* duration_str = strchr(channel, '\t');
		if (!duration_str)
			continue;
		*duration_str++ = '\0';

		if (id[0] == '\0' || strcmp(id, "NA") == 0)
			continue;

		YouTubeResult* r = &op->results.items[count];
		strncpy(r->id, id, YT_MAX_ID - 1);
		r->id[YT_MAX_ID - 1] = '\0';
		strncpy(r->title, title, YT_MAX_TITLE - 1);
		r->title[YT_MAX_TITLE - 1] = '\0';
		strncpy(r->channel, channel, YT_MAX_CHANNEL - 1);
		r->channel[YT_MAX_CHANNEL - 1] = '\0';
		r->duration_sec = parse_duration(duration_str);

		count++;
	}

	fclose(f);
	unlink(temp_file);
	unlink(temp_err);

	return op->cancel ? -1 : count;
}

// Background thread: search YouTube via yt-dlp
static void* search_thread_func(void* arg) {
	YouTubeAsyncOp* op = (YouTubeAsyncOp*)arg;

	int count = search_via_ytdlp(op);

	if (op->cancel) {
		op->state = YT_OP_IDLE;
		return NULL;
	}

	if (count <= 0) {
		op->results.count = 0;
		if (op->error[0] == '\0') {
			snprintf(op->error, sizeof(op->error), "No results found");
		}
		op->state = YT_OP_ERROR;
	} else {
		op->results.count = count;
		op->state = YT_OP_DONE;
	}

	return NULL;
}

void YouTube_searchAsync(const char* query) {
	// Cancel any existing search
	YouTube_cancelSearch();

	memset(&search_op, 0, sizeof(search_op));
	strncpy(search_op.query, query, sizeof(search_op.query) - 1);
	search_op.state = YT_OP_RUNNING;

	pthread_create(&search_op.thread, NULL, search_thread_func, &search_op);
}

YouTubeAsyncOp* YouTube_getSearchOp(void) {
	return &search_op;
}

// Background thread: resolve stream URL via yt-dlp
static void* resolve_thread_func(void* arg) {
	YouTubeAsyncOp* op = (YouTubeAsyncOp*)arg;

	// yt-dlp: get best stream URL (720p max for device capability)
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 YTDLP_BIN " -g -f \"best[height<=720]/best\""
					   " --no-warnings --socket-timeout 15"
					   " \"https://www.youtube.com/watch?v=%s\" 2>/dev/null",
			 op->query); // query holds video ID

	FILE* pipe = popen(cmd, "r");
	if (!pipe) {
		snprintf(op->error, sizeof(op->error), "Failed to run yt-dlp");
		op->state = YT_OP_ERROR;
		return NULL;
	}

	char url[YT_MAX_URL];
	url[0] = '\0';

	if (fgets(url, sizeof(url), pipe)) {
		int len = strlen(url);
		if (len > 0 && url[len - 1] == '\n')
			url[len - 1] = '\0';
	}

	pclose(pipe);

	if (op->cancel) {
		op->state = YT_OP_IDLE;
		return NULL;
	}

	if (url[0] == '\0') {
		snprintf(op->error, sizeof(op->error), "Could not get stream URL");
		op->state = YT_OP_ERROR;
	} else {
		strncpy(op->resolved_url, url, YT_MAX_URL - 1);
		op->resolved_url[YT_MAX_URL - 1] = '\0';
		op->state = YT_OP_DONE;
	}

	return NULL;
}

void YouTube_resolveUrlAsync(const char* video_id) {
	YouTube_cancelResolve();

	memset(&resolve_op, 0, sizeof(resolve_op));
	strncpy(resolve_op.query, video_id, sizeof(resolve_op.query) - 1);
	resolve_op.state = YT_OP_RUNNING;

	pthread_create(&resolve_op.thread, NULL, resolve_thread_func, &resolve_op);
}

YouTubeAsyncOp* YouTube_getResolveOp(void) {
	return &resolve_op;
}

void YouTube_cancelSearch(void) {
	if (search_op.state == YT_OP_RUNNING) {
		search_op.cancel = true;
		// Kill yt-dlp to unblock system() in the search thread
		system("killall yt-dlp 2>/dev/null");
		pthread_join(search_op.thread, NULL);
		search_op.state = YT_OP_IDLE;
		search_op.cancel = false;
	}
}

void YouTube_cancelResolve(void) {
	if (resolve_op.state == YT_OP_RUNNING) {
		resolve_op.cancel = true;
		// Kill yt-dlp to unblock pclose() in the resolve thread
		system("killall yt-dlp 2>/dev/null");
		pthread_join(resolve_op.thread, NULL);
		resolve_op.state = YT_OP_IDLE;
		resolve_op.cancel = false;
	}
}

// Background thread: fetch channel info via yt-dlp JSON
static void* channel_info_thread_func(void* arg) {
	YouTubeChannelInfoOp* op = (YouTubeChannelInfoOp*)arg;

	// Step 1: Use yt-dlp to get video metadata JSON (includes channel info)
	const char* temp_file = "/tmp/yt_channel_info.json";
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 YTDLP_BIN " -j --no-warnings --socket-timeout 15"
					   " \"https://www.youtube.com/watch?v=%s\" > %s 2>/dev/null",
			 op->video_id, temp_file);

	LOG_info("yt-dlp channel info: %s\n", cmd);

	int ret = system(cmd);

	if (op->cancel) {
		unlink(temp_file);
		op->state = YT_OP_IDLE;
		return NULL;
	}

	if (ret != 0) {
		snprintf(op->error, sizeof(op->error), "Failed to fetch channel info");
		unlink(temp_file);
		op->state = YT_OP_ERROR;
		return NULL;
	}

	// Step 2: Read and parse JSON
	FILE* f = fopen(temp_file, "r");
	if (!f) {
		snprintf(op->error, sizeof(op->error), "Failed to read channel info");
		op->state = YT_OP_ERROR;
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 2 * 1024 * 1024) {
		fclose(f);
		unlink(temp_file);
		snprintf(op->error, sizeof(op->error), "Invalid channel info data");
		op->state = YT_OP_ERROR;
		return NULL;
	}

	char* buf = malloc(size + 1);
	if (!buf) {
		fclose(f);
		unlink(temp_file);
		snprintf(op->error, sizeof(op->error), "Out of memory");
		op->state = YT_OP_ERROR;
		return NULL;
	}

	fread(buf, 1, size, f);
	buf[size] = '\0';
	fclose(f);
	unlink(temp_file);

	if (op->cancel) {
		free(buf);
		op->state = YT_OP_IDLE;
		return NULL;
	}

	JSON_Value* root = json_parse_string(buf);
	free(buf);

	if (!root) {
		snprintf(op->error, sizeof(op->error), "Failed to parse channel info");
		op->state = YT_OP_ERROR;
		return NULL;
	}

	JSON_Object* obj = json_value_get_object(root);
	if (!obj) {
		json_value_free(root);
		snprintf(op->error, sizeof(op->error), "Invalid channel info format");
		op->state = YT_OP_ERROR;
		return NULL;
	}

	// Extract channel name
	const char* channel = json_object_get_string(obj, "channel");
	if (channel) {
		strncpy(op->info.name, channel, sizeof(op->info.name) - 1);
		op->info.name[sizeof(op->info.name) - 1] = '\0';
	}

	// Extract subscriber count
	double follower_count = json_object_get_number(obj, "channel_follower_count");
	if (follower_count > 0) {
		if (follower_count >= 1000000) {
			snprintf(op->info.subscriber_count, sizeof(op->info.subscriber_count),
					 "%.1fM subscribers", follower_count / 1000000.0);
		} else if (follower_count >= 1000) {
			snprintf(op->info.subscriber_count, sizeof(op->info.subscriber_count),
					 "%.1fK subscribers", follower_count / 1000.0);
		} else {
			snprintf(op->info.subscriber_count, sizeof(op->info.subscriber_count),
					 "%d subscribers", (int)follower_count);
		}
	} else {
		strncpy(op->info.subscriber_count, "Subscribers hidden", sizeof(op->info.subscriber_count) - 1);
	}

	// Extract channel URL for avatar fetch (copy before freeing JSON)
	char channel_url_buf[512] = {0};
	const char* channel_url_raw = json_object_get_string(obj, "channel_url");
	if (channel_url_raw) {
		strncpy(channel_url_buf, channel_url_raw, sizeof(channel_url_buf) - 1);
		strncpy(op->info.channel_url, channel_url_raw, sizeof(op->info.channel_url) - 1);
	}
	const char* ch_id_raw = json_object_get_string(obj, "channel_id");
	if (ch_id_raw) {
		strncpy(op->info.channel_id_str, ch_id_raw, sizeof(op->info.channel_id_str) - 1);
	}
	const char* channel_url = channel_url_buf;

	json_value_free(root);

	if (op->cancel) {
		op->state = YT_OP_IDLE;
		return NULL;
	}

	// Step 3: Try to download channel avatar via thumbnail URL
	// YouTube channel avatars follow a pattern: use uploader_id or channel_id
	const char* avatar_path = "/tmp/yt_channel_avatar.jpg";
	op->info.avatar_path[0] = '\0';

	if (channel_url && channel_url[0]) {
		LOG_info("Channel avatar: fetching page %s\n", channel_url);

		// Fetch channel page HTML and save to temp file for inspection
		char fetch_cmd[2048];
		snprintf(fetch_cmd, sizeof(fetch_cmd),
				 WGET_BIN " -qO- --no-check-certificate -T 10 \"%s\" 2>/dev/null"
						  " > /tmp/yt_channel_page.html",
				 channel_url);
		system(fetch_cmd);

		if (!op->cancel) {
			// Try multiple patterns to extract avatar URL
			// Pattern 1: og:image meta tag (most reliable)
			system("grep -o 'property=\"og:image\" content=\"[^\"]*\"' /tmp/yt_channel_page.html"
				   " | head -1 | sed 's/.*content=\"//;s/\"$//' > /tmp/yt_avatar_url.txt");

			// Check if we got a URL
			FILE* url_f = fopen("/tmp/yt_avatar_url.txt", "r");
			char avatar_url[1024] = {0};
			if (url_f) {
				if (fgets(avatar_url, sizeof(avatar_url), url_f)) {
					int len = strlen(avatar_url);
					if (len > 0 && avatar_url[len - 1] == '\n')
						avatar_url[len - 1] = '\0';
				}
				fclose(url_f);
			}

			// Pattern 2: thumbnailUrl in JSON (fallback)
			if (!avatar_url[0] && !op->cancel) {
				system("grep -o '\"thumbnailUrl\":\"[^\"]*\"' /tmp/yt_channel_page.html"
					   " | head -1 | sed 's/\"thumbnailUrl\":\"//;s/\"$//' > /tmp/yt_avatar_url.txt");
				url_f = fopen("/tmp/yt_avatar_url.txt", "r");
				if (url_f) {
					if (fgets(avatar_url, sizeof(avatar_url), url_f)) {
						int len = strlen(avatar_url);
						if (len > 0 && avatar_url[len - 1] == '\n')
							avatar_url[len - 1] = '\0';
					}
					fclose(url_f);
				}
			}

			LOG_info("Channel avatar URL: '%s'\n", avatar_url);

			if (avatar_url[0] && !op->cancel) {
				// Download the avatar image
				char dl_cmd[2048];
				snprintf(dl_cmd, sizeof(dl_cmd),
						 WGET_BIN " -q -T 10 --no-check-certificate -O '%s' '%s' 2>/dev/null",
						 avatar_path, avatar_url);
				system(dl_cmd);

				// Verify download
				struct stat st;
				if (stat(avatar_path, &st) == 0 && st.st_size > 0) {
					strncpy(op->info.avatar_path, avatar_path, sizeof(op->info.avatar_path) - 1);
					LOG_info("Channel avatar downloaded: %ld bytes\n", st.st_size);
				} else {
					LOG_info("Channel avatar download failed\n");
				}
			}

			unlink("/tmp/yt_avatar_url.txt");
			unlink("/tmp/yt_channel_page.html");
		}
	} else {
		LOG_info("Channel avatar: no channel_url available\n");
	}

	if (op->cancel) {
		op->state = YT_OP_IDLE;
		return NULL;
	}

	op->info.loaded = true;
	op->state = YT_OP_DONE;
	return NULL;
}

void YouTube_fetchChannelInfoAsync(const char* video_id) {
	YouTube_cancelChannelInfo();

	memset(&channel_info_op, 0, sizeof(channel_info_op));
	strncpy(channel_info_op.video_id, video_id, sizeof(channel_info_op.video_id) - 1);
	channel_info_op.state = YT_OP_RUNNING;

	pthread_create(&channel_info_op.thread, NULL, channel_info_thread_func, &channel_info_op);
}

YouTubeChannelInfoOp* YouTube_getChannelInfoOp(void) {
	return &channel_info_op;
}

void YouTube_cancelChannelInfo(void) {
	if (channel_info_op.state == YT_OP_RUNNING) {
		channel_info_op.cancel = true;
		system("killall yt-dlp 2>/dev/null");
		system("killall wget 2>/dev/null");
		pthread_join(channel_info_op.thread, NULL);
		channel_info_op.state = YT_OP_IDLE;
		channel_info_op.cancel = false;
	}
}

// Background thread: fetch channel uploads via yt-dlp
static void* uploads_thread_func(void* arg) {
	YouTubeUploadsOp* op = (YouTubeUploadsOp*)arg;

	// Sanitize URL
	char safe_url[1024];
	int j = 0;
	for (int i = 0; op->channel_url[i] && j < (int)sizeof(safe_url) - 2; i++) {
		char c = op->channel_url[i];
		if (c == '\'' || c == '`' || c == '$' || c == '\\' || c == ';' || c == '&' || c == '|')
			continue;
		safe_url[j++] = c;
	}
	safe_url[j] = '\0';

	const char* temp_file = "/tmp/yt_uploads.txt";
	const char* temp_err = "/tmp/yt_uploads_err.txt";

	char cmd[2048];
	if (safe_url[0]) {
		snprintf(cmd, sizeof(cmd),
				 YTDLP_BIN " '%s/videos'"
						   " --flat-playlist --no-warnings --socket-timeout 15"
						   " --playlist-end %d"
						   " --print '%%(id)s\t%%(title)s\t%%(channel)s\t%%(duration)s'"
						   " > %s 2> %s",
				 safe_url, YT_MAX_RESULTS, temp_file, temp_err);
	} else {
		snprintf(cmd, sizeof(cmd),
				 YTDLP_BIN " 'https://www.youtube.com/channel/%s/videos'"
						   " --flat-playlist --no-warnings --socket-timeout 15"
						   " --playlist-end %d"
						   " --print '%%(id)s\t%%(title)s\t%%(channel)s\t%%(duration)s'"
						   " > %s 2> %s",
				 op->channel_id, YT_MAX_RESULTS, temp_file, temp_err);
	}

	LOG_info("yt-dlp uploads: %s\n", cmd);
	int ret = system(cmd);
	(void)ret;

	if (op->cancel) {
		unlink(temp_file);
		unlink(temp_err);
		op->state = YT_OP_IDLE;
		return NULL;
	}

	FILE* f = fopen(temp_file, "r");
	if (!f) {
		snprintf(op->error, sizeof(op->error), "Failed to fetch uploads");
		unlink(temp_err);
		op->state = YT_OP_ERROR;
		return NULL;
	}

	char line[2048];
	int count = 0;
	while (fgets(line, sizeof(line), f) && count < YT_MAX_RESULTS) {
		if (op->cancel)
			break;
		int len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (len == 0)
			continue;

		char* id = line;
		char* title = strchr(id, '\t');
		if (!title)
			continue;
		*title++ = '\0';
		char* channel = strchr(title, '\t');
		if (!channel)
			continue;
		*channel++ = '\0';
		char* duration_str = strchr(channel, '\t');
		if (!duration_str)
			continue;
		*duration_str++ = '\0';

		if (id[0] == '\0' || strcmp(id, "NA") == 0)
			continue;

		YouTubeResult* r = &op->results.items[count];
		strncpy(r->id, id, YT_MAX_ID - 1);
		r->id[YT_MAX_ID - 1] = '\0';
		strncpy(r->title, title, YT_MAX_TITLE - 1);
		r->title[YT_MAX_TITLE - 1] = '\0';
		strncpy(r->channel, channel, YT_MAX_CHANNEL - 1);
		r->channel[YT_MAX_CHANNEL - 1] = '\0';
		r->duration_sec = parse_duration(duration_str);
		count++;
	}
	fclose(f);
	unlink(temp_file);
	unlink(temp_err);

	if (op->cancel) {
		op->state = YT_OP_IDLE;
		return NULL;
	}
	if (count <= 0) {
		op->results.count = 0;
		if (op->error[0] == '\0')
			snprintf(op->error, sizeof(op->error), "No videos found");
		op->state = YT_OP_ERROR;
	} else {
		op->results.count = count;
		op->state = YT_OP_DONE;
	}
	return NULL;
}

void YouTube_fetchUploadsAsync(const char* channel_url, const char* channel_id) {
	YouTube_cancelUploads();
	memset(&uploads_op, 0, sizeof(uploads_op));
	if (channel_url)
		strncpy(uploads_op.channel_url, channel_url, sizeof(uploads_op.channel_url) - 1);
	if (channel_id)
		strncpy(uploads_op.channel_id, channel_id, sizeof(uploads_op.channel_id) - 1);
	uploads_op.state = YT_OP_RUNNING;
	pthread_create(&uploads_op.thread, NULL, uploads_thread_func, &uploads_op);
}

YouTubeUploadsOp* YouTube_getUploadsOp(void) {
	return &uploads_op;
}

void YouTube_cancelUploads(void) {
	if (uploads_op.state == YT_OP_RUNNING) {
		uploads_op.cancel = true;
		system("killall yt-dlp 2>/dev/null");
		pthread_join(uploads_op.thread, NULL);
		uploads_op.state = YT_OP_IDLE;
		uploads_op.cancel = false;
	}
}

void YouTube_saveVideosCache(const char* channel_id, YouTubeSearchResults* results) {
	char dir[512], path[512];
	if (!Subscriptions_getChannelDir(channel_id, dir, sizeof(dir)))
		return;
	if (!Subscriptions_getVideosPath(channel_id, path, sizeof(path)))
		return;
	mkdir(APP_DATA_DIR, 0755);
	mkdir(APP_YOUTUBE_DIR, 0755);
	mkdir(dir, 0755);

	JSON_Value* root = json_value_init_array();
	JSON_Array* arr = json_value_get_array(root);
	for (int i = 0; i < results->count; i++) {
		JSON_Value* item = json_value_init_object();
		JSON_Object* obj = json_value_get_object(item);
		json_object_set_string(obj, "id", results->items[i].id);
		json_object_set_string(obj, "title", results->items[i].title);
		json_object_set_string(obj, "channel", results->items[i].channel);
		json_object_set_number(obj, "duration", results->items[i].duration_sec);
		json_array_append_value(arr, item);
	}
	json_serialize_to_file_pretty(root, path);
	json_value_free(root);
}

int YouTube_loadVideosCache(const char* channel_id, YouTubeSearchResults* results) {
	char path[512];
	if (!Subscriptions_getVideosPath(channel_id, path, sizeof(path)))
		return 0;
	memset(results, 0, sizeof(*results));

	FILE* fp = fopen(path, "r");
	if (!fp)
		return 0;
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size <= 0 || size > 512 * 1024) {
		fclose(fp);
		return 0;
	}

	char* buf = malloc(size + 1);
	if (!buf) {
		fclose(fp);
		return 0;
	}
	fread(buf, 1, size, fp);
	buf[size] = '\0';
	fclose(fp);

	JSON_Value* root = json_parse_string(buf);
	free(buf);
	if (!root)
		return 0;

	JSON_Array* arr = json_value_get_array(root);
	if (!arr) {
		json_value_free(root);
		return 0;
	}

	int count = (int)json_array_get_count(arr);
	if (count > YT_MAX_RESULTS)
		count = YT_MAX_RESULTS;

	for (int i = 0; i < count; i++) {
		JSON_Object* obj = json_array_get_object(arr, i);
		if (!obj)
			continue;
		YouTubeResult* r = &results->items[results->count];
		const char* id = json_object_get_string(obj, "id");
		const char* title = json_object_get_string(obj, "title");
		const char* channel = json_object_get_string(obj, "channel");
		if (!id || !id[0])
			continue;
		strncpy(r->id, id, YT_MAX_ID - 1);
		if (title)
			strncpy(r->title, title, YT_MAX_TITLE - 1);
		if (channel)
			strncpy(r->channel, channel, YT_MAX_CHANNEL - 1);
		r->duration_sec = (int)json_object_get_number(obj, "duration");
		results->count++;
	}
	json_value_free(root);
	return results->count;
}

// Background thread: download thumbnails for a specific channel
static void* channel_thumb_thread(void* arg) {
	YouTubeThumbDownloader* dl = (YouTubeThumbDownloader*)arg;
	char thumb_dir[512];
	if (!Subscriptions_getThumbDir(channel_thumb_dl.channel_id, thumb_dir, sizeof(thumb_dir))) {
		dl->running = false;
		return NULL;
	}
	char chan_dir[512];
	Subscriptions_getChannelDir(channel_thumb_dl.channel_id, chan_dir, sizeof(chan_dir));
	mkdir(APP_DATA_DIR, 0755);
	mkdir(APP_YOUTUBE_DIR, 0755);
	mkdir(chan_dir, 0755);
	mkdir(thumb_dir, 0755);

	for (int i = 0; i < dl->count; i++) {
		if (dl->cancel)
			break;
		char path[512];
		snprintf(path, sizeof(path), "%s/%s.jpg", thumb_dir, dl->ids[i]);
		struct stat st;
		if (stat(path, &st) == 0 && st.st_size > 0 && is_jpeg_complete(path)) {
			dl->downloaded_count = i + 1;
			continue;
		}
		char cmd[1024];
		const char* qualities[] = {"sddefault", "hqdefault", "mqdefault"};
		bool ok = false;
		for (int q = 0; q < 3 && !ok && !dl->cancel; q++) {
			snprintf(cmd, sizeof(cmd),
					 WGET_BIN " -q -T 10 --no-check-certificate -O '%s' "
							  "'https://i.ytimg.com/vi/%s/%s.jpg' 2>/dev/null",
					 path, dl->ids[i], qualities[q]);
			system(cmd);
			if (stat(path, &st) == 0 && st.st_size > 0 && is_jpeg_complete(path)) {
				ok = true;
			} else {
				unlink(path);
			}
		}
		dl->downloaded_count = i + 1;
	}
	dl->running = false;
	return NULL;
}

void YouTube_downloadChannelThumbnails(const char* channel_id, YouTubeSearchResults* results) {
	if (channel_thumb_dl.dl.running) {
		channel_thumb_dl.dl.cancel = true;
		pthread_detach(channel_thumb_dl.dl.thread);
		channel_thumb_dl.dl.running = false;
	}
	memset(&channel_thumb_dl, 0, sizeof(channel_thumb_dl));
	strncpy(channel_thumb_dl.channel_id, channel_id, SUBS_MAX_ID - 1);
	channel_thumb_dl.dl.count = results->count;
	for (int i = 0; i < results->count; i++) {
		strncpy(channel_thumb_dl.dl.ids[i], results->items[i].id, YT_MAX_ID - 1);
	}
	channel_thumb_dl.dl.running = true;
	pthread_create(&channel_thumb_dl.dl.thread, NULL, channel_thumb_thread, &channel_thumb_dl.dl);
}

bool YouTube_getChannelThumbnailPath(const char* channel_id, const char* video_id, char* path_out, int size) {
	char thumb_dir[512];
	if (!Subscriptions_getThumbDir(channel_id, thumb_dir, sizeof(thumb_dir)))
		return false;
	snprintf(path_out, size, "%s/%s.jpg", thumb_dir, video_id);
	struct stat st;
	if (stat(path_out, &st) != 0 || st.st_size == 0)
		return false;
	if (!is_jpeg_complete(path_out)) {
		unlink(path_out);
		return false;
	}
	return true;
}

bool YouTube_downloadAvatar(const char* channel_id, const char* avatar_url) {
	if (!channel_id || !channel_id[0] || !avatar_url || !avatar_url[0])
		return false;
	char dir[512], path[512];
	Subscriptions_getChannelDir(channel_id, dir, sizeof(dir));
	Subscriptions_getAvatarPath(channel_id, path, sizeof(path));
	mkdir(APP_DATA_DIR, 0755);
	mkdir(APP_YOUTUBE_DIR, 0755);
	mkdir(dir, 0755);
	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
			 WGET_BIN " -q -T 10 --no-check-certificate -O '%s' '%s' 2>/dev/null",
			 path, avatar_url);
	system(cmd);
	struct stat st;
	return (stat(path, &st) == 0 && st.st_size > 0);
}

void YouTube_cleanup(void) {
	YouTube_cancelSearch();
	YouTube_cancelResolve();
	YouTube_cancelChannelInfo();
	YouTube_cancelThumbnails();
	YouTube_cancelUploads();
	if (channel_thumb_dl.dl.running) {
		channel_thumb_dl.dl.cancel = true;
		pthread_detach(channel_thumb_dl.dl.thread);
		channel_thumb_dl.dl.running = false;
	}
}
