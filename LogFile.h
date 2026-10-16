#ifndef TRUE_RELATIVE_LOG_FILE_H
#define TRUE_RELATIVE_LOG_FILE_H
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#define LOG_FILE_LIMIT (256u * 1024u)

/* Bring an older, unlimited log under the same cap while keeping its tail. */
static inline bool log_keep_tail(HANDLE file, LONGLONG size)
{
    char buffer[4096];
    LONGLONG source = size - LOG_FILE_LIMIT;
    for (DWORD offset = 0; offset < LOG_FILE_LIMIT;) {
        DWORD amount = LOG_FILE_LIMIT - offset;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        LARGE_INTEGER position;
        DWORD bytes;
        position.QuadPart = source + offset;
        if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN) ||
            !ReadFile(file, buffer, amount, &bytes, NULL) || bytes != amount) return false;
        position.QuadPart = offset;
        if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN) ||
            !WriteFile(file, buffer, amount, &bytes, NULL) || bytes != amount) return false;
        offset += amount;
    }
    LARGE_INTEGER end;
    end.QuadPart = LOG_FILE_LIMIT;
    return SetFilePointerEx(file, end, NULL, FILE_BEGIN) && SetEndOfFile(file);
}

/* Each translation unit writes its own log. Serialize the complete rotate/write
   operation so background requests cannot rotate the same file concurrently. */
static inline void log_append_bounded(const char *path, const char *line, DWORD length)
{
    static SRWLOCK lock = SRWLOCK_INIT;
    char previous[MAX_PATH];
    int count = snprintf(previous, sizeof(previous), "%s.old", path);
    if (!length || length > LOG_FILE_LIMIT || count < 0 || count >= (int)sizeof(previous)) return;
    AcquireSRWLockExclusive(&lock);
    HANDLE file = INVALID_HANDLE_VALUE;
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (GetFileAttributesExA(previous, GetFileExInfoStandard, &attributes) &&
        (attributes.nFileSizeHigh || attributes.nFileSizeLow > LOG_FILE_LIMIT) &&
        !DeleteFileA(previous)) goto done;
    file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size)) goto done;
    if (size.QuadPart > LOG_FILE_LIMIT) {
        if (!log_keep_tail(file, size.QuadPart)) goto done;
        size.QuadPart = LOG_FILE_LIMIT;
    }
    if (size.QuadPart + length > LOG_FILE_LIMIT) {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        if (!MoveFileExA(path, previous, MOVEFILE_REPLACE_EXISTING)) goto done;
        file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) goto done;
    }
    LARGE_INTEGER end;
    end.QuadPart = 0;
    DWORD written;
    if (SetFilePointerEx(file, end, NULL, FILE_END))
        WriteFile(file, line, length, &written, NULL);
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    ReleaseSRWLockExclusive(&lock);
}
#endif
