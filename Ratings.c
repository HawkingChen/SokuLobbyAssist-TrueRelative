#include "Ratings.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define MAX_JSON (12u * 1024u * 1024u)
#define MAX_RECORDS 12000u
#define REFRESH_MS 86400000u

typedef struct { int start, end, next; char kind; } Token;
typedef struct {
    char name[129], played_as[129], handle[65];
    double rating, rd;
    unsigned games;
    char chars[3][32];
    double char_ratings[3];
    unsigned char_games[3];
    unsigned char_count;
} Record;
typedef struct { char profile[129], target[129]; } Manual;
typedef struct {
    char profile[129], handle[65];
    FILETIME checked;
    bool pending, in_flight, valid;
} OtherClaim;

static CRITICAL_SECTION ratings_lock;
static Record *records;
static unsigned record_count;
static Manual manual[256];
static unsigned manual_count;
static FILETIME fetched_at;
static char cache_path[MAX_PATH], map_path[MAX_PATH];
static char ca_path[MAX_PATH];
static char claim_cache_path[MAX_PATH];
static char claim_profile[129], claim_handle[65];
static FILETIME claim_checked;
static bool claim_pending;
static OtherClaim other_claims[256];
static unsigned other_claim_count;
static char other_claim_path[MAX_PATH];
static bool other_worker_running;
static volatile LONG started;
static DWORD last_fetch_error;

static uint64_t filetime_number(FILETIME t)
{
    return ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime;
}
static bool valid_utf8_string(char *dst, size_t cap, const char *src, const Token *t)
{
    if (!t || t->kind != '"') return false;
    size_t out = 0;
    for (int i = t->start; i < t->end; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\\') {
            if (++i >= t->end) return false;
            c = (unsigned char)src[i];
            if (c == 'u') {
                if (i + 4 >= t->end) return false;
                unsigned v = 0;
                for (int j = 0; j < 4; ++j) {
                    c = (unsigned char)src[++i];
                    v <<= 4;
                    if (c >= '0' && c <= '9') v += c - '0';
                    else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
                    else return false;
                }
                if (v < 0x80) { if (out + 1 >= cap) return false; dst[out++] = (char)v; }
                else if (v < 0x800) {
                    if (out + 2 >= cap) return false;
                    dst[out++] = (char)(0xc0 | (v >> 6)); dst[out++] = (char)(0x80 | (v & 63));
                } else {
                    if (out + 3 >= cap) return false;
                    dst[out++] = (char)(0xe0 | (v >> 12));
                    dst[out++] = (char)(0x80 | ((v >> 6) & 63)); dst[out++] = (char)(0x80 | (v & 63));
                }
                continue;
            }
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c != '"' && c != '\\' && c != '/') return false;
        }
        if (out + 1 >= cap) return false;
        dst[out++] = (char)c;
    }
    dst[out] = 0;
    return true;
}
static bool parse_tokens(const char *json, size_t length, Token **out, int *count)
{
    size_t cap = 4096;
    Token *tokens = malloc(cap * sizeof(*tokens));
    int *stack = malloc(128 * sizeof(*stack));
    if (!tokens || !stack) { free(tokens); free(stack); return false; }
    int n = 0, depth = 0;
    for (size_t i = 0; i < length;) {
        unsigned char c = (unsigned char)json[i];
        if (c <= ' ' || c == ',' || c == ':') { ++i; continue; }
        if (c == '}' || c == ']') {
            if (!depth || tokens[stack[depth - 1]].kind != (c == '}' ? '{' : '[')) goto fail;
            tokens[stack[--depth]].next = n;
            ++i; continue;
        }
        if ((size_t)n == cap) {
            if (cap >= 1048576) goto fail;
            cap *= 2;
            Token *larger = realloc(tokens, cap * sizeof(*tokens));
            if (!larger) goto fail;
            tokens = larger;
        }
        Token *t = &tokens[n];
        t->kind = (char)c;
        t->start = (int)i;
        t->next = n + 1;
        if (c == '{' || c == '[') {
            if (depth == 128) goto fail;
            stack[depth++] = n++;
            ++i; continue;
        }
        if (c == '"') {
            t->start = (int)++i;
            while (i < length) {
                if (json[i] == '\\') { i += 2; continue; }
                if (json[i] == '"') break;
                ++i;
            }
            if (i >= length) goto fail;
            t->end = (int)i++;
        } else {
            while (i < length && json[i] != ',' && json[i] != '}' && json[i] != ']' && json[i] > ' ') ++i;
            t->end = (int)i;
        }
        ++n;
    }
    if (depth || !n) goto fail;
    free(stack);
    *out = tokens; *count = n;
    return true;
fail:
    free(tokens); free(stack); return false;
}
static bool key_is(const char *json, const Token *t, const char *key)
{
    size_t length = strlen(key);
    return t->kind == '"' && t->end - t->start == (int)length &&
        !memcmp(json + t->start, key, length);
}
static int property(const char *json, const Token *tokens, int object, const char *key)
{
    if (object < 0 || tokens[object].kind != '{') return -1;
    for (int i = object + 1; i < tokens[object].next;) {
        int value = i + 1;
        if (value >= tokens[object].next) return -1;
        if (key_is(json, &tokens[i], key)) return value;
        i = tokens[value].next;
    }
    return -1;
}
static double number(const char *json, const Token *tokens, int index)
{
    if (index < 0) return 0;
    return strtod(json + tokens[index].start, NULL);
}
static void string_property(char *out, size_t cap, const char *json,
                            const Token *tokens, int object, const char *key)
{
    int index = property(json, tokens, object, key);
    if (index >= 0) valid_utf8_string(out, cap, json, &tokens[index]);
}
static bool parse_ratings(const char *json, size_t length, Record **result, unsigned *result_count)
{
    Token *tokens = NULL;
    int token_count = 0;
    if (!parse_tokens(json, length, &tokens, &token_count)) return false;
    int players = property(json, tokens, 0, "players");
    if (players < 0 || tokens[players].kind != '[') { free(tokens); return false; }
    Record *rows = calloc(MAX_RECORDS, sizeof(*rows));
    if (!rows) { free(tokens); return false; }
    unsigned n = 0;
    for (int i = players + 1; i < tokens[players].next; i = tokens[i].next) {
        if (tokens[i].kind != '{' || n >= MAX_RECORDS) goto invalid;
        Record *r = &rows[n];
        string_property(r->name, sizeof(r->name), json, tokens, i, "name");
        string_property(r->played_as, sizeof(r->played_as), json, tokens, i, "playedAs");
        string_property(r->handle, sizeof(r->handle), json, tokens, i, "handle");
        r->rating = number(json, tokens, property(json, tokens, i, "rating"));
        r->rd = number(json, tokens, property(json, tokens, i, "rd"));
        r->games = (unsigned)number(json, tokens, property(json, tokens, i, "games"));
        if (!r->name[0] || r->rating <= 0 || r->rd < 0) goto invalid;
        int chars = property(json, tokens, i, "characters");
        if (chars >= 0 && tokens[chars].kind == '[') {
            for (int j = chars + 1; j < tokens[chars].next; j = tokens[j].next) {
                if (tokens[j].kind != '{') continue;
                char character[32] = {0};
                string_property(character, sizeof(character), json, tokens, j, "character");
                if (!character[0]) continue;
                double score = number(json, tokens, property(json, tokens, j, "rating"));
                unsigned games = (unsigned)number(json, tokens, property(json, tokens, j, "games"));
                unsigned k = 0;
                while (k < r->char_count &&
                       (games < r->char_games[k] ||
                        (games == r->char_games[k] && score <= r->char_ratings[k]))) ++k;
                if (k >= 3) continue;
                if (r->char_count < 3) ++r->char_count;
                for (unsigned shift = r->char_count - 1; shift > k; --shift) {
                    strcpy(r->chars[shift], r->chars[shift - 1]);
                    r->char_ratings[shift] = r->char_ratings[shift - 1];
                    r->char_games[shift] = r->char_games[shift - 1];
                }
                strcpy(r->chars[k], character);
                r->char_ratings[k] = score;
                r->char_games[k] = games;
            }
        }
        ++n;
    }
    free(tokens);
    if (!n) { free(rows); return false; }
    *result = rows; *result_count = n;
    return true;
invalid:
    free(tokens); free(rows); return false;
}
static char *read_file(const char *path, size_t *length, FILETIME *written)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) || (long)(*length = (size_t)ftell(file)) <= 0 || *length > MAX_JSON ||
        fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char *json = malloc(*length + 1);
    if (!json) { fclose(file); return NULL; }
    bool ok = fread(json, 1, *length, file) == *length;
    fclose(file);
    if (!ok) { free(json); return NULL; }
    json[*length] = 0;
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (written && GetFileAttributesExA(path, GetFileExInfoStandard, &attrs)) *written = attrs.ftLastWriteTime;
    return json;
}
static bool write_cache(const char *json, size_t length)
{
    char temporary[MAX_PATH];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", cache_path) >= (int)sizeof(temporary)) return false;
    FILE *file = fopen(temporary, "wb");
    if (!file) return false;
    bool ok = fwrite(json, 1, length, file) == length;
    if (fclose(file)) ok = false;
    if (!ok) { DeleteFileA(temporary); return false; }
    return MoveFileExA(temporary, cache_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
// Serialize transfers because the bundled TLS build has shared RNG state.
static CRITICAL_SECTION download_lock;
static INIT_ONCE curl_once = INIT_ONCE_STATIC_INIT;
static CURLcode curl_init_result = CURLE_FAILED_INIT;
static BOOL CALLBACK initialize_curl(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once; (void)parameter; (void)context;
    InitializeCriticalSection(&download_lock);
    curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    return TRUE;
}
static void network_log(const char *message)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s", ca_path);
    char *slash = strrchr(path, '\\');
    if (!slash || (size_t)(slash - path) + 25 >= sizeof(path)) return;
    strcpy(slash + 1, "TrueRelative-network.log");
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now; GetLocalTime(&now);
    char line[1024];
    int size = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, message);
    DWORD written;
    if (size > 0 && size < (int)sizeof(line)) WriteFile(file, line, (DWORD)size, &written, NULL);
    CloseHandle(file);
}
typedef struct { char *data; size_t size; } Download;
static size_t receive_json(char *data, size_t size, size_t count, void *context)
{
    Download *download = context;
    if (size && count > MAX_JSON / size) return 0;
    size_t bytes = size * count;
    if (bytes > MAX_JSON - download->size) return 0;
    if (!bytes) return 0;
    char *next = realloc(download->data, download->size + bytes + 1);
    if (!next) return 0;
    download->data = next;
    memcpy(next + download->size, data, bytes);
    download->size += bytes;
    next[download->size] = 0;
    return bytes;
}
static char *fetch_url(const char *url, size_t *length)
{
    *length = 0;
    InitOnceExecuteOnce(&curl_once, initialize_curl, NULL, NULL);
    if (curl_init_result != CURLE_OK) {
        network_log("libcurl initialization failed"); return NULL;
    }
    EnterCriticalSection(&download_lock);
    CURL *curl = curl_easy_init();
    if (!curl) {
        network_log("libcurl handle allocation failed");
        LeaveCriticalSection(&download_lock);
        return NULL;
    }
    Download download = {0};
    char error[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TrueRelative/0.7.9");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_json);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    last_fetch_error = (DWORD)result;
    char message[640];
    snprintf(message, sizeof(message), "HTTPS %s: curl=%d HTTP=%ld bytes=%lu %s",
        strstr(url, "/api/ratings") ? "ratings" : "profile lookup", (int)result,
        status, (unsigned long)download.size, result == CURLE_OK ? "OK" :
        (error[0] ? error : curl_easy_strerror(result)));
    network_log(message);
    curl_easy_cleanup(curl);
    LeaveCriticalSection(&download_lock);
    if (result != CURLE_OK || !download.size) { free(download.data); return NULL; }
    *length = download.size;
    return download.data;
}
static char *fetch_curl(size_t *length)
{
    return fetch_url("https://soku.cleista.cc/api/ratings?min=1&status=all", length);
}
static bool parse_claim(const char *json, size_t length, const char *profile, char handle[65])
{
    Token *tokens = NULL;
    int count = 0;
    handle[0] = 0;
    if (!parse_tokens(json, length, &tokens, &count) || tokens[0].kind != '{') {
        free(tokens); return false;
    }
    int entry = property(json, tokens, 0, profile);
    bool valid = entry >= 0;
    if (valid && tokens[entry].kind == '{') {
        string_property(handle, 65, json, tokens, entry, "handle");
        size_t n = strlen(handle);
        if (n < 2 || n > 24) valid = false;
        for (size_t i = 0; valid && i < n; ++i)
            if (!((handle[i] >= 'a' && handle[i] <= 'z') ||
                  (handle[i] >= 'A' && handle[i] <= 'Z') ||
                  (handle[i] >= '0' && handle[i] <= '9') || handle[i] == '_')) valid = false;
    } else if (valid && tokens[entry].kind != 'n') valid = false;
    if (!valid) handle[0] = 0;
    free(tokens);
    return valid;
}
static void load_claim_cache(void)
{
    FILE *file = fopen(claim_cache_path, "rb");
    if (!file) return;
    char line[320];
    if (fgets(line, sizeof(line), file)) {
        char *first = strchr(line, '\t');
        char *second = first ? strchr(first + 1, '\t') : NULL;
        if (first && second) {
            *first++ = 0; *second++ = 0;
            unsigned long long stamp = strtoull(second, NULL, 10);
            FILETIME now; GetSystemTimeAsFileTime(&now);
            uint64_t age = filetime_number(now) - (uint64_t)stamp;
            if (strlen(line) < sizeof(claim_profile) && strlen(first) < sizeof(claim_handle) &&
                stamp && age < (uint64_t)REFRESH_MS * 10000) {
                strcpy(claim_profile, line);
                strcpy(claim_handle, first);
                claim_checked.dwLowDateTime = (DWORD)stamp;
                claim_checked.dwHighDateTime = (DWORD)(stamp >> 32);
            }
        }
    }
    fclose(file);
}
static void save_claim_cache(const char *profile, const char *handle, FILETIME checked)
{
    char temporary[MAX_PATH];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", claim_cache_path) >= (int)sizeof(temporary)) return;
    FILE *file = fopen(temporary, "wb");
    if (!file) return;
    bool ok = fprintf(file, "%s\t%s\t%llu\n", profile, handle,
        (unsigned long long)filetime_number(checked)) > 0;
    if (fclose(file)) ok = false;
    if (!ok || !MoveFileExA(temporary, claim_cache_path,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileA(temporary);
}
static bool claim_url(const char *profile, char url[512])
{
    strcpy(url, "https://soku.cleista.cc/api/names/profiles?name=");
    static const char hex[] = "0123456789ABCDEF";
    size_t offset = strlen(url);
    for (const unsigned char *p = (const unsigned char *)profile; *p; ++p) {
        unsigned char c = *p;
        if (offset + 4 >= 512) return false;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '~') url[offset++] = c;
        else { url[offset++] = '%'; url[offset++] = hex[c >> 4]; url[offset++] = hex[c & 15]; }
    }
    url[offset] = 0;
    return true;
}
static DWORD WINAPI claim_worker(void *argument)
{
    char *profile = argument;
    char url[512];
    char handle[65] = {0};
    bool valid = false;
    if (claim_url(profile, url)) {
        size_t length = 0;
        char *json = fetch_url(url, &length);
        if (json) { valid = parse_claim(json, length, profile, handle); free(json); }
    }
    FILETIME now; GetSystemTimeAsFileTime(&now);
    EnterCriticalSection(&ratings_lock);
    if (!strcmp(claim_profile, profile)) {
        strcpy(claim_handle, handle);
        claim_checked = now;
        if (!valid) {
            uint64_t retry = filetime_number(now) - (uint64_t)(REFRESH_MS - 300000u) * 10000;
            claim_checked.dwLowDateTime = (DWORD)retry;
            claim_checked.dwHighDateTime = (DWORD)(retry >> 32);
        }
        claim_pending = false;
    }
    LeaveCriticalSection(&ratings_lock);
    if (valid) save_claim_cache(profile, handle, now);
    free(profile);
    return 0;
}
bool ratings_own_claim(const char *profile_utf8, char handle[65])
{
    handle[0] = 0;
    if (!started || !profile_utf8 || !*profile_utf8 ||
        strlen(profile_utf8) >= sizeof(claim_profile) ||
        strpbrk(profile_utf8, "\t\r\n")) return false;
    EnterCriticalSection(&ratings_lock);
    if (strcmp(claim_profile, profile_utf8) && !claim_pending) {
        strcpy(claim_profile, profile_utf8);
        claim_handle[0] = 0;
        claim_checked.dwLowDateTime = claim_checked.dwHighDateTime = 0;
    }
    FILETIME now; GetSystemTimeAsFileTime(&now);
    uint64_t age = filetime_number(now) - filetime_number(claim_checked);
    if (!claim_pending && age >= (uint64_t)REFRESH_MS * 10000) {
        char *copy = malloc(strlen(claim_profile) + 1);
        if (copy) {
            strcpy(copy, claim_profile);
            claim_pending = true;
            HANDLE thread = CreateThread(NULL, 0, claim_worker, copy, 0, NULL);
            if (thread) CloseHandle(thread);
            else { claim_pending = false; free(copy); }
        }
    }
    bool found = !strcmp(claim_profile, profile_utf8) && claim_handle[0];
    if (found) strcpy(handle, claim_handle);
    LeaveCriticalSection(&ratings_lock);
    return found;
}
static void load_other_claims(void)
{
    FILE *file = fopen(other_claim_path, "rb");
    if (!file) return;
    char line[320];
    FILETIME now; GetSystemTimeAsFileTime(&now);
    while (fgets(line, sizeof(line), file) && other_claim_count < 256) {
        char *first = strchr(line, '\t');
        char *second = first ? strchr(first + 1, '\t') : NULL;
        if (!first || !second) continue;
        *first++ = 0; *second++ = 0;
        unsigned long long stamp = strtoull(second, NULL, 10);
        if (!line[0] || strpbrk(line, "\r\n") || strlen(line) >= sizeof(other_claims[0].profile) ||
            strlen(first) >= sizeof(other_claims[0].handle) || !stamp ||
            filetime_number(now) - (uint64_t)stamp >= (uint64_t)REFRESH_MS * 10000) continue;
        OtherClaim *entry = &other_claims[other_claim_count++];
        strcpy(entry->profile, line);
        strcpy(entry->handle, first);
        entry->checked.dwLowDateTime = (DWORD)stamp;
        entry->checked.dwHighDateTime = (DWORD)(stamp >> 32);
        entry->valid = true;
    }
    fclose(file);
}
static void save_other_claims(void)
{
    OtherClaim *copy = malloc(sizeof(other_claims));
    if (!copy) return;
    EnterCriticalSection(&ratings_lock);
    unsigned count = other_claim_count;
    memcpy(copy, other_claims, count * sizeof(*copy));
    LeaveCriticalSection(&ratings_lock);
    char temporary[MAX_PATH];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", other_claim_path) >= (int)sizeof(temporary)) {
        free(copy); return;
    }
    FILE *file = fopen(temporary, "wb");
    if (!file) { free(copy); return; }
    bool ok = true;
    for (unsigned i = 0; i < count; ++i) {
        if (!copy[i].valid) continue;
        if (fprintf(file, "%s\t%s\t%llu\n", copy[i].profile, copy[i].handle,
            (unsigned long long)filetime_number(copy[i].checked)) <= 0) ok = false;
    }
    if (fclose(file)) ok = false;
    if (!ok || !MoveFileExA(temporary, other_claim_path,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileA(temporary);
    free(copy);
}
static DWORD WINAPI other_claim_worker(void *unused)
{
    (void)unused;
    for (;;) {
        char profile[129] = {0};
        unsigned index = 0;
        EnterCriticalSection(&ratings_lock);
        for (; index < other_claim_count; ++index)
            if (other_claims[index].pending && !other_claims[index].in_flight) break;
        if (index == other_claim_count) {
            other_worker_running = false;
            LeaveCriticalSection(&ratings_lock);
            return 0;
        }
        OtherClaim *entry = &other_claims[index];
        strcpy(profile, entry->profile);
        entry->pending = false;
        entry->in_flight = true;
        LeaveCriticalSection(&ratings_lock);
        char url[512], handle[65] = {0};
        bool valid = false;
        if (claim_url(profile, url)) {
            size_t length = 0;
            char *json = fetch_url(url, &length);
            if (json) { valid = parse_claim(json, length, profile, handle); free(json); }
        }
        FILETIME now; GetSystemTimeAsFileTime(&now);
        EnterCriticalSection(&ratings_lock);
        entry = &other_claims[index];
        if (valid) {
            strcpy(entry->handle, handle);
            entry->checked = now;
            entry->valid = true;
        } else {
            uint64_t retry = filetime_number(now) - (uint64_t)(REFRESH_MS - 300000u) * 10000;
            entry->checked.dwLowDateTime = (DWORD)retry;
            entry->checked.dwHighDateTime = (DWORD)(retry >> 32);
        }
        entry->in_flight = false;
        LeaveCriticalSection(&ratings_lock);
        if (valid) save_other_claims();
        Sleep(500);
    }
}
bool ratings_other_claim(const char *profile_utf8, char handle[65])
{
    handle[0] = 0;
    if (!started || !profile_utf8 || !*profile_utf8 ||
        strlen(profile_utf8) >= sizeof(other_claims[0].profile) ||
        strpbrk(profile_utf8, "\t\r\n")) return false;
    EnterCriticalSection(&ratings_lock);
    if (!record_count) {
        LeaveCriticalSection(&ratings_lock);
        return false;
    }
    unsigned index = 0;
    for (; index < other_claim_count; ++index)
        if (!strcmp(other_claims[index].profile, profile_utf8)) break;
    if (index == other_claim_count) {
        if (other_claim_count < 256) ++other_claim_count;
        else {
            uint64_t oldest = UINT64_MAX;
            for (unsigned i = 0; i < other_claim_count; ++i) {
                uint64_t checked = filetime_number(other_claims[i].checked);
                if (!other_claims[i].in_flight && checked < oldest) {
                    oldest = checked; index = i;
                }
            }
        }
        if (index < other_claim_count) {
            memset(&other_claims[index], 0, sizeof(other_claims[index]));
            strcpy(other_claims[index].profile, profile_utf8);
        }
    }
    bool found = false;
    if (index < other_claim_count) {
        OtherClaim *entry = &other_claims[index];
        FILETIME now; GetSystemTimeAsFileTime(&now);
        uint64_t age = filetime_number(now) - filetime_number(entry->checked);
        if (age >= (uint64_t)REFRESH_MS * 10000 && !entry->in_flight) entry->pending = true;
        if (entry->valid && entry->handle[0]) { strcpy(handle, entry->handle); found = true; }
        if (entry->pending && !other_worker_running) {
            other_worker_running = true;
            HANDLE thread = CreateThread(NULL, 0, other_claim_worker, NULL, 0, NULL);
            if (thread) CloseHandle(thread);
            else other_worker_running = false;
        }
    }
    LeaveCriticalSection(&ratings_lock);
    return found;
}
static void publish(Record *rows, unsigned count, FILETIME time)
{
    EnterCriticalSection(&ratings_lock);
    Record *old = records;
    records = rows; record_count = count; fetched_at = time;
    LeaveCriticalSection(&ratings_lock);
    free(old);
}
static void load_manual(void)
{
    FILE *file = fopen(map_path, "rb");
    if (!file) return;
    char line[300];
    while (fgets(line, sizeof(line), file) && manual_count < 256) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab++ = 0;
        tab[strcspn(tab, "\r\n")] = 0;
        if (!line[0] || !tab[0] || strlen(line) >= sizeof(manual[0].profile) ||
            strlen(tab) >= sizeof(manual[0].target)) continue;
        strcpy(manual[manual_count].profile, line);
        strcpy(manual[manual_count].target, tab);
        ++manual_count;
    }
    fclose(file);
}
static DWORD WINAPI ratings_worker(void *unused)
{
    (void)unused;
    size_t length = 0;
    FILETIME written = {0};
    char *json = read_file(cache_path, &length, &written);
    if (json) {
        Record *rows = NULL; unsigned count = 0;
        if (parse_ratings(json, length, &rows, &count)) publish(rows, count, written);
        free(json);
    }
    for (;;) {
        FILETIME now; GetSystemTimeAsFileTime(&now);
        EnterCriticalSection(&ratings_lock);
        FILETIME last = fetched_at;
        unsigned count_now = record_count;
        LeaveCriticalSection(&ratings_lock);
        uint64_t age = filetime_number(now) - filetime_number(last);
        if (count_now && age < (uint64_t)REFRESH_MS * 10000) {
            Sleep((DWORD)((uint64_t)REFRESH_MS - age / 10000));
            continue;
        }
        bool refreshed = false;
        json = fetch_curl(&length);
        if (json) {
            Record *rows = NULL; unsigned count = 0;
            if (parse_ratings(json, length, &rows, &count)) {
                if (!write_cache(json, length)) network_log("ratings cache write failed; using downloaded data in memory");
                GetSystemTimeAsFileTime(&now);
                publish(rows, count, now);
                refreshed = true;
            } else network_log("ratings JSON parse failed; keeping previous cache");
            free(json);
        }
        Sleep(refreshed ? REFRESH_MS : 60000);
    }
}
void ratings_start(HMODULE plugin)
{
    if (InterlockedCompareExchange(&started, 1, 0)) return;
    InitializeCriticalSection(&ratings_lock);
    char path[MAX_PATH];
    wchar_t wide[MAX_PATH];
    if (!GetModuleFileNameW(plugin, wide, MAX_PATH)) return;
    wchar_t *slash = wcsrchr(wide, L'\\');
    if (!slash) return;
    slash[1] = 0;
    if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, path, sizeof(path), NULL, NULL)) return;
    snprintf(cache_path, sizeof(cache_path), "%sratings-cache.json", path);
    snprintf(claim_cache_path, sizeof(claim_cache_path), "%sown-claim-cache.tsv", path);
    snprintf(other_claim_path, sizeof(other_claim_path), "%sother-claim-cache.tsv", path);
    snprintf(map_path, sizeof(map_path), "%sratings-map.tsv", path);
    snprintf(ca_path, sizeof(ca_path), "%scurl-ca-bundle.crt", path);
    load_manual();
    load_claim_cache();
    load_other_claims();
    HANDLE thread = CreateThread(NULL, 0, ratings_worker, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}
static bool lookup(const char *profile_utf8, const char *public_name, bool handle_only, RatingMatch *out)
{
    memset(out, 0, sizeof(*out));
    if (!started || (!public_name && (!profile_utf8 || !*profile_utf8))) return false;
    EnterCriticalSection(&ratings_lock);
    const char *mapped = public_name;
    if (!mapped) for (unsigned i = 0; i < manual_count; ++i)
        if (!strcmp(profile_utf8, manual[i].profile)) { mapped = manual[i].target; break; }
    int found = -1, matches = 0;
    for (int pass = mapped ? 0 : 2; pass <= (mapped ? (handle_only ? 0 : 1) : 3); ++pass) {
        found = -1; matches = 0;
        for (unsigned i = 0; i < record_count; ++i) {
            bool yes = pass == 0 ? (records[i].handle[0] && !strcmp(records[i].handle, mapped)) :
                       pass == 1 ? !strcmp(records[i].name, mapped) :
                       pass == 2 ? (records[i].played_as[0] && !strcmp(records[i].played_as, profile_utf8)) :
                                   !strcmp(records[i].name, profile_utf8);
            if (yes) { found = (int)i; ++matches; }
        }
        if (matches) {
            if (matches == 1) { out->manual = pass < 2; out->same_name = pass == 3; }
            break;
        }
    }
    if (matches == 1) {
        const Record *r = &records[found];
        out->found = true; out->rating = r->rating; out->rd = r->rd; out->games = r->games;
        out->char_count = r->char_count;
        memcpy(out->chars, r->chars, sizeof(out->chars));
        memcpy(out->char_ratings, r->char_ratings, sizeof(out->char_ratings));
        out->fetched_at = fetched_at;
        FILETIME now; GetSystemTimeAsFileTime(&now);
        out->stale = filetime_number(now) - filetime_number(fetched_at) > (uint64_t)REFRESH_MS * 10000;
    }
    LeaveCriticalSection(&ratings_lock);
    return out->found;
}
bool ratings_lookup(const char *profile_utf8, RatingMatch *out)
{
    if (lookup(profile_utf8, NULL, false, out)) return true;
    if (!profile_utf8) return false;
    const char *start = profile_utf8;
    while (*start == ' ') ++start;
    size_t length = strlen(start);
    while (length && start[length - 1] == ' ') --length;
    if (!length || (start == profile_utf8 && length == strlen(profile_utf8)) || length >= 129)
        return false;
    char trimmed[129];
    memcpy(trimmed, start, length);
    trimmed[length] = 0;
    return lookup(trimmed, NULL, false, out);
}
bool ratings_lookup_public(const char *public_name, RatingMatch *out)
{
    if (!public_name || !*public_name) { memset(out, 0, sizeof(*out)); return false; }
    return lookup(NULL, public_name, false, out);
}
bool ratings_lookup_handle(const char *handle, RatingMatch *out)
{
    if (!handle || !*handle) { memset(out, 0, sizeof(*out)); return false; }
    return lookup(NULL, handle, true, out);
}
