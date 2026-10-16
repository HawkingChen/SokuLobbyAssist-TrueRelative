#ifndef TRUE_RELATIVE_AUTO_TARGET_H
#define TRUE_RELATIVE_AUTO_TARGET_H
#include <stdint.h>

typedef struct {
    uint32_t current, candidate, since;
} AutoTarget;

static inline void auto_target_reset(AutoTarget *state)
{
    *state = (AutoTarget){UINT32_MAX, UINT32_MAX, 0};
}

/* IDs are sorted by distance. A different nearest player must remain nearest
   for 300 ms. Never retain a target outside the selectable nearby list. */
static inline uint32_t auto_target_update(AutoTarget *state,
    const uint32_t *ids, unsigned count, uint32_t now)
{
    if (!count) { auto_target_reset(state); return UINT32_MAX; }
    unsigned i = 0;
    while (i < count && ids[i] != state->current) ++i;
    if (i == count) {
        state->current = ids[0];
        state->candidate = UINT32_MAX;
    } else if (ids[0] == state->current) {
        state->candidate = UINT32_MAX;
    } else if (state->candidate != ids[0]) {
        state->candidate = ids[0];
        state->since = now;
    } else if ((uint32_t)(now - state->since) >= 300) {
        state->current = state->candidate;
        state->candidate = UINT32_MAX;
    }
    return state->current;
}
#endif
