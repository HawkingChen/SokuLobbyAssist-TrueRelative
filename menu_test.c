#define TRUE_RELATIVE_TEST_HARNESS
#include "TrueRelative.c"
#include <assert.h>
static int __thiscall assert_muted_input(void *menu)
{
    (void)menu;
    GameInput empty = {0};
    assert(!memcmp(&GAME_INPUT, &empty, sizeof(empty)));
    return 1;
}
int main(int argc, char **argv)
{
    assert(same_rating_name("Takagi ", "Takagi"));
    assert(!same_rating_name("Takagi ", "Takagi2"));
    assert(!show_relative_level);
    settings_page = 0;
    settings_row = 2;
    settings_adjust(1);
    assert(show_relative_level);
    settings_adjust(1);
    assert(!show_relative_level);
    assert(rating_color(2800) == 0xffff7088);
    assert(rating_color(2799) == 0xffe794f5);
    assert(rating_color(2500) == 0xffe794f5);
    assert(rating_color(2499) == 0xffffd071);
    assert(rating_color(2300) == 0xffffd071);
    assert(rating_color(2299) == 0xff8bd7ff);
    assert(rating_color(2000) == 0xff8bd7ff);
    assert(rating_color(1999) == 0xffa7e69b);
    assert(rating_color(1800) == 0xffa7e69b);
    assert(rating_color(1799) == 0xffc5cfdd);
    assert(total_rating_color(1400, 100) == 0xffa4e0b6);
    assert(total_rating_color(1400, 101) == 0xffc5cfdd);
    assert(!wcscmp(relative_level(2800, 50), L"相对7+"));
    assert(!wcscmp(relative_level(2799, 50), L"相对6"));
    assert(!wcscmp(relative_level(2500, 50), L"相对6"));
    assert(!wcscmp(relative_level(2499, 50), L"相对5"));
    assert(!wcscmp(relative_level(2300, 50), L"相对5"));
    assert(!wcscmp(relative_level(2299, 50), L"相对4"));
    assert(!wcscmp(relative_level(2000, 50), L"相对4"));
    assert(!wcscmp(relative_level(1999, 50), L"相对3"));
    assert(!wcscmp(relative_level(1800, 50), L"相对3"));
    assert(!wcscmp(relative_level(1799, 50), L"相对<3"));
    assert(!wcscmp(relative_level(1400, 100), L"相对1萌新"));
    assert(!wcscmp(relative_level(1401, 100), L"相对<3"));
    assert(!wcscmp(relative_level(1400, 101), L"相对<3"));
    char temp_dir[MAX_PATH], temp_file[MAX_PATH];
    assert(GetTempPathA(sizeof(temp_dir), temp_dir));
    assert(GetTempFileNameA(temp_dir, "rl_", 0, temp_file));
    FILE *levels = fopen(temp_file, "wb");
    assert(levels);
    fputs("[Thresholds]\nrelative_7_plus=2600\nrelative_6=2300\nrelative_5=2100\n"
          "relative_4=1700\nrelative_3=1500\n[Newbie]\nrating_max=1299\ngames_max=49\n", levels);
    assert(!fclose(levels));
    load_relative_thresholds(temp_file);
    assert(!wcscmp(relative_level(2599, 100), L"相对6"));
    assert(!wcscmp(relative_level(2099, 100), L"相对4"));
    assert(!wcscmp(relative_level(1299, 49), L"相对1萌新"));
    assert(!wcscmp(relative_level(1299, 50), L"相对<3"));
    assert(rating_color(2599) == 0xffe794f5);
    assert(rating_color(2300) == 0xffe794f5);
    assert(rating_color(2299) == 0xffffd071);
    assert(total_rating_color(1299, 49) == 0xffa4e0b6);
    assert(total_rating_color(1299, 50) == 0xffc5cfdd);
#ifdef TRUE_RELATIVE_VISUALS
    assert(lobby_visual_relation(2000, 1901) == 0);
    assert(lobby_visual_relation(2000, 1900) == 1);
    assert(lobby_visual_relation(2000, 2299) == 0);
    assert(lobby_visual_relation(2000, 2300) == 2);
#endif
    DeleteFileA(temp_file);
    assert(!wcscmp(character_name_zh("reimu"), L"灵梦"));
    assert(!wcscmp(character_name_zh("youmu"), L"妖梦"));
    assert(!wcscmp(character_name_zh("yuyuko"), L"幽幽子"));
    if (argc == 3) {
        HMODULE current = LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
        HMODULE older = LoadLibraryExA(argv[2], NULL, DONT_RESOLVE_DLL_REFERENCES);
        assert(current && older);
        assert(supported_lobby(current));
        lobby_module = current;
        void **current_vtable = (void **)((unsigned char *)current + LOBBY_MENU_VTABLE_RVA);
        if (!verified_menu_vtable(current_vtable))
            fprintf(stderr, "vtable at %p: %p %p %p %p (base %p)\n", (void *)current_vtable,
                current_vtable[0], current_vtable[1], current_vtable[2], current_vtable[3], (void *)current);
        assert(verified_menu_vtable(current_vtable));
        assert(!supported_lobby(older));
        lobby_module = NULL;
        FreeLibrary(current); FreeLibrary(older);
        puts("lobby SHA-256 gate: OK");
    }
    unsigned char lobby_data[0x2000] = {0};
    unsigned char player_data[4 * 0x64] = {0};
    *(unsigned char **)(lobby_data + 0x220) = player_data;
    *(unsigned char **)(lobby_data + 0x224) = player_data + sizeof(player_data);
    *(uint32_t *)(lobby_data + 0x204) = 100;
    *(uint32_t *)(lobby_data + 0x208) = 200;
    unsigned char connection[0x13c] = {0};
    uint32_t me_id = 7;
    *(unsigned char **)(lobby_data + 0x210) = connection;
    *(uint32_t **)(connection + 0x138) = &me_id;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned char *p = player_data + i * 0x64;
        *(uint32_t *)p = 7 + i;
        const char *name = i == 0 ? "self" : i == 1 ? "waiting" : "nearby";
        memcpy(p + 4, name, strlen(name));
        *(unsigned *)(p + 0x14) = (unsigned)strlen(name);
        *(unsigned *)(p + 0x18) = 15;
        *(uint32_t *)(p + 0x3c) = 100 + i * 10;
        *(uint32_t *)(p + 0x40) = 200;
        p[0x44] = i == 1 ? 1 : 0;
    }
    unsigned char *same_player = player_data + 3 * 0x64;
    *(uint32_t *)same_player = 8;
    memcpy(same_player + 4, "waiting", 7);
    *(unsigned *)(same_player + 0x14) = 7;
    *(unsigned *)(same_player + 0x18) = 15;
    *(uint32_t *)(same_player + 0x3c) = 110;
    *(uint32_t *)(same_player + 0x40) = 200;
    same_player[0x44] = 1;
    VisiblePlayer copied[256];
    assert(copy_players(lobby_data, copied) == 3);
    assert(!strcmp(copied[1].name, "waiting") && copied[1].status == 1);
    unsigned order[3] = {0};
    assert(nearby_players(lobby_data, copied, 3, order) == 2);
    assert(order[0] == 1 && order[1] == 2);
    *(uint32_t *)(lobby_data + 0x204) = 70; /* camera differs; nearest uses avatar */
    assert(nearby_players(lobby_data, copied, 3, order) == 2);
    assert(order[0] == 1 && order[1] == 2);
    selected_player_id = UINT32_MAX;
    cycle_selected(copied, order, 2);
    assert(selected_player_id == copied[1].id);
    copied[2].x = 101;
    assert(nearby_players(lobby_data, copied, 3, order) == 2);
    assert(order[0] == 2 && selected_player_id == copied[1].id);
    cycle_selected(copied, order, 2);
    assert(selected_player_id == UINT32_MAX);
    assert(display_mode == DISPLAY_AUTO && show_own_rating);
    selected_player_id = copied[1].id;
    toggle_auto_off();
    assert(display_mode == DISPLAY_OFF && show_own_rating && selected_player_id == UINT32_MAX);
    toggle_auto_off();
    assert(display_mode == DISPLAY_AUTO && show_own_rating && selected_player_id == UINT32_MAX);
    settings_page = 0;
    settings_row = 0;
    settings_adjust(1);
    assert(display_mode == DISPLAY_MANUAL && show_own_rating);
    settings_adjust(1);
    assert(display_mode == DISPLAY_OFF && show_own_rating);
    settings_adjust(-1);
    assert(display_mode == DISPLAY_MANUAL && show_own_rating);
    toggle_auto_off();
    assert(display_mode == DISPLAY_AUTO && show_own_rating);
    /* As the avatar walks, the nearest auto detail target changes. */
    copied[1].x = 110;
    copied[2].x = 120;
    copied[0].x = 119;
    assert(nearby_players(lobby_data, copied, 3, order) == 2);
    assert(order[0] == 2);
    char save_file[MAX_PATH];
    assert(GetTempFileNameA(temp_dir, "ini", 0, save_file));
    FILE *settings = fopen(save_file, "wb");
    assert(settings);
    fputs("; keep comment\r\n[OwnRating]\r\naccount=custom-name\r\n[Overlay]\r\nposition=above\r\n", settings);
    assert(!fclose(settings));
    assert(save_ini_value(save_file, "Overlay", "position", "center"));
    assert(save_ini_value(save_file, "Display", "mode", "manual"));
    char saved_text[512] = {0};
    settings = fopen(save_file, "rb");
    assert(settings);
    assert(fread(saved_text, 1, sizeof(saved_text) - 1, settings) > 0);
    fclose(settings);
    assert(strstr(saved_text, "; keep comment"));
    assert(strstr(saved_text, "account=custom-name"));
    assert(strstr(saved_text, "position=center"));
    assert(strstr(saved_text, "[Display]\r\nmode=manual"));
    DeleteFileA(save_file);
    GameInput input = {0};
    settings_open = true;
    settings_page = 0;
    settings_row = 0;
    input.verticalAxis = 1;
    settings_input(input, true, false, false, false);
    assert(settings_row == 1);
    input.verticalAxis = 0;
    input.horizontalAxis = 1;
    settings_input(input, true, false, false, false);
    assert(!show_own_rating);
    show_own_rating = true;
    /* Physical Start+Back works even with unbound game pause/select actions. */
    memset(&input, 0, sizeof(input));
    settings_open = false;
    settings_dirty = false;
    pad_menu_was_down = false;
    settings_input(input, true, false, false, true);
    assert(settings_open);
    settings_input(input, true, false, false, true);
    assert(settings_open); /* held chord must not toggle every frame */
    settings_input(input, true, false, false, false);
    settings_input(input, true, false, false, true);
    assert(!settings_open);
    settings_input(input, true, false, false, false);
    settings_input(input, true, true, false, true);
    assert(!settings_open); /* never open while chatting */
    settings_input(input, true, false, false, true);
    assert(!settings_open); /* release required after blocked chord */
    settings_input(input, false, false, false, false);
    assert(!settings_open);
    original_process = assert_muted_input;
    settings_open = true;
    GAME_INPUT.horizontalAxis = 1;
    GAME_INPUT.a = 1;
    assert(process_hook(lobby_data, NULL) == 1);
    assert(GAME_INPUT.horizontalAxis == 1 && GAME_INPUT.a == 1);
    settings_open = false;
    settings_dirty = false;
    puts("player layout, three display modes, own-card independence and input masking: OK");
    return 0;
}
