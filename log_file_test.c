#include "LogFile.h"
#include <assert.h>
#include <string.h>

static DWORD size_of(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    assert(GetFileAttributesExA(path, GetFileExInfoStandard, &data));
    assert(!data.nFileSizeHigh);
    return data.nFileSizeLow;
}

static void seed(const char *path, DWORD length, char value)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    char block[4096];
    memset(block, value, sizeof(block));
    while (length) {
        DWORD amount = length < sizeof(block) ? length : sizeof(block);
        assert(fwrite(block, 1, amount, file) == amount);
        length -= amount;
    }
    assert(!fclose(file));
}

static void starts_with(const char *path, char value)
{
    FILE *file = fopen(path, "rb");
    assert(file && fgetc(file) == value);
    fclose(file);
}

static DWORD WINAPI write_thread(void *argument)
{
    const char *path = argument;
    char line[256];
    memset(line, 'X', sizeof(line));
    line[sizeof(line) - 1] = '\n';
    for (unsigned i = 0; i < 1000; ++i)
        log_append_bounded(path, line, sizeof(line));
    return 0;
}

int main(void)
{
    char directory[MAX_PATH], path[MAX_PATH], previous[MAX_PATH];
    assert(GetTempPathA(sizeof(directory), directory));
    assert(GetTempFileNameA(directory, "trl", 0, path));
    assert(snprintf(previous, sizeof(previous), "%s.old", path) < (int)sizeof(previous));

    seed(path, LOG_FILE_LIMIT - 4, 'A');
    log_append_bounded(path, "B\n", 2);
    assert(size_of(path) == LOG_FILE_LIMIT - 2);
    log_append_bounded(path, "C\n", 2);
    assert(size_of(path) == LOG_FILE_LIMIT);
    assert(GetFileAttributesA(previous) == INVALID_FILE_ATTRIBUTES);
    log_append_bounded(path, "D\n", 2);
    assert(size_of(path) == 2 && size_of(previous) == LOG_FILE_LIMIT);
    starts_with(path, 'D');
    starts_with(previous, 'A');

    /* The next rotation replaces the single backup, including across opens. */
    seed(path, LOG_FILE_LIMIT - 1, 'E');
    log_append_bounded(path, "F\n", 2);
    assert(size_of(path) == 2 && size_of(previous) == LOG_FILE_LIMIT - 1);
    starts_with(previous, 'E');

    /* Old releases may have left arbitrarily large files. Keep the latest tail. */
    seed(previous, LOG_FILE_LIMIT + 20, 'R');
    seed(path, 2 * LOG_FILE_LIMIT, 'S');
    FILE *file = fopen(path, "ab");
    assert(file && fwrite("Q", 1, 1, file) == 1);
    fclose(file);
    log_append_bounded(path, "T\n", 2);
    assert(size_of(path) == 2 && size_of(previous) == LOG_FILE_LIMIT);
    starts_with(previous, 'S');
    file = fopen(previous, "rb");
    assert(file && !fseek(file, -1, SEEK_END) && fgetc(file) == 'Q');
    fclose(file);

    /* A locked backup must not cause appending beyond the cap or data loss. */
    seed(path, LOG_FILE_LIMIT, 'U');
    HANDLE held = CreateFileA(previous, GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(held != INVALID_HANDLE_VALUE);
    log_append_bounded(path, "W\n", 2);
    assert(size_of(path) == LOG_FILE_LIMIT);
    starts_with(path, 'U');
    CloseHandle(held);
    log_append_bounded(path, "W\n", 2);
    assert(size_of(path) == 2 && size_of(previous) == LOG_FILE_LIMIT);

    seed(path, 0, 0);
    assert(DeleteFileA(previous));
    HANDLE threads[4];
    for (unsigned i = 0; i < 4; ++i) {
        threads[i] = CreateThread(NULL, 0, write_thread, path, 0, NULL);
        assert(threads[i]);
    }
    assert(WaitForMultipleObjects(4, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < 4; ++i) CloseHandle(threads[i]);
    assert(size_of(path) <= LOG_FILE_LIMIT && size_of(previous) == LOG_FILE_LIMIT);
    const char *files[] = {path, previous};
    for (unsigned i = 0; i < 2; ++i) {
        file = fopen(files[i], "rb");
        assert(file);
        char line[256];
        size_t bytes;
        while ((bytes = fread(line, 1, sizeof(line), file))) {
            assert(bytes == sizeof(line) && line[255] == '\n');
            for (unsigned j = 0; j < 255; ++j) assert(line[j] == 'X');
        }
        fclose(file);
    }
    assert(DeleteFileA(path) && DeleteFileA(previous));
    puts("Bounded logs: limits, rotation, oversized legacy files, locked backup and concurrent writes passed");
    return 0;
}
