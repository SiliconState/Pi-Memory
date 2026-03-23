/*
 * pi-memory — durable agent memory store
 *
 * SQLite-backed CLI. No runtime deps beyond libsqlite3.
 * Single binary. Works 6 months from now. Works 6 years from now.
 *
 * Build:   make           (Unix)
 *          cl /O2 ...     (MSVC)
 * Install: make install   (copies to ~/.pi/memory/pi-memory)
 *
 * Usage:
 *   pi-memory log decision <title> --choice <str> [--context <str>]
 *                                  [--rationale <str>] [--alternatives <str>]
 *                                  [--consequences <str>] [--tags <str>]
 *                                  [--project <proj>]
 *
 *   pi-memory log finding  <content> [--source <str>] [--category <str>]
 *                                    [--confidence verified|assumption|unverified]
 *                                    [--tags <str>] [--project <proj>]
 *
 *   pi-memory log lesson   <what_failed> [--why <str>] [--fix <str>]
 *                                        [--tags <str>] [--project <proj>]
 *
 *   pi-memory log entity   <name> [--type <str>] [--description <str>]
 *                                 [--notes <str>] [--project <proj>]
 *
 *   pi-memory query   [--project <proj>] [--type decision|finding|lesson|entity] [--limit <n>]
 *   pi-memory search  <keyword> [--project <proj>] [--limit <n>]
 *   pi-memory recent  [--n <n>]
 *   pi-memory state   <project> [--phase <str>] [--summary <str>] [--next <str>]
 *   pi-memory export  [--project <proj>] [--format md|json]
 *   pi-memory init    [<project>] [--file <path>]
 *   pi-memory sync    <file> [--project <proj>] [--limit <n>]
 *   pi-memory projects
 *
 * Project auto-detection (used when --project is omitted):
 *   1. PI_MEMORY_PROJECT env var
 *   2. git remote get-url origin  →  repo name
 *   3. basename of current working directory
 *   4. "global"
 *
 * Platforms: macOS, Linux, Windows (MSVC, MinGW, Clang-CL)
 */

/* compat.h provides cross-platform shims (getopt, mkdir, home dir, etc.) */
#include "compat.h"

#include <ctype.h>
#include <errno.h>
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
extern int optreset;
#endif
#endif

#define VERSION       "2.2.0"
#define SCHEMA_VERSION 3
#define MAX_PATH      2048
#define MAX_BUF       8192
#define MAX_JSON_VAL  65536
#define MAX_LINE      (64 * 1024 * 1024)   /* 64MB max JSONL line */
#define DEFAULT_LIMIT 20
#define DEFAULT_N     10

/* ─────────────────────────────────────────────────────────────────
   Schema
   ───────────────────────────────────────────────────────────────── */

static const char *SCHEMA =
    "CREATE TABLE IF NOT EXISTS decisions ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
    "  project      TEXT NOT NULL DEFAULT 'global',"
    "  title        TEXT NOT NULL,"
    "  context      TEXT,"           /* why was this decision needed */
    "  choice       TEXT NOT NULL,"  /* what was decided */
    "  rationale    TEXT,"           /* why this over alternatives */
    "  alternatives TEXT,"           /* what else was considered */
    "  consequences TEXT,"           /* what this opens/closes */
    "  tags         TEXT,"
    "  status       TEXT NOT NULL DEFAULT 'active'"  /* active | superseded */
    ");"
    "CREATE TABLE IF NOT EXISTS findings ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
    "  project      TEXT NOT NULL DEFAULT 'global',"
    "  source       TEXT,"           /* URL, file, experiment */
    "  category     TEXT,"           /* pi-capabilities, smb-pain, competitive... */
    "  content      TEXT NOT NULL,"
    "  confidence   TEXT NOT NULL DEFAULT 'assumption',"  /* verified | assumption | unverified */
    "  tags         TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS lessons ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
    "  project      TEXT NOT NULL DEFAULT 'global',"
    "  what_failed  TEXT NOT NULL,"
    "  why          TEXT,"
    "  fix          TEXT,"
    "  tags         TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS project_state ("
    "  project        TEXT PRIMARY KEY,"
    "  updated_at     TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
    "  phase          TEXT,"
    "  summary        TEXT,"
    "  next_actions   TEXT,"            /* pipe-separated list */
    "  session_count  INTEGER DEFAULT 0,"
    "  total_tokens   INTEGER DEFAULT 0,"
    "  total_cost     REAL    DEFAULT 0,"
    "  last_model     TEXT,"
    "  last_provider  TEXT,"
    "  last_session_id TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS entities ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
    "  updated_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
    "  project      TEXT NOT NULL DEFAULT 'global',"
    "  name         TEXT NOT NULL,"
    "  type         TEXT,"           /* tool | service | concept | framework | person */
    "  description  TEXT,"
    "  notes        TEXT,"
    "  UNIQUE(project, name)"
    ");"
    "CREATE TABLE IF NOT EXISTS sessions ("
    "  id             TEXT PRIMARY KEY,"     /* session UUID from header */
    "  project        TEXT NOT NULL,"
    "  cwd            TEXT,"
    "  session_file   TEXT,"
    "  started_at     TEXT,"
    "  ended_at       TEXT,"
    "  model          TEXT,"
    "  provider       TEXT,"
    "  total_tokens   INTEGER DEFAULT 0,"
    "  total_cost     REAL    DEFAULT 0,"
    "  message_count  INTEGER DEFAULT 0,"
    "  user_count     INTEGER DEFAULT 0,"
    "  assistant_count INTEGER DEFAULT 0,"
    "  tool_count     INTEGER DEFAULT 0,"
    "  compaction_count INTEGER DEFAULT 0,"
    "  ingested_at    TEXT DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))"
    ");";

/* ─────────────────────────────────────────────────────────────────
   Utilities
   ───────────────────────────────────────────────────────────────── */

static void get_memory_dir(char *out, size_t size) {
    const char *home = pi_get_home();
    snprintf(out, size, "%s/.pi/memory", home);
}

static void get_db_path(char *out, size_t size) {
    char dir[MAX_PATH];
    get_memory_dir(dir, sizeof(dir));
    snprintf(out, size, "%s/memory.db", dir);
}

static int ensure_dir(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
        tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (PI_IS_SEP(*p)) {
            char saved = *p;
            *p = '\0';
            if (pi_mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = saved;
        }
    }
    if (pi_mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int exec_sql(sqlite3 *db, const char *sql, const char *what) {
    char *errmsg = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &errmsg) == SQLITE_OK) return 0;
    fprintf(stderr, "error: %s failed: %s\n", what, errmsg ? errmsg : sqlite3_errmsg(db));
    sqlite3_free(errmsg);
    return -1;
}

static int db_user_version(sqlite3 *db, int *out) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "error: cannot read schema version: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return 0;
    }

    fprintf(stderr, "error: cannot read schema version: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
}

static int set_db_user_version(sqlite3 *db, int version) {
    char sql[64];
    snprintf(sql, sizeof(sql), "PRAGMA user_version=%d;", version);
    return exec_sql(db, sql, "schema version update");
}

static int table_has_column(sqlite3 *db, const char *table, const char *column) {
    char sql[128];
    sqlite3_stmt *stmt = NULL;
    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "error: cannot inspect table %s: %s\n", table, sqlite3_errmsg(db));
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name && strcmp(name, column) == 0) {
            sqlite3_finalize(stmt);
            return 1;
        }
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int ensure_column(sqlite3 *db, const char *table, const char *column, const char *sql) {
    int has_column = table_has_column(db, table, column);
    if (has_column < 0) return -1;
    if (has_column) return 0;
    return exec_sql(db, sql, column);
}

static sqlite3 *open_db(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    get_memory_dir(dir, sizeof(dir));
    get_db_path(path, sizeof(path));

    if (ensure_dir(dir) != 0) {
        fprintf(stderr, "error: cannot create %s: %s\n", dir, strerror(errno));
        return NULL;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        fprintf(stderr, "error: cannot open db at %s: %s\n", path,
            db ? sqlite3_errmsg(db) : "sqlite3_open failed");
        sqlite3_close(db);
        return NULL;
    }

    /* Set these BEFORE any writes so concurrent opens don't deadlock */
    sqlite3_busy_timeout(db, 5000);
    if (exec_sql(db, "PRAGMA foreign_keys=ON;", "foreign_keys pragma") != 0) {
        sqlite3_close(db);
        return NULL;
    }

    int schema_version = 0;
    if (db_user_version(db, &schema_version) != 0) {
        sqlite3_close(db);
        return NULL;
    }
    int original_schema_version = schema_version;

    if (schema_version < SCHEMA_VERSION && exec_sql(db, "PRAGMA journal_mode=WAL;", "WAL mode") != 0) {
        sqlite3_close(db);
        return NULL;
    }

    if (exec_sql(db, SCHEMA, "schema init") != 0) {
        sqlite3_close(db);
        return NULL;
    }

    if (schema_version < 1) {
        if (ensure_column(db, "decisions", "session_id", "ALTER TABLE decisions ADD COLUMN session_id TEXT;") != 0 ||
            ensure_column(db, "findings",  "session_id", "ALTER TABLE findings ADD COLUMN session_id TEXT;") != 0 ||
            ensure_column(db, "lessons",   "session_id", "ALTER TABLE lessons ADD COLUMN session_id TEXT;") != 0 ||
            ensure_column(db, "entities",  "session_id", "ALTER TABLE entities ADD COLUMN session_id TEXT;") != 0) {
            sqlite3_close(db);
            return NULL;
        }
        schema_version = 1;
    }

    if (schema_version < 2) {
        if (ensure_column(db, "project_state", "session_count",   "ALTER TABLE project_state ADD COLUMN session_count INTEGER DEFAULT 0;") != 0 ||
            ensure_column(db, "project_state", "total_tokens",    "ALTER TABLE project_state ADD COLUMN total_tokens INTEGER DEFAULT 0;") != 0 ||
            ensure_column(db, "project_state", "total_cost",      "ALTER TABLE project_state ADD COLUMN total_cost REAL DEFAULT 0;") != 0 ||
            ensure_column(db, "project_state", "last_model",      "ALTER TABLE project_state ADD COLUMN last_model TEXT;") != 0 ||
            ensure_column(db, "project_state", "last_provider",   "ALTER TABLE project_state ADD COLUMN last_provider TEXT;") != 0 ||
            ensure_column(db, "project_state", "last_session_id", "ALTER TABLE project_state ADD COLUMN last_session_id TEXT;") != 0) {
            sqlite3_close(db);
            return NULL;
        }
        schema_version = 2;
    }

    if (schema_version < 3) {
        if (exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_decisions_session_id ON decisions(session_id);", "idx_decisions_session_id") != 0 ||
            exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_findings_session_id ON findings(session_id);", "idx_findings_session_id") != 0 ||
            exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_lessons_session_id ON lessons(session_id);", "idx_lessons_session_id") != 0 ||
            exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_sessions_project ON sessions(project);", "idx_sessions_project") != 0) {
            sqlite3_close(db);
            return NULL;
        }
        schema_version = 3;
    }

    if (original_schema_version != SCHEMA_VERSION && set_db_user_version(db, SCHEMA_VERSION) != 0) {
        sqlite3_close(db);
        return NULL;
    }

    return db;
}

/* Safe column text — never returns NULL */
static const char *col(sqlite3_stmt *s, int i) {
    const unsigned char *v = sqlite3_column_text(s, i);
    return v ? (const char *)v : "";
}

/* Truncate src into dst with "..." if too long */
static void trunc_str(const char *src, char *dst, size_t max) {
    if (!src || !*src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len < max) {
        memcpy(dst, src, len + 1);
    } else {
        memcpy(dst, src, max - 4);
        memcpy(dst + max - 4, "...", 4);
    }
}

/* JSON-escape a string into dst. Writes JSON null for NULL src. */
static void json_str(const char *src, char *dst, size_t size) {
    if (!src || !*src) { snprintf(dst, size, "null"); return; }
    size_t j = 0;
    dst[j++] = '"';
    for (const char *p = src; *p && j < size - 4; p++) {
        switch (*p) {
            case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
            case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
            case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
            case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
            case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
            default:   dst[j++] = *p;
        }
    }
    dst[j++] = '"';
    dst[j]   = '\0';
}

/* ─────────────────────────────────────────────────────────────────
   Minimal JSON field extraction (no library — targeted strstr + escapes)
   Only handles the specific JSONL shapes produced by Pi sessions.
   ───────────────────────────────────────────────────────────────── */

/*
 * Read a dynamic-length line from FILE. Caller must free().
 * Returns NULL on EOF / error.
 */
static char *read_dyn_line(FILE *f, size_t *out_len) {
    size_t cap = 4096, pos = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    int c;
    while ((c = fgetc(f)) != EOF && c != '\n') {
        if (pos + 2 >= cap) {
            cap *= 2;
            if (cap > (size_t)MAX_LINE) { free(buf); return NULL; }
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[pos++] = (char)c;
    }
    if (pos == 0 && c == EOF) { free(buf); return NULL; }
    buf[pos] = '\0';
    if (out_len) *out_len = pos;
    return buf;
}

/*
 * Extract a JSON string value for a given key.
 * Searches for  "key":"..."  and decodes escapes into buf.
 * start_after: if non-NULL, only search after this position in line.
 * Returns 0 on success, -1 if not found.
 */
static int jx_str(const char *line, const char *key, char *buf, size_t bufsize) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *hit = strstr(line, pattern);
    if (!hit) {
        /* Try with space after colon:  "key": "..." */
        snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
        hit = strstr(line, pattern);
        if (!hit) return -1;
    }
    /* advance past the pattern to the opening quote's content */
    hit += strlen(pattern);

    size_t i = 0;
    while (*hit && i < bufsize - 1) {
        if (*hit == '\\' && *(hit + 1)) {
            hit++;
            switch (*hit) {
                case '"':  buf[i++] = '"';  break;
                case '\\': buf[i++] = '\\'; break;
                case 'n':  buf[i++] = '\n'; break;
                case 'r':  buf[i++] = '\r'; break;
                case 't':  buf[i++] = '\t'; break;
                case '/':  buf[i++] = '/';  break;
                default:   buf[i++] = *hit; break;
            }
        } else if (*hit == '"') {
            break;
        } else {
            buf[i++] = *hit;
        }
        hit++;
    }
    buf[i] = '\0';
    return 0;
}

/*
 * Extract a JSON integer value:  "key":123
 * Returns 0 on success, -1 if not found.
 */
static int jx_int(const char *line, const char *key, long *out) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *hit = strstr(line, pattern);
    if (!hit) return -1;
    hit += strlen(pattern);
    while (*hit == ' ') hit++;
    if (*hit == '-' || (*hit >= '0' && *hit <= '9')) {
        *out = strtol(hit, NULL, 10);
        return 0;
    }
    return -1;
}

/*
 * Extract a JSON double value:  "key":1.23
 */
static int jx_double(const char *line, const char *key, double *out) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *hit = strstr(line, pattern);
    if (!hit) return -1;
    hit += strlen(pattern);
    while (*hit == ' ') hit++;
    if (*hit == '-' || *hit == '.' || (*hit >= '0' && *hit <= '9')) {
        *out = strtod(hit, NULL);
        return 0;
    }
    return -1;
}

/*
 * Extract a JSON double value, but only search after `after_key` is found.
 * Used to disambiguate keys like "total" that appear in multiple nested objects.
 * e.g., jx_double_after(line, "cost", "total", &val) finds "cost":{..."total":X}
 */
static int jx_double_after(const char *line, const char *after_key, const char *key, double *out) {
    char apattern[256];
    snprintf(apattern, sizeof(apattern), "\"%s\":", after_key);
    const char *base = strstr(line, apattern);
    if (!base) return -1;
    return jx_double(base, key, out);
}

/*
 * Extract project name from a cwd path (basename).
 */
static void project_from_cwd(const char *cwd, char *out, size_t size) {
    if (!cwd || !*cwd) { snprintf(out, size, "global"); return; }
    const char *p = cwd + strlen(cwd);
    while (p > cwd && !PI_IS_SEP(*(p - 1))) p--;
    snprintf(out, size, "%s", p);
}

/* ─────────────────────────────────────────────────────────────────
   Project auto-detection
   ───────────────────────────────────────────────────────────────── */

/*
 * Returns the project name for the current context.
 * Resolution order:
 *   1. PI_MEMORY_PROJECT env var
 *   2. git remote origin URL  →  last path component, no ".git"
 *   3. basename of cwd
 *   4. "global"
 * Result is cached in a static buffer — safe to call many times.
 * Never returns NULL.
 */
static const char *auto_project(void) {
    static char buf[256] = {0};
    if (buf[0]) return buf;

    /* 1. env var */
    const char *env = getenv("PI_MEMORY_PROJECT");
    if (env && *env) {
        snprintf(buf, sizeof(buf), "%s", env);
        return buf;
    }

    /* 2. git remote origin */
#ifdef _WIN32
    FILE *fp = popen("git remote get-url origin 2>NUL", "r");
#else
    FILE *fp = popen("git remote get-url origin 2>/dev/null", "r");
#endif
    if (fp) {
        char raw[512] = {0};
        if (fgets(raw, sizeof(raw), fp) && raw[0]) {
            /* strip trailing newline / .git */
            size_t len = strlen(raw);
            if (len > 0 && raw[len-1] == '\n') raw[--len] = '\0';
            if (len > 4 && strcmp(raw + len - 4, ".git") == 0) raw[len-=4] = '\0';
            /* take component after last '/', '\', or ':' */
            char *p = raw + len;
            while (p > raw && *(p-1) != '/' && *(p-1) != '\\' && *(p-1) != ':') p--;
            if (*p) {
                snprintf(buf, sizeof(buf), "%s", p);
                pclose(fp);
                return buf;
            }
        }
        pclose(fp);
    }

    /* 3. basename of cwd */
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd))) {
        /* find last path separator (/ or \) */
        char *p = cwd + strlen(cwd);
        while (p > cwd && !PI_IS_SEP(*(p-1))) p--;
        if (*p) {
            snprintf(buf, sizeof(buf), "%s", p);
            return buf;
        }
    }

    /* 4. fallback */
    snprintf(buf, sizeof(buf), "global");
    return buf;
}

/* getopt() keeps global state; reset before each subcommand parse. */
static void reset_getopt_state(void) {
#if defined(_WIN32)
    /* Our bundled getopt resets correctly with optind=1 */
    optind = 1;
#elif defined(__GLIBC__)
    /* glibc resets correctly when optind is set to 0. */
    optind = 0;
#else
    optind = 1;
#endif

#if !defined(_WIN32) && (defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__))
    optreset = 1;
#endif
}

/* ─────────────────────────────────────────────────────────────────
   Print helpers
   ───────────────────────────────────────────────────────────────── */

static void print_decision(sqlite3_stmt *s) {
    /* cols: 0=id 1=created_at 2=project 3=title 4=context 5=choice
             6=rationale 7=alternatives 8=consequences 9=tags 10=status 11=session_id */
    char buf[300];
    printf("\n  [DECISION #%d] [%s] %s  status=%s\n",
        sqlite3_column_int(s, 0), col(s,2), col(s,1), col(s,10));
    printf("  Title:       %s\n", col(s,3));
    printf("  Choice:      %s\n", col(s,5));
    trunc_str(col(s,6), buf, 140); if (buf[0]) printf("  Rationale:   %s\n", buf);
    trunc_str(col(s,4), buf, 140); if (buf[0]) printf("  Context:     %s\n", buf);
    if (col(s,7)[0]) printf("  Alternatives: %s\n", col(s,7));
    if (col(s,8)[0]) printf("  Consequences: %s\n", col(s,8));
    if (col(s,9)[0]) printf("  Tags:        %s\n",  col(s,9));
    if (col(s,11)[0]) printf("  Session:     %s\n",  col(s,11));
    printf("  ─────────────────────────────────────────────────────────\n");
}

static void print_finding(sqlite3_stmt *s) {
    /* cols: 0=id 1=created_at 2=project 3=source 4=category
             5=content 6=confidence 7=tags 8=session_id */
    char buf[300];
    printf("\n  [FINDING #%d] [%s] %s  [%s]\n",
        sqlite3_column_int(s, 0), col(s,2), col(s,1), col(s,6));
    if (col(s,4)[0]) printf("  Category:    %s\n", col(s,4));
    if (col(s,3)[0]) printf("  Source:      %s\n", col(s,3));
    trunc_str(col(s,5), buf, 240); printf("  Content:     %s\n", buf);
    if (col(s,7)[0]) printf("  Tags:        %s\n", col(s,7));
    if (col(s,8)[0]) printf("  Session:     %s\n", col(s,8));
    printf("  ─────────────────────────────────────────────────────────\n");
}

static void print_lesson(sqlite3_stmt *s) {
    /* cols: 0=id 1=created_at 2=project 3=what_failed 4=why 5=fix 6=tags 7=session_id */
    char buf[300];
    printf("\n  [LESSON #%d] [%s] %s\n",
        sqlite3_column_int(s, 0), col(s,2), col(s,1));
    printf("  Failed:      %s\n", col(s,3));
    trunc_str(col(s,4), buf, 140); if (buf[0]) printf("  Why:         %s\n", buf);
    trunc_str(col(s,5), buf, 140); if (buf[0]) printf("  Fix:         %s\n", buf);
    if (col(s,6)[0]) printf("  Tags:        %s\n", col(s,6));
    if (col(s,7)[0]) printf("  Session:     %s\n", col(s,7));
    printf("  ─────────────────────────────────────────────────────────\n");
}

static void print_entity(sqlite3_stmt *s) {
    /* cols: 0=id 1=created_at 2=updated_at 3=project 4=name 5=type 6=description 7=notes 8=session_id */
    printf("\n  [ENTITY #%d] [%s] %s  (%s)\n",
        sqlite3_column_int(s, 0), col(s,3), col(s,1), col(s,5)[0] ? col(s,5) : "untyped");
    printf("  Name:        %s\n", col(s,4));
    if (col(s,6)[0]) printf("  Description: %s\n", col(s,6));
    if (col(s,7)[0]) printf("  Notes:       %s\n", col(s,7));
    if (col(s,8)[0]) printf("  Session:     %s\n", col(s,8));
    printf("  ─────────────────────────────────────────────────────────\n");
}

/* ─────────────────────────────────────────────────────────────────
   LOG DECISION
   ───────────────────────────────────────────────────────────────── */

static int cmd_log_decision(int argc, char *argv[]) {
    char *title        = NULL;
    char *project      = (char*)auto_project();
    char *choice       = NULL;
    char *context      = NULL;
    char *rationale    = NULL;
    char *alternatives = NULL;
    char *consequences = NULL;
    char *tags         = NULL;
    char *session_id   = NULL;

    static struct option opts[] = {
        {"project",      required_argument, 0, 'P'},
        {"choice",       required_argument, 0, 'c'},
        {"context",      required_argument, 0, 'C'},
        {"rationale",    required_argument, 0, 'r'},
        {"alternatives", required_argument, 0, 'a'},
        {"consequences", required_argument, 0, 'q'},
        {"tags",         required_argument, 0, 't'},
        {"session-id",   required_argument, 0, 'S'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:c:C:r:a:q:t:S:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project      = optarg; break;
            case 'c': choice       = optarg; break;
            case 'C': context      = optarg; break;
            case 'r': rationale    = optarg; break;
            case 'a': alternatives = optarg; break;
            case 'q': consequences = optarg; break;
            case 't': tags         = optarg; break;
            case 'S': session_id   = optarg; break;
            default:
                fprintf(stderr, "error: unknown option.\n"); return 1;
        }
    }
    if (optind < argc) title = argv[optind];

    if (!title  || !*title)  { fprintf(stderr, "error: <title> is required.\nUsage: pi-memory log decision <title> --choice <str> [options]\n"); return 1; }
    if (!choice || !*choice) { fprintf(stderr, "error: --choice is required.\n"); return 1; }

    sqlite3 *db = open_db();
    if (!db) return 1;

    const char *sql =
        "INSERT INTO decisions"
        " (project, title, context, choice, rationale, alternatives, consequences, tags, session_id)"
        " VALUES (?,?,?,?,?,?,?,?,?)";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, project,      -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, title,        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, context,      -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, choice,       -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, rationale,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, alternatives, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, consequences, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, tags,         -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, session_id,   -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "error: insert failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);
    printf("+ decision  [%s] %s\n", project, title);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   LOG FINDING
   ───────────────────────────────────────────────────────────────── */

static int cmd_log_finding(int argc, char *argv[]) {
    char *content    = NULL;
    char *project    = (char*)auto_project();
    char *source     = NULL;
    char *category   = NULL;
    char *confidence = "assumption";
    char *tags       = NULL;
    char *session_id = NULL;

    static struct option opts[] = {
        {"project",    required_argument, 0, 'P'},
        {"source",     required_argument, 0, 's'},
        {"category",   required_argument, 0, 'g'},
        {"confidence", required_argument, 0, 'd'},
        {"tags",       required_argument, 0, 't'},
        {"session-id", required_argument, 0, 'S'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:s:g:d:t:S:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project    = optarg; break;
            case 's': source     = optarg; break;
            case 'g': category   = optarg; break;
            case 'd': confidence = optarg; break;
            case 't': tags       = optarg; break;
            case 'S': session_id = optarg; break;
            default: return 1;
        }
    }
    if (optind < argc) content = argv[optind];
    if (!content || !*content) { fprintf(stderr, "error: <content> is required.\nUsage: pi-memory log finding <content> [options]\n"); return 1; }

    sqlite3 *db = open_db();
    if (!db) return 1;

    const char *sql =
        "INSERT INTO findings (project, source, category, content, confidence, tags, session_id)"
        " VALUES (?,?,?,?,?,?,?)";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, project,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, source,     -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, category,   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, content,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, confidence, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, tags,       -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, session_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc != SQLITE_DONE) { fprintf(stderr, "error: insert failed.\n"); return 1; }
    printf("+ finding   [%s] %s\n", project, category ? category : "uncategorized");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   LOG LESSON
   ───────────────────────────────────────────────────────────────── */

static int cmd_log_lesson(int argc, char *argv[]) {
    char *what_failed = NULL;
    char *project     = (char*)auto_project();
    char *why         = NULL;
    char *fix         = NULL;
    char *tags        = NULL;
    char *session_id  = NULL;

    static struct option opts[] = {
        {"project",    required_argument, 0, 'P'},
        {"why",        required_argument, 0, 'w'},
        {"fix",        required_argument, 0, 'f'},
        {"tags",       required_argument, 0, 't'},
        {"session-id", required_argument, 0, 'S'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:w:f:t:S:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project    = optarg; break;
            case 'w': why        = optarg; break;
            case 'f': fix        = optarg; break;
            case 't': tags       = optarg; break;
            case 'S': session_id = optarg; break;
            default: return 1;
        }
    }
    if (optind < argc) what_failed = argv[optind];
    if (!what_failed || !*what_failed) { fprintf(stderr, "error: <what_failed> is required.\n"); return 1; }

    sqlite3 *db = open_db();
    if (!db) return 1;

    const char *sql =
        "INSERT INTO lessons (project, what_failed, why, fix, tags, session_id) VALUES (?,?,?,?,?,?)";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, project,     -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, what_failed, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, why,         -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fix,         -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, tags,        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, session_id,  -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc != SQLITE_DONE) { fprintf(stderr, "error: insert failed.\n"); return 1; }
    printf("+ lesson    [%s] %s\n", project, what_failed);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   LOG ENTITY
   ───────────────────────────────────────────────────────────────── */

static int cmd_log_entity(int argc, char *argv[]) {
    char *name        = NULL;
    char *project     = (char*)auto_project();
    char *type        = NULL;
    char *description = NULL;
    char *notes       = NULL;
    char *session_id  = NULL;

    static struct option opts[] = {
        {"project",     required_argument, 0, 'P'},
        {"type",        required_argument, 0, 'T'},
        {"description", required_argument, 0, 'D'},
        {"notes",       required_argument, 0, 'n'},
        {"session-id",  required_argument, 0, 'S'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:T:D:n:S:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project     = optarg; break;
            case 'T': type        = optarg; break;
            case 'D': description = optarg; break;
            case 'n': notes       = optarg; break;
            case 'S': session_id  = optarg; break;
            default: return 1;
        }
    }
    if (optind < argc) name = argv[optind];
    if (!name || !*name) { fprintf(stderr, "error: <name> is required.\n"); return 1; }

    sqlite3 *db = open_db();
    if (!db) return 1;

    const char *sql =
        "INSERT INTO entities (project, name, type, description, notes, session_id)"
        " VALUES (?,?,?,?,?,?)"
        " ON CONFLICT(project, name) DO UPDATE SET"
        "   type        = COALESCE(excluded.type,        entities.type),"
        "   description = COALESCE(excluded.description, entities.description),"
        "   notes       = COALESCE(excluded.notes,       entities.notes),"
        "   session_id  = COALESCE(excluded.session_id,  entities.session_id),"
        "   updated_at  = strftime('%Y-%m-%dT%H:%M:%SZ','now')";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, project,     -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name,        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, type,        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, description, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, notes,       -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, session_id,  -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc != SQLITE_DONE) { fprintf(stderr, "error: upsert failed.\n"); return 1; }
    printf("+ entity    [%s] %s (%s)\n", project, name, type ? type : "untyped");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   QUERY
   ───────────────────────────────────────────────────────────────── */

static int cmd_query(int argc, char *argv[]) {
    char *project = (char*)auto_project();
    char *type = NULL;
    char *session_id = NULL;
    int   limit = DEFAULT_LIMIT;

    static struct option opts[] = {
        {"project",    required_argument, 0, 'P'},
        {"type",       required_argument, 0, 'T'},
        {"limit",      required_argument, 0, 'l'},
        {"session-id", required_argument, 0, 'S'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:T:l:S:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project = optarg;       break;
            case 'T': type = optarg;          break;
            case 'l': limit = atoi(optarg);   break;
            case 'S': session_id = optarg;    break;
            default: return 1;
        }
    }

    sqlite3 *db = open_db();
    if (!db) return 1;

    int found = 0;
    sqlite3_stmt *stmt;

    if (!type || strcmp(type, "decision") == 0) {
        if (session_id) {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,project,title,context,choice,rationale,alternatives,consequences,tags,status,session_id"
                " FROM decisions WHERE project=? AND session_id=? ORDER BY created_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 3, limit);
        } else {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,project,title,context,choice,rationale,alternatives,consequences,tags,status,session_id"
                " FROM decisions WHERE project=? ORDER BY created_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 2, limit);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) { print_decision(stmt); found++; }
        sqlite3_finalize(stmt);
    }

    if (!type || strcmp(type, "finding") == 0) {
        if (session_id) {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,project,source,category,content,confidence,tags,session_id"
                " FROM findings WHERE project=? AND session_id=? ORDER BY created_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 3, limit);
        } else {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,project,source,category,content,confidence,tags,session_id"
                " FROM findings WHERE project=? ORDER BY created_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 2, limit);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) { print_finding(stmt); found++; }
        sqlite3_finalize(stmt);
    }

    if (!type || strcmp(type, "lesson") == 0) {
        if (session_id) {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,project,what_failed,why,fix,tags,session_id"
                " FROM lessons WHERE project=? AND session_id=? ORDER BY created_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 3, limit);
        } else {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,project,what_failed,why,fix,tags,session_id"
                " FROM lessons WHERE project=? ORDER BY created_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 2, limit);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) { print_lesson(stmt); found++; }
        sqlite3_finalize(stmt);
    }

    if (!type || strcmp(type, "entity") == 0) {
        if (session_id) {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,updated_at,project,name,type,description,notes,session_id"
                " FROM entities WHERE project=? AND session_id=? ORDER BY updated_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 3, limit);
        } else {
            sqlite3_prepare_v2(db,
                "SELECT id,created_at,updated_at,project,name,type,description,notes,session_id"
                " FROM entities WHERE project=? ORDER BY updated_at DESC LIMIT ?",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
            sqlite3_bind_int (stmt, 2, limit);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) { print_entity(stmt); found++; }
        sqlite3_finalize(stmt);
    }

    if (!found) {
        if (session_id)
            printf("  no records for project '%s' with session '%s'\n", project, session_id);
        else
            printf("  no records for project '%s'\n", project);
    }

    sqlite3_close(db);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   SEARCH — full-text across all tables, all projects by default
   ───────────────────────────────────────────────────────────────── */

static int cmd_search(int argc, char *argv[]) {
    char *keyword = NULL;
    char *project = NULL;   /* NULL = search all projects */
    char *session_id = NULL;
    int   limit = DEFAULT_LIMIT;

    static struct option opts[] = {
        {"project",    required_argument, 0, 'P'},
        {"limit",      required_argument, 0, 'l'},
        {"session-id", required_argument, 0, 'S'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:l:S:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project = optarg;       break;
            case 'l': limit   = atoi(optarg); break;
            case 'S': session_id = optarg;    break;
            default: return 1;
        }
    }
    if (optind < argc) keyword = argv[optind];
    if (!keyword || !*keyword) {
        fprintf(stderr, "error: <keyword> is required.\nUsage: pi-memory search <keyword> [--project <proj>] [--session-id <id>]\n");
        return 1;
    }

    char pattern[MAX_BUF];
    snprintf(pattern, sizeof(pattern), "%%%s%%", keyword);

    sqlite3 *db = open_db();
    if (!db) return 1;

    printf("  search: \"%s\"%s%s\n\n",
        keyword,
        project ? "" : "  (all projects)",
        session_id ? "  [session filtered]" : "");

    int found = 0;
    sqlite3_stmt *stmt;

    /* decisions */
    if (project && session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,title,context,choice,rationale,alternatives,consequences,tags,status,session_id"
            " FROM decisions WHERE project=? AND session_id=?"
            " AND (title LIKE ? OR choice LIKE ? OR rationale LIKE ? OR context LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 8, limit);
    } else if (project) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,title,context,choice,rationale,alternatives,consequences,tags,status,session_id"
            " FROM decisions WHERE project=?"
            " AND (title LIKE ? OR choice LIKE ? OR rationale LIKE ? OR context LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 7, limit);
    } else if (session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,title,context,choice,rationale,alternatives,consequences,tags,status,session_id"
            " FROM decisions WHERE session_id=?"
            " AND (title LIKE ? OR choice LIKE ? OR rationale LIKE ? OR context LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 7, limit);
    } else {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,title,context,choice,rationale,alternatives,consequences,tags,status,session_id"
            " FROM decisions"
            " WHERE title LIKE ? OR choice LIKE ? OR rationale LIKE ? OR context LIKE ? OR tags LIKE ?"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 6, limit);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) { print_decision(stmt); found++; }
    sqlite3_finalize(stmt);

    /* findings */
    if (project && session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,source,category,content,confidence,tags,session_id"
            " FROM findings WHERE project=? AND session_id=?"
            " AND (content LIKE ? OR category LIKE ? OR source LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 7, limit);
    } else if (project) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,source,category,content,confidence,tags,session_id"
            " FROM findings WHERE project=?"
            " AND (content LIKE ? OR category LIKE ? OR source LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 6, limit);
    } else if (session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,source,category,content,confidence,tags,session_id"
            " FROM findings WHERE session_id=?"
            " AND (content LIKE ? OR category LIKE ? OR source LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 6, limit);
    } else {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,source,category,content,confidence,tags,session_id"
            " FROM findings WHERE content LIKE ? OR category LIKE ? OR source LIKE ? OR tags LIKE ?"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 5, limit);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) { print_finding(stmt); found++; }
    sqlite3_finalize(stmt);

    /* lessons */
    if (project && session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,what_failed,why,fix,tags,session_id"
            " FROM lessons WHERE project=? AND session_id=?"
            " AND (what_failed LIKE ? OR why LIKE ? OR fix LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 7, limit);
    } else if (project) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,what_failed,why,fix,tags,session_id"
            " FROM lessons WHERE project=?"
            " AND (what_failed LIKE ? OR why LIKE ? OR fix LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 6, limit);
    } else if (session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,what_failed,why,fix,tags,session_id"
            " FROM lessons WHERE session_id=?"
            " AND (what_failed LIKE ? OR why LIKE ? OR fix LIKE ? OR tags LIKE ?)"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 6, limit);
    } else {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,project,what_failed,why,fix,tags,session_id"
            " FROM lessons"
            " WHERE what_failed LIKE ? OR why LIKE ? OR fix LIKE ? OR tags LIKE ?"
            " ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 5, limit);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) { print_lesson(stmt); found++; }
    sqlite3_finalize(stmt);

    /* entities */
    if (project && session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,updated_at,project,name,type,description,notes,session_id"
            " FROM entities WHERE project=? AND session_id=?"
            " AND (name LIKE ? OR type LIKE ? OR description LIKE ? OR notes LIKE ?)"
            " ORDER BY updated_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 7, limit);
    } else if (project) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,updated_at,project,name,type,description,notes,session_id"
            " FROM entities WHERE project=?"
            " AND (name LIKE ? OR type LIKE ? OR description LIKE ? OR notes LIKE ?)"
            " ORDER BY updated_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 6, limit);
    } else if (session_id) {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,updated_at,project,name,type,description,notes,session_id"
            " FROM entities WHERE session_id=?"
            " AND (name LIKE ? OR type LIKE ? OR description LIKE ? OR notes LIKE ?)"
            " ORDER BY updated_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 6, limit);
    } else {
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,updated_at,project,name,type,description,notes,session_id"
            " FROM entities"
            " WHERE name LIKE ? OR type LIKE ? OR description LIKE ? OR notes LIKE ?"
            " ORDER BY updated_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 5, limit);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) { print_entity(stmt); found++; }
    sqlite3_finalize(stmt);

    printf("\n  %d result(s).\n", found);

    sqlite3_close(db);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   RECENT
   ───────────────────────────────────────────────────────────────── */

static int cmd_recent(int argc, char *argv[]) {
    int n = DEFAULT_N;

    static struct option opts[] = {
        {"n", required_argument, 0, 'n'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "n:", opts, NULL)) != -1) {
        if (opt == 'n') n = atoi(optarg);
    }

    sqlite3 *db = open_db();
    if (!db) return 1;

    const char *sql =
        "SELECT 'DECISION' AS kind, created_at, project, title AS summary, session_id FROM decisions"
        " UNION ALL"
        " SELECT 'FINDING', created_at, project, substr(content,1,72), session_id FROM findings"
        " UNION ALL"
        " SELECT 'LESSON', created_at, project, what_failed, session_id FROM lessons"
        " UNION ALL"
        " SELECT 'ENTITY', updated_at, project, name||' ('||COALESCE(type,'untyped')||')', session_id FROM entities"
        " ORDER BY created_at DESC LIMIT ?";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, n);

    printf("\n  %-10s %-12s %-18s %-12s %s\n",
        "type", "date", "project", "session", "summary");
    printf("  %-10s %-12s %-18s %-12s %s\n",
        "──────────", "────────────", "──────────────────", "────────────",
        "────────────────────────────────────────────────────");

    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char date[13], summary[80], sid[16] = "-";
        /* take only date portion of created_at */
        snprintf(date, sizeof(date), "%.10s", col(stmt, 1));
        trunc_str(col(stmt, 3), summary, sizeof(summary) - 1);
        if (col(stmt, 4)[0]) snprintf(sid, sizeof(sid), "%.8s", col(stmt, 4));
        printf("  %-10s %-12s %-18s %-12s %s\n",
            col(stmt, 0), date, col(stmt, 2), sid, summary);
        found++;
    }
    sqlite3_finalize(stmt);

    if (!found) printf("  no memories yet.\n");
    printf("\n");

    sqlite3_close(db);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   STATE
   ───────────────────────────────────────────────────────────────── */

static int cmd_state(int argc, char *argv[]) {
    char *project = NULL;
    char *phase   = NULL;
    char *summary = NULL;
    char *next    = NULL;   /* pipe-separated next actions */

    static struct option opts[] = {
        {"phase",   required_argument, 0, 'h'},
        {"summary", required_argument, 0, 's'},
        {"next",    required_argument, 0, 'n'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "h:s:n:", opts, NULL)) != -1) {
        switch (opt) {
            case 'h': phase   = optarg; break;
            case 's': summary = optarg; break;
            case 'n': next    = optarg; break;
            default: return 1;
        }
    }
    if (optind < argc) project = argv[optind];
    if (!project || !*project) {
        fprintf(stderr, "error: <project> is required.\n"
            "Usage: pi-memory state <project> [--phase <str>] [--summary <str>] [--next <str>]\n");
        return 1;
    }

    sqlite3 *db = open_db();
    if (!db) return 1;

    if (phase || summary || next) {
        /* UPSERT: only overwrite fields that are explicitly provided */
        const char *sql =
            "INSERT INTO project_state (project, phase, summary, next_actions)"
            " VALUES (?,?,?,?)"
            " ON CONFLICT(project) DO UPDATE SET"
            "   phase        = COALESCE(excluded.phase,        phase),"
            "   summary      = COALESCE(excluded.summary,      summary),"
            "   next_actions = COALESCE(excluded.next_actions, next_actions),"
            "   updated_at   = strftime('%Y-%m-%dT%H:%M:%SZ','now')";
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, phase,   -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, summary, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, next,    -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        printf("  state updated for '%s'\n", project);
    } else {
        /* READ */
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db,
            "SELECT project, updated_at, phase, summary, next_actions,"
            " session_count, total_tokens, total_cost, last_provider, last_model, last_session_id"
            " FROM project_state WHERE project=?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("\n  ══ State: %s ══\n", col(stmt, 0));
            printf("  Updated:  %s\n",  col(stmt, 1));
            printf("  Phase:    %s\n",  col(stmt, 2)[0] ? col(stmt, 2) : "(not set)");
            printf("  Summary:  %s\n",  col(stmt, 3)[0] ? col(stmt, 3) : "(not set)");
            if (col(stmt, 4)[0]) {
                printf("  Next:\n");
                char buf[MAX_BUF];
                snprintf(buf, sizeof(buf), "%s", col(stmt, 4));
                char *tok = strtok(buf, "|");
                while (tok) {
                    /* trim leading spaces */
                    while (*tok == ' ') tok++;
                    printf("    · %s\n", tok);
                    tok = strtok(NULL, "|");
                }
            }

            /* v2.1 rollup fields from ingested sessions */
            if (col(stmt, 5)[0] || col(stmt, 6)[0] || col(stmt, 7)[0]) {
                printf("  Sessions: %s  Tokens: %s  Cost: $%.2f\n",
                    col(stmt, 5)[0] ? col(stmt, 5) : "0",
                    col(stmt, 6)[0] ? col(stmt, 6) : "0",
                    col(stmt, 7)[0] ? sqlite3_column_double(stmt, 7) : 0.0);
            }
            if (col(stmt, 9)[0] || col(stmt, 8)[0]) {
                printf("  Last model: %s/%s\n",
                    col(stmt, 8)[0] ? col(stmt, 8) : "unknown",
                    col(stmt, 9)[0] ? col(stmt, 9) : "unknown");
            }
            if (col(stmt, 10)[0]) {
                printf("  Last session: %s\n", col(stmt, 10));
            }
            printf("\n");
        } else {
            printf("  no state for project '%s'\n", project);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   EXPORT
   ───────────────────────────────────────────────────────────────── */

static int cmd_export(int argc, char *argv[]) {
    char *project = (char*)auto_project();
    char *format  = "md";

    static struct option opts[] = {
        {"project", required_argument, 0, 'P'},
        {"format",  required_argument, 0, 'F'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:F:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project = optarg; break;
            case 'F': format  = optarg; break;
            default: return 1;
        }
    }

    sqlite3 *db = open_db();
    if (!db) return 1;

    sqlite3_stmt *stmt;
    char esc[MAX_BUF];

    if (strcmp(format, "json") == 0) {
        printf("{\"project\":\"%s\",\"decisions\":[", project);

        sqlite3_prepare_v2(db,
            "SELECT id,created_at,title,choice,rationale,context,tags,status,session_id"
            " FROM decisions WHERE project=? ORDER BY created_at",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        int first = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) printf(",");
            first = 0;
            json_str(col(stmt,2), esc, sizeof(esc));
            printf("{\"id\":%d,\"created_at\":\"%s\",\"title\":%s,",
                sqlite3_column_int(stmt,0), col(stmt,1), esc);
            json_str(col(stmt,3), esc, sizeof(esc)); printf("\"choice\":%s,", esc);
            json_str(col(stmt,4), esc, sizeof(esc)); printf("\"rationale\":%s,", esc);
            json_str(col(stmt,7), esc, sizeof(esc)); printf("\"status\":%s,", esc);
            json_str(col(stmt,8), esc, sizeof(esc)); printf("\"session_id\":%s}", esc);
        }
        sqlite3_finalize(stmt);

        printf("],\"findings\":[");
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,source,category,content,confidence,tags,session_id"
            " FROM findings WHERE project=? ORDER BY created_at",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        first = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) printf(",");
            first = 0;
            json_str(col(stmt,4), esc, sizeof(esc));
            printf("{\"id\":%d,\"created_at\":\"%s\",\"confidence\":\"%s\",\"category\":\"%s\",\"content\":%s,",
                sqlite3_column_int(stmt,0), col(stmt,1), col(stmt,5), col(stmt,3), esc);
            json_str(col(stmt,7), esc, sizeof(esc)); printf("\"session_id\":%s}", esc);
        }
        sqlite3_finalize(stmt);

        printf("],\"lessons\":[");
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,what_failed,why,fix,session_id FROM lessons WHERE project=? ORDER BY created_at",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        first = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) printf(",");
            first = 0;
            json_str(col(stmt,2), esc, sizeof(esc));
            printf("{\"id\":%d,\"created_at\":\"%s\",\"what_failed\":%s",
                sqlite3_column_int(stmt,0), col(stmt,1), esc);
            if (col(stmt,3)[0]) { json_str(col(stmt,3), esc, sizeof(esc)); printf(",\"why\":%s", esc); }
            if (col(stmt,4)[0]) { json_str(col(stmt,4), esc, sizeof(esc)); printf(",\"fix\":%s", esc); }
            if (col(stmt,5)[0]) { json_str(col(stmt,5), esc, sizeof(esc)); printf(",\"session_id\":%s", esc); }
            printf("}");
        }
        sqlite3_finalize(stmt);

        printf("],\"entities\":[");
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,name,type,description,notes,session_id"
            " FROM entities WHERE project=? ORDER BY name",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        first = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) printf(",");
            first = 0;
            json_str(col(stmt,2), esc, sizeof(esc));
            printf("{\"id\":%d,\"created_at\":\"%s\",\"name\":%s",
                sqlite3_column_int(stmt,0), col(stmt,1), esc);
            if (col(stmt,3)[0]) { json_str(col(stmt,3), esc, sizeof(esc)); printf(",\"type\":%s", esc); }
            if (col(stmt,4)[0]) { json_str(col(stmt,4), esc, sizeof(esc)); printf(",\"description\":%s", esc); }
            if (col(stmt,5)[0]) { json_str(col(stmt,5), esc, sizeof(esc)); printf(",\"notes\":%s", esc); }
            if (col(stmt,6)[0]) { json_str(col(stmt,6), esc, sizeof(esc)); printf(",\"session_id\":%s", esc); }
            printf("}");
        }
        sqlite3_finalize(stmt);
        printf("]}\n");

    } else {
        /* Markdown */
        printf("# Memory Export — %s\n\n", project);

        printf("## Decisions\n\n");
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,title,context,choice,rationale,alternatives,tags,status,session_id"
            " FROM decisions WHERE project=? ORDER BY created_at",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("### %s  `%s`\n*%s*\n\n", col(stmt,2), col(stmt,8), col(stmt,1));
            if (col(stmt,3)[0]) printf("**Context:** %s\n\n",      col(stmt,3));
            printf("**Choice:** %s\n\n", col(stmt,4));
            if (col(stmt,5)[0]) printf("**Rationale:** %s\n\n",    col(stmt,5));
            if (col(stmt,6)[0]) printf("**Alternatives:** %s\n\n", col(stmt,6));
            if (col(stmt,7)[0]) printf("*Tags: %s*\n\n",           col(stmt,7));
            if (col(stmt,9)[0]) printf("*Session: %s*\n\n",        col(stmt,9));
            printf("---\n\n");
        }
        sqlite3_finalize(stmt);

        printf("## Findings\n\n");
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,source,category,content,confidence,tags,session_id"
            " FROM findings WHERE project=? ORDER BY created_at",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("### [%s] %s\n*%s*  source: %s\n\n",
                col(stmt,5), col(stmt,3)[0] ? col(stmt,3) : "General",
                col(stmt,1), col(stmt,2)[0] ? col(stmt,2) : "unspecified");
            printf("%s\n\n", col(stmt,4));
            if (col(stmt,6)[0]) printf("*Tags: %s*\n\n", col(stmt,6));
            if (col(stmt,7)[0]) printf("*Session: %s*\n\n", col(stmt,7));
            printf("---\n\n");
        }
        sqlite3_finalize(stmt);

        printf("## Lessons\n\n");
        sqlite3_prepare_v2(db,
            "SELECT id,created_at,what_failed,why,fix,tags,session_id"
            " FROM lessons WHERE project=? ORDER BY created_at",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("### %s\n*%s*\n\n", col(stmt,2), col(stmt,1));
            if (col(stmt,3)[0]) printf("**Why:** %s\n\n", col(stmt,3));
            if (col(stmt,4)[0]) printf("**Fix:** %s\n\n", col(stmt,4));
            if (col(stmt,5)[0]) printf("*Tags: %s*\n\n",  col(stmt,5));
            if (col(stmt,6)[0]) printf("*Session: %s*\n\n", col(stmt,6));
            printf("---\n\n");
        }
        sqlite3_finalize(stmt);

        printf("## Entities\n\n");
        sqlite3_prepare_v2(db,
            "SELECT name,type,description,notes FROM entities WHERE project=? ORDER BY name",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("- **%s** (%s)", col(stmt,0), col(stmt,1)[0] ? col(stmt,1) : "untyped");
            if (col(stmt,2)[0]) printf(" — %s", col(stmt,2));
            if (col(stmt,3)[0]) printf(" *(notes: %s)*", col(stmt,3));
            printf("\n");
        }
        sqlite3_finalize(stmt);
        printf("\n");
    }

    sqlite3_close(db);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   SYNC — inject live memory into a markdown file via comment markers

   Place these markers anywhere in a .md file:

     <!-- pi-memory:decisions:start -->
     <!-- pi-memory:decisions:end -->

     <!-- pi-memory:state:start -->
     <!-- pi-memory:state:end -->

   Then run:
     pi-memory sync agents.md --project pi-framework

   Content between markers is replaced with live data from the DB.
   Run at session start. Run after every decision. Keep it wired.
   ───────────────────────────────────────────────────────────────── */

/* ── string builder ── */
typedef struct { char *buf; size_t len, cap; } SB;

static void sb_init(SB *s) {
    s->cap = 8192; s->len = 0;
    s->buf = malloc(s->cap);
    if (s->buf) s->buf[0] = '\0';
}

static void sb_grow(SB *s, size_t need) {
    while (s->len + need + 1 > s->cap) {
        s->cap *= 2;
        s->buf = realloc(s->buf, s->cap);
    }
}

static void sb_catn(SB *s, const char *p, size_t n) {
    if (!p || !n) return;
    sb_grow(s, n);
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static void sb_cat(SB *s, const char *p) {
    if (p && *p) sb_catn(s, p, strlen(p));
}

static void sb_free(SB *s) {
    free(s->buf);
    s->buf = NULL;
    s->len = s->cap = 0;
}

/* ── file I/O ── */
static long fread_all(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc((size_t)sz + 1);
    if (!*out) { fclose(f); return -1; }
    if (fread(*out, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(*out); return -1; }
    (*out)[sz] = '\0';
    fclose(f);
    return sz;
}

static int fwrite_all(const char *path, const char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

/* ── content generators ── */

static void gen_decisions(SB *out, sqlite3 *db, const char *project, int limit) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
        "SELECT id, created_at, title, choice, rationale, status"
        " FROM decisions WHERE project=? ORDER BY created_at DESC LIMIT ?",
        -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, limit);

    char line[MAX_BUF];
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        /* title */
        snprintf(line, sizeof(line), "- **%s**  \n", col(stmt, 2));
        sb_cat(out, line);
        /* choice */
        snprintf(line, sizeof(line), "  *Choice:* %s  \n", col(stmt, 3));
        sb_cat(out, line);
        /* rationale (optional) */
        if (col(stmt, 4)[0]) {
            snprintf(line, sizeof(line), "  *Why:* %s  \n", col(stmt, 4));
            sb_cat(out, line);
        }
        /* date + status */
        snprintf(line, sizeof(line), "  `%.10s` `%s`\n\n",
            col(stmt, 1), col(stmt, 5));
        sb_cat(out, line);
        found++;
    }
    sqlite3_finalize(stmt);
    if (!found) sb_cat(out, "*No decisions logged yet.*\n");
}

static void gen_state(SB *out, sqlite3 *db, const char *project) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
        "SELECT phase, summary, next_actions, updated_at,"
        " session_count, total_tokens, total_cost, last_provider, last_model, last_session_id"
        " FROM project_state WHERE project=?",
        -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);

    char line[MAX_BUF];
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(line, sizeof(line), "**Phase:** %s  \n",
            col(stmt, 0)[0] ? col(stmt, 0) : "(not set)");
        sb_cat(out, line);
        snprintf(line, sizeof(line), "**Updated:** %.10s  \n", col(stmt, 3));
        sb_cat(out, line);
        if (col(stmt, 1)[0]) {
            snprintf(line, sizeof(line), "**Summary:** %s  \n", col(stmt, 1));
            sb_cat(out, line);
        }
        if (col(stmt, 2)[0]) {
            sb_cat(out, "\n**Next:**\n");
            char buf[MAX_BUF];
            snprintf(buf, sizeof(buf), "%s", col(stmt, 2));
            char *tok = strtok(buf, "|");
            while (tok) {
                while (*tok == ' ') tok++;
                snprintf(line, sizeof(line), "- %s\n", tok);
                sb_cat(out, line);
                tok = strtok(NULL, "|");
            }
        }

        if (col(stmt, 4)[0] || col(stmt, 5)[0] || col(stmt, 6)[0]) {
            snprintf(line, sizeof(line), "\n**Session Stats:** %s sessions · %s tokens · $%.2f total cost\n",
                col(stmt, 4)[0] ? col(stmt, 4) : "0",
                col(stmt, 5)[0] ? col(stmt, 5) : "0",
                col(stmt, 6)[0] ? sqlite3_column_double(stmt, 6) : 0.0);
            sb_cat(out, line);
        }
        if (col(stmt, 8)[0] || col(stmt, 7)[0]) {
            snprintf(line, sizeof(line), "**Last Model:** %s/%s\n",
                col(stmt, 7)[0] ? col(stmt, 7) : "unknown",
                col(stmt, 8)[0] ? col(stmt, 8) : "unknown");
            sb_cat(out, line);
        }
        if (col(stmt, 9)[0]) {
            snprintf(line, sizeof(line), "**Last Session:** %s\n", col(stmt, 9));
            sb_cat(out, line);
        }
    } else {
        snprintf(line, sizeof(line), "*No state for project '%s'.*\n", project);
        sb_cat(out, line);
    }
    sqlite3_finalize(stmt);
}

static int cmd_sync(int argc, char *argv[]) {
    char *file    = NULL;
    char *project = (char*)auto_project();
    int   limit   = 10;

    static struct option opts[] = {
        {"project", required_argument, 0, 'P'},
        {"limit",   required_argument, 0, 'l'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:l:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project = optarg;       break;
            case 'l': limit   = atoi(optarg); break;
            default: return 1;
        }
    }
    if (optind < argc) file = argv[optind];
    if (!file || !*file) {
        fprintf(stderr,
            "error: <file> is required.\n"
            "Usage: pi-memory sync <file> [--project <proj>] [--limit <n>]\n"
            "Markers in file:\n"
            "  <!-- pi-memory:decisions:start --> ... <!-- pi-memory:decisions:end -->\n"
            "  <!-- pi-memory:state:start -->     ... <!-- pi-memory:state:end -->\n");
        return 1;
    }

    char *content = NULL;
    if (fread_all(file, &content) < 0) {
        fprintf(stderr, "error: cannot read '%s': %s\n", file, strerror(errno));
        return 1;
    }

    sqlite3 *db = open_db();
    if (!db) { free(content); return 1; }

    /* Section types we handle */
    const char *types[] = { "decisions", "state", NULL };

    SB result;
    sb_init(&result);

    const char *p    = content;
    int         hits = 0;

    while (*p) {
        /* Find the earliest start marker across all types */
        const char *found_at   = NULL;
        const char *found_type = NULL;

        for (int i = 0; types[i]; i++) {
            char sm[128];
            snprintf(sm, sizeof(sm), "<!-- pi-memory:%s:start -->", types[i]);
            const char *cand = strstr(p, sm);
            if (cand && (!found_at || cand < found_at)) {
                found_at   = cand;
                found_type = types[i];
            }
        }

        if (!found_at) {
            /* No more markers — copy the rest verbatim */
            sb_cat(&result, p);
            break;
        }

        /* Build start + end marker strings */
        char sm[128], em[128];
        snprintf(sm, sizeof(sm), "<!-- pi-memory:%s:start -->", found_type);
        snprintf(em, sizeof(em), "<!-- pi-memory:%s:end -->",   found_type);
        size_t sm_len = strlen(sm);
        size_t em_len = strlen(em);

        /* Copy everything up to and including the start marker */
        sb_catn(&result, p, (size_t)(found_at - p) + sm_len);
        sb_cat(&result, "\n");

        /* Generate replacement content */
        SB gen;
        sb_init(&gen);
        if      (strcmp(found_type, "decisions") == 0) gen_decisions(&gen, db, project, limit);
        else if (strcmp(found_type, "state")     == 0) gen_state    (&gen, db, project);
        sb_cat(&result, gen.buf);
        sb_free(&gen);

        /* Find the end marker and skip past it (or just past start if missing) */
        const char *end_at = strstr(found_at + sm_len, em);
        if (end_at) {
            sb_cat(&result, em);
            p = end_at + em_len;
        } else {
            p = found_at + sm_len;
        }
        hits++;
    }

    sqlite3_close(db);

    if (fwrite_all(file, result.buf, result.len) != 0) {
        fprintf(stderr, "error: cannot write '%s': %s\n", file, strerror(errno));
        sb_free(&result);
        free(content);
        return 1;
    }

    printf("  synced %d section(s) in %s  [project: %s]\n", hits, file, project);
    sb_free(&result);
    free(content);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   PROJECTS — list all projects that have ever written a record
   ───────────────────────────────────────────────────────────────── */

static int cmd_projects(void) {
    sqlite3 *db = open_db();
    if (!db) return 1;

    const char *sql =
        "SELECT project, COUNT(*) AS total,"
        "  SUM(CASE WHEN src='d' THEN 1 ELSE 0 END) AS decisions,"
        "  SUM(CASE WHEN src='f' THEN 1 ELSE 0 END) AS findings,"
        "  SUM(CASE WHEN src='l' THEN 1 ELSE 0 END) AS lessons,"
        "  MAX(ts) AS last_activity"
        " FROM ("
        "  SELECT project, 'd' AS src, created_at AS ts FROM decisions"
        "  UNION ALL"
        "  SELECT project, 'f', created_at FROM findings"
        "  UNION ALL"
        "  SELECT project, 'l', created_at FROM lessons"
        " )"
        " GROUP BY project"
        " ORDER BY last_activity DESC";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    printf("\n  %-24s %5s  %5s  %5s  %5s  %s\n",
        "project", "total", "dec", "find", "less", "last activity");
    printf("  %-24s %5s  %5s  %5s  %5s  %s\n",
        "────────────────────────", "─────", "─────", "─────", "─────",
        "─────────────────────");

    int found = 0;
    const char *cur = auto_project();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *proj = col(stmt, 0);
        int is_cur = strcmp(proj, cur) == 0;
        printf("  %-24s %5d  %5d  %5d  %5d  %.10s%s\n",
            proj,
            sqlite3_column_int(stmt, 1),
            sqlite3_column_int(stmt, 2),
            sqlite3_column_int(stmt, 3),
            sqlite3_column_int(stmt, 4),
            col(stmt, 5),
            is_cur ? "  ← current" : "");
        found++;
    }
    sqlite3_finalize(stmt);

    if (!found) printf("  no projects yet.\n");

    /* also check project_state for projects with only state set */
    sqlite3_prepare_v2(db,
        "SELECT project FROM project_state"
        " WHERE project NOT IN ("
        "  SELECT DISTINCT project FROM decisions UNION"
        "  SELECT DISTINCT project FROM findings  UNION"
        "  SELECT DISTINCT project FROM lessons"
        " )",
        -1, &stmt, NULL);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *proj = col(stmt, 0);
        printf("  %-24s %5s  %5s  %5s  %5s  (state only)\n",
            proj, "0", "0", "0", "0");
    }
    sqlite3_finalize(stmt);

    printf("\n  auto-detected project: %s\n\n", cur);
    sqlite3_close(db);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   INIT — bootstrap a MEMORY.md for any project
   ───────────────────────────────────────────────────────────────── */

static int cmd_init(int argc, char *argv[]) {
    char *project = (char*)auto_project();
    char *file    = "MEMORY.md";

    static struct option opts[] = {
        {"file", required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "f:", opts, NULL)) != -1) {
        if (opt == 'f') file = optarg;
    }
    /* optional positional: project name override */
    if (optind < argc) project = argv[optind];

    /* seed an empty project_state row if none exists */
    sqlite3 *db = open_db();
    if (!db) return 1;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO project_state (project) VALUES (?)",
        -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* check if file already exists */
    FILE *check = fopen(file, "r");
    if (check) {
        fclose(check);
        printf("  %s already exists — skipping write (not overwriting).\n", file);
        printf("  To sync live sections: pi-memory sync %s --project %s\n", file, project);
        return 0;
    }

    /* write the generic MEMORY.md template */
    FILE *f = fopen(file, "w");
    if (!f) {
        fprintf(stderr, "error: cannot create %s: %s\n", file, strerror(errno));
        return 1;
    }

    fprintf(f,
"# Memory — %s\n"
"\n"
"> Durable knowledge base for this project, backed by `pi-memory` (SQLite at `~/.pi/memory/memory.db`).\n"
">\n"
"> **Refresh live sections:**\n"
"> ```bash\n"
"> pi-memory sync %s --project %s\n"
"> ```\n"
"\n"
"---\n"
"\n"
"## Project State\n"
"\n"
"<!-- pi-memory:state:start -->\n"
"*No state yet. Set it with:*\n"
"```bash\n"
"pi-memory state %s --phase \"...\" --summary \"...\" --next \"task1|task2|task3\"\n"
"```\n"
"<!-- pi-memory:state:end -->\n"
"\n"
"---\n"
"\n"
"## Recent Decisions\n"
"\n"
"<!-- pi-memory:decisions:start -->\n"
"*No decisions yet.*\n"
"<!-- pi-memory:decisions:end -->\n"
"\n"
"---\n"
"\n"
"## Findings & Lessons\n"
"\n"
"Query live: `pi-memory query --project %s` or `pi-memory search <keyword>`\n"
"\n"
"---\n"
"\n"
"## Quick Reference\n"
"\n"
"```bash\n"
"# Log\n"
"pi-memory log decision \"Title\" --choice \"...\" --rationale \"...\" --project %s\n"
"pi-memory log finding  \"Fact\"  --category \"...\" --confidence verified --project %s\n"
"pi-memory log lesson   \"What broke\" --why \"...\" --fix \"...\" --project %s\n"
"pi-memory log entity   \"Name\" --type service --description \"...\" --project %s\n"
"\n"
"# Read\n"
"pi-memory state %s\n"
"pi-memory query   --project %s --limit 20\n"
"pi-memory search  <keyword> --project %s\n"
"pi-memory recent  --n 20\n"
"pi-memory export  --project %s --format md\n"
"\n"
"# Sync this file\n"
"pi-memory sync %s --project %s\n"
"\n"
"# List all projects\n"
"pi-memory projects\n"
"```\n",
        project,           /* title */
        file, project,     /* sync command in header */
        project,           /* state example */
        project,           /* findings query */
        project, project, project, project,  /* log examples */
        project,           /* state read */
        project, project,  /* query, search */
        project,           /* export */
        file, project      /* sync */
    );

    fclose(f);

    printf("  initialized: project='%s'  file='%s'\n", project, file);
    printf("\n  next steps:\n");
    printf("    pi-memory state %s --phase \"Week 1\" --summary \"...\" --next \"task1|task2\"\n", project);
    printf("    pi-memory sync  %s --project %s\n", file, project);
    printf("    pi-memory log decision \"First decision\" --choice \"...\" --project %s\n", project);
    printf("\n");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   INGEST-SESSION helpers (semantic extraction + rollups)
   ───────────────────────────────────────────────────────────────── */

#define MAX_AUTO_DECISIONS 256
#define MAX_AUTO_LESSONS   256
#define MAX_AUTO_ENTITIES  512

typedef struct {
    char title[240];
    char choice[1200];
    char rationale[600];
} AutoDecision;

typedef struct {
    char what_failed[240];
    char why[600];
    char fix[1200];
} AutoLesson;

typedef struct {
    char name[128];
    char type[32];
    char description[280];
} AutoEntity;

static int str_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

static int text_has_any_ci(const char *text, const char **needles) {
    if (!text) return 0;
    for (int i = 0; needles[i]; i++) {
        if (str_contains_ci(text, needles[i])) return 1;
    }
    return 0;
}

static void collapse_ws(const char *src, char *dst, size_t size) {
    if (!src || !*src) { dst[0] = '\0'; return; }
    size_t j = 0;
    int prev_space = 0;
    while (*src && j < size - 1) {
        unsigned char c = (unsigned char)*src++;
        if (isspace(c)) {
            if (!prev_space) dst[j++] = ' ';
            prev_space = 1;
        } else {
            dst[j++] = (char)c;
            prev_space = 0;
        }
    }
    dst[j] = '\0';
    while (j > 0 && dst[j - 1] == ' ') dst[--j] = '\0';
}

static void title_from_text(const char *text, char *out, size_t size) {
    char clean[1600];
    collapse_ws(text, clean, sizeof(clean));

    const char *p = clean;
    while (*p == '#' || *p == '-' || *p == '*' || *p == ':' || *p == ' ') p++;

    size_t i = 0;
    while (p[i] && i < size - 1) {
        if (p[i] == '.' || p[i] == '!' || p[i] == '?' || p[i] == '\n') break;
        out[i] = p[i];
        i++;
        if (i >= 96) break;
    }
    out[i] = '\0';

    if (!out[0]) snprintf(out, size, "Session-derived item");
}

static int is_noise_entity_token(const char *tok) {
    static const char *noise[] = {
        "The","This","That","With","From","Into","Then","When","Where","While",
        "User","Assistant","Tool","Error","Warning","Info","Summary","Project",
        "Token","Tokens","Cost","Model","Provider","Session","Compaction",
        "Choice","Rationale","Context","Next","Steps","Goal","Constraints",
        "Progress","Done","Blocked","Critical","Recent","Conversation",
        NULL
    };

    for (int i = 0; noise[i]; i++) {
        if (strcmp(tok, noise[i]) == 0) return 1;
    }
    return 0;
}

static int looks_entity_token(const char *tok) {
    size_t len = strlen(tok);
    if (len < 4 || len > 80) return 0;
    if (is_noise_entity_token(tok)) return 0;

    int upper = 0, lower = 0, digit = 0;
    for (const char *p = tok; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isupper(c)) upper = 1;
        else if (islower(c)) lower = 1;
        else if (isdigit(c)) digit = 1;
        else if (c != '_' && c != '-') return 0;
    }

    if (upper && lower) return 1;       /* CamelCase / mixed case */
    if (upper && digit) return 1;       /* e.g. ERC20 */
    if (strstr(tok, "Service") || strstr(tok, "Factory") || strstr(tok, "Manager") ||
        strstr(tok, "Client")  || strstr(tok, "Bridge")  || strstr(tok, "Treasury") ||
        strstr(tok, "Extension") || strstr(tok, "Provider") || strstr(tok, "Contract"))
        return 1;

    return 0;
}

static int add_auto_entity(AutoEntity *arr, int *count, const char *name, const char *type, const char *desc) {
    if (!name || !*name || *count >= MAX_AUTO_ENTITIES) return 0;

    for (int i = 0; i < *count; i++) {
        if (strcmp(arr[i].name, name) == 0) return 0;
    }

    snprintf(arr[*count].name, sizeof(arr[*count].name), "%s", name);
    snprintf(arr[*count].type, sizeof(arr[*count].type), "%s", type ? type : "concept");
    snprintf(arr[*count].description, sizeof(arr[*count].description), "%s", desc ? desc : "Auto-extracted from session");
    (*count)++;
    return 1;
}

static int add_auto_decision(AutoDecision *arr, int *count, const char *title, const char *choice, const char *rationale) {
    if (!title || !*title || !choice || !*choice || *count >= MAX_AUTO_DECISIONS) return 0;

    for (int i = 0; i < *count; i++) {
        if (strcmp(arr[i].title, title) == 0 && strcmp(arr[i].choice, choice) == 0) return 0;
    }

    snprintf(arr[*count].title, sizeof(arr[*count].title), "%s", title);
    snprintf(arr[*count].choice, sizeof(arr[*count].choice), "%s", choice);
    snprintf(arr[*count].rationale, sizeof(arr[*count].rationale), "%s", rationale ? rationale : "Auto-extracted from session patterns");
    (*count)++;
    return 1;
}

static int add_auto_lesson(AutoLesson *arr, int *count, const char *what_failed, const char *why, const char *fix) {
    if (!what_failed || !*what_failed || !fix || !*fix || *count >= MAX_AUTO_LESSONS) return 0;

    for (int i = 0; i < *count; i++) {
        if (strcmp(arr[i].what_failed, what_failed) == 0 && strcmp(arr[i].fix, fix) == 0) return 0;
    }

    snprintf(arr[*count].what_failed, sizeof(arr[*count].what_failed), "%s", what_failed);
    snprintf(arr[*count].why, sizeof(arr[*count].why), "%s", why ? why : "Auto-inferred from session error/fix sequence");
    snprintf(arr[*count].fix, sizeof(arr[*count].fix), "%s", fix);
    (*count)++;
    return 1;
}

static void extract_entities_from_text(const char *text, AutoEntity *entities, int *entity_count) {
    if (!text || !*text) return;

    char token[96];
    size_t t = 0;
    for (const char *p = text; ; p++) {
        unsigned char c = (unsigned char)*p;
        int keep = isalnum(c) || c == '_' || c == '-';

        if (keep && t < sizeof(token) - 1) {
            token[t++] = (char)c;
        } else {
            if (t > 0) {
                token[t] = '\0';
                if (looks_entity_token(token)) {
                    add_auto_entity(entities, entity_count, token, "concept", "Auto-extracted from conversation text");
                }
                t = 0;
            }
            if (!*p) break;
        }
    }
}

static int update_project_rollup(sqlite3 *db, const char *project) {
    sqlite3_stmt *agg;
    sqlite3_prepare_v2(db,
        "SELECT COUNT(*), COALESCE(SUM(total_tokens),0), COALESCE(SUM(total_cost),0)"
        " FROM sessions WHERE project=?",
        -1, &agg, NULL);
    sqlite3_bind_text(agg, 1, project, -1, SQLITE_STATIC);

    int session_count = 0;
    long total_tokens = 0;
    double total_cost = 0;
    if (sqlite3_step(agg) == SQLITE_ROW) {
        session_count = sqlite3_column_int(agg, 0);
        total_tokens = (long)sqlite3_column_int64(agg, 1);
        total_cost = sqlite3_column_double(agg, 2);
    }
    sqlite3_finalize(agg);

    sqlite3_stmt *latest;
    sqlite3_prepare_v2(db,
        "SELECT model, provider, id FROM sessions"
        " WHERE project=? ORDER BY ended_at DESC, started_at DESC LIMIT 1",
        -1, &latest, NULL);
    sqlite3_bind_text(latest, 1, project, -1, SQLITE_STATIC);

    const char *last_model = "";
    const char *last_provider = "";
    const char *last_session_id = "";
    if (sqlite3_step(latest) == SQLITE_ROW) {
        last_model = col(latest, 0);
        last_provider = col(latest, 1);
        last_session_id = col(latest, 2);
    }

    char model_copy[256], provider_copy[256], session_copy[256];
    snprintf(model_copy, sizeof(model_copy), "%s", last_model);
    snprintf(provider_copy, sizeof(provider_copy), "%s", last_provider);
    snprintf(session_copy, sizeof(session_copy), "%s", last_session_id);
    sqlite3_finalize(latest);

    sqlite3_stmt *up;
    sqlite3_prepare_v2(db,
        "INSERT INTO project_state (project, session_count, total_tokens, total_cost,"
        " last_model, last_provider, last_session_id)"
        " VALUES (?,?,?,?,?,?,?)"
        " ON CONFLICT(project) DO UPDATE SET"
        "  session_count=excluded.session_count,"
        "  total_tokens=excluded.total_tokens,"
        "  total_cost=excluded.total_cost,"
        "  last_model=excluded.last_model,"
        "  last_provider=excluded.last_provider,"
        "  last_session_id=excluded.last_session_id,"
        "  updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now')",
        -1, &up, NULL);
    sqlite3_bind_text(up, 1, project, -1, SQLITE_STATIC);
    sqlite3_bind_int(up, 2, session_count);
    sqlite3_bind_int64(up, 3, total_tokens);
    sqlite3_bind_double(up, 4, total_cost);
    sqlite3_bind_text(up, 5, model_copy, -1, SQLITE_STATIC);
    sqlite3_bind_text(up, 6, provider_copy, -1, SQLITE_STATIC);
    sqlite3_bind_text(up, 7, session_copy, -1, SQLITE_STATIC);

    int rc = sqlite3_step(up);
    sqlite3_finalize(up);
    return rc == SQLITE_DONE ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────────────
   INGEST-SESSION — parse a Pi session .jsonl and extract metadata
   ───────────────────────────────────────────────────────────────── */

static int cmd_ingest_session(int argc, char *argv[]) {
    char *filepath = NULL;
    char *project_override = NULL;
    int   dry_run = 0;

    static struct option opts[] = {
        {"project", required_argument, 0, 'P'},
        {"dry-run", no_argument,       0, 'n'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:n", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project_override = optarg; break;
            case 'n': dry_run = 1;               break;
            default: return 1;
        }
    }
    if (optind < argc) filepath = argv[optind];
    if (!filepath || !*filepath) {
        fprintf(stderr,
            "error: <path.jsonl> is required.\n"
            "Usage: pi-memory ingest-session <path.jsonl> [--project <proj>] [--dry-run]\n");
        return 1;
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s': %s\n", filepath, strerror(errno));
        return 1;
    }

    /* ── Accumulators ── */
    char session_id[256]   = {0};
    char cwd[MAX_PATH]     = {0};
    char project[256]      = {0};
    char started_at[64]    = {0};
    char ended_at[64]      = {0};
    char model[256]        = {0};
    char provider[256]     = {0};
    long total_tokens      = 0;
    double total_cost      = 0.0;
    int  message_count     = 0;
    int  user_count        = 0;
    int  assistant_count   = 0;
    int  tool_count        = 0;
    int  compaction_count  = 0;
    int  line_num          = 0;

    /* ── Semantic extraction buckets ── */
    AutoDecision decisions[MAX_AUTO_DECISIONS];
    int decision_count = 0;
    AutoLesson lessons[MAX_AUTO_LESSONS];
    int lesson_count = 0;
    AutoEntity entities[MAX_AUTO_ENTITIES];
    int entity_count = 0;
    char recent_error[600] = {0};

    /* ── Compaction summaries to store ── */
    #define MAX_COMPACTIONS 128
    char *compaction_summaries[MAX_COMPACTIONS];
    int   comp_idx = 0;

    /* ── Parse line by line ── */
    size_t len;
    char *line;
    char typebuf[64];

    while ((line = read_dyn_line(f, &len)) != NULL) {
        line_num++;

        if (jx_str(line, "type", typebuf, sizeof(typebuf)) != 0) {
            free(line);
            continue;
        }

        if (strcmp(typebuf, "session") == 0 && line_num == 1) {
            jx_str(line, "id", session_id, sizeof(session_id));
            jx_str(line, "cwd", cwd, sizeof(cwd));
            jx_str(line, "timestamp", started_at, sizeof(started_at));

            if (project_override) snprintf(project, sizeof(project), "%s", project_override);
            else project_from_cwd(cwd, project, sizeof(project));

        } else if (strcmp(typebuf, "message") == 0) {
            char role[32] = {0};
            char text[MAX_JSON_VAL] = {0};
            jx_str(line, "role", role, sizeof(role));
            message_count++;
            jx_str(line, "timestamp", ended_at, sizeof(ended_at));

            /* best-effort first text extraction */
            int has_text = (jx_str(line, "text", text, sizeof(text)) == 0);

            if (strcmp(role, "user") == 0) {
                user_count++;
                if (has_text) {
                    extract_entities_from_text(text, entities, &entity_count);
                }
            } else if (strcmp(role, "assistant") == 0) {
                assistant_count++;

                long tok = 0;
                if (jx_int(line, "totalTokens", &tok) == 0) total_tokens += tok;
                double cost = 0.0;
                if (jx_double_after(line, "cost", "total", &cost) == 0) total_cost += cost;

                jx_str(line, "model", model, sizeof(model));
                jx_str(line, "provider", provider, sizeof(provider));

                if (has_text) {
                    const char *strong_decision_kw[] = {
                        "decision:", "we decided", "decided to", "chose to", "chosen approach",
                        "we will use", "we will adopt", NULL
                    };
                    const char *weak_decision_kw[] = {
                        "implemented", "created", "added", "migrated", "refactored", "updated", NULL
                    };
                    const char *reason_kw[] = {
                        "because", "so that", "to avoid", "for security", "for performance", "rationale", NULL
                    };
                    const char *fix_kw[] = {
                        "fixed", "resolved", "workaround", "patched", "changed", "updated", "solution", NULL
                    };

                    int strong = text_has_any_ci(text, strong_decision_kw);
                    int weak = text_has_any_ci(text, weak_decision_kw) && text_has_any_ci(text, reason_kw);
                    if ((strong || weak) && strlen(text) >= 48) {
                        char title[240];
                        char choice[1200];
                        title_from_text(text, title, sizeof(title));
                        collapse_ws(text, choice, sizeof(choice));
                        add_auto_decision(decisions, &decision_count, title, choice,
                            "Auto-extracted from assistant decision language");
                    }

                    if (recent_error[0] && text_has_any_ci(text, fix_kw)) {
                        char fix[1200];
                        collapse_ws(text, fix, sizeof(fix));
                        add_auto_lesson(lessons, &lesson_count, recent_error,
                            "Error surfaced in tools/logs during the session",
                            fix);
                        recent_error[0] = '\0';
                    }

                    extract_entities_from_text(text, entities, &entity_count);
                }

            } else if (strcmp(role, "toolResult") == 0) {
                tool_count++;

                char tool_name[128] = {0};
                jx_str(line, "toolName", tool_name, sizeof(tool_name));
                if (tool_name[0]) {
                    char desc[280];
                    snprintf(desc, sizeof(desc), "Pi tool used in session (%s)", tool_name);
                    add_auto_entity(entities, &entity_count, tool_name, "tool", desc);
                }

                if (strstr(line, "\"isError\":true") || strstr(line, "\"isError\": true")) {
                    char errtxt[MAX_JSON_VAL] = {0};
                    jx_str(line, "text", errtxt, sizeof(errtxt));

                    char failed[240];
                    if (tool_name[0] && errtxt[0]) {
                        char clean[800];
                        collapse_ws(errtxt, clean, sizeof(clean));
                        snprintf(failed, sizeof(failed), "%s failed: %.180s", tool_name, clean);
                    } else if (tool_name[0]) {
                        snprintf(failed, sizeof(failed), "%s failed", tool_name);
                    } else {
                        snprintf(failed, sizeof(failed), "Tool execution failed");
                    }
                    snprintf(recent_error, sizeof(recent_error), "%s", failed);
                }
            }

        } else if (strcmp(typebuf, "compaction") == 0) {
            compaction_count++;
            jx_str(line, "timestamp", ended_at, sizeof(ended_at));

            if (comp_idx < MAX_COMPACTIONS) {
                char *summary = malloc(MAX_JSON_VAL);
                if (summary) {
                    if (jx_str(line, "summary", summary, MAX_JSON_VAL) == 0) {
                        compaction_summaries[comp_idx] = summary;
                        comp_idx++;
                    } else {
                        free(summary);
                    }
                }
            }

        } else if (strcmp(typebuf, "model_change") == 0) {
            jx_str(line, "modelId", model, sizeof(model));
            jx_str(line, "provider", provider, sizeof(provider));
            jx_str(line, "timestamp", ended_at, sizeof(ended_at));
        } else {
            jx_str(line, "timestamp", ended_at, sizeof(ended_at));
        }

        free(line);
    }
    fclose(f);

    if (!session_id[0]) {
        fprintf(stderr, "error: no session header found in '%s'\n", filepath);
        for (int i = 0; i < comp_idx; i++) free(compaction_summaries[i]);
        return 1;
    }

    printf("\n  ══ Session Ingest: %s ══\n", filepath);
    printf("  Session:      %.8s...  (%s)\n", session_id, session_id);
    printf("  Project:      %s\n", project);
    printf("  CWD:          %s\n", cwd);
    printf("  Started:      %s\n", started_at);
    printf("  Ended:        %s\n", ended_at);
    printf("  Model:        %s/%s\n", provider[0] ? provider : "unknown", model[0] ? model : "unknown");
    printf("  Messages:     %d total  (%d user, %d assistant, %d tool)\n",
        message_count, user_count, assistant_count, tool_count);
    printf("  Tokens:       %ld\n", total_tokens);
    printf("  Cost:         $%.4f\n", total_cost);
    printf("  Compactions:  %d\n", compaction_count);
    printf("  Extracted:    %d decisions, %d lessons, %d entities\n",
        decision_count, lesson_count, entity_count);

    if (dry_run) {
        printf("\n  [DRY RUN — nothing written]\n\n");
        for (int i = 0; i < comp_idx; i++) free(compaction_summaries[i]);
        return 0;
    }

    sqlite3 *db = open_db();
    if (!db) {
        for (int i = 0; i < comp_idx; i++) free(compaction_summaries[i]);
        return 1;
    }

    int inserted_decisions = 0;
    int inserted_lessons = 0;
    int upserted_entities = 0;
    int inserted_compactions = 0;

    /* Upsert session record */
    {
        const char *sql =
            "INSERT INTO sessions"
            " (id, project, cwd, session_file, started_at, ended_at,"
            "  model, provider, total_tokens, total_cost,"
            "  message_count, user_count, assistant_count, tool_count, compaction_count)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(id) DO UPDATE SET"
            "  ended_at=excluded.ended_at, model=excluded.model,"
            "  provider=excluded.provider, total_tokens=excluded.total_tokens,"
            "  total_cost=excluded.total_cost, message_count=excluded.message_count,"
            "  user_count=excluded.user_count, assistant_count=excluded.assistant_count,"
            "  tool_count=excluded.tool_count, compaction_count=excluded.compaction_count,"
            "  ingested_at=strftime('%Y-%m-%dT%H:%M:%SZ','now')";

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        sqlite3_bind_text  (stmt,  1, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt,  2, project,    -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt,  3, cwd,        -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt,  4, filepath,   -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt,  5, started_at, -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt,  6, ended_at,   -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt,  7, model,      -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt,  8, provider,   -1, SQLITE_STATIC);
        sqlite3_bind_int64 (stmt,  9, total_tokens);
        sqlite3_bind_double(stmt, 10, total_cost);
        sqlite3_bind_int   (stmt, 11, message_count);
        sqlite3_bind_int   (stmt, 12, user_count);
        sqlite3_bind_int   (stmt, 13, assistant_count);
        sqlite3_bind_int   (stmt, 14, tool_count);
        sqlite3_bind_int   (stmt, 15, compaction_count);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "  error: session insert failed: %s\n", sqlite3_errmsg(db));
        } else {
            printf("\n  + session record stored\n");
        }
    }

    /* Store compaction summaries (dedupe by source+content) */
    {
        char source[300];
        snprintf(source, sizeof(source), "session:%s", session_id);

        for (int i = 0; i < comp_idx; i++) {
            sqlite3_stmt *check;
            sqlite3_prepare_v2(db,
                "SELECT 1 FROM findings WHERE source=? AND category='compaction-summary' AND content=? LIMIT 1",
                -1, &check, NULL);
            sqlite3_bind_text(check, 1, source, -1, SQLITE_STATIC);
            sqlite3_bind_text(check, 2, compaction_summaries[i], -1, SQLITE_STATIC);
            int exists = (sqlite3_step(check) == SQLITE_ROW);
            sqlite3_finalize(check);
            if (exists) continue;

            sqlite3_stmt *stmt;
            sqlite3_prepare_v2(db,
                "INSERT INTO findings"
                " (project, source, category, content, confidence, tags, session_id)"
                " VALUES (?,?,?,?,?,?,?)",
                -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, project,                    -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, source,                     -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, "compaction-summary",       -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, compaction_summaries[i],    -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, "verified",                 -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 6, "auto-ingested,compaction", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 7, session_id,                 -1, SQLITE_STATIC);

            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc == SQLITE_DONE) inserted_compactions++;
        }
    }

    /* Store auto decisions */
    for (int i = 0; i < decision_count; i++) {
        sqlite3_stmt *check;
        sqlite3_prepare_v2(db,
            "SELECT 1 FROM decisions WHERE project=? AND session_id=? AND title=? AND choice=? LIMIT 1",
            -1, &check, NULL);
        sqlite3_bind_text(check, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(check, 2, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(check, 3, decisions[i].title, -1, SQLITE_STATIC);
        sqlite3_bind_text(check, 4, decisions[i].choice, -1, SQLITE_STATIC);
        int exists = (sqlite3_step(check) == SQLITE_ROW);
        sqlite3_finalize(check);
        if (exists) continue;

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db,
            "INSERT INTO decisions"
            " (project,title,context,choice,rationale,alternatives,consequences,tags,status,session_id)"
            " VALUES (?,?,?,?,?,?,?,?,?,?)",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, decisions[i].title, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, "Auto-extracted from session ingest", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, decisions[i].choice, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, decisions[i].rationale, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, "", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, "", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, "auto-ingested,decision", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, "active", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,10, session_id, -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_DONE) inserted_decisions++;
    }

    /* Store auto lessons */
    for (int i = 0; i < lesson_count; i++) {
        sqlite3_stmt *check;
        sqlite3_prepare_v2(db,
            "SELECT 1 FROM lessons WHERE project=? AND session_id=? AND what_failed=? AND fix=? LIMIT 1",
            -1, &check, NULL);
        sqlite3_bind_text(check, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(check, 2, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(check, 3, lessons[i].what_failed, -1, SQLITE_STATIC);
        sqlite3_bind_text(check, 4, lessons[i].fix, -1, SQLITE_STATIC);
        int exists = (sqlite3_step(check) == SQLITE_ROW);
        sqlite3_finalize(check);
        if (exists) continue;

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db,
            "INSERT INTO lessons (project, what_failed, why, fix, tags, session_id) VALUES (?,?,?,?,?,?)",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, lessons[i].what_failed, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, lessons[i].why, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, lessons[i].fix, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, "auto-ingested,lesson", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, session_id, -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_DONE) inserted_lessons++;
    }

    /* Store/upsert entities */
    for (int i = 0; i < entity_count; i++) {
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db,
            "INSERT INTO entities (project,name,type,description,notes,session_id)"
            " VALUES (?,?,?,?,?,?)"
            " ON CONFLICT(project, name) DO UPDATE SET"
            "   type=excluded.type, description=excluded.description, notes=excluded.notes,"
            "   session_id=excluded.session_id,"
            "   updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now')",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, entities[i].name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, entities[i].type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, entities[i].description, -1, SQLITE_STATIC);

        char notes[300];
        snprintf(notes, sizeof(notes), "Auto-extracted from session %.8s", session_id);
        sqlite3_bind_text(stmt, 5, notes, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, session_id, -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_DONE) upserted_entities++;
    }

    /* Roll up session totals into project_state */
    if (update_project_rollup(db, project) != 0) {
        fprintf(stderr, "  warning: failed to update project rollup\n");
    }

    printf("  + compaction summaries: %d inserted\n", inserted_compactions);
    printf("  + auto decisions:       %d inserted\n", inserted_decisions);
    printf("  + auto lessons:         %d inserted\n", inserted_lessons);
    printf("  + auto entities:        %d upserted\n", upserted_entities);

    sqlite3_close(db);

    for (int i = 0; i < comp_idx; i++) free(compaction_summaries[i]);

    printf("\n  done.\n\n");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   SESSIONS — list ingested sessions
   ───────────────────────────────────────────────────────────────── */

static int cmd_sessions(int argc, char *argv[]) {
    char *project = NULL;
    int   limit   = DEFAULT_LIMIT;

    static struct option opts[] = {
        {"project", required_argument, 0, 'P'},
        {"limit",   required_argument, 0, 'l'},
        {0, 0, 0, 0}
    };

    int opt;
    reset_getopt_state();
    while ((opt = getopt_long(argc, argv, "P:l:", opts, NULL)) != -1) {
        switch (opt) {
            case 'P': project = optarg;       break;
            case 'l': limit   = atoi(optarg); break;
            default: return 1;
        }
    }

    sqlite3 *db = open_db();
    if (!db) return 1;

    sqlite3_stmt *stmt;
    if (project) {
        sqlite3_prepare_v2(db,
            "SELECT id, project, model, provider, total_tokens, total_cost,"
            "  message_count, compaction_count, started_at, ended_at"
            " FROM sessions WHERE project=? ORDER BY started_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, project, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 2, limit);
    } else {
        sqlite3_prepare_v2(db,
            "SELECT id, project, model, provider, total_tokens, total_cost,"
            "  message_count, compaction_count, started_at, ended_at"
            " FROM sessions ORDER BY started_at DESC LIMIT ?",
            -1, &stmt, NULL);
        sqlite3_bind_int(stmt, 1, limit);
    }

    printf("\n  %-10s %-18s %-24s %8s %8s %5s %s\n",
        "session", "project", "model", "tokens", "cost", "msgs", "date");
    printf("  %-10s %-18s %-24s %8s %8s %5s %s\n",
        "──────────", "──────────────────", "────────────────────────",
        "────────", "────────", "─────", "──────────");

    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char short_id[11];
        snprintf(short_id, sizeof(short_id), "%.8s..", col(stmt, 0));
        printf("  %-10s %-18s %-24s %8ld   $%5.2f %5d  %.10s\n",
            short_id, col(stmt, 1),
            col(stmt, 2)[0] ? col(stmt, 2) : col(stmt, 3),
            (long)sqlite3_column_int64(stmt, 4),
            sqlite3_column_double(stmt, 5),
            sqlite3_column_int(stmt, 6),
            col(stmt, 8));
        found++;
    }
    sqlite3_finalize(stmt);

    if (!found) printf("  no sessions ingested yet.\n");
    printf("\n");

    sqlite3_close(db);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   USAGE
   ───────────────────────────────────────────────────────────────── */

static void usage(void) {
    printf(
        "pi-memory v" VERSION "  —  durable agent memory store\n"
        "  db:      ~/.pi/memory/memory.db\n"
        "  project: auto-detected from PI_MEMORY_PROJECT | git remote | cwd | 'global'\n\n"
        "  Commands:\n"
        "    init    [<project>] [--file <path>]   bootstrap MEMORY.md for any project\n"
        "    projects                              list all projects with record counts\n\n"
        "    log decision <title>       --choice <str>\n"
        "                               [--context <str>] [--rationale <str>]\n"
        "                               [--alternatives <str>] [--consequences <str>]\n"
        "                               [--tags <str>] [--project <proj>]\n"
        "                               [--session-id <id>]\n\n"
        "    log finding  <content>     [--source <str>] [--category <str>]\n"
        "                               [--confidence verified|assumption|unverified]\n"
        "                               [--tags <str>] [--project <proj>]\n"
        "                               [--session-id <id>]\n\n"
        "    log lesson   <what_failed> [--why <str>] [--fix <str>]\n"
        "                               [--tags <str>] [--project <proj>]\n"
        "                               [--session-id <id>]\n\n"
        "    log entity   <name>        [--type <str>] [--description <str>]\n"
        "                               [--notes <str>] [--project <proj>]\n"
        "                               [--session-id <id>]\n\n"
        "    query   [--project <proj>] [--type decision|finding|lesson|entity] [--limit <n>]\n"
        "    search  <keyword>          [--project <proj>] [--limit <n>]\n"
        "    recent  [--n <n>]\n"
        "    state   <project>          [--phase <str>] [--summary <str>] [--next <str>]\n"
        "    export  [--project <proj>] [--format md|json]\n"
        "    sync    <file>             [--project <proj>] [--limit <n>]\n\n"
        "    ingest-session <path.jsonl>   [--project <proj>] [--dry-run]\n"
        "    sessions                      [--project <proj>] [--limit <n>]\n\n"
        "  Defaults: --project <auto>  --limit 20  --n 10  --confidence assumption\n"
        "  --session-id links a memory entry to a specific Pi session UUID\n"
        "  --next takes pipe-separated values: \"task1|task2|task3\"\n"
        "  sync replaces <!-- pi-memory:decisions:start/end --> and\n"
        "       <!-- pi-memory:state:start/end --> markers in <file>\n"
        "  ingest-session parses a Pi .jsonl session file, stores session metadata\n"
        "    and compaction summaries as findings. Idempotent (re-ingest updates).\n"
        "  set PI_MEMORY_PROJECT=<name> to pin project in any shell or script\n"
    );
}

/* ─────────────────────────────────────────────────────────────────
   MAIN
   ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 0; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "log") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: 'log' requires a subcommand: decision | finding | lesson | entity\n");
            return 1;
        }
        const char *sub    = argv[2];
        int         sub_ac = argc - 2;
        char      **sub_av = argv + 2;

        if (strcmp(sub, "decision") == 0) return cmd_log_decision(sub_ac, sub_av);
        if (strcmp(sub, "finding")  == 0) return cmd_log_finding (sub_ac, sub_av);
        if (strcmp(sub, "lesson")   == 0) return cmd_log_lesson  (sub_ac, sub_av);
        if (strcmp(sub, "entity")   == 0) return cmd_log_entity  (sub_ac, sub_av);
        fprintf(stderr, "error: unknown log subcommand '%s'\n", sub);
        return 1;
    }

    if (strcmp(cmd, "init")            == 0) return cmd_init           (argc - 1, argv + 1);
    if (strcmp(cmd, "projects")       == 0) return cmd_projects();
    if (strcmp(cmd, "query")          == 0) return cmd_query          (argc - 1, argv + 1);
    if (strcmp(cmd, "search")         == 0) return cmd_search         (argc - 1, argv + 1);
    if (strcmp(cmd, "recent")         == 0) return cmd_recent         (argc - 1, argv + 1);
    if (strcmp(cmd, "state")          == 0) return cmd_state          (argc - 1, argv + 1);
    if (strcmp(cmd, "export")         == 0) return cmd_export         (argc - 1, argv + 1);
    if (strcmp(cmd, "sync")           == 0) return cmd_sync           (argc - 1, argv + 1);
    if (strcmp(cmd, "ingest-session") == 0) return cmd_ingest_session (argc - 1, argv + 1);
    if (strcmp(cmd, "sessions")       == 0) return cmd_sessions       (argc - 1, argv + 1);

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    }

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("pi-memory v" VERSION "\n");
        return 0;
    }

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage();
    return 1;
}
