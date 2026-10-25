#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
typedef bool (__cdecl *InitializeFn)(HMODULE, HMODULE);
int main(int argc, char **argv)
{
    assert(argc == 3);
    HMODULE lobby = LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
    assert(lobby);
    void **table = (void **)((BYTE *)lobby + 0x23c64c);
    uintptr_t previous[4];
    for (unsigned i = 0; i < 4; ++i) previous[i] = (uintptr_t)table[i];
    HMODULE plugin = LoadLibraryA(argv[2]);
    assert(plugin);
    InitializeFn initialize = (InitializeFn)(void *)GetProcAddress(plugin, "Initialize");
    assert(initialize);
    *(BYTE *)((BYTE *)lobby + 0x29998d) = 1;
    assert(initialize(plugin, NULL));
    assert(!*(BYTE *)((BYTE *)lobby + 0x29998d));
    assert((uintptr_t)table[1] == previous[1]);
    for (unsigned i = 0; i < 4; ++i) if (i != 1) {
        assert((uintptr_t)table[i] != previous[i]);
        MEMORY_BASIC_INFORMATION memory;
        assert(VirtualQuery(table[i], &memory, sizeof(memory)));
        assert(memory.AllocationBase == plugin);
    }
    uintptr_t installed[4];
    for (unsigned i = 0; i < 4; ++i) installed[i] = (uintptr_t)table[i];
    assert(initialize(plugin, NULL));
    for (unsigned i = 0; i < 4; ++i) assert((uintptr_t)table[i] == installed[i]);
    puts("Real DLL loader: independent plugin hooks installed once; native ratings disabled; protocol code untouched");
    return 0;
}
