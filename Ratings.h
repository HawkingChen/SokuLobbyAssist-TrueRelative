#ifndef SOKU_RATINGS_H
#define SOKU_RATINGS_H
#include <windows.h>
#include <stdbool.h>

typedef struct {
    bool found, manual, same_name, stale;
    double rating, rd;
    unsigned games;
    char chars[3][32];
    double char_ratings[3];
    unsigned char_count;
    FILETIME fetched_at;
} RatingMatch;

void ratings_start(HMODULE plugin);
bool ratings_lookup(const char *profile_utf8, RatingMatch *out);
bool ratings_lookup_public(const char *public_name, RatingMatch *out);
bool ratings_own_claim(const char *profile_utf8, char handle[65]);
bool ratings_other_claim(const char *profile_utf8, char handle[65]);

#endif
