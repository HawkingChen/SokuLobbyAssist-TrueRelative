#define TRUE_RELATIVE_TEST_HARNESS
#include "TrueRelative.c"
#include <assert.h>
int main(int argc, char **argv)
{
    assert(show_relative_level);
    toggle_relative_display();
    assert(!show_relative_level);
    toggle_relative_display();
    assert(show_relative_level);
    assert(rating_color(2500) == 0xffff7088);
    assert(rating_color(2499) == 0xffe794f5);
    assert(rating_color(2200) == 0xffe794f5);
    assert(rating_color(2000) == 0xffffd071);
    assert(rating_color(1800) == 0xff8bd7ff);
    assert(rating_color(1600) == 0xffa7e69b);
    assert(rating_color(1599) == 0xffc5cfdd);
    assert(total_rating_color(1399, 99) == 0xffa4e0b6);
    assert(total_rating_color(1399, 100) == 0xffc5cfdd);
    assert(!wcscmp(relative_level(2500, 50), L"相对7+"));
    assert(!wcscmp(relative_level(2200, 50), L"相对5"));
    assert(!wcscmp(relative_level(2249, 50), L"相对5"));
    assert(!wcscmp(relative_level(2250, 50), L"相对6"));
    assert(!wcscmp(relative_level(2000, 50), L"相对5"));
    assert(!wcscmp(relative_level(1800, 50), L"相对4"));
    assert(!wcscmp(relative_level(1600, 50), L"相对3"));
    assert(!wcscmp(relative_level(1599, 50), L"相对<3"));
    assert(!wcscmp(relative_level(1399, 99), L"相对1萌新"));
    assert(!wcscmp(relative_level(1400, 99), L"相对<3"));
    assert(!wcscmp(relative_level(1399, 100), L"相对<3"));
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
    assert(automatic_mode && show_own_rating);
    selected_player_id = copied[1].id;
    toggle_automatic_mode();
    assert(!automatic_mode && !show_own_rating && selected_player_id == UINT32_MAX);
    toggle_automatic_mode();
    assert(automatic_mode && show_own_rating && selected_player_id == UINT32_MAX);
    /* As the avatar walks, the nearest auto detail target changes. */
    copied[1].x = 110;
    copied[2].x = 120;
    copied[0].x = 119;
    assert(nearby_players(lobby_data, copied, 3, order) == 2);
    assert(order[0] == 2);
    puts("player layout, avatar-relative nearest selection and F10 auto mode: OK");
    return 0;
}
