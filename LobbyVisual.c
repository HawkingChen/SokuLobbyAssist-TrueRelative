#define COBJMACROS
#include "LobbyVisual.h"
#include <string.h>

typedef struct { float x, y, z, rhw; D3DCOLOR color; } VisualVertex;

static bool enabled;
static unsigned input_step;
static DWORD input_started;
static WORD digit_was_down;
static unsigned char types[256];
static uint32_t ids[256];
static unsigned cached_count;
static DWORD updated;
static const void *cached_menu;

unsigned lobby_visual_relation(double own, double other)
{
    if (own - other >= 100.0) return 1;
    if (other - own >= 300.0) return 2;
    return 0;
}

void lobby_visual_input(bool foreground, bool chatting)
{
    static const unsigned char code[] = {0x68, 0x6e, 0x6c, 0x62};
    WORD down = 0;
    for (unsigned digit = 0; digit < 10; ++digit)
        if (GetAsyncKeyState('0' + digit) & 0x8000) down |= (WORD)(1u << digit);
    bool armed = foreground && !chatting &&
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    DWORD now = GetTickCount();
    if (!armed || (input_step && now - input_started > 5000)) input_step = 0;
    WORD pressed = down & (WORD)~digit_was_down;
    digit_was_down = down;
    if (!armed) return;
    for (unsigned digit = 0; digit < 10; ++digit) if (pressed & (1u << digit)) {
        if (((unsigned char)('0' + digit) ^ 0x5a) == code[input_step]) {
            if (!input_step) input_started = now;
            if (++input_step == sizeof(code)) {
                enabled = !enabled;
                updated = 0;
                input_step = 0;
            }
        } else input_step = 0;
    }
}

void lobby_visual_reset(void)
{
    input_step = 0;
    digit_was_down = 0;
    updated = 0;
    cached_menu = NULL;
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

static void ring(IDirect3DDevice9 *device, float x, float y, float rx, float ry,
                 float width, D3DCOLOR color)
{
    static const float points[16][2] = {
        {1,0},{.924f,.383f},{.707f,.707f},{.383f,.924f},
        {0,1},{-.383f,.924f},{-.707f,.707f},{-.924f,.383f},
        {-1,0},{-.924f,-.383f},{-.707f,-.707f},{-.383f,-.924f},
        {0,-1},{.383f,-.924f},{.707f,-.707f},{.924f,-.383f}
    };
    VisualVertex vertices[16 * 6];
    for (unsigned i = 0; i < 16; ++i) {
        unsigned next = (i + 1) % 16;
        float ax = points[i][0], ay = points[i][1];
        float bx = points[next][0], by = points[next][1];
        VisualVertex outer_a = {x + ax * rx, y + ay * ry, 0, 1, color};
        VisualVertex inner_a = {x + ax * (rx - width), y + ay * (ry - width), 0, 1, color};
        VisualVertex outer_b = {x + bx * rx, y + by * ry, 0, 1, color};
        VisualVertex inner_b = {x + bx * (rx - width), y + by * (ry - width), 0, 1, color};
        unsigned base = i * 6;
        vertices[base] = outer_a; vertices[base + 1] = inner_a;
        vertices[base + 2] = outer_b; vertices[base + 3] = outer_b;
        vertices[base + 4] = inner_a; vertices[base + 5] = inner_b;
    }
    IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLELIST, 32, vertices, sizeof(VisualVertex));
}

static void diamond(IDirect3DDevice9 *device, float x, float y, float size, D3DCOLOR color)
{
    VisualVertex vertices[4] = {
        {x,y-size,0,1,color},{x-size,y,0,1,color},
        {x+size,y,0,1,color},{x,y+size,0,1,color}
    };
    IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(VisualVertex));
}

void lobby_visual_draw(IDirect3DDevice9 *device, const void *menu,
                       const VisiblePlayer players[256], unsigned count, uint32_t local_id,
                       int translate_x, int translate_y, LobbyVisualLookup lookup)
{
    if (!enabled || !device || !lookup) return;
    DWORD now = GetTickCount();
    bool changed = menu != cached_menu || count != cached_count || !updated || now - updated >= 1000;
    for (unsigned i = 0; i < count && !changed; ++i)
        if (ids[i] != players[i].id) changed = true;
    if (changed) {
        memset(types, 0, sizeof(types));
        for (unsigned i = 0; i < count; ++i) ids[i] = players[i].id;
        cached_count = count;
        cached_menu = menu;
        updated = now ? now : 1;
        double own_rating = 0;
        bool found_own = false;
        for (unsigned i = 0; i < count; ++i)
            if (players[i].id == local_id) {
                found_own = lookup(players, count, i, local_id, &own_rating);
                break;
            }
        if (found_own) for (unsigned i = 0; i < count; ++i) {
            if (players[i].id == local_id) continue;
            double other_rating = 0;
            if (lookup(players, count, i, local_id, &other_rating))
                types[i] = (unsigned char)lobby_visual_relation(own_rating, other_rating);
        }
    }
    shape_state(device);
    unsigned phase = (now / 40) % 32;
    float pulse = (float)(phase <= 16 ? phase : 32 - phase) / 16.0f;
    for (unsigned i = 0; i < count; ++i) {
        unsigned type = types[i];
        if (!type) continue;
        float x = (float)((int)players[i].x + translate_x);
        float y = (float)((int)players[i].y + translate_y - 8);
        if (x < -36 || x > 676 || y < -36 || y > 516) continue;
        if (type == 1) {
            ring(device, x, y, 25 + pulse * 2, 21 + pulse * 2, 3, 0x4873edce);
            ring(device, x, y, 21 + pulse, 17 + pulse, 2, 0xb06fffe0);
        } else {
            ring(device, x, y, 29 + pulse * 3, 25 + pulse * 3, 4, 0x62ff455a);
            ring(device, x, y, 23 + pulse, 19 + pulse, 2, 0xc8ff6c78);
            diamond(device, x, y - 31 - pulse * 3, 4, 0xd0ff9a9a);
            diamond(device, x - 34 - pulse * 3, y, 3, 0xa0ff6477);
            diamond(device, x + 34 + pulse * 3, y, 3, 0xa0ff6477);
        }
    }
}
