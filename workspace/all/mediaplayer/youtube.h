#ifndef __YOUTUBE_H__
#define __YOUTUBE_H__

#include <stdbool.h>
#include "subscriptions.h"
#include <pthread.h>

#define YT_MAX_RESULTS 30
#define YT_MAX_TITLE 256
#define YT_MAX_CHANNEL 128
#define YT_MAX_ID 16
#define YT_MAX_URL 2048

// A single YouTube search result
typedef struct {
	char id[YT_MAX_ID];
	char title[YT_MAX_TITLE];
	char channel[YT_MAX_CHANNEL];
	int duration_sec; // -1 if unknown
} YouTubeResult;

// Search results container
typedef struct {
	YouTubeResult items[YT_MAX_RESULTS];
	int count;
} YouTubeSearchResults;

// Async operation state
typedef enum {
	YT_OP_IDLE,
	YT_OP_RUNNING,
	YT_OP_DONE,
	YT_OP_ERROR
} YouTubeOpState;

// Async operation context (for search and URL resolve)
typedef struct {
	YouTubeOpState state;
	char error[256];
	volatile bool cancel;

	// Search results (populated after search completes)
	YouTubeSearchResults results;

	// Resolved stream URL (populated after resolve completes)
	char resolved_url[YT_MAX_URL];

	// Internal
	pthread_t thread;
	char query[512]; // Search query or video ID
} YouTubeAsyncOp;

// Thumbnail downloader state
typedef struct {
	volatile int downloaded_count; // Number of thumbnails downloaded so far
	volatile bool cancel;		   // Set to true to cancel downloads
	pthread_t thread;
	char ids[YT_MAX_RESULTS][YT_MAX_ID]; // Copy of video IDs
	int count;							 // Total number of videos
	bool running;						 // True while thread is active
} YouTubeThumbDownloader;

// Initialize YouTube module
void YouTube_init(void);

// Start async search (non-blocking, poll with YouTube_getSearchOp)
void YouTube_searchAsync(const char* query);

// Get search operation state
YouTubeAsyncOp* YouTube_getSearchOp(void);

// Start async URL resolution for a video ID (non-blocking)
void YouTube_resolveUrlAsync(const char* video_id);

// Get URL resolve operation state
YouTubeAsyncOp* YouTube_getResolveOp(void);

// Cancel any running operation
void YouTube_cancelSearch(void);
void YouTube_cancelResolve(void);

// Start background thumbnail downloads for search results
void YouTube_downloadThumbnails(YouTubeSearchResults* results);

// Cancel and join thumbnail download thread
void YouTube_cancelThumbnails(void);

// Get thumbnail downloader state (for polling progress)
YouTubeThumbDownloader* YouTube_getThumbDownloader(void);

// Build thumbnail file path for a video ID; returns true if file exists on disk
bool YouTube_getThumbnailPath(const char* video_id, char* path_out, int size);

// Retry downloading a single thumbnail in the background (non-blocking)
// Returns true if a retry was started or is already running for this ID
bool YouTube_retryThumbnail(const char* video_id);

// Channel info (fetched for channel details page)
typedef struct {
	char name[128];
	char subscriber_count[64];
	char video_count[64];
	char avatar_path[512];
	bool loaded;
	char channel_url[512];
	char channel_id_str[64];
} YouTubeChannelInfo;

// Async channel info operation
typedef struct {
	YouTubeOpState state;
	char error[256];
	volatile bool cancel;
	YouTubeChannelInfo info;
	pthread_t thread;
	char video_id[YT_MAX_ID];
} YouTubeChannelInfoOp;

// Async channel uploads fetch operation
typedef struct {
	YouTubeOpState state;
	char error[256];
	volatile bool cancel;
	YouTubeSearchResults results;
	pthread_t thread;
	char channel_url[SUBS_MAX_URL];
	char channel_id[SUBS_MAX_ID];
} YouTubeUploadsOp;

// Start async channel info fetch for a video ID
void YouTube_fetchChannelInfoAsync(const char* video_id);

// Get channel info operation state
YouTubeChannelInfoOp* YouTube_getChannelInfoOp(void);

// Cancel channel info fetch
void YouTube_cancelChannelInfo(void);

// Fetch latest uploads from a channel (by channel URL or ID)
void YouTube_fetchUploadsAsync(const char* channel_url, const char* channel_id);
YouTubeUploadsOp* YouTube_getUploadsOp(void);
void YouTube_cancelUploads(void);

// Save/load cached videos for a channel
void YouTube_saveVideosCache(const char* channel_id, YouTubeSearchResults* results);
int YouTube_loadVideosCache(const char* channel_id, YouTubeSearchResults* results);

// Per-channel thumbnail management
void YouTube_downloadChannelThumbnails(const char* channel_id, YouTubeSearchResults* results);
bool YouTube_getChannelThumbnailPath(const char* channel_id, const char* video_id, char* path_out, int size);

// Download channel avatar to per-channel directory
bool YouTube_downloadAvatar(const char* channel_id, const char* avatar_url);

// Cleanup
void YouTube_cleanup(void);

#endif
