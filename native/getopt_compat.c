#ifdef _WIN32

#include "getopt_compat.h"

#include <string.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;
int optreset = 0;

static int short_index = 0;
static int scan_index = 1;
static int current_index = -1;
static int first_nonopt = -1;

static void reset_parser_state(void) {
    short_index = 0;
    scan_index = optind > 0 ? optind : 1;
    current_index = -1;
    first_nonopt = -1;
    optarg = NULL;
}

static int find_short_option(const char *optstring, char c) {
    const char *p = optstring;
    while (*p) {
        if (*p == c) return (int)(p - optstring);
        p++;
    }
    return -1;
}

static int parse_long_option(int argc, char * const argv[], const struct option *longopts, int *longindex) {
    char *arg = argv[current_index] + 2;
    char *value = strchr(arg, '=');
    size_t name_len = value ? (size_t)(value - arg) : strlen(arg);

    if (value) *value++ = '\0';

    for (int i = 0; longopts && longopts[i].name; i++) {
        if (strlen(longopts[i].name) == name_len && strncmp(longopts[i].name, arg, name_len) == 0) {
            if (longindex) *longindex = i;

            if (longopts[i].has_arg == required_argument) {
                if (value && *value) {
                    optarg = value;
                    scan_index = current_index + 1;
                } else if (current_index + 1 < argc) {
                    optarg = argv[current_index + 1];
                    scan_index = current_index + 2;
                } else {
                    scan_index = current_index + 1;
                    current_index = -1;
                    optopt = longopts[i].val;
                    return '?';
                }
            } else if (longopts[i].has_arg == optional_argument) {
                optarg = value;
                scan_index = current_index + 1;
            } else {
                if (value) {
                    scan_index = current_index + 1;
                    current_index = -1;
                    optopt = longopts[i].val;
                    return '?';
                }
                scan_index = current_index + 1;
            }

            current_index = -1;
            if (longopts[i].flag) {
                *longopts[i].flag = longopts[i].val;
                return 0;
            }
            return longopts[i].val;
        }
    }

    scan_index = current_index + 1;
    current_index = -1;
    return '?';
}

int getopt_long(int argc, char * const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    optarg = NULL;

    if (optreset) {
        optreset = 0;
        reset_parser_state();
    }

    if (optind <= 0) optind = 1;
    if (scan_index <= 0) scan_index = optind;

    while (current_index == -1) {
        if (scan_index >= argc) {
            optind = first_nonopt >= 0 ? first_nonopt : scan_index;
            return -1;
        }

        char *arg = argv[scan_index];
        if (!arg || arg[0] != '-' || arg[1] == '\0') {
            if (first_nonopt < 0) first_nonopt = scan_index;
            scan_index++;
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            scan_index++;
            optind = first_nonopt >= 0 ? first_nonopt : scan_index;
            return -1;
        }

        current_index = scan_index;
        if (arg[1] == '-') {
            return parse_long_option(argc, argv, longopts, longindex);
        }
        short_index = 1;
    }

    char *arg = argv[current_index];
    char c = arg[short_index++];
    int idx = find_short_option(optstring, c);
    if (idx < 0) {
        if (arg[short_index] == '\0') {
            scan_index = current_index + 1;
            current_index = -1;
            short_index = 0;
        }
        optopt = c;
        return '?';
    }

    if (optstring[idx + 1] == ':') {
        if (arg[short_index] != '\0') {
            optarg = &arg[short_index];
            scan_index = current_index + 1;
            current_index = -1;
            short_index = 0;
        } else if (current_index + 1 < argc) {
            optarg = argv[current_index + 1];
            scan_index = current_index + 2;
            current_index = -1;
            short_index = 0;
        } else {
            scan_index = current_index + 1;
            current_index = -1;
            short_index = 0;
            optopt = c;
            return '?';
        }
    } else if (arg[short_index] == '\0') {
        scan_index = current_index + 1;
        current_index = -1;
        short_index = 0;
    }

    return c;
}

#endif
