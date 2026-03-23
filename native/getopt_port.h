/*
 * getopt_port.h — portable getopt / getopt_long for Windows
 *
 * Based on public-domain implementations. Provides the subset of
 * POSIX getopt_long used by pi-memory:
 *   - getopt_long(argc, argv, optstring, longopts, NULL)
 *   - struct option { name, has_arg, flag, val }
 *   - optarg, optind, opterr, optopt
 *   - required_argument, no_argument
 *
 * On non-Windows platforms this header is never included — the
 * system <getopt.h> is used instead (via compat.h).
 *
 * License: public domain / MIT — use freely.
 */

#ifndef GETOPT_PORT_H
#define GETOPT_PORT_H

#ifdef _WIN32

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef no_argument
#define no_argument       0
#define required_argument 1
#define optional_argument 2
#endif

struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};

static char *optarg = NULL;
static int   optind = 1;
static int   opterr = 1;
static int   optopt = '?';

/* Internal state for scanning within an argv element */
static const char *_pi_nextchar = NULL;

static int getopt_long(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longindex)
{
    optarg = NULL;

    if (optind >= argc || !argv[optind])
        return -1;

    /* Handle "--" end-of-options marker */
    if (strcmp(argv[optind], "--") == 0) {
        optind++;
        return -1;
    }

    /* ── Long options: --name or --name=value ── */
    if (argv[optind][0] == '-' && argv[optind][1] == '-' && longopts) {
        const char *arg = argv[optind] + 2;
        const char *eq  = strchr(arg, '=');
        size_t nlen = eq ? (size_t)(eq - arg) : strlen(arg);

        for (int i = 0; longopts[i].name; i++) {
            if (strncmp(longopts[i].name, arg, nlen) == 0 &&
                strlen(longopts[i].name) == nlen) {

                if (longindex) *longindex = i;
                optind++;

                if (longopts[i].has_arg == required_argument) {
                    if (eq) {
                        optarg = (char *)(eq + 1);
                    } else if (optind < argc) {
                        optarg = argv[optind++];
                    } else {
                        optopt = longopts[i].val;
                        return '?';
                    }
                } else if (longopts[i].has_arg == optional_argument) {
                    if (eq) optarg = (char *)(eq + 1);
                } else {
                    /* no_argument — ignore any =value */
                }

                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }

        /* Unknown long option */
        optopt = 0;
        optind++;
        return '?';
    }

    /* ── Short options: -x or -xyz (bundled) ── */
    if (argv[optind][0] == '-' && argv[optind][1] != '\0') {
        if (!_pi_nextchar || !*_pi_nextchar)
            _pi_nextchar = argv[optind] + 1;

        char c = *_pi_nextchar++;
        const char *spec = strchr(optstring, c);

        if (!*_pi_nextchar) {
            optind++;
            _pi_nextchar = NULL;
        }

        if (!spec) {
            optopt = c;
            return '?';
        }

        if (spec[1] == ':') {
            /* Option requires an argument */
            if (_pi_nextchar && *_pi_nextchar) {
                /* Argument is the rest of this argv element: -fvalue */
                optarg = (char *)_pi_nextchar;
                _pi_nextchar = NULL;
                optind++;
            } else if (optind < argc) {
                optarg = argv[optind++];
            } else {
                optopt = c;
                return '?';
            }
        }

        return c;
    }

    /* Not an option — stop processing */
    return -1;
}

/* Plain getopt (no long options) — just call getopt_long with NULL longopts */
static int getopt(int argc, char *const argv[], const char *optstring) {
    return getopt_long(argc, argv, optstring, NULL, NULL);
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* GETOPT_PORT_H */
