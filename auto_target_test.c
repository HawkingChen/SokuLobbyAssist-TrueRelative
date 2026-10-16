#include "AutoTarget.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    AutoTarget state;
    auto_target_reset(&state);
    const uint32_t ab[] = {1, 2}, ba[] = {2, 1}, ca[] = {3, 1};
    assert(auto_target_update(&state, ab, 2, 0) == 1);
    assert(auto_target_update(&state, ba, 2, 10) == 1);
    assert(auto_target_update(&state, ba, 2, 309) == 1);
    assert(auto_target_update(&state, ba, 2, 310) == 2);
    /* Alternating nearest players cancel the pending switch. */
    assert(auto_target_update(&state, ab, 2, 320) == 2);
    assert(auto_target_update(&state, ba, 2, 600) == 2);
    assert(auto_target_update(&state, ab, 2, 620) == 2);
    assert(auto_target_update(&state, ab, 2, 919) == 2);
    assert(auto_target_update(&state, ab, 2, 920) == 1);
    /* A different challenger starts a fresh delay. */
    assert(auto_target_update(&state, ba, 2, 1000) == 1);
    assert(auto_target_update(&state, ca, 2, 1200) == 1);
    assert(auto_target_update(&state, ca, 2, 1499) == 1);
    assert(auto_target_update(&state, ca, 2, 1500) == 3);
    /* Departed targets switch immediately; an empty lobby clears state. */
    assert(auto_target_update(&state, ab, 2, 1501) == 1);
    assert(auto_target_update(&state, NULL, 0, 1502) == UINT32_MAX);
    assert(auto_target_update(&state, ba, 2, 1503) == 2);
    /* The Windows tick counter can wrap during the delay. */
    auto_target_reset(&state);
    assert(auto_target_update(&state, ab, 2, UINT32_MAX - 200) == 1);
    assert(auto_target_update(&state, ba, 2, UINT32_MAX - 100) == 1);
    assert(auto_target_update(&state, ba, 2, 198) == 1);
    assert(auto_target_update(&state, ba, 2, 199) == 2);
    puts("Auto target tests passed");
    return 0;
}
