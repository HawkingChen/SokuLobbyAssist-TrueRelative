#include "Ratings.c"
#include <assert.h>

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--claim")) {
        GetModuleFileNameA(NULL, ca_path, sizeof(ca_path));
        char *slash = strrchr(ca_path, '\\');
        assert(slash);
        slash[1] = 0;
        strcat(ca_path, "curl-ca-bundle.crt");
        strcpy(other_claim_path, ca_path);
        char *last = strrchr(other_claim_path, '\\');
        assert(last);
        strcpy(last + 1, "claim-test-cache.tsv");
        DeleteFileA(other_claim_path);
        InitializeCriticalSection(&ratings_lock);
        started = 1;
        char handle[65];
        assert(!ratings_other_claim("elder 5++", handle));
        assert(!other_claim_count);
        Record *ready = calloc(1, sizeof(*ready));
        assert(ready);
        FILETIME fetched; GetSystemTimeAsFileTime(&fetched);
        publish(ready, 1, fetched);
        assert(!ratings_other_claim("elder 5++", handle));
        for (unsigned i = 0; i < 120 && !ratings_other_claim("elder 5++", handle); ++i) Sleep(250);
        assert(!strcmp(handle, "elder"));
        FILE *cache = fopen(other_claim_path, "rb");
        assert(cache);
        fclose(cache);
        DeleteFileA(other_claim_path);
        puts("Windows queued claimed-name lookup and cache: OK");
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "--fetch") || !strcmp(argv[1], "--bad-ca"))) {
        GetModuleFileNameA(NULL, ca_path, sizeof(ca_path));
        char *slash = strrchr(ca_path, '\\');
        assert(slash);
        slash[1] = 0;
        strcat(ca_path, "curl-ca-bundle.crt");
        GetModuleFileNameA(NULL, cache_path, sizeof(cache_path));
        slash = strrchr(cache_path, '\\');
        assert(slash);
        slash[1] = 0;
        strcat(cache_path, "rating-test-cache.json");
        size_t length = 0;
        bool bad_ca = !strcmp(argv[1], "--bad-ca");
        if (bad_ca) strcat(ca_path, ".missing");
        char *json = fetch_curl(&length);
        if (bad_ca) {
            assert(!json && !length && last_fetch_error != CURLE_OK);
            puts("HTTPS rejects missing CA bundle: OK");
            return 0;
        }
        if (!json) { printf("curl failed: %lu\n", (unsigned long)last_fetch_error); return 2; }
        assert(length > 100000);
        Record *rows = NULL; unsigned count = 0;
        assert(parse_ratings(json, length, &rows, &count) && count > 6000);
        printf("Windows leaderboard fetch: OK (%u records)\n", count);
        assert(write_cache(json, length));
        size_t saved_length = 0;
        char *saved = read_file(cache_path, &saved_length, NULL);
        assert(saved && saved_length == length && !memcmp(saved, json, length));
        free(saved); DeleteFileA(cache_path);
        free(rows); free(json);
        return 0;
    }
    Download download = {0};
    assert(receive_json("{}", 1, 2, &download) == 2);
    assert(download.size == 2 && !strcmp(download.data, "{}"));
    assert(receive_json("x", MAX_JSON, 2, &download) == 0);
    download.size = MAX_JSON;
    assert(receive_json("x", 1, 1, &download) == 0);
    free(download.data);
    const char fixture[] = "{\"status\":{\"computedAt\":123},\"players\":["
        "{\"name\":\"Public A\",\"playedAs\":\"profile A\",\"handle\":\"handle-a\","
        "\"rating\":1510.5,\"rd\":68.2,\"games\":300,\"characters\":["
        "{\"character\":\"reimu\",\"rating\":1490,\"games\":20},"
        "{\"character\":\"marisa\",\"rating\":1600,\"games\":1},"
        "{\"character\":\"yuyuko\",\"rating\":1500,\"games\":5},"
        "{\"character\":\"alice\",\"rating\":1700,\"games\":1}]},"
        "{\"name\":\"duplicate\",\"playedAs\":null,\"handle\":null,\"rating\":1200,\"rd\":90,\"characters\":[]},"
        "{\"name\":\"duplicate\",\"playedAs\":null,\"handle\":null,\"rating\":1300,\"rd\":80,\"characters\":[]},"
        "{\"name\":\"名字\",\"playedAs\":null,\"handle\":null,\"rating\":1450,\"rd\":75,\"characters\":[]},"
        "{\"name\":\"handle-a\",\"playedAs\":null,\"handle\":\"other\",\"rating\":1100,\"rd\":99,\"characters\":[]}] }";
    InitializeCriticalSection(&ratings_lock);
    started = 1;
    Record *rows = NULL; unsigned count = 0;
    assert(parse_ratings(fixture, strlen(fixture), &rows, &count) && count == 5);
    char claim[65];
    const char claim_fixture[] = "{\"profile A\":{\"handle\":\"handle_a\",\"displayName\":\"Public A\",\"verified\":false}}";
    assert(parse_claim(claim_fixture, strlen(claim_fixture), "profile A", claim) &&
        !strcmp(claim, "handle_a"));
    const char no_claim[] = "{\"profile A\":null}";
    assert(parse_claim(no_claim, strlen(no_claim), "profile A", claim) && !claim[0]);
    FILETIME now; GetSystemTimeAsFileTime(&now);
    publish(rows, count, now);
    strcpy(other_claims[0].profile, "claimed alias");
    strcpy(other_claims[0].handle, "handle-a");
    other_claims[0].checked = now;
    other_claims[0].valid = true;
    other_claim_count = 1;
    assert(ratings_other_claim("claimed alias", claim) && !strcmp(claim, "handle-a"));
    char temp_dir[MAX_PATH];
    assert(GetTempPathA(sizeof(temp_dir), temp_dir));
    assert(GetTempFileNameA(temp_dir, "clm", 0, other_claim_path));
    save_other_claims();
    memset(other_claims, 0, sizeof(other_claims));
    other_claim_count = 0;
    load_other_claims();
    assert(other_claim_count == 1 && ratings_other_claim("claimed alias", claim) &&
        !strcmp(claim, "handle-a"));
    DeleteFileA(other_claim_path);
    other_claims[0].handle[0] = 0;
    assert(!ratings_other_claim("claimed alias", claim));
    RatingMatch match;
    assert(ratings_lookup("profile A", &match) && match.rating == 1510.5 &&
        match.games == 300 && !match.same_name && match.char_count == 3 &&
        !strcmp(match.chars[0], "reimu") && !strcmp(match.chars[1], "yuyuko") &&
        !strcmp(match.chars[2], "alice"));
    assert(ratings_lookup("profile A ", &match) && match.rating == 1510.5);
    assert(ratings_lookup_public("handle-a", &match) && match.rating == 1510.5);
    assert(ratings_lookup_handle("handle-a", &match) && match.rating == 1510.5);
    assert(!ratings_lookup_handle("Public A", &match));
    assert(ratings_lookup_public("Public A", &match) && match.rating == 1510.5);
    assert(!ratings_lookup_public("duplicate", &match));
    assert(!ratings_lookup("duplicate", &match));
    assert(ratings_lookup("名字", &match) && match.same_name);
    strcpy(manual[0].profile, "local"); strcpy(manual[0].target, "handle-a"); manual_count = 1;
    assert(ratings_lookup("local", &match) && match.manual && match.rating == 1510.5);
    strcpy(manual[1].profile, "local name"); strcpy(manual[1].target, "Public A"); manual_count = 2;
    assert(ratings_lookup("local name", &match) && match.manual && match.rating == 1510.5);
    EnterCriticalSection(&ratings_lock);
    uint64_t old = filetime_number(fetched_at) - (uint64_t)25 * 60 * 60 * 10000000;
    fetched_at.dwLowDateTime = (DWORD)old;
    fetched_at.dwHighDateTime = (DWORD)(old >> 32);
    LeaveCriticalSection(&ratings_lock);
    assert(ratings_lookup("profile A", &match) && match.stale);
    if (argc == 2) {
        size_t length = 0;
        char *json = read_file(argv[1], &length, NULL);
        assert(json);
        Record *real = NULL; unsigned real_count = 0;
        assert(parse_ratings(json, length, &real, &real_count) && real_count > 6000);
        printf("live leaderboard parser: OK (%u records)\n", real_count);
        free(real); free(json);
    }
    puts("rating lookup: OK (profile, unique same-name, duplicate rejection, manual handle)");
    return 0;
}
