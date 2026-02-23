#ifndef __IPTV_CURATED_H__
#define __IPTV_CURATED_H__

#include "iptv.h"

typedef struct {
	char name[64];
	char code[8];
} CuratedTVCountry;

typedef struct {
	char name[IPTV_MAX_NAME];
	char url[IPTV_MAX_URL];
	char category[IPTV_MAX_GROUP];
	char logo[IPTV_MAX_LOGO];
	char decryption_key[IPTV_MAX_KEY];
	char country_code[8];
} CuratedTVChannel;

void IPTV_curated_init(void);
void IPTV_curated_cleanup(void);
int IPTV_curated_get_country_count(void);
const CuratedTVCountry* IPTV_curated_get_countries(void);
int IPTV_curated_get_channel_count(const char* country_code);
const CuratedTVChannel* IPTV_curated_get_channels(const char* country_code, int* count);

#endif
