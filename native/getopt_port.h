/*
 * getopt_port.h — portable getopt / getopt_long for Windows
 *
 * Header-only implementation used by pi-memory on Windows. This keeps the
 * native build simple while preserving the parser behavior that was verified
 * on the dedicated Windows support branch.
 */

#ifndef GETOPT_PORT_H
#define GETOPT_PORT_H

#ifdef _WIN32

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__) || defined(__GNUC__)
#define PI_GETOPT_UNUSED __attribute__((unused))
#else
#define PI_GETOPT_UNUSED
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
static int   PI_GETOPT_UNUSED opterr = 1;
static int   optopt = 0;
static int   optreset = 0;

static int _pi_short_index = 0;
static int _pi_scan_index = 1;
static int _pi_current_index = -1;
static int _pi_first_nonopt = -1;

static void _pi_reset_parser_state(void) {
    _pi_short_index = 0;
    _pi_scan_index = optind > 0 ? optind : 1;
    _pi_current_index = -1;
    _pi_first_nonopt = -1;
    optarg = NULL;
}

static int _pi_find_short_option(const char *optstring, char c) {
    const char *p = optstring;
    while (*p) {
        if (*p == c) return (int)(p - optstring);
        p++;
    }
    return -1;
}

static int _pi_parse_long_option(int argc, char * const argv[], const struct option *longopts, int *longindex) {
    char *arg = argv[_pi_current_index] + 2;
    char *value = strchr(arg, '=');
    size_t name_len = value ? (size_t)(value - arg) : strlen(arg);

    if (value) *value++ = '\0';

    for (int i = 0; longopts && longopts[i].name; i++) {
        if (strlen(longopts[i].name) == name_len && strncmp(longopts[i].name, arg, name_len) == 0) {
            if (longindex) *longindex = i;

            if (longopts[i].has_arg == required_argument) {
                if (value && *value) {
                    optarg = value;
                    _pi_scan_index = _pi_current_index + 1;
                } else if (_pi_current_index + 1 < argc) {
                    optarg = argv[_pi_current_index + 1];
                    _pi_scan_index = _pi_current_index + 2;
                } else {
                    _pi_scan_index = _pi_current_index + 1;
                    _pi_current_index = -1;
                    optopt = longopts[i].val;
                    return '?';
                }
            } else if (longopts[i].has_arg == optional_argument) {
                optarg = value;
                _pi_scan_index = _pi_current_index + 1;
            } else {
                if (value) {
                    _pi_scan_index = _pi_current_index + 1;
                    _pi_current_index = -1;
                    optopt = longopts[i].val;
                    return '?';
                }
                _pi_scan_index = _pi_current_index + 1;
            }

            _pi_current_index = -1;
            if (longopts[i].flag) {
                *longopts[i].flag = longopts[i].val;
                return 0;
            }
            return longopts[i].val;
        }
    }

    _pi_scan_index = _pi_current_index + 1;
    _pi_current_index = -1;
    return '?';
}

static int getopt_long(int argc, char * const argv[], const char *optstring,
                       const struct option *longopts, int *longindex) {
    optarg = NULL;

    if (optreset) {
        optreset = 0;
        _pi_reset_parser_state();
    }

    if (optind <= 0) optind = 1;
    if (_pi_scan_index <= 0) _pi_scan_index = optind;

    while (_pi_current_index == -1) {
        if (_pi_scan_index >= argc) {
            optind = _pi_first_nonopt >= 0 ? _pi_first_nonopt : _pi_scan_index;
            return -1;
        }

        char *arg = argv[_pi_scan_index];
        if (!arg || arg[0] != '-' || arg[1] == '\0') {
            if (_pi_first_nonopt < 0) _pi_first_nonopt = _pi_scan_index;
            _pi_scan_index++;
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            _pi_scan_index++;
            optind = _pi_first_nonopt >= 0 ? _pi_first_nonopt : _pi_scan_index;
            return -1;
        }

        _pi_current_index = _pi_scan_index;
        if (arg[1] == '-') {
            return _pi_parse_long_option(argc, argv, longopts, longindex);
        }
        _pi_short_index = 1;
    }

    char *arg = argv[_pi_current_index];
    char c = arg[_pi_short_index++];
    int idx = _pi_find_short_option(optstring, c);
    if (idx < 0) {
        if (arg[_pi_short_index] == '\0') {
            _pi_scan_index = _pi_current_index + 1;
            _pi_current_index = -1;
            _pi_short_index = 0;
        }
        optopt = c;
        return '?';
    }

    if (optstring[idx + 1] == ':') {
        if (arg[_pi_short_index] != '\0') {
            optarg = &arg[_pi_short_index];
            _pi_scan_index = _pi_current_index + 1;
            _pi_current_index = -1;
            _pi_short_index = 0;
        } else if (_pi_current_index + 1 < argc) {
            optarg = argv[_pi_current_index + 1];
            _pi_scan_index = _pi_current_index + 2;
            _pi_current_index = -1;
            _pi_short_index = 0;
        } else {
            _pi_scan_index = _pi_current_index + 1;
            _pi_current_index = -1;
            _pi_short_index = 0;
            optopt = c;
            return '?';
        }
    } else if (arg[_pi_short_index] == '\0') {
        _pi_scan_index = _pi_current_index + 1;
        _pi_current_index = -1;
        _pi_short_index = 0;
    }

    return c;
}

static int PI_GETOPT_UNUSED getopt(int argc, char * const argv[], const char *optstring) {
    return getopt_long(argc, argv, optstring, NULL, NULL);
}

#ifdef __cplusplus
}
#endif

#undef PI_GETOPT_UNUSED

#endif /* _WIN32 */
#endif /* GETOPT_PORT_H */
