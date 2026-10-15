#ifndef TRUE_RELATIVE_LOBBY_VISUAL_H
#define TRUE_RELATIVE_LOBBY_VISUAL_H
#include "LobbyPlayer.h"
#include <d3d9.h>
#include <stdbool.h>

typedef bool (*LobbyVisualLookup)(const VisiblePlayer players[256], unsigned count,
                                  unsigned index, uint32_t local_id, double *rating);

void lobby_visual_input(bool foreground, bool chatting);
void lobby_visual_draw(IDirect3DDevice9 *device, const void *menu,
                       const VisiblePlayer players[256], unsigned count, uint32_t local_id,
                       int translate_x, int translate_y, LobbyVisualLookup lookup);
void lobby_visual_reset(void);
unsigned lobby_visual_relation(double own, double other);

#endif
