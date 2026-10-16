#include "MatchCache.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    MatchCache cache = {0};
    int index, pass;
    assert(!match_cache_get(&cache, "player", 0, &index, &pass));
    match_cache_put(&cache, "player", 0, 8, 3);
    assert(match_cache_get(&cache, "player", 0, &index, &pass) && index == 8 && pass == 3);
    assert(!match_cache_get(&cache, "player", 1, &index, &pass));
    match_cache_put(&cache, "player", 1, -1, 0);
    assert(match_cache_get(&cache, "player", 1, &index, &pass) && index == -1);
    match_cache_clear(&cache);
    assert(!match_cache_get(&cache, "player", 0, &index, &pass));
    for (unsigned i = 0; i < 65; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "player%u", i);
        match_cache_put(&cache, name, 0, (int)i, 2);
    }
    assert(!match_cache_get(&cache, "player0", 0, &index, &pass));
    assert(match_cache_get(&cache, "player64", 0, &index, &pass) && index == 64);
    char oversized[130];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    match_cache_put(&cache, oversized, 0, 99, 2);
    assert(!match_cache_get(&cache, oversized, 0, &index, &pass));
    puts("Match cache: modes, misses, invalidation, capacity and long names passed");
    return 0;
}
