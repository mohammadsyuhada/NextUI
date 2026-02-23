#ifndef __SUBSCRIPTIONS_H__
#define __SUBSCRIPTIONS_H__

#include <stdbool.h>
#include <time.h>

#define SUBS_MAX_CHANNELS 50
#define SUBS_MAX_NAME 128
#define SUBS_MAX_ID 64
#define SUBS_MAX_URL 512

// A bookmarked YouTube channel
typedef struct {
	char channel_id[SUBS_MAX_ID];
	char channel_name[SUBS_MAX_NAME];
	char channel_url[SUBS_MAX_URL]; // e.g. "https://www.youtube.com/@Handle"
	int video_count;				// Cached video count
	int seen_video_count;			// Video count when user last viewed channel
	time_t last_updated;			// Unix timestamp of last refresh
} SubscriptionChannel;

// All subscriptions
typedef struct {
	SubscriptionChannel channels[SUBS_MAX_CHANNELS];
	int count;
} SubscriptionList;

// Initialize (loads from disk)
void Subscriptions_init(void);

// Get the subscription list (read-only)
const SubscriptionList* Subscriptions_getList(void);

// Check if a channel is subscribed
bool Subscriptions_isSubscribed(const char* channel_name);

// Add a channel subscription (by name, since yt-dlp search doesn't always return channel IDs)
// Returns true if added, false if already exists or list full
bool Subscriptions_add(const char* channel_name);

// Remove a subscription by index
void Subscriptions_removeAt(int index);

// Remove a subscription by channel name; returns true if found and removed
bool Subscriptions_removeByName(const char* channel_name);

// Add a channel subscription with full metadata
bool Subscriptions_addFull(const char* channel_name, const char* channel_id, const char* channel_url);

// Update metadata for a channel (video_count, last_updated)
void Subscriptions_updateMeta(int index, int video_count, time_t last_updated);

// Mark a channel as seen (sets seen_video_count = count), clears "New" badge
void Subscriptions_markSeen(int index, int count);

// Get channel data directory path: APP_YOUTUBE_DIR "/<channel_id>"
bool Subscriptions_getChannelDir(const char* channel_id, char* path_out, int size);

// Get videos.json path for a channel
bool Subscriptions_getVideosPath(const char* channel_id, char* path_out, int size);

// Get avatar.jpg path for a channel
bool Subscriptions_getAvatarPath(const char* channel_id, char* path_out, int size);

// Get thumbnails directory path for a channel
bool Subscriptions_getThumbDir(const char* channel_id, char* path_out, int size);

// Delete channel data directory (called on unsubscribe)
void Subscriptions_deleteChannelData(const char* channel_id);

// Save to disk
void Subscriptions_save(void);

// Cleanup
void Subscriptions_cleanup(void);

#endif
