/* The few host calls that differ between POSIX and Windows (MinGW-w64), so the shim
 * builds for both (docs/windows.md). */
#ifndef PLATFORM_HOST_COMPAT_H
#define PLATFORM_HOST_COMPAT_H

#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>

/* Binary mode: Windows' open() would otherwise translate line endings. */
#define HOST_O_FLAGS O_BINARY
#define host_mkdir(path, mode) ((void)(mode), _mkdir(path))

static inline struct tm *host_localtime(const time_t *t, struct tm *out)
{
    return localtime_s(out, t) == 0 ? out : NULL;
}

static inline struct tm *host_gmtime(const time_t *t, struct tm *out)
{
    return gmtime_s(out, t) == 0 ? out : NULL;
}

static inline void host_realtime(struct timespec *ts)
{
    FILETIME ft; /* 100 ns units since 1601 */
    GetSystemTimePreciseAsFileTime(&ft);
    unsigned long long t = (((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull;
    ts->tv_sec = (time_t)(t / 10000000ull);
    ts->tv_nsec = (long)(t % 10000000ull) * 100;
}

/* The local time's offset from UTC in seconds, daylight saving included (tm_gmtoff). */
static inline long host_gmtoff(time_t now, const struct tm *local)
{
    struct tm g;
    if (gmtime_s(&g, &now) != 0)
        return 0;
    g.tm_isdst = local->tm_isdst; /* UTC's fields read as local time: `now` minus the offset */
    return (long)(now - mktime(&g));
}

/* An absolute path for an existing `path`, Windows style ("C:\psp\chexa"). */
static inline char *host_realpath(const char *path, char *out, size_t size)
{
    DWORD attr;
    if (!_fullpath(out, path, size) || (attr = GetFileAttributesA(out)) == INVALID_FILE_ATTRIBUTES)
        return NULL;
    return out;
}

/* This thread's CPU time in nanoseconds, or 0. */
static inline long long host_thread_cpu_ns(void)
{
    FILETIME create, exit_time, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &create, &exit_time, &kernel, &user))
        return 0;
    unsigned long long k = ((unsigned long long)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    unsigned long long u = ((unsigned long long)user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (long long)((k + u) * 100ull); /* 100 ns units */
}

static inline bool host_is_dir_sep(char c) { return c == '/' || c == '\\'; }
#else
#define HOST_O_FLAGS O_CLOEXEC
#define host_mkdir(path, mode) mkdir(path, mode)
#define host_localtime localtime_r
#define host_gmtime gmtime_r

static inline void host_realtime(struct timespec *ts)
{
    clock_gettime(CLOCK_REALTIME, ts);
}

static inline long host_gmtoff(time_t now, const struct tm *local)
{
    (void)now;
    return local->tm_gmtoff;
}

static inline char *host_realpath(const char *path, char *out, size_t size)
{
    (void)size; /* PATH_MAX bytes: the fortified glibc aborts with less */
    return realpath(path, out);
}

static inline long long host_thread_cpu_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

static inline bool host_is_dir_sep(char c) { return c == '/'; }
#endif

#endif
