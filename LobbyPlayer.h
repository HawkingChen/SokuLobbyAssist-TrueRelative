#ifndef TRUE_RELATIVE_LOBBY_PLAYER_H
#define TRUE_RELATIVE_LOBBY_PLAYER_H
#include <stdint.h>

typedef struct {
    uint32_t id, x, y;
    unsigned char status;
    char name[129];
} VisiblePlayer;

#endif
