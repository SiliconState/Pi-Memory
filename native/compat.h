/*
 * compat.h — cross-platform shims for pi-memory
 *
 * Provides POSIX-like APIs on Windows (MSVC and MinGW).
 * On Unix/macOS, just wraps the standard headers.
 */

#ifndef PI_MEMORY_COMPAT_H
#define PI_MEMORY_COMPAT_H

#ifdef _WIN32

/* ── Windows ───────────────────────────────────────────────────── */

/* Silence MSVC deprecation warnings for POSIX names */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

#include <direct.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Bundled getopt_long for Windows (MSVC has no getopt) */
#include "getopt_port.h"

/* mkdir: Windows _mkdir takes no mode parameter */
static inline int pi_mkdir(const char *path, int mode) {
    (void)mode;
    return _mkdir(path);
}

/* popen / pclose — MSVC prefixes with underscore */
#ifndef popen
#define popen  _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif

/* getcwd */
#ifndef getcwd
#define getcwd _getcwd
#endif

/* Home directory — no pwd.h on Windows */
static inline const char *pi_get_home(void) {
    const char *home = getenv("USERPROFILE");
    if (!home || !*home) home = getenv("HOME");       /* Git Bash / MSYS2 */
    if ((!home || !*home)) {
        const char *drive = getenv("HOMEDRIVE");
        const char *path = getenv("HOMEPATH");
        if (drive && *drive && path && *path) {
            static char combined[1024];
            snprintf(combined, sizeof(combined), "%s%s", drive, path);
            return combined;
        }
    }
    if (!home || !*home) home = getenv("TEMP");
    if (!home || !*home) home = ".";
    return home;
}

static inline void pi_normalize_path(char *path) {
    if (!path) return;
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
}

/* Path separator */
#define PI_PATH_SEP '\\'
#define PI_IS_SEP(c) ((c) == '/' || (c) == '\\')

#else

/* ── POSIX (Linux / macOS / BSD) ──────────────────────────────── */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdlib.h>
#include <getopt.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define pi_mkdir(path, mode) mkdir(path, mode)

static inline const char *pi_get_home(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = (pw && pw->pw_dir) ? pw->pw_dir : "/tmp";
    }
    return home;
}

static inline void pi_normalize_path(char *path) {
    (void)path;
}

#define PI_PATH_SEP '/'
#define PI_IS_SEP(c) ((c) == '/')

#endif /* _WIN32 */

#endif /* PI_MEMORY_COMPAT_H */
