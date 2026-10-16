#define COBJMACROS
#include "Ratings.h"
#include "LobbyPlayer.h"
#include "AutoTarget.h"
#ifdef TRUE_RELATIVE_VISUALS
#include "LobbyVisual.h"
#endif
#include <d3d9.h>
#include <xinput.h>
#include "PadShortcut.h"
#include <d3dx9core.h>
#include <wincrypt.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* Hisoutensoku 1.10a addresses and layouts from SokuDev/SokuLib. No C++ STL
   objects from SokuLobbies or Giuroll are touched across their ABI boundary. */
/* InLobbyMenu's MSVC RTTI vtable in verified 0.7.5.2 and 0.7.5.4 DLLs.
   The game MenuConnect vtable is only a router and is not the in-lobby menu. */
#define LOBBY_MENU_VTABLE_RVA 0x2129a4
typedef struct { int horizontalAxis, verticalAxis, a, b, c, d, changeCard, spellcard, pause, select; } GameInput;
#ifdef TRUE_RELATIVE_TEST_HARNESS
static IDirect3DDevice9 *harness_device;
static HWND harness_window;
#define GAME_DEVICE harness_device
#define GAME_WINDOW harness_window
static GameInput harness_input;
#define GAME_INPUT harness_input
#else
#define GAME_DEVICE (*(IDirect3DDevice9 **)0x8a0e30)
#define GAME_WINDOW (*(HWND *)0x89ff90)
#define GAME_INPUT (*(GameInput *)0x89a280)
#endif
typedef int (__thiscall *MenuMethod)(void *);
typedef void *(__thiscall *DestroyMethod)(void *, unsigned char);
typedef HRESULT (WINAPI *CreateFontFn)(IDirect3DDevice9 *, INT, UINT, UINT, UINT,
    BOOL, DWORD, DWORD, DWORD, DWORD, LPCWSTR, ID3DXFont **);

static MenuMethod original_process, original_render;
static DestroyMethod original_destroy;
static HMODULE lobby_module;
static void **lobby_menu_vtable;
static ID3DXFont *font;
static ID3DXFont *small_font;
static IDirect3DDevice9 *font_device;
static char log_path[MAX_PATH];
static bool installed;
static bool f8_was_down;
static bool f9_was_down, f10_was_down;
static bool pad_menu_was_down;
static bool settings_open, settings_dirty;
static unsigned settings_page, settings_row;
static DWORD settings_repeat_at;
static int settings_last_direction;
typedef enum { DISPLAY_AUTO, DISPLAY_MANUAL, DISPLAY_OFF } DisplayMode;
static DisplayMode display_mode = DISPLAY_AUTO;
static bool show_own_rating = true;
static bool show_relative_level;
static uint32_t selected_player_id = UINT32_MAX;
static AutoTarget auto_target = {UINT32_MAX, UINT32_MAX, 0};
typedef enum { POSITION_ABOVE, POSITION_CENTER, POSITION_RIGHT } CardPosition;
static CardPosition card_position = POSITION_ABOVE;
static int card_offset_x, card_offset_y;
static int own_x = 12, own_y = 12;
static char own_account[129];
static char overlay_config_path[MAX_PATH];
static char level_config_path[MAX_PATH];
typedef struct {
    int relative7plus, relative6, relative5, relative4, relative3;
    int newbie_rating_max, newbie_games_max;
} RelativeThresholds;
static RelativeThresholds relative_thresholds = {2800, 2500, 2300, 2000, 1800, 1400, 100};
/* SHA-256 allowlist for verified Chinese lobby DLL builds. */
static const BYTE supported_lobby_sha256[][32] = {
  {
    0xdb,0xce,0x5a,0x6a,0x59,0xe9,0x4e,0x2d,0x4c,0x43,0xbc,0xfb,0xd6,0x9f,0xce,0x35,
    0xad,0x98,0x04,0xa4,0xdd,0x20,0xe7,0x6a,0xa2,0x84,0x17,0xa3,0xc6,0xab,0x28,0xcd
  },
  {
    0x52,0x6d,0x84,0x37,0xc0,0x0a,0x85,0xcd,0xbe,0xb5,0x38,0x8e,0xa8,0xbf,0xc1,0x82,
    0x29,0xcb,0x64,0x92,0x78,0xf0,0x79,0xe0,0x00,0xe2,0x79,0x9d,0x5d,0xe5,0x0c,0x87
  }
};

static bool supported_lobby(HMODULE module)
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(module, path, sizeof(path))) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE digest[32], buffer[16384];
    DWORD length = sizeof(digest);
    bool ok = CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    while (ok) {
        size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (count && !CryptHashData(hash, buffer, (DWORD)count, 0)) ok = false;
        if (count < sizeof(buffer)) { if (ferror(file)) ok = false; break; }
    }
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) && length == sizeof(digest);
    bool supported = false;
    if (ok) for (unsigned i = 0; i < sizeof(supported_lobby_sha256) / sizeof(supported_lobby_sha256[0]); ++i)
        if (!memcmp(digest, supported_lobby_sha256[i], sizeof(digest))) supported = true;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    fclose(file);
    return ok && supported;
}

static bool verified_menu_vtable(void **vtable)
{
    static const uintptr_t versions[][4] = {
        {0x104f60, 0x1059b0, 0x119d80, 0x11bbe0}, /* 0.7.5.2 */
        {0x104f60, 0x1059b0, 0x119dc0, 0x11bc20}  /* 0.7.5.4 */
    };
    if (!vtable) return false;
    for (unsigned build = 0; build < sizeof(versions) / sizeof(versions[0]); ++build) {
        bool match = true;
        for (unsigned i = 0; i < 4; ++i)
            if ((uintptr_t)vtable[i] != (uintptr_t)lobby_module + versions[build][i]) match = false;
        if (match) return true;
    }
    return false;
}

static bool module_directory(HMODULE module, char out[MAX_PATH])
{
    wchar_t wide[MAX_PATH];
    if (!GetModuleFileNameW(module, wide, MAX_PATH)) return false;
    wchar_t *slash = wcsrchr(wide, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    return WideCharToMultiByte(CP_ACP, 0, wide, -1, out, MAX_PATH, NULL, NULL) > 0;
}

static void log_line(const char *message)
{
    FILE *file = fopen(log_path, "a");
    if (file) { fprintf(file, "%lu %s\n", (unsigned long)GetTickCount(), message); fclose(file); }
}
typedef struct { float x, y, z, rhw; D3DCOLOR color; } Vertex;
static void rect(IDirect3DDevice9 *device, float x, float y, float w, float h, D3DCOLOR color)
{
    Vertex v[4] = {{x-.5f,y-.5f,0,1,color},{x+w-.5f,y-.5f,0,1,color},
                   {x-.5f,y+h-.5f,0,1,color},{x+w-.5f,y+h-.5f,0,1,color}};
    IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
}
static bool ensure_font(IDirect3DDevice9 *device)
{
    if (font && small_font && device == font_device) return true;
    if (font) { ID3DXFont_Release(font); font = NULL; }
    if (small_font) { ID3DXFont_Release(small_font); small_font = NULL; }
    HMODULE d3dx = GetModuleHandleA("d3dx9_43.dll");
    if (!d3dx) d3dx = LoadLibraryA("d3dx9_43.dll");
    if (!d3dx) return false;
    CreateFontFn create_font = (CreateFontFn)(void *)GetProcAddress(d3dx, "D3DXCreateFontW");
    if (!create_font) return false;
    /* Recreate the font when the game changes its Direct3D device. */
    HRESULT result = create_font(device, 19, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei", &font);
    if (FAILED(result) || !font) return false;
    result = create_font(device, 12, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei", &small_font);
    if (FAILED(result) || !small_font) {
        ID3DXFont_Release(font); font = NULL;
        if (small_font) { ID3DXFont_Release(small_font); small_font = NULL; }
        return false;
    }
    font_device = device;
    return true;
}
static void release_font(void)
{
    if (font) { ID3DXFont_Release(font); font = NULL; }
    if (small_font) { ID3DXFont_Release(small_font); small_font = NULL; }
    font_device = NULL;
}
static bool readable(const void *pointer, size_t length)
{
    MEMORY_BASIC_INFORMATION info;
    if (!pointer || !VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)pointer + length <= (uintptr_t)info.BaseAddress + info.RegionSize;
}
static bool player_name(const unsigned char *player, char out[129])
{
    unsigned size = *(const unsigned *)(player + 0x14);
    unsigned capacity = *(const unsigned *)(player + 0x18);
    if (!size || size > 128 || size > capacity) return false;
    const char *source = capacity <= 15 ? (const char *)(player + 4) :
        *(const char *const *)(player + 4);
    if (!readable(source, size)) return false;
    memcpy(out, source, size); out[size] = 0;
    return true;
}
static bool same_rating_name(const char *first, const char *second)
{
    while (*first == ' ') ++first;
    while (*second == ' ') ++second;
    size_t first_len = strlen(first), second_len = strlen(second);
    while (first_len && first[first_len - 1] == ' ') --first_len;
    while (second_len && second[second_len - 1] == ' ') --second_len;
    return first_len == second_len && !memcmp(first, second, first_len);
}
static unsigned copy_players(void *menu, VisiblePlayer out[256])
{
    unsigned char *object = menu;
    const unsigned char *begin = *(const unsigned char **)(object + 0x220);
    const unsigned char *end = *(const unsigned char **)(object + 0x224);
    if (!begin || end < begin || (uintptr_t)end - (uintptr_t)begin > 256 * 0x64 ||
        ((uintptr_t)end - (uintptr_t)begin) % 0x64 || !readable(begin, (uintptr_t)end - (uintptr_t)begin)) return 0;
    unsigned n = 0;
    for (const unsigned char *p = begin; p < end; p += 0x64) {
        VisiblePlayer candidate;
        if (!player_name(p, candidate.name)) continue;
        candidate.id = *(const uint32_t *)p;
        candidate.x = *(const uint32_t *)(p + 0x3c);
        candidate.y = *(const uint32_t *)(p + 0x40);
        candidate.status = p[0x44];
        unsigned duplicate = 0;
        for (; duplicate < n; ++duplicate)
            if (out[duplicate].id == candidate.id) break;
        if (duplicate < n) {
            static volatile LONG duplicate_logged;
            if (!InterlockedCompareExchange(&duplicate_logged, 1, 0))
                log_line("duplicate lobby player ID merged before drawing");
            if (candidate.status == 1 || out[duplicate].status != 1) out[duplicate] = candidate;
        } else out[n++] = candidate;
    }
    return n;
}
static uint32_t local_player_id(const unsigned char *object)
{
    const unsigned char *connection = *(const unsigned char *const *)(object + 0x210);
    if (readable(connection, 0x13c)) {
        const unsigned char *me = *(const unsigned char *const *)(connection + 0x138);
        if (readable(me, sizeof(uint32_t))) return *(const uint32_t *)me;
    }
    return UINT32_MAX;
}
static unsigned nearby_players(void *menu, VisiblePlayer players[256], unsigned count, unsigned order[3])
{
    const unsigned char *object = menu;
    uint32_t camera_x = *(const uint32_t *)(object + 0x204);
    uint32_t camera_y = *(const uint32_t *)(object + 0x208);
    /* Measure from the local avatar so the nearest target follows walking even
       when the lobby camera is panned or stops following at map edges. */
    uint32_t local_id = local_player_id(object);
    for (unsigned i = 0; i < count; ++i) {
        if (players[i].id == local_id) {
            camera_x = players[i].x;
            camera_y = players[i].y;
            break;
        }
    }
    uint64_t best[3] = {UINT64_MAX, UINT64_MAX, UINT64_MAX};
    unsigned n = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (players[i].id == local_id) continue;
        int64_t dx = (int64_t)players[i].x - camera_x;
        int64_t dy = (int64_t)players[i].y - camera_y;
        uint64_t distance = (uint64_t)(dx * dx + dy * dy);
        if (distance > 450u * 450u) continue;
        for (unsigned j = 0; j < 3; ++j) {
            if (distance < best[j]) {
                for (unsigned k = 2; k > j; --k) { best[k] = best[k - 1]; order[k] = order[k - 1]; }
                best[j] = distance; order[j] = i;
                if (n < 3) ++n;
                break;
            }
        }
    }
    return n;
}
static void cycle_selected(const VisiblePlayer players[256], const unsigned order[3], unsigned nearby)
{
    unsigned next = 0;
    for (unsigned j = 0; j < nearby; ++j)
        if (players[order[j]].id == selected_player_id) { next = j + 1; break; }
    selected_player_id = next < nearby ? players[order[next]].id : UINT32_MAX;
}
static const wchar_t *character_name_zh(const char *key)
{
    static const struct { const char *key; const wchar_t *name; } names[] = {
        {"reimu", L"灵梦"}, {"marisa", L"魔理沙"}, {"sakuya", L"咲夜"},
        {"alice", L"爱丽丝"}, {"patchouli", L"帕秋莉"}, {"youmu", L"妖梦"},
        {"remilia", L"蕾米"}, {"yuyuko", L"幽幽子"}, {"yukari", L"紫"},
        {"suika", L"萃香"}, {"reisen", L"铃仙"}, {"aya", L"文"},
        {"komachi", L"小町"}, {"iku", L"衣玖"}, {"tenshi", L"天子"},
        {"sanae", L"早苗"}, {"cirno", L"琪露诺"}, {"meiling", L"美铃"},
        {"utsuho", L"空"}, {"suwako", L"诹访子"}
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(key, names[i].key)) return names[i].name;
    return NULL;
}
static void rating_text(int x, int y, int width, const wchar_t *message, D3DCOLOR color)
{
    static volatile LONG logged;
    wchar_t clipped[160];
    int pixels = 0;
    size_t length = wcslen(message), limit = 0;
    for (; limit < length && limit < 157; ++limit) {
        int glyph_width = message[limit] < 128 ? 10 : 19;
        if (pixels + glyph_width > width - 1) break;
        pixels += glyph_width;
    }
    if (limit < length) {
        while (limit && pixels + 19 > width - 1) {
            --limit;
            pixels -= message[limit] < 128 ? 10 : 19;
        }
        wmemcpy(clipped, message, limit);
        clipped[limit++] = L'…';
        clipped[limit] = 0;
        message = clipped;
    }
    RECT shadow = {x + 1, y + 1, x + width + 1, y + 23};
    RECT area = {x, y, x + width, y + 22};
    const DWORD flags = DT_LEFT | DT_NOCLIP;
    ID3DXFont_DrawTextW(font, NULL, message, -1, &shadow, flags, 0xff091018);
    int drawn = ID3DXFont_DrawTextW(font, NULL, message, -1, &area, flags, color);
    if (!InterlockedCompareExchange(&logged, 1, 0)) {
        char line[96];
        snprintf(line, sizeof(line), "first rating text draw returned %d", drawn);
        log_line(line);
    }
}
static void rating_small_text(int x, int y, int width, const wchar_t *message, D3DCOLOR color)
{
    if (!small_font || width <= 0) return;
    RECT shadow = {x + 1, y + 1, x + width + 1, y + 17};
    RECT area = {x, y, x + width, y + 16};
    const DWORD flags = DT_LEFT | DT_NOCLIP | DT_SINGLELINE;
    ID3DXFont_DrawTextW(small_font, NULL, message, -1, &shadow, flags, 0xff091018);
    ID3DXFont_DrawTextW(small_font, NULL, message, -1, &area, flags, color);
}
static int text_width(const wchar_t *message, bool small)
{
    int width = 0;
    for (const wchar_t *p = message; *p; ++p)
        width += small ? (*p < 128 ? 7 : 12) : (*p < 128 ? 10 : 19);
    return width;
}
static void rating_score_line(int x, int y, int width, double score, double rd,
                              const wchar_t *level, D3DCOLOR color)
{
    wchar_t score_text[32], rd_text[32], level_text[64];
    swprintf(score_text, 32, L"%.0f", score);
    swprintf(rd_text, 32, L" ±%.0f", rd);
    if (level) swprintf(level_text, 64, L" %ls", level);
    else level_text[0] = 0;
    int score_width = text_width(score_text, false);
    int rd_width = text_width(rd_text, true);
    int level_width = text_width(level_text, false);
    if (score_width + rd_width + level_width > width) {
        level_text[0] = 0;
        level_width = 0;
    }
    if (score_width > 0) rating_text(x, y, score_width + 1, score_text, color);
    if (rd_width > 0 && score_width + rd_width <= width)
        rating_small_text(x + score_width, y + 4, rd_width + 1, rd_text, 0xffc2cfdd);
    if (level_width > 0 && score_width + rd_width + level_width <= width)
        rating_text(x + score_width + rd_width, y, level_width + 1, level_text, color);
}
static D3DCOLOR rating_color(double score)
{
    if (score >= relative_thresholds.relative7plus) return 0xffff7088;
    if (score >= relative_thresholds.relative6) return 0xffe794f5;
    if (score >= relative_thresholds.relative5) return 0xffffd071;
    if (score >= relative_thresholds.relative4) return 0xff8bd7ff;
    if (score >= relative_thresholds.relative3) return 0xffa7e69b;
    return 0xffc5cfdd;
}
static D3DCOLOR total_rating_color(double score, unsigned games)
{
    if (score < relative_thresholds.relative3 &&
        score <= relative_thresholds.newbie_rating_max &&
        games <= (unsigned)relative_thresholds.newbie_games_max) return 0xffa4e0b6;
    return rating_color(score);
}
static int bounded(int value, int low, int high);
static const wchar_t *relative_level(double score, unsigned games)
{
    if (score >= relative_thresholds.relative7plus) return L"相对7+";
    if (score >= relative_thresholds.relative6) return L"相对6";
    if (score >= relative_thresholds.relative5) return L"相对5";
    if (score >= relative_thresholds.relative4) return L"相对4";
    if (score >= relative_thresholds.relative3) return L"相对3";
    if (score <= relative_thresholds.newbie_rating_max && games <= (unsigned)relative_thresholds.newbie_games_max)
        return L"相对1萌新";
    return L"相对<3";
}
static void load_relative_thresholds(const char *path)
{
    if (!path || !*path) return;
    relative_thresholds.relative7plus = bounded((int)GetPrivateProfileIntA("Thresholds", "relative_7_plus", 2800, path), 0, 10000);
    relative_thresholds.relative6 = bounded((int)GetPrivateProfileIntA("Thresholds", "relative_6", 2500, path), 0, 10000);
    relative_thresholds.relative5 = bounded((int)GetPrivateProfileIntA("Thresholds", "relative_5", 2300, path), 0, 10000);
    relative_thresholds.relative4 = bounded((int)GetPrivateProfileIntA("Thresholds", "relative_4", 2000, path), 0, 10000);
    relative_thresholds.relative3 = bounded((int)GetPrivateProfileIntA("Thresholds", "relative_3", 1800, path), 0, 10000);
    relative_thresholds.newbie_rating_max = bounded((int)GetPrivateProfileIntA("Newbie", "rating_max", 1400, path), 0, 10000);
    relative_thresholds.newbie_games_max = bounded((int)GetPrivateProfileIntA("Newbie", "games_max", 100, path), 0, 1000000);
    if (relative_thresholds.relative6 > relative_thresholds.relative7plus)
        relative_thresholds.relative6 = relative_thresholds.relative7plus;
    if (relative_thresholds.relative5 > relative_thresholds.relative6)
        relative_thresholds.relative5 = relative_thresholds.relative6;
    if (relative_thresholds.relative4 > relative_thresholds.relative5)
        relative_thresholds.relative4 = relative_thresholds.relative5;
    if (relative_thresholds.relative3 > relative_thresholds.relative4)
        relative_thresholds.relative3 = relative_thresholds.relative4;
}
static int bounded(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}
static void load_own_account(void)
{
    FILE *file = fopen(overlay_config_path, "rb");
    if (!file) return;
    bool own_section = false;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *value = line;
        while (*value == ' ' || *value == '\t') ++value;
        if (*value == '[') { own_section = !strncmp(value, "[OwnRating]", 11); continue; }
        if (!own_section || strncmp(value, "account=", 8)) continue;
        value += 8;
        value[strcspn(value, "\r\n")] = 0;
        size_t length = strlen(value);
        while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) value[--length] = 0;
        if (length < sizeof(own_account)) memcpy(own_account, value, length + 1);
        break;
    }
    fclose(file);
}
static void load_overlay_settings(HMODULE module)
{
    char position[24];
    char module_path[MAX_PATH];
    if (!module_directory(module, module_path)) return;
    snprintf(overlay_config_path, sizeof(overlay_config_path), "%sSokuLobbyAssist.ini", module_path);
    snprintf(level_config_path, sizeof(level_config_path), "%srelative-levels.txt", module_path);
    load_relative_thresholds(level_config_path);
    GetPrivateProfileStringA("Overlay", "position", "above", position, sizeof(position), overlay_config_path);
    if (!strcmp(position, "center")) card_position = POSITION_CENTER;
    else if (!strcmp(position, "right")) card_position = POSITION_RIGHT;
    card_offset_x = bounded((int)GetPrivateProfileIntA("Overlay", "offset_x", 0, overlay_config_path), -300, 300);
    card_offset_y = bounded((int)GetPrivateProfileIntA("Overlay", "offset_y", 0, overlay_config_path), -300, 300);
    own_x = bounded((int)GetPrivateProfileIntA("OwnRating", "x", 12, overlay_config_path), 4, 620);
    own_y = bounded((int)GetPrivateProfileIntA("OwnRating", "y", 12, overlay_config_path), 4, 460);
    char mode[24];
    GetPrivateProfileStringA("Display", "mode", "", mode, sizeof(mode), overlay_config_path);
    if (!strcmp(mode, "manual")) display_mode = DISPLAY_MANUAL;
    else if (!strcmp(mode, "off")) display_mode = DISPLAY_OFF;
    else if (!strcmp(mode, "auto")) display_mode = DISPLAY_AUTO;
    else display_mode = GetPrivateProfileIntA("Display", "automatic", 1, overlay_config_path) ? DISPLAY_AUTO : DISPLAY_MANUAL;
    show_own_rating = GetPrivateProfileIntA("Display", "own_rating", 1, overlay_config_path) != 0;
    show_relative_level = GetPrivateProfileIntA("Display", "relative_level", 0, overlay_config_path) != 0;
#ifdef TRUE_RELATIVE_VISUALS
    lobby_visual_configure(GetPrivateProfileIntA("Markers", "enabled", 0, overlay_config_path) != 0,
        bounded(GetPrivateProfileIntA("Markers", "lower_gap", 100, overlay_config_path), 1, 5000),
        bounded(GetPrivateProfileIntA("Markers", "higher_gap", 300, overlay_config_path), 1, 5000));
#endif
    load_own_account();
}
/* Replace just the requested INI value, retaining the user's comments and account mapping. */
static bool save_ini_value(const char *path, const char *section, const char *key, const char *value)
{
    char lines[16384];
    size_t size = 0;
    FILE *in = fopen(path, "rb");
    if (in) {
        size = fread(lines, 1, sizeof(lines) - 1, in);
        if (!feof(in)) { fclose(in); return false; }
        fclose(in);
    }
    lines[size] = 0;
    char output[17000];
    size_t used = 0;
    bool in_section = false, found_section = false, written = false;
    const char *cursor = lines;
    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor + 1) : strlen(cursor);
        if (*cursor == '[') {
            if (in_section && !written) {
                int n = snprintf(output + used, sizeof(output) - used, "%s=%s\r\n", key, value);
                if (n < 0 || (size_t)n >= sizeof(output) - used) return false;
                used += n; written = true;
            }
            in_section = !strncmp(cursor + 1, section, strlen(section)) && cursor[1 + strlen(section)] == ']';
            if (in_section) found_section = true;
        }
        bool match_key = in_section && !strncmp(cursor, key, strlen(key)) && cursor[strlen(key)] == '=';
        if (match_key) {
            int n = snprintf(output + used, sizeof(output) - used, "%s=%s\r\n", key, value);
            if (n < 0 || (size_t)n >= sizeof(output) - used) return false;
            used += n; written = true;
        } else {
            if (length >= sizeof(output) - used) return false;
            memcpy(output + used, cursor, length); used += length;
        }
        cursor += length;
    }
    if (!written) {
        if (used && output[used - 1] != '\n') output[used++] = '\n';
        int n = snprintf(output + used, sizeof(output) - used, "%s%s%s%s=%s\r\n",
            found_section ? "" : "\r\n[", found_section ? "" : section,
            found_section ? "" : "]\r\n", key, value);
        if (n < 0 || (size_t)n >= sizeof(output) - used) return false;
        used += n;
    }
    char temp[MAX_PATH];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return false;
    FILE *out = fopen(temp, "wb");
    if (!out) return false;
    bool ok = fwrite(output, 1, used, out) == used;
    if (fclose(out)) ok = false;
    if (!ok) { DeleteFileA(temp); return false; }
    if (!MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temp); return false;
    }
    return true;
}
static void save_settings(void)
{
    char value[32];
    bool ok = true;
#define SAVE_NUMBER(path, section, key, number) do { \
    snprintf(value, sizeof(value), "%d", (int)(number)); \
    if (!save_ini_value(path, section, key, value)) ok = false; \
} while (0)
    if (!save_ini_value(overlay_config_path, "Display", "mode",
        display_mode == DISPLAY_AUTO ? "auto" : display_mode == DISPLAY_MANUAL ? "manual" : "off")) ok = false;
    SAVE_NUMBER(overlay_config_path, "Display", "own_rating", show_own_rating);
    SAVE_NUMBER(overlay_config_path, "Display", "relative_level", show_relative_level);
    if (!save_ini_value(overlay_config_path, "Overlay", "position",
        card_position == POSITION_CENTER ? "center" : card_position == POSITION_RIGHT ? "right" : "above")) ok = false;
    SAVE_NUMBER(overlay_config_path, "Overlay", "offset_x", card_offset_x);
    SAVE_NUMBER(overlay_config_path, "Overlay", "offset_y", card_offset_y);
    SAVE_NUMBER(overlay_config_path, "OwnRating", "x", own_x);
    SAVE_NUMBER(overlay_config_path, "OwnRating", "y", own_y);
#ifdef TRUE_RELATIVE_VISUALS
    SAVE_NUMBER(overlay_config_path, "Markers", "enabled", lobby_visual_enabled());
    SAVE_NUMBER(overlay_config_path, "Markers", "lower_gap", lobby_visual_lower_gap());
    SAVE_NUMBER(overlay_config_path, "Markers", "higher_gap", lobby_visual_higher_gap());
#endif
    SAVE_NUMBER(level_config_path, "Thresholds", "relative_7_plus", relative_thresholds.relative7plus);
    SAVE_NUMBER(level_config_path, "Thresholds", "relative_6", relative_thresholds.relative6);
    SAVE_NUMBER(level_config_path, "Thresholds", "relative_5", relative_thresholds.relative5);
    SAVE_NUMBER(level_config_path, "Thresholds", "relative_4", relative_thresholds.relative4);
    SAVE_NUMBER(level_config_path, "Thresholds", "relative_3", relative_thresholds.relative3);
    SAVE_NUMBER(level_config_path, "Newbie", "rating_max", relative_thresholds.newbie_rating_max);
    SAVE_NUMBER(level_config_path, "Newbie", "games_max", relative_thresholds.newbie_games_max);
#undef SAVE_NUMBER
    log_line(ok ? "settings saved" : "settings save failed");
    if (ok) settings_dirty = false;
}
static void shape_state(IDirect3DDevice9 *device)
{
    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_SetTexture(device, 0, NULL);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE, 15);
}
static void rating_card(IDirect3DDevice9 *device, int x, int y, int width, int height, D3DCOLOR accent)
{
    shape_state(device);
    rect(device, x, y, width, height, 0xb8111b2a);
    rect(device, x, y, 3, height, accent);
    rect(device, x + 3, y, width - 3, 1, 0x9aa4c5d8);
}
static void rating_separator(IDirect3DDevice9 *device, int x, int y, int width)
{
    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_SetTexture(device, 0, NULL);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    rect(device, x, y, width, 1, 0x8a9aafc3);
}
typedef struct { int x, y, width, height; } CardRect;
static bool cards_overlap(CardRect a, CardRect b)
{
    const int gap = 6;
    return a.x < b.x + b.width + gap && b.x < a.x + a.width + gap &&
           a.y < b.y + b.height + gap && b.y < a.y + a.height + gap;
}
static bool place_card(int *x, int *y, int width, int height,
                       const CardRect placed[256], unsigned placed_count)
{
    const int max_x = 636 - width, max_y = 476 - height;
    int base_x = bounded(*x, 4, max_x), base_y = bounded(*y, 4, max_y);
    const int step_x = width + 10, step_y = height + 8;
    for (unsigned ring = 0; ring <= 5; ++ring) {
        int candidates[8][2] = {
            {0, -(int)ring * step_y}, {0, (int)ring * step_y},
            {-(int)ring * step_x, 0}, {(int)ring * step_x, 0},
            {-(int)ring * step_x, -(int)ring * step_y},
            {(int)ring * step_x, -(int)ring * step_y},
            {-(int)ring * step_x, (int)ring * step_y},
            {(int)ring * step_x, (int)ring * step_y}
        };
        for (unsigned c = 0; c < (ring ? 8u : 1u); ++c) {
            CardRect candidate = {
                bounded(base_x + candidates[c][0], 4, max_x),
                bounded(base_y + candidates[c][1], 4, max_y), width, height
            };
            bool collision = false;
            for (unsigned i = 0; i < placed_count; ++i)
                if (cards_overlap(candidate, placed[i])) { collision = true; break; }
            if (!collision) { *x = candidate.x; *y = candidate.y; return true; }
        }
    }
    return false;
}
static void wide_player_name(wchar_t out[129], const char *name)
{
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, out, 129) &&
        !MultiByteToWideChar(932, 0, name, -1, out, 129))
        wcscpy(out, L"玩家");
}
static bool match_player(const VisiblePlayer players[256], unsigned count, unsigned index,
                         uint32_t local_id, RatingMatch *match)
{
    bool own = players[index].id == local_id;
    unsigned duplicates = 0;
    for (unsigned j = 0; j < count; ++j)
        if (same_rating_name(players[index].name, players[j].name)) ++duplicates;
    if (own && own_account[0]) return ratings_lookup_public(own_account, match);
    if (own) {
        char handle[65];
        if (ratings_own_claim(players[index].name, handle))
            return ratings_lookup_handle(handle, match);
        return duplicates == 1 && ratings_lookup(players[index].name, match);
    }
    if (duplicates != 1) return false;
    if (ratings_lookup(players[index].name, match)) return true;
    char handle[65];
    return ratings_other_claim(players[index].name, handle) && ratings_lookup_handle(handle, match);
}
#ifdef TRUE_RELATIVE_VISUALS
static bool visual_rating(const VisiblePlayer players[256], unsigned count, unsigned index,
                          uint32_t local_id, double *rating)
{
    RatingMatch match;
    if (!match_player(players, count, index, local_id, &match)) return false;
    *rating = match.rating;
    return true;
}
#endif
static void draw_ratings(IDirect3DDevice9 *device, void *menu)
{
    if (!device || FAILED(IDirect3DDevice9_TestCooperativeLevel(device))) return;
    VisiblePlayer players[256];
    unsigned count = copy_players(menu, players);
    if (!count) { auto_target_reset(&auto_target); return; }
    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(IDirect3DDevice9_CreateStateBlock(device, D3DSBT_ALL, &saved))) return;
    if (!ensure_font(device)) { IDirect3DStateBlock9_Release(saved); return; }
    const unsigned char *object = menu;
    float zoom = *(const float *)(object + 0x20c);
    if (zoom < 0.99f || zoom > 1.01f) { auto_target_reset(&auto_target); goto done; }
    int translate_x = *(const int *)(object + 0x198);
    int translate_y = *(const int *)(object + 0x19c);
    uint32_t local_id = local_player_id(object);
#ifdef TRUE_RELATIVE_VISUALS
    lobby_visual_draw(device, menu, players, count, local_id, translate_x, translate_y, visual_rating);
#endif
    unsigned order[3] = {0};
    unsigned nearby = nearby_players(menu, players, count, order);
    int selected = -1;
    uint32_t nearby_ids[3];
    for (unsigned j = 0; j < nearby; ++j) nearby_ids[j] = players[order[j]].id;
    uint32_t detail_id;
    if (display_mode == DISPLAY_AUTO)
        detail_id = auto_target_update(&auto_target, nearby_ids, nearby, GetTickCount());
    else {
        auto_target_reset(&auto_target);
        detail_id = display_mode == DISPLAY_MANUAL ? selected_player_id : UINT32_MAX;
    }
    for (unsigned j = 0; j < nearby; ++j)
        if (players[order[j]].id == detail_id) selected = (int)order[j];
    CardRect placed[256];
    unsigned placed_count = 0;
    /* Draw only the selected nearby player and the optional local-player card. */
    for (unsigned draw_pass = 0; draw_pass < 2; ++draw_pass) for (unsigned i = 0; i < count; ++i) {
        bool own = players[i].id == local_id;
        bool pinned_own = own && show_own_rating;
        if ((int)i != selected && !pinned_own) continue;
        bool detailed = (int)i == selected || pinned_own;
        if (detailed != (draw_pass == 0)) continue;
        int avatar_x = (int)players[i].x + translate_x;
        int avatar_y = (int)players[i].y + translate_y;
        if (!pinned_own && (avatar_x < -16 || avatar_x > 656 || avatar_y < -16 || avatar_y > 520)) continue;
        RatingMatch match;
        bool found = match_player(players, count, i, local_id, &match);
        int width = detailed ? 206 : 180;
        SYSTEMTIME stamp = {0};
        bool show_stamp = false;
        if (found && match.stale) {
            FILETIME local_time;
            show_stamp = FileTimeToLocalFileTime(&match.fetched_at, &local_time) &&
                         FileTimeToSystemTime(&local_time, &stamp);
        }
        bool show_characters = detailed && found && match.char_count;
        int lines = 2 + show_stamp + (show_characters ? match.char_count + 1 : 0);
        int height = 13 + lines * 21;
        int x, y;
        if (pinned_own) { x = own_x; y = own_y; }
        else if (card_position == POSITION_CENTER) {
            x = avatar_x - width / 2 + card_offset_x;
            y = avatar_y - height / 2 + card_offset_y;
        } else if (card_position == POSITION_RIGHT) {
            x = avatar_x + 38 + card_offset_x;
            y = avatar_y - 70 + card_offset_y;
            if (x + width > 636) x = avatar_x - width - 38 + card_offset_x;
        } else {
            x = avatar_x - width / 2 + card_offset_x;
            y = avatar_y - height - 48 + card_offset_y;
        }
        x = bounded(x, 4, 636 - width);
        y = bounded(y, 4, 476 - height);
        if (!place_card(&x, &y, width, height, placed, placed_count)) continue;
        placed[placed_count++] = (CardRect){x, y, width, height};
        D3DCOLOR color = found ? total_rating_color(match.rating, match.games) : 0xff8e9cad;
        rating_card(device, x, y, width, height, color);
        /* D3DXFont expects the game's texture stages. The solid-card pass
           uses diffuse-only states, so restore them before drawing glyphs. */
        IDirect3DStateBlock9_Apply(saved);
        int text_x = x + 11, text_w = width - 19;
        int next_y = y + 6;
        wchar_t name[129];
        wide_player_name(name, players[i].name);
        rating_text(text_x, next_y, text_w, name, detailed ? 0xff9fdbff : 0xffd0e7f5);
        next_y += 21;
        wchar_t line[160];
        if (found) {
            rating_score_line(text_x, next_y, text_w, match.rating, match.rd,
                show_relative_level ? relative_level(match.rating, match.games) : NULL, color);
        } else rating_text(text_x, next_y, text_w, L"评分未知", 0xffb4c4d4);
        next_y += 21;
        if (show_stamp) {
            swprintf(line, 160, L"缓存 %04u-%02u-%02u %02u:%02u",
                stamp.wYear, stamp.wMonth, stamp.wDay, stamp.wHour, stamp.wMinute);
            rating_text(text_x, next_y, text_w, line, 0xffffca80);
            next_y += 21;
        }
        if (!detailed || !found) continue;
        if (show_characters) {
            rating_separator(device, x + 10, next_y + 1, width - 20);
            IDirect3DStateBlock9_Apply(saved);
            next_y += 10;
        }
        for (unsigned j = 0; j < match.char_count; ++j) {
            wchar_t character[32] = {0};
            const wchar_t *chinese = character_name_zh(match.chars[j]);
            if (!chinese && !MultiByteToWideChar(CP_UTF8, 0, match.chars[j], -1, character, 32)) continue;
            swprintf(line, 160, L"%ls  %.0f", chinese ? chinese : character, match.char_ratings[j]);
            rating_text(text_x, next_y, text_w, line, rating_color(match.char_ratings[j]));
            next_y += 21;
        }
    }
done:
    IDirect3DStateBlock9_Apply(saved);
    IDirect3DStateBlock9_Release(saved);
}
static void toggle_auto_off(void)
{
    auto_target_reset(&auto_target);
    display_mode = display_mode == DISPLAY_AUTO ? DISPLAY_OFF : DISPLAY_AUTO;
    selected_player_id = UINT32_MAX;
}
static const wchar_t *settings_label(unsigned page, unsigned row)
{
    static const wchar_t *display[] = {L"等级分显示", L"显示自己的信息", L"显示相对等级", L"评分框位置"};
    static const wchar_t *markers[] = {L"全体玩家标记", L"绿圈：低于自己多少分", L"红框：高于自己多少分"};
    static const wchar_t *levels[] = {L"相对7+ 最低分", L"相对6 最低分", L"相对5 最低分", L"相对4 最低分", L"相对3 最低分", L"萌新最高分", L"萌新最多局数"};
    return page == 0 ? display[row] : page == 1 ? markers[row] : levels[row];
}
static unsigned settings_rows(void) { return settings_page == 0 ? 4 : settings_page == 1 ? 3 : 7; }
static void settings_value(wchar_t value[32], unsigned page, unsigned row)
{
    if (page == 0) {
        switch (row) {
        case 0: wcscpy(value, display_mode == DISPLAY_AUTO ? L"自动" :
            display_mode == DISPLAY_MANUAL ? L"手动(F8)" : L"关闭"); return;
        case 1: wcscpy(value, show_own_rating ? L"开" : L"关"); return;
        case 2: wcscpy(value, show_relative_level ? L"开" : L"关"); return;
        default: wcscpy(value, card_position == POSITION_ABOVE ? L"头上" : card_position == POSITION_CENTER ? L"身上" : L"右侧"); return;
        }
    }
    if (page == 1) {
#ifdef TRUE_RELATIVE_VISUALS
        if (row == 0) wcscpy(value, lobby_visual_enabled() ? L"开" : L"关");
        else swprintf(value, 32, L"%d", row == 1 ? lobby_visual_lower_gap() : lobby_visual_higher_gap());
#else
        wcscpy(value, L"不可用");
#endif
        return;
    }
    int numbers[] = {relative_thresholds.relative7plus, relative_thresholds.relative6,
        relative_thresholds.relative5, relative_thresholds.relative4, relative_thresholds.relative3,
        relative_thresholds.newbie_rating_max, relative_thresholds.newbie_games_max};
    swprintf(value, 32, L"%d", numbers[row]);
}
static void settings_adjust(int direction)
{
    int step = direction * ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 10 : 1);
    if (settings_page == 0) {
        switch (settings_row) {
        case 0:
            auto_target_reset(&auto_target);
            display_mode = (DisplayMode)((display_mode + (direction > 0 ? 1 : 2)) % 3);
            selected_player_id = UINT32_MAX;
            break;
        case 1: show_own_rating = !show_own_rating; break;
        case 2: show_relative_level = !show_relative_level; break;
        case 3:
            card_position = (CardPosition)((card_position + (direction > 0 ? 1 : 2)) % 3);
            card_offset_x = card_offset_y = 0;
            break;
        }
    } else if (settings_page == 1) {
#ifdef TRUE_RELATIVE_VISUALS
        bool enabled = lobby_visual_enabled();
        int lower = lobby_visual_lower_gap(), higher = lobby_visual_higher_gap();
        if (settings_row == 0) enabled = !enabled;
        else if (settings_row == 1) lower = bounded(lower + direction * 10, 10, 5000);
        else higher = bounded(higher + direction * 10, 10, 5000);
        lobby_visual_configure(enabled, lower, higher);
#endif
    } else if (settings_page == 2) {
        int *numbers[] = {&relative_thresholds.relative7plus, &relative_thresholds.relative6,
            &relative_thresholds.relative5, &relative_thresholds.relative4, &relative_thresholds.relative3,
            &relative_thresholds.newbie_rating_max, &relative_thresholds.newbie_games_max};
        int low = settings_row < 4 ? *numbers[settings_row + 1] : settings_row == 4 ? 1 : 0;
        int high = settings_row > 0 && settings_row < 5 ? *numbers[settings_row - 1] :
            settings_row == 5 ? relative_thresholds.relative3 - 1 : 10000;
        if (settings_row == 6) high = 1000000;
        *numbers[settings_row] = bounded(*numbers[settings_row] + step * 10, low, high);
        if (settings_row == 4 && relative_thresholds.newbie_rating_max >= relative_thresholds.relative3)
            relative_thresholds.newbie_rating_max = relative_thresholds.relative3 - 1;
    }
    settings_dirty = true;
}
static void settings_close(void)
{
    settings_open = false;
    if (settings_dirty) save_settings();
}
/* Game action bindings are not physical Start/Back buttons. Steam Input's
   virtual XInput controller exposes these independently of profile bindings. */
static bool physical_pad_menu_combo(void)
{
    typedef DWORD (WINAPI *GetStateFn)(DWORD, XINPUT_STATE *);
    static GetStateFn get_state;
    static bool initialized;
    if (!initialized) {
        initialized = true;
        const char *libraries[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (unsigned i = 0; i < sizeof(libraries) / sizeof(libraries[0]); ++i) {
            HMODULE library = LoadLibraryA(libraries[i]);
            if (!library) continue;
            get_state = (GetStateFn)(void *)GetProcAddress(library, "XInputGetState");
            if (get_state) break;
            FreeLibrary(library);
        }
        log_line(get_state ? "controller shortcut: XInput available" :
                            "controller shortcut: XInput unavailable; game bindings only");
    }
    if (!get_state) return false;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state;
        if (get_state(i, &state) == ERROR_SUCCESS &&
            pad_shortcut_buttons(state.Gamepad.wButtons)) return true;
    }
    return false;
}
static void settings_input(GameInput input, bool foreground, bool chatting, bool f9_down,
                           bool physical_combo)
{
    bool pad_combo = foreground && (physical_combo || (input.pause > 0 && input.select > 0));
    bool pad_pressed = pad_shortcut_pressed(&pad_menu_was_down, pad_combo, foreground, chatting);
    bool open_trigger = (f9_down && !f9_was_down && !chatting) || pad_pressed;
    f9_was_down = f9_down;
    if (open_trigger) {
        if (settings_open) settings_close();
        else { settings_open = true; settings_page = 0; settings_row = 0; settings_last_direction = 0; }
        return;
    }
    if (!settings_open || !foreground) return;
    if (input.b == 1) { settings_close(); return; }
    if (input.changeCard == 1) {
        settings_page = (settings_page + 1) % 3;
        settings_row = 0;
    }
    int direction = 0;
    if (input.verticalAxis < 0) direction = -2;
    else if (input.verticalAxis > 0) direction = 2;
    else if (input.horizontalAxis < 0) direction = -1;
    else if (input.horizontalAxis > 0) direction = 1;
    if (!direction && input.a == 1) direction = 1;
    DWORD now = GetTickCount();
    if (!direction) { settings_last_direction = 0; return; }
    if (direction != settings_last_direction || now >= settings_repeat_at) {
        if (direction == -2) settings_row = (settings_row + settings_rows() - 1) % settings_rows();
        else if (direction == 2) settings_row = (settings_row + 1) % settings_rows();
        else settings_adjust(direction);
        settings_repeat_at = now + (direction == settings_last_direction ? 75 : 280);
    }
    settings_last_direction = direction;
}
static void draw_settings(IDirect3DDevice9 *device)
{
    if (!settings_open || !device || FAILED(IDirect3DDevice9_TestCooperativeLevel(device))) return;
    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(IDirect3DDevice9_CreateStateBlock(device, D3DSBT_ALL, &saved))) return;
    if (!ensure_font(device)) { IDirect3DStateBlock9_Release(saved); return; }
    const int x = 105, y = 57, width = 430;
    shape_state(device);
    rect(device, x, y, width, 358, 0xea101722);
    rect(device, x, y, 4, 358, 0xff72cae8);
    rect(device, x, y, width, 2, 0xff507f9c);
    rect(device, x + 12, y + 68 + (int)settings_row * 30, width - 24, 27, 0x8065a2c5);
    IDirect3DStateBlock9_Apply(saved);
    const wchar_t *titles[] = {L"显示", L"玩家标记", L"相对阈值"};
    wchar_t title[80];
    swprintf(title, 80, L"真实相对  ·  %ls  (%u/3)", titles[settings_page], settings_page + 1);
    rating_text(x + 18, y + 13, width - 36, title, 0xffd7f3ff);
    rating_small_text(x + 18, y + 42, width - 36, L"F9 / Start+Select 关闭     换卡键 下一页", 0xffabc3d4);
    for (unsigned i = 0; i < settings_rows(); ++i) {
        wchar_t value[32];
        settings_value(value, settings_page, i);
        D3DCOLOR color = i == settings_row ? 0xffffffff : 0xffc5d9e6;
        rating_text(x + 22, y + 72 + i * 30, 300, settings_label(settings_page, i), color);
        rating_text(x + 323, y + 72 + i * 30, 86, value, i == settings_row ? 0xffffdc8b : color);
    }
    if (settings_page == 0) {
        rating_small_text(x + 22, y + 250, width - 44, L"F8：手动模式下切换附近玩家", 0xffabc3d4);
        rating_small_text(x + 22, y + 272, width - 44, L"F10：等级分自动显示 / 关闭", 0xffabc3d4);
    }
    rating_small_text(x + 18, y + 334, width - 36, L"方向键调整  ·  确认键切换  ·  取消键保存退出", 0xffabc3d4);
    IDirect3DStateBlock9_Apply(saved);
    IDirect3DStateBlock9_Release(saved);
}
static int __fastcall process_hook(void *menu, void *unused)
{
    (void)unused;
    bool foreground = GetForegroundWindow() == GAME_WINDOW;
    bool f8_down = foreground && (GetAsyncKeyState(VK_F8) & 0x8000);
    bool f9_down = foreground && (GetAsyncKeyState(VK_F9) & 0x8000);
    bool f10_down = foreground && (GetAsyncKeyState(VK_F10) & 0x8000);
    bool chatting = ((unsigned char *)menu)[0x1154] != 0;
    GameInput saved_input = GAME_INPUT;
    bool was_settings_open = settings_open;
    settings_input(saved_input, foreground, chatting, f9_down,
        foreground && physical_pad_menu_combo());
    if (f8_down && !f8_was_down && !chatting && !settings_open && display_mode == DISPLAY_MANUAL) {
        VisiblePlayer players[256];
        unsigned order[3];
        unsigned count = copy_players(menu, players);
        unsigned nearby = nearby_players(menu, players, count, order);
        cycle_selected(players, order, nearby);
    }
    f8_was_down = f8_down;
    if (f10_down && !f10_was_down && !chatting && !settings_open)
        toggle_auto_off();
    f10_was_down = f10_down;
    if (settings_open || was_settings_open) memset(&GAME_INPUT, 0, sizeof(GameInput));
    int result = original_process(menu);
    if (settings_open || was_settings_open) GAME_INPUT = saved_input;
    if (!result) { selected_player_id = UINT32_MAX; auto_target_reset(&auto_target); }
    return result;
}
static int __fastcall render_hook(void *menu, void *unused)
{
    (void)unused;
    int result = original_render(menu);
    draw_ratings(GAME_DEVICE, menu);
    draw_settings(GAME_DEVICE);
    return result;
}
static void *__fastcall destroy_hook(void *menu, void *unused, unsigned char flag)
{
    (void)unused;
    auto_target_reset(&auto_target);
    selected_player_id = UINT32_MAX;
    if (settings_open) settings_close();
#ifdef TRUE_RELATIVE_VISUALS
    lobby_visual_reset();
#endif
    release_font();
    return original_destroy(menu, flag);
}
static bool replace_pointer(void **slot, void *value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, value);
    VirtualProtect(slot, sizeof(void *), old, &old);
    return true;
}

__declspec(dllexport) bool CheckVersion(const BYTE hash[16])
{
    const BYTE supported[16] = {0xdf,0x35,0xd1,0xfb,0xc7,0xb5,0x83,0x31,0x7a,0xda,0xbe,0x8c,0xd9,0xf5,0x3b,0x2e};
    return hash && !memcmp(hash, supported, 16);
}
__declspec(dllexport) int getPriority(void) { return -60000; }
__declspec(dllexport) bool Initialize(HMODULE module, HMODULE parent)
{
    (void)parent;
    if (installed) return true;
    if (!module_directory(module, log_path)) return false;
    strcat(log_path, "TrueRelative.log");
    lobby_module = GetModuleHandleA("SokuLobbiesMod.dll");
    if (!lobby_module) { log_line("inactive: SokuLobbiesMod is not loaded"); return true; }
    if (!supported_lobby(lobby_module)) {
        log_line("inactive: lobby DLL is not a verified Chinese 0.7.5.2 or 0.7.5.4 build"); return true;
    }
    lobby_menu_vtable = (void **)((unsigned char *)lobby_module + LOBBY_MENU_VTABLE_RVA);
    if (!verified_menu_vtable(lobby_menu_vtable)) {
        log_line("inactive: verified DLL has an unexpected InLobbyMenu vtable"); return true;
    }
    original_process = (MenuMethod)lobby_menu_vtable[2];
    original_render = (MenuMethod)lobby_menu_vtable[3];
    original_destroy = (DestroyMethod)lobby_menu_vtable[0];
    if (!replace_pointer(&lobby_menu_vtable[2], process_hook) ||
        !replace_pointer(&lobby_menu_vtable[3], render_hook) ||
        !replace_pointer(&lobby_menu_vtable[0], destroy_hook)) {
        replace_pointer(&lobby_menu_vtable[2], original_process);
        replace_pointer(&lobby_menu_vtable[3], original_render);
        replace_pointer(&lobby_menu_vtable[0], original_destroy);
        return false;
    }
    installed = true;
    load_overlay_settings(module);
    ratings_start(module);
    log_line("installed: compatible with Chinese lobby 0.7.5.2/0.7.5.4");
    return true;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
