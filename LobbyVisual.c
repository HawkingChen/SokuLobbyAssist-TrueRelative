#define COBJMACROS
#include "LobbyVisual.h"
#include <string.h>

typedef struct { float x, y, z, rhw; D3DCOLOR color; } VisualVertex;

static bool enabled;
static int lower_gap = 100;
static int higher_gap = 300;
static unsigned char types[256];
static uint32_t ids[256];
static unsigned cached_count;
static DWORD updated;
static const void *cached_menu;

unsigned lobby_visual_relation(double own, double other)
{
    if (own - other >= lower_gap) return 1;
    if (other - own >= higher_gap) return 2;
    return 0;
}

void lobby_visual_configure(bool show, int lower, int higher)
{
    enabled = show;
    lower_gap = lower;
    higher_gap = higher;
    updated = 0;
}

bool lobby_visual_enabled(void) { return enabled; }
int lobby_visual_lower_gap(void) { return lower_gap; }
int lobby_visual_higher_gap(void) { return higher_gap; }

void lobby_visual_reset(void)
{
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

static void box(IDirect3DDevice9 *device, float x, float y, float width, float height,
                D3DCOLOR color)
{
    VisualVertex vertices[4] = {
        {x,y,0,1,color},{x+width,y,0,1,color},
        {x,y+height,0,1,color},{x+width,y+height,0,1,color}
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
        float avatar_y = (float)((int)players[i].y + translate_y);
        float y = avatar_y - 50;
        if (x < -48 || x > 688 || y < -48 || y > 528) continue;
        if (type == 1) {
            ring(device, x, y, 51 + pulse * 2, 70 + pulse * 2, 6, 0x5073edce);
            ring(device, x, y, 46 + pulse * 2, 65 + pulse * 2, 3, 0xb06fffe0);
            diamond(device, x, y - 69 - pulse * 2, 4, 0xd4e3fff3);
        } else {
            float left = x - 45 - pulse;
            float top = avatar_y - 117 - pulse;
            float width = 96 + pulse * 2;
            float height = 139 + pulse * 2;
            box(device, left, top, width, height, 0x28ff304a);
            box(device, left - 2, top - 2, width + 4, 2, 0x58ff6177);
            box(device, left - 2, top + height, width + 4, 2, 0x58ff6177);
            box(device, left - 2, top, 2, height, 0x58ff6177);
            box(device, left + width, top, 2, height, 0x58ff6177);
            box(device, left, top, width, 3, 0xdcff5369);
            box(device, left, top + height - 3, width, 3, 0xdcff5369);
            box(device, left, top, 3, height, 0xdcff5369);
            box(device, left + width - 3, top, 3, height, 0xdcff5369);
            /* Three small marks near the top avoid covering the avatar. */
            for (int j = 0; j < 3; ++j) {
                float mark_x = left + width / 2 - 15 + j * 12;
                box(device, mark_x - 2, top + 10, 7, 22, 0x70ff7182);
                box(device, mark_x, top + 10, 3, 17, 0xeaffd7df);
                box(device, mark_x, top + 30, 3, 4, 0xeaffd7df);
            }
        }
    }
}
