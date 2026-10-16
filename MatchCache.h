#ifndef TRUE_RELATIVE_MATCH_CACHE_H
#define TRUE_RELATIVE_MATCH_CACHE_H
#include <stdbool.h>
#include <string.h>

/* Cache indices, never pointers into a leaderboard that can be replaced.
   Callers hold ratings_lock, and clear the cache on each published snapshot. */
typedef struct {
    char key[129];
    unsigned kind;
    int index, pass;
    bool valid;
} MatchCacheEntry;
typedef struct {
    MatchCacheEntry entries[64];
    unsigned next;
} MatchCache;

static inline bool match_cache_get(const MatchCache *cache, const char *key,
                                  unsigned kind, int *index, int *pass)
{
    for (unsigned i = 0; i < 64; ++i) {
        const MatchCacheEntry *entry = &cache->entries[i];
        if (entry->valid && entry->kind == kind && !strcmp(entry->key, key)) {
            *index = entry->index;
            *pass = entry->pass;
            return true;
        }
    }
    return false;
}
static inline void match_cache_put(MatchCache *cache, const char *key,
                                  unsigned kind, int index, int pass)
{
    if (strlen(key) >= sizeof(cache->entries[0].key)) return;
    MatchCacheEntry *entry = &cache->entries[cache->next];
    strcpy(entry->key, key);
    entry->kind = kind;
    entry->index = index;
    entry->pass = pass;
    entry->valid = true;
    cache->next = (cache->next + 1) % 64;
}
static inline void match_cache_clear(MatchCache *cache)
{
    memset(cache, 0, sizeof(*cache));
}
#endif
