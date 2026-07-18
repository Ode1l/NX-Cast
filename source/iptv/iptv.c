#include "iptv/iptv.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <libavformat/avformat.h>
#include <switch.h>

#include "iptv/fetch.h"
#include "iptv/xmltv.h"
#include "log/log.h"
#include "player/renderer.h"

#define IPTV_BASE_DIR "sdmc:/switch/NX-Cast"
#define IPTV_CACHE_DIR IPTV_ROOT_DIR "/cache"
#define IPTV_PLAYLIST_CACHE_DIR IPTV_CACHE_DIR "/playlists"
#define IPTV_EPG_CACHE_DIR IPTV_CACHE_DIR "/epg"
#define IPTV_LOGO_CACHE_DIR IPTV_CACHE_DIR "/logos"
#define IPTV_SOURCES_FILE IPTV_ROOT_DIR "/sources.tsv"
#define IPTV_FAVORITES_FILE IPTV_ROOT_DIR "/favorites.txt"
#define IPTV_RECENT_FILE IPTV_ROOT_DIR "/recent.txt"
#define IPTV_REMOTE_PLAYLIST_LIMIT (16U * 1024U * 1024U)
#define IPTV_XMLTV_LIMIT (64U * 1024U * 1024U)
#define IPTV_LOGO_LIMIT (4U * 1024U * 1024U)
#define IPTV_WORKER_STACK_SIZE 0x80000
#define IPTV_FALLBACK_NAME "IPTV Channel"

typedef struct
{
    IptvChannel *channels;
    int channel_count;
    IptvSource sources[IPTV_MAX_SOURCES];
    int source_count;
    char groups[IPTV_MAX_GROUPS][IPTV_GROUP_MAX];
    int group_count;
    int hls_stream_count;
    int logo_cached_count;
    int epg_channel_count;
} IptvCatalog;

static Mutex g_mutex;
static CondVar g_worker_cond;
static Thread g_worker_thread;
static bool g_sync_initialized;
static bool g_worker_started;
static bool g_worker_stop;
static bool g_refresh_all_requested;
static uint32_t g_refresh_source_requested;
static bool g_logo_requested;
static uint32_t g_logo_channel_id;
static char g_logo_url[IPTV_LOGO_URL_MAX];
static char g_logo_path[IPTV_PATH_MAX];

static IptvChannel *g_channels;
static int g_channel_count;
static IptvSource g_sources[IPTV_MAX_SOURCES];
static int g_source_count;
static char g_groups[IPTV_MAX_GROUPS][IPTV_GROUP_MAX];
static int g_group_count;
static int g_visible[IPTV_MAX_CHANNELS];
static int g_visible_count;
static int g_selected_index;
static int g_source_selected_index;
static int g_filter_index;
static uint32_t g_recent_ids[IPTV_MAX_RECENT];
static int g_recent_count;
static bool g_initialized;
static bool g_loaded;
static bool g_refreshing;
static uint32_t g_refreshing_source_id;
static int g_hls_stream_count;
static int g_logo_cached_count;
static int g_epg_channel_count;
static char g_search[IPTV_SEARCH_MAX];
static char g_status[IPTV_STATUS_MAX];
static char g_last_name[IPTV_NAME_MAX];
static char g_last_url[IPTV_URL_MAX];
static bool g_preinstalled_refresh_needed;
static int g_preinstalled_changed_count;

static void iptv_queue_selected_logo_locked(void);

static void iptv_copy(char *out, size_t out_size, const char *value)
{
    size_t length;

    if (!out || out_size == 0)
        return;
    if (!value)
        value = "";
    length = strlen(value);
    if (length >= out_size)
        length = out_size - 1;
    memmove(out, value, length);
    out[length] = '\0';
}

static char *iptv_trim(char *text)
{
    char *end;

    if (!text)
        return text;
    while (*text && isspace((unsigned char)*text))
        ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return text;
}

static uint32_t iptv_hash_bytes(uint32_t hash, const char *text)
{
    while (text && *text)
    {
        hash ^= (unsigned char)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t iptv_hash_string(const char *text)
{
    uint32_t hash = iptv_hash_bytes(2166136261u, text);
    return hash ? hash : 1u;
}

static uint32_t iptv_channel_id(uint32_t source_id, const char *tvg_id, const char *url)
{
    char source[16];
    uint32_t hash = 2166136261u;

    snprintf(source, sizeof(source), "%08x", source_id);
    hash = iptv_hash_bytes(hash, source);
    hash = iptv_hash_bytes(hash, "|");
    hash = iptv_hash_bytes(hash, tvg_id && tvg_id[0] ? tvg_id : url);
    return hash ? hash : 1u;
}

static bool iptv_path_exists(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && info.st_size > 0;
}

static time_t iptv_path_mtime(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 ? info.st_mtime : (time_t)0;
}

static void iptv_ensure_directories(void)
{
    mkdir(IPTV_BASE_DIR, 0777);
    mkdir(IPTV_ROOT_DIR, 0777);
    mkdir(IPTV_CACHE_DIR, 0777);
    mkdir(IPTV_PLAYLIST_CACHE_DIR, 0777);
    mkdir(IPTV_EPG_CACHE_DIR, 0777);
    mkdir(IPTV_LOGO_CACHE_DIR, 0777);
}

static bool iptv_has_m3u_extension(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    return dot && (strcasecmp(dot, ".m3u") == 0 || strcasecmp(dot, ".m3u8") == 0);
}

static bool iptv_url_is_remote(const char *url)
{
    return url && (strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0);
}

static bool iptv_url_has_query_token(const char *url, const char *token)
{
    size_t token_length;

    if (!url || !token)
        return false;
    token_length = strlen(token);
    for (const char *cursor = url; *cursor; ++cursor)
    {
        if (strncasecmp(cursor, token, token_length) == 0)
            return true;
    }
    return false;
}

bool iptv_url_looks_like_playlist(const char *url)
{
    const char *end;
    const char *path_start;
    size_t path_length;

    if (!url || !url[0])
        return false;

    end = strpbrk(url, "?#");
    if (!end)
        end = url + strlen(url);
    path_start = end;
    while (path_start > url && path_start[-1] != '/')
        --path_start;
    path_length = (size_t)(end - path_start);
    if ((path_length >= 4 && strncasecmp(end - 4, ".m3u", 4) == 0) ||
        (path_length >= 5 && strncasecmp(end - 5, ".m3u8", 5) == 0))
        return true;

    return iptv_url_has_query_token(url, "type=m3u") ||
           iptv_url_has_query_token(url, "format=m3u") ||
           iptv_url_has_query_token(url, "output=m3u") ||
           iptv_url_has_query_token(url, "playlist=m3u");
}

static bool iptv_url_has_scheme(const char *url)
{
    const char *separator;

    if (!url || !url[0])
        return false;
    if (strncasecmp(url, "sdmc:/", 6) == 0 || url[0] == '/')
        return true;
    separator = strstr(url, "://");
    if (!separator || separator == url)
        return false;
    for (const char *p = url; p < separator; ++p)
    {
        if (!isalnum((unsigned char)*p) && *p != '+' && *p != '-' && *p != '.')
            return false;
    }
    return true;
}

static bool iptv_url_is_playable(const char *url)
{
    if (!url || !url[0])
        return false;
    return strncasecmp(url, "http://", 7) == 0 ||
           strncasecmp(url, "https://", 8) == 0 ||
           strncasecmp(url, "rtsp://", 7) == 0 ||
           strncasecmp(url, "rtmp://", 7) == 0 ||
           strncasecmp(url, "udp://", 6) == 0 ||
           strncasecmp(url, "rtp://", 6) == 0 ||
           strncasecmp(url, "mms://", 6) == 0 ||
           strncasecmp(url, "file://", 7) == 0 ||
           strncasecmp(url, "sdmc:/", 6) == 0 ||
           url[0] == '/';
}

static void iptv_path_dirname(const char *path, char *out, size_t out_size)
{
    const char *slash;
    size_t length;

    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    slash = path ? strrchr(path, '/') : NULL;
    if (!slash)
        return;
    length = (size_t)(slash - path);
    if (length >= out_size)
        length = out_size - 1;
    memcpy(out, path, length);
    out[length] = '\0';
}

static void iptv_resolve_url(const char *base, const char *reference, char *out, size_t out_size)
{
    const char *authority;
    const char *path;
    char directory[IPTV_URL_MAX];

    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!reference || !reference[0])
        return;
    if (!base || !base[0])
    {
        iptv_copy(out, out_size, reference);
        return;
    }
    if (iptv_url_is_remote(base))
    {
        authority = strstr(base, "://");
        path = authority ? strchr(authority + 3, '/') : NULL;
        if (reference[0] == '/' && reference[1] == '/' && authority)
        {
            size_t scheme_length = (size_t)(authority - base);
            snprintf(out, out_size, "%.*s:%s", (int)scheme_length, base, reference);
            return;
        }
        if (reference[0] == '/' && authority)
        {
            size_t prefix = path ? (size_t)(path - base) : strlen(base);
            if (prefix >= out_size)
                prefix = out_size - 1;
            memcpy(out, base, prefix);
            out[prefix] = '\0';
            strncat(out, reference, out_size - strlen(out) - 1);
            return;
        }
    }
    if (iptv_url_has_scheme(reference))
    {
        iptv_copy(out, out_size, reference);
        return;
    }
    iptv_path_dirname(base, directory, sizeof(directory));
    if (directory[0])
        snprintf(out, out_size, "%s/%s", directory, reference);
    else
        iptv_copy(out, out_size, reference);
}

static bool iptv_contains_casefold(const char *haystack, const char *needle)
{
    size_t needle_length;

    if (!needle || !needle[0])
        return true;
    if (!haystack)
        return false;
    needle_length = strlen(needle);
    for (; *haystack; ++haystack)
    {
        if (strncasecmp(haystack, needle, needle_length) == 0)
            return true;
    }
    return false;
}

static void iptv_parse_attr(const char *line, const char *attribute, char *out, size_t out_size)
{
    size_t attribute_length;

    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!line || !attribute)
        return;
    attribute_length = strlen(attribute);
    for (const char *p = line; *p; ++p)
    {
        if (strncasecmp(p, attribute, attribute_length) != 0)
            continue;
        if (p != line && !isspace((unsigned char)p[-1]))
            continue;
        p += attribute_length;
        while (isspace((unsigned char)*p))
            ++p;
        if (*p != '=')
            continue;
        ++p;
        while (isspace((unsigned char)*p))
            ++p;
        char quote = (*p == '"' || *p == '\'') ? *p++ : '\0';
        size_t used = 0;
        while (*p && used + 1 < out_size)
        {
            if ((quote && *p == quote) || (!quote && (isspace((unsigned char)*p) || *p == ',')))
                break;
            out[used++] = *p++;
        }
        out[used] = '\0';
        return;
    }
}

static void iptv_parse_extinf_title(const char *line, char *out, size_t out_size)
{
    const char *comma = line ? strrchr(line, ',') : NULL;
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (comma && comma[1])
        iptv_copy(out, out_size, comma + 1);
}

static void iptv_parse_m3u_header_epg(const char *line, char *out, size_t out_size)
{
    iptv_parse_attr(line, "url-tvg", out, out_size);
    if (!out[0])
        iptv_parse_attr(line, "x-tvg-url", out, out_size);
}

static void iptv_source_display_name(const char *source_name, char *out, size_t out_size)
{
    char *dot;
    iptv_copy(out, out_size, source_name ? source_name : "IPTV source");
    dot = strrchr(out, '.');
    if (dot && (strcasecmp(dot, ".m3u") == 0 || strcasecmp(dot, ".m3u8") == 0))
        *dot = '\0';
}

static void iptv_source_name_from_url(const char *url, char *out, size_t out_size)
{
    const char *start = strstr(url ? url : "", "://");
    const char *end;
    size_t length;

    start = start ? start + 3 : url;
    end = start ? strchr(start, '/') : NULL;
    length = start ? (end ? (size_t)(end - start) : strlen(start)) : 0;
    if (length == 0)
    {
        iptv_copy(out, out_size, "Remote IPTV");
        return;
    }
    if (length >= out_size)
        length = out_size - 1;
    memcpy(out, start, length);
    out[length] = '\0';
}

const char *iptv_playlist_kind_name(IptvPlaylistKind kind)
{
    switch (kind)
    {
    case IPTV_PLAYLIST_CHANNEL_LIST:
        return "channel-list";
    case IPTV_PLAYLIST_HLS_STREAM:
        return "hls-stream";
    default:
        return "unknown";
    }
}

IptvPlaylistKind iptv_classify_playlist_file(const char *path)
{
    FILE *file;
    char buffer[IPTV_URL_MAX + 256];
    bool saw_entry = false;
    bool saw_extinf = false;

    file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return IPTV_PLAYLIST_UNKNOWN;
    while (fgets(buffer, sizeof(buffer), file))
    {
        char *line = iptv_trim(buffer);
        if (!line[0])
            continue;
        if (strncasecmp(line, "#EXT-X-", 7) == 0)
        {
            fclose(file);
            return IPTV_PLAYLIST_HLS_STREAM;
        }
        if (strncasecmp(line, "#EXTINF", 7) == 0)
            saw_extinf = true;
        else if (line[0] != '#')
            saw_entry = true;
    }
    fclose(file);
    return (saw_extinf || saw_entry) ? IPTV_PLAYLIST_CHANNEL_LIST : IPTV_PLAYLIST_UNKNOWN;
}

static int iptv_compare_names(const void *left, const void *right)
{
    const char *const *a = left;
    const char *const *b = right;
    return strcasecmp(*a, *b);
}

static bool iptv_id_in_list(const uint32_t *ids, int count, uint32_t id)
{
    for (int i = 0; i < count; ++i)
    {
        if (ids[i] == id)
            return true;
    }
    return false;
}

static int iptv_load_id_file(const char *path, uint32_t *ids, int capacity)
{
    FILE *file = fopen(path, "rb");
    char line[40];
    int count = 0;

    if (!file)
        return 0;
    while (count < capacity && fgets(line, sizeof(line), file))
    {
        unsigned int value;
        if (sscanf(line, "%x", &value) == 1 && value != 0 && !iptv_id_in_list(ids, count, value))
            ids[count++] = value;
    }
    fclose(file);
    return count;
}

static bool iptv_write_id_file(const char *path, const uint32_t *ids, int count)
{
    char temporary[IPTV_PATH_MAX];
    FILE *file;

    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (!file)
        return false;
    for (int i = 0; i < count; ++i)
        fprintf(file, "%08x\n", ids[i]);
    if (fclose(file) != 0)
    {
        remove(temporary);
        return false;
    }
    remove(path);
    if (rename(temporary, path) != 0)
    {
        remove(temporary);
        return false;
    }
    return true;
}

static bool iptv_save_remote_sources(const IptvSource *sources, int source_count)
{
    char temporary[IPTV_PATH_MAX];
    FILE *file;

    snprintf(temporary, sizeof(temporary), "%s.tmp", IPTV_SOURCES_FILE);
    file = fopen(temporary, "wb");
    if (!file)
        return false;
    fprintf(file, "# id\tenabled\tname\tplaylist-url\txmltv-url\n");
    for (int i = 0; i < source_count; ++i)
    {
        const IptvSource *source = &sources[i];
        if (source->local)
            continue;
        fprintf(file, "%08x\t%d\t%s\t%s\t%s\n",
                source->id,
                source->enabled ? 1 : 0,
                source->name,
                source->url,
                source->epg_url);
    }
    if (fclose(file) != 0)
    {
        remove(temporary);
        return false;
    }
    remove(IPTV_SOURCES_FILE);
    if (rename(temporary, IPTV_SOURCES_FILE) != 0)
    {
        remove(temporary);
        return false;
    }
    return true;
}

static bool iptv_remove_preinstalled_source(const char *target_url)
{
    FILE *input = fopen(IPTV_PREINSTALLED_SOURCES_FILE, "rb");
    FILE *output;
    char temporary[IPTV_PATH_MAX];
    char line[IPTV_URL_MAX * 2 + IPTV_SOURCE_MAX + 32];
    bool removed = false;
    bool ok = true;

    if (!input)
        return errno == ENOENT;
    snprintf(temporary, sizeof(temporary), "%s.tmp", IPTV_PREINSTALLED_SOURCES_FILE);
    output = fopen(temporary, "wb");
    if (!output)
    {
        fclose(input);
        return false;
    }

    while (fgets(line, sizeof(line), input))
    {
        char parsed[sizeof(line)];
        char *entry;
        char *url;
        char *separator;
        bool matches = false;

        iptv_copy(parsed, sizeof(parsed), line);
        entry = iptv_trim(parsed);
        if ((unsigned char)entry[0] == 0xEF &&
            (unsigned char)entry[1] == 0xBB && (unsigned char)entry[2] == 0xBF)
            entry = iptv_trim(entry + 3);
        if (entry[0] && entry[0] != '#')
        {
            separator = strchr(entry, '|');
            if (separator)
            {
                url = iptv_trim(separator + 1);
                separator = strchr(url, '|');
                if (separator)
                    *separator = '\0';
                url = iptv_trim(url);
            }
            else
            {
                url = entry;
            }
            matches = iptv_url_is_remote(url) && strcasecmp(url, target_url) == 0;
        }
        if (matches)
        {
            removed = true;
            continue;
        }
        if (fputs(line, output) == EOF)
        {
            ok = false;
            break;
        }
    }
    if (ferror(input))
        ok = false;
    if (fclose(input) != 0)
        ok = false;
    if (fclose(output) != 0)
        ok = false;
    if (!ok)
    {
        remove(temporary);
        return false;
    }
    if (!removed)
    {
        remove(temporary);
        return true;
    }
    remove(IPTV_PREINSTALLED_SOURCES_FILE);
    if (rename(temporary, IPTV_PREINSTALLED_SOURCES_FILE) != 0)
    {
        remove(temporary);
        return false;
    }
    return true;
}

static int iptv_merge_preinstalled_sources(IptvSource *sources, int count, int capacity, bool *changed)
{
    FILE *file = fopen(IPTV_PREINSTALLED_SOURCES_FILE, "rb");
    char line[IPTV_URL_MAX * 2 + IPTV_SOURCE_MAX + 32];
    int line_number = 0;

    if (!file)
        return count;
    while (count < capacity && fgets(line, sizeof(line), file))
    {
        char *entry;
        char *name = NULL;
        char *url;
        char *epg = NULL;
        char *separator;
        uint32_t id;
        int existing = -1;
        bool source_changed = false;
        bool source_needs_refresh = false;

        ++line_number;
        entry = iptv_trim(line);
        if (line_number == 1 && (unsigned char)entry[0] == 0xEF &&
            (unsigned char)entry[1] == 0xBB && (unsigned char)entry[2] == 0xBF)
            entry = iptv_trim(entry + 3);
        if (!entry[0] || entry[0] == '#')
            continue;

        separator = strchr(entry, '|');
        if (separator)
        {
            *separator = '\0';
            name = iptv_trim(entry);
            url = iptv_trim(separator + 1);
            separator = strchr(url, '|');
            if (separator)
            {
                *separator = '\0';
                epg = iptv_trim(separator + 1);
                url = iptv_trim(url);
            }
        }
        else
        {
            url = entry;
        }

        if (!iptv_url_is_remote(url))
        {
            log_warn("[iptv] ignored invalid preinstalled source line=%d\n", line_number);
            continue;
        }
        if (epg && epg[0] && !iptv_url_is_remote(epg))
        {
            log_warn("[iptv] ignored invalid preinstalled EPG line=%d\n", line_number);
            epg = NULL;
        }

        id = iptv_hash_string(url);
        for (int i = 0; i < count; ++i)
        {
            if (sources[i].id == id || strcasecmp(sources[i].url, url) == 0)
            {
                existing = i;
                break;
            }
        }

        if (existing >= 0)
        {
            IptvSource *source = &sources[existing];
            if (name && name[0] && strcmp(source->name, name) != 0)
            {
                iptv_copy(source->name, sizeof(source->name), name);
                source_changed = true;
            }
            if (epg && epg[0] && strcmp(source->epg_url, epg) != 0)
            {
                iptv_copy(source->epg_url, sizeof(source->epg_url), epg);
                source_changed = true;
                source_needs_refresh = true;
            }
            if (!iptv_path_exists(source->cache_path))
                source_needs_refresh = true;
        }
        else
        {
            IptvSource *source = &sources[count++];
            memset(source, 0, sizeof(*source));
            source->id = id;
            source->enabled = true;
            if (name && name[0])
                iptv_copy(source->name, sizeof(source->name), name);
            else
                iptv_source_name_from_url(url, source->name, sizeof(source->name));
            iptv_copy(source->url, sizeof(source->url), url);
            if (epg)
                iptv_copy(source->epg_url, sizeof(source->epg_url), epg);
            snprintf(source->cache_path, sizeof(source->cache_path), "%s/%08x.m3u", IPTV_PLAYLIST_CACHE_DIR, id);
            source_changed = true;
            source_needs_refresh = true;
        }

        if (source_changed)
        {
            *changed = true;
            ++g_preinstalled_changed_count;
        }
        if (source_needs_refresh)
            g_preinstalled_refresh_needed = true;
        log_info("[iptv] preinstalled source accepted line=%d changed=%d refresh=%d epg=%d\n",
                 line_number,
                 source_changed ? 1 : 0,
                 source_needs_refresh ? 1 : 0,
                 epg && epg[0] ? 1 : 0);
    }
    fclose(file);
    log_info("[iptv] preinstalled source file loaded sources=%d changed=%d refresh=%d\n",
             count,
             g_preinstalled_changed_count,
             g_preinstalled_refresh_needed ? 1 : 0);
    return count;
}

static int iptv_load_remote_sources(IptvSource *sources, int capacity)
{
    FILE *file = fopen(IPTV_SOURCES_FILE, "rb");
    char line[IPTV_URL_MAX * 2 + IPTV_SOURCE_MAX + 96];
    int count = 0;
    bool changed = false;

    while (file && count < capacity && fgets(line, sizeof(line), file))
    {
        char *save = NULL;
        char *id_text = strtok_r(line, "\t\r\n", &save);
        char *enabled_text = strtok_r(NULL, "\t\r\n", &save);
        char *name = strtok_r(NULL, "\t\r\n", &save);
        char *url = strtok_r(NULL, "\t\r\n", &save);
        char *epg = strtok_r(NULL, "\t\r\n", &save);
        unsigned int id;
        IptvSource *source;

        if (!id_text || !enabled_text || !name || !url || sscanf(id_text, "%x", &id) != 1 || !iptv_url_is_remote(url))
            continue;
        source = &sources[count++];
        memset(source, 0, sizeof(*source));
        source->id = id ? id : iptv_hash_string(url);
        source->enabled = atoi(enabled_text) != 0;
        iptv_copy(source->name, sizeof(source->name), name);
        iptv_copy(source->url, sizeof(source->url), url);
        if (epg)
            iptv_copy(source->epg_url, sizeof(source->epg_url), epg);
        snprintf(source->cache_path, sizeof(source->cache_path), "%s/%08x.m3u", IPTV_PLAYLIST_CACHE_DIR, source->id);
        source->cache_ready = iptv_path_exists(source->cache_path);
        source->refreshed_at = iptv_path_mtime(source->cache_path);
    }
    if (file)
        fclose(file);
    count = iptv_merge_preinstalled_sources(sources, count, capacity, &changed);
    if (changed && !iptv_save_remote_sources(sources, count))
        log_warn("[iptv] failed to persist preinstalled sources\n");
    return count;
}

static bool iptv_save_sources_locked(void)
{
    return iptv_save_remote_sources(g_sources, g_source_count);
}

static void iptv_catalog_add_group(IptvCatalog *catalog, const char *group)
{
    if (!group || !group[0] || catalog->group_count >= IPTV_MAX_GROUPS)
        return;
    for (int i = 0; i < catalog->group_count; ++i)
    {
        if (strcasecmp(catalog->groups[i], group) == 0)
            return;
    }
    iptv_copy(catalog->groups[catalog->group_count++], IPTV_GROUP_MAX, group);
}

static bool iptv_catalog_add_channel(IptvCatalog *catalog,
                                     IptvSource *source,
                                     const char *name,
                                     const char *group,
                                     const char *tvg_id,
                                     const char *logo_url,
                                     const char *url,
                                     const uint32_t *favorites,
                                     int favorite_count,
                                     const uint32_t *recent,
                                     int recent_count)
{
    IptvChannel *channel;
    char fallback[IPTV_NAME_MAX];

    if (!catalog || !source || catalog->channel_count >= IPTV_MAX_CHANNELS || !iptv_url_is_playable(url))
        return false;
    channel = &catalog->channels[catalog->channel_count++];
    memset(channel, 0, sizeof(*channel));
    channel->source_id = source->id;
    iptv_copy(channel->name, sizeof(channel->name), name);
    if (!channel->name[0])
    {
        snprintf(fallback, sizeof(fallback), "%s %d", IPTV_FALLBACK_NAME, catalog->channel_count);
        iptv_copy(channel->name, sizeof(channel->name), fallback);
    }
    iptv_copy(channel->group, sizeof(channel->group), group);
    iptv_copy(channel->source, sizeof(channel->source), source->name);
    iptv_copy(channel->tvg_id, sizeof(channel->tvg_id), tvg_id);
    iptv_copy(channel->logo_url, sizeof(channel->logo_url), logo_url);
    iptv_copy(channel->url, sizeof(channel->url), url);
    channel->id = iptv_channel_id(source->id, channel->tvg_id, channel->url);
    channel->favorite = iptv_id_in_list(favorites, favorite_count, channel->id);
    channel->recent = iptv_id_in_list(recent, recent_count, channel->id);
    if (channel->logo_url[0])
    {
        snprintf(channel->logo_path, sizeof(channel->logo_path), "%s/%08x.img", IPTV_LOGO_CACHE_DIR, channel->id);
        channel->logo_cached = iptv_path_exists(channel->logo_path);
        if (channel->logo_cached)
            ++catalog->logo_cached_count;
    }
    iptv_catalog_add_group(catalog, channel->group);
    ++source->channel_count;
    return true;
}

static int iptv_parse_playlist(IptvCatalog *catalog,
                               IptvSource *source,
                               const char *path,
                               const uint32_t *favorites,
                               int favorite_count,
                               const uint32_t *recent,
                               int recent_count)
{
    FILE *file = fopen(path, "rb");
    char buffer[IPTV_URL_MAX + 768];
    char pending_name[IPTV_NAME_MAX] = "";
    char pending_group[IPTV_GROUP_MAX] = "";
    char pending_tvg_id[IPTV_TVG_ID_MAX] = "";
    char pending_logo[IPTV_LOGO_URL_MAX] = "";
    int before = catalog->channel_count;

    if (!file)
        return 0;
    while (fgets(buffer, sizeof(buffer), file))
    {
        char *line = iptv_trim(buffer);
        if (!line[0])
            continue;
        if (strncasecmp(line, "#EXTM3U", 7) == 0 && !source->epg_url[0])
        {
            char epg[IPTV_URL_MAX];
            iptv_parse_m3u_header_epg(line, epg, sizeof(epg));
            if (epg[0])
                iptv_resolve_url(source->url, epg, source->epg_url, sizeof(source->epg_url));
            continue;
        }
        if (strncasecmp(line, "#EXTINF", 7) == 0)
        {
            char tvg_name[IPTV_NAME_MAX];
            pending_name[0] = pending_group[0] = pending_tvg_id[0] = pending_logo[0] = '\0';
            iptv_parse_extinf_title(line, pending_name, sizeof(pending_name));
            iptv_parse_attr(line, "tvg-name", tvg_name, sizeof(tvg_name));
            if (!pending_name[0])
                iptv_copy(pending_name, sizeof(pending_name), tvg_name);
            iptv_parse_attr(line, "group-title", pending_group, sizeof(pending_group));
            iptv_parse_attr(line, "tvg-id", pending_tvg_id, sizeof(pending_tvg_id));
            iptv_parse_attr(line, "tvg-logo", pending_logo, sizeof(pending_logo));
            continue;
        }
        if (line[0] == '#')
            continue;

        char playable[IPTV_URL_MAX];
        char resolved_logo[IPTV_LOGO_URL_MAX];
        iptv_resolve_url(source->url, line, playable, sizeof(playable));
        iptv_resolve_url(source->url, pending_logo, resolved_logo, sizeof(resolved_logo));
        iptv_catalog_add_channel(catalog,
                                 source,
                                 pending_name,
                                 pending_group,
                                 pending_tvg_id,
                                 resolved_logo,
                                 playable,
                                 favorites,
                                 favorite_count,
                                 recent,
                                 recent_count);
        pending_name[0] = pending_group[0] = pending_tvg_id[0] = pending_logo[0] = '\0';
    }
    fclose(file);
    return catalog->channel_count - before;
}

static bool iptv_catalog_build(IptvCatalog *catalog)
{
    DIR *directory;
    struct dirent *entry;
    char local_names[IPTV_MAX_SOURCES][IPTV_SOURCE_MAX];
    char *sorted_names[IPTV_MAX_SOURCES];
    uint32_t favorites[IPTV_MAX_CHANNELS];
    uint32_t recent[IPTV_MAX_RECENT];
    int favorite_count;
    int recent_count;
    int local_count = 0;

    memset(catalog, 0, sizeof(*catalog));
    catalog->channels = calloc(IPTV_MAX_CHANNELS, sizeof(*catalog->channels));
    if (!catalog->channels)
        return false;
    favorite_count = iptv_load_id_file(IPTV_FAVORITES_FILE, favorites, IPTV_MAX_CHANNELS);
    recent_count = iptv_load_id_file(IPTV_RECENT_FILE, recent, IPTV_MAX_RECENT);
    catalog->source_count = iptv_load_remote_sources(catalog->sources, IPTV_MAX_SOURCES);

    directory = opendir(IPTV_ROOT_DIR);
    if (directory)
    {
        while ((entry = readdir(directory)) != NULL && local_count + catalog->source_count < IPTV_MAX_SOURCES)
        {
            if (entry->d_name[0] == '.' || !iptv_has_m3u_extension(entry->d_name))
                continue;
            iptv_copy(local_names[local_count], sizeof(local_names[local_count]), entry->d_name);
            sorted_names[local_count] = local_names[local_count];
            ++local_count;
        }
        closedir(directory);
    }
    qsort(sorted_names, (size_t)local_count, sizeof(sorted_names[0]), iptv_compare_names);
    for (int i = 0; i < local_count; ++i)
    {
        IptvSource *source = &catalog->sources[catalog->source_count++];
        memset(source, 0, sizeof(*source));
        source->local = true;
        source->enabled = true;
        iptv_source_display_name(sorted_names[i], source->name, sizeof(source->name));
        snprintf(source->url, sizeof(source->url), "%s/%s", IPTV_ROOT_DIR, sorted_names[i]);
        iptv_copy(source->cache_path, sizeof(source->cache_path), source->url);
        source->id = iptv_hash_string(source->url);
        source->cache_ready = true;
        source->refreshed_at = iptv_path_mtime(source->url);
    }

    for (int i = 0; i < catalog->source_count && catalog->channel_count < IPTV_MAX_CHANNELS; ++i)
    {
        IptvSource *source = &catalog->sources[i];
        IptvPlaylistKind kind;
        const char *path;
        int added = 0;

        if (!source->enabled)
        {
            iptv_copy(source->status, sizeof(source->status), "Disabled");
            continue;
        }
        path = source->local ? source->url : source->cache_path;
        source->cache_ready = iptv_path_exists(path);
        if (!source->cache_ready)
        {
            iptv_copy(source->status, sizeof(source->status), "Not cached - refresh this source");
            continue;
        }
        kind = iptv_classify_playlist_file(path);
        if (kind == IPTV_PLAYLIST_HLS_STREAM)
        {
            added = iptv_catalog_add_channel(catalog,
                                             source,
                                             source->name,
                                             source->local ? "Local HLS" : "Remote HLS",
                                             "",
                                             "",
                                             source->url,
                                             favorites,
                                             favorite_count,
                                             recent,
                                             recent_count) ? 1 : 0;
            if (added)
                ++catalog->hls_stream_count;
        }
        else if (kind == IPTV_PLAYLIST_CHANNEL_LIST)
        {
            added = iptv_parse_playlist(catalog,
                                        source,
                                        path,
                                        favorites,
                                        favorite_count,
                                        recent,
                                        recent_count);
        }
        if (added > 0)
            snprintf(source->status, sizeof(source->status), "%d channel%s", added, added == 1 ? "" : "s");
        else
            iptv_copy(source->status, sizeof(source->status), "No playable channels");

        if (source->epg_url[0])
        {
            char epg_path[IPTV_PATH_MAX];
            char epg_error[96];
            snprintf(epg_path, sizeof(epg_path), "%s/%08x.xmltv", IPTV_EPG_CACHE_DIR, source->id);
            if (iptv_path_exists(epg_path))
                iptv_xmltv_apply_file(epg_path,
                                      source->id,
                                      catalog->channels,
                                      catalog->channel_count,
                                      time(NULL),
                                      epg_error,
                                      sizeof(epg_error));
        }
    }

    for (int i = 0; i < catalog->channel_count; ++i)
    {
        if (catalog->channels[i].now_title[0] || catalog->channels[i].next_title[0])
            ++catalog->epg_channel_count;
    }
    return true;
}

static bool iptv_channel_matches_locked(const IptvChannel *channel)
{
    if (!channel)
        return false;
    if (g_filter_index == 1 && !channel->favorite)
        return false;
    if (g_filter_index == 2 && !channel->recent)
        return false;
    if (g_filter_index >= 3)
    {
        int group_index = g_filter_index - 3;
        if (group_index >= g_group_count || strcasecmp(channel->group, g_groups[group_index]) != 0)
            return false;
    }
    return iptv_contains_casefold(channel->name, g_search) ||
           iptv_contains_casefold(channel->group, g_search) ||
           iptv_contains_casefold(channel->tvg_id, g_search);
}

static void iptv_rebuild_visible_locked(uint32_t preferred_id)
{
    g_visible_count = 0;
    if (g_filter_index > g_group_count + 2)
        g_filter_index = 0;

    if (g_filter_index == 2)
    {
        for (int recent_index = 0; recent_index < g_recent_count; ++recent_index)
        {
            for (int channel_index = 0; channel_index < g_channel_count; ++channel_index)
            {
                if (g_channels[channel_index].id == g_recent_ids[recent_index] &&
                    iptv_channel_matches_locked(&g_channels[channel_index]))
                {
                    g_visible[g_visible_count++] = channel_index;
                    break;
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < g_channel_count; ++i)
        {
            if (iptv_channel_matches_locked(&g_channels[i]))
                g_visible[g_visible_count++] = i;
        }
    }

    if (preferred_id)
    {
        for (int i = 0; i < g_visible_count; ++i)
        {
            if (g_channels[g_visible[i]].id == preferred_id)
            {
                g_selected_index = i;
                return;
            }
        }
    }
    if (g_visible_count <= 0)
        g_selected_index = 0;
    else if (g_selected_index >= g_visible_count)
        g_selected_index = g_visible_count - 1;
    else if (g_selected_index < 0)
        g_selected_index = 0;
}

static void iptv_commit_catalog(IptvCatalog *catalog)
{
    uint32_t preferred_channel = 0;
    uint32_t preferred_source = 0;
    uint32_t favorites[IPTV_MAX_CHANNELS];
    int favorite_count;

    mutexLock(&g_mutex);
    if (g_visible_count > 0 && g_selected_index >= 0 && g_selected_index < g_visible_count)
        preferred_channel = g_channels[g_visible[g_selected_index]].id;
    if (g_source_selected_index >= 0 && g_source_selected_index < g_source_count)
        preferred_source = g_sources[g_source_selected_index].id;
    memcpy(g_channels, catalog->channels, (size_t)catalog->channel_count * sizeof(*g_channels));
    if (catalog->channel_count < g_channel_count)
        memset(g_channels + catalog->channel_count, 0, (size_t)(g_channel_count - catalog->channel_count) * sizeof(*g_channels));
    g_channel_count = catalog->channel_count;
    memcpy(g_sources, catalog->sources, sizeof(g_sources));
    g_source_count = catalog->source_count;
    memcpy(g_groups, catalog->groups, sizeof(g_groups));
    g_group_count = catalog->group_count;
    g_hls_stream_count = catalog->hls_stream_count;
    g_logo_cached_count = catalog->logo_cached_count;
    g_epg_channel_count = catalog->epg_channel_count;
    favorite_count = iptv_load_id_file(IPTV_FAVORITES_FILE, favorites, IPTV_MAX_CHANNELS);
    g_recent_count = iptv_load_id_file(IPTV_RECENT_FILE, g_recent_ids, IPTV_MAX_RECENT);
    for (int i = 0; i < g_channel_count; ++i)
    {
        g_channels[i].favorite = iptv_id_in_list(favorites, favorite_count, g_channels[i].id);
        g_channels[i].recent = iptv_id_in_list(g_recent_ids, g_recent_count, g_channels[i].id);
    }
    g_loaded = true;
    iptv_rebuild_visible_locked(preferred_channel);
    g_source_selected_index = 0;
    if (preferred_source)
    {
        for (int i = 0; i < g_source_count; ++i)
        {
            if (g_sources[i].id == preferred_source)
            {
                g_source_selected_index = i;
                break;
            }
        }
    }
    if (!g_refreshing)
    {
        if (g_channel_count > 0)
            snprintf(g_status, sizeof(g_status), "Loaded %d channels from %d sources.", g_channel_count, g_source_count);
        else if (g_source_count > 0)
            iptv_copy(g_status, sizeof(g_status), "Sources loaded. Refresh remote sources or add local M3U files.");
        else
            iptv_copy(g_status, sizeof(g_status), "No IPTV sources. Add a remote source or copy M3U files to the IPTV folder.");
    }
    iptv_queue_selected_logo_locked();
    mutexUnlock(&g_mutex);
}

static bool iptv_rebuild_catalog(void)
{
    IptvCatalog catalog;
    bool ok = iptv_catalog_build(&catalog);

    if (!ok)
        return false;
    iptv_commit_catalog(&catalog);
    free(catalog.channels);
    return true;
}

static bool iptv_find_source_copy(uint32_t id, IptvSource *out)
{
    bool found = false;
    mutexLock(&g_mutex);
    for (int i = 0; i < g_source_count; ++i)
    {
        if (g_sources[i].id == id)
        {
            *out = g_sources[i];
            found = true;
            break;
        }
    }
    mutexUnlock(&g_mutex);
    return found;
}

static bool iptv_find_playlist_guide_url(const IptvSource *source, char *out, size_t out_size)
{
    const char *path;
    FILE *file;
    char buffer[IPTV_URL_MAX + 768];

    if (!source || !out || out_size == 0)
        return false;
    out[0] = '\0';
    path = source->local ? source->url : source->cache_path;
    file = fopen(path, "rb");
    if (!file)
        return false;

    while (fgets(buffer, sizeof(buffer), file))
    {
        char *line = iptv_trim(buffer);
        char declared[IPTV_URL_MAX];

        if (!line[0])
            continue;
        if (strncasecmp(line, "#EXTM3U", 7) != 0)
            break;
        iptv_parse_m3u_header_epg(line, declared, sizeof(declared));
        if (declared[0])
            iptv_resolve_url(source->url, declared, out, out_size);
        break;
    }
    fclose(file);
    return out[0] != '\0';
}

static bool iptv_refresh_source_files(const IptvSource *source, char *message, size_t message_size)
{
    char error[IPTV_STATUS_MAX] = "";
    char automatic_epg_url[IPTV_URL_MAX] = "";
    const char *epg_url;
    bool playlist_ok = true;
    bool epg_ok = true;

    if (!source)
        return false;
    if (!source->local)
        playlist_ok = iptv_fetch_to_file(source->url,
                                         source->cache_path,
                                         IPTV_REMOTE_PLAYLIST_LIMIT,
                                         error,
                                         sizeof(error));
    epg_url = source->epg_url;
    if (playlist_ok && !epg_url[0] && iptv_find_playlist_guide_url(source, automatic_epg_url, sizeof(automatic_epg_url)))
        epg_url = automatic_epg_url;
    if (playlist_ok && epg_url[0])
    {
        char epg_path[IPTV_PATH_MAX];
        snprintf(epg_path, sizeof(epg_path), "%s/%08x.xmltv", IPTV_EPG_CACHE_DIR, source->id);
        epg_ok = iptv_fetch_to_file(epg_url,
                                    epg_path,
                                    IPTV_XMLTV_LIMIT,
                                    error,
                                    sizeof(error));
    }
    if (playlist_ok && epg_ok)
    {
        snprintf(message,
                 message_size,
                 "%s %s%s",
                 source->name,
                 epg_url[0] ? "and programme guide refreshed." : "refreshed.",
                 automatic_epg_url[0] ? " Guide link found in M3U." : "");
        return true;
    }
    log_warn("[iptv] refresh failed source=%.64s stage=%s error=%.96s\n",
             source->name,
             playlist_ok ? "epg" : "playlist",
             error[0] ? error : "unknown error");
    snprintf(message, message_size, "Refresh failed for %.70s: %.80s", source->name, error);
    return false;
}

static void iptv_finish_refresh(const char *message)
{
    mutexLock(&g_mutex);
    g_refreshing = false;
    g_refreshing_source_id = 0;
    iptv_copy(g_status, sizeof(g_status), message);
    mutexUnlock(&g_mutex);
}

static void iptv_cache_logo(uint32_t channel_id, const char *url, const char *path)
{
    char error[96] = "";

    if (!url[0] || !path[0] || iptv_path_exists(path))
        return;
    if (!iptv_fetch_to_file(url, path, IPTV_LOGO_LIMIT, error, sizeof(error)))
    {
        log_warn("[iptv] logo cache failed channel=%08x error=%s\n", channel_id, error);
        return;
    }
    mutexLock(&g_mutex);
    for (int i = 0; i < g_channel_count; ++i)
    {
        if (g_channels[i].id == channel_id)
        {
            if (!g_channels[i].logo_cached)
                ++g_logo_cached_count;
            g_channels[i].logo_cached = true;
            break;
        }
    }
    mutexUnlock(&g_mutex);
}

static void iptv_worker(void *argument)
{
    (void)argument;
    for (;;)
    {
        bool refresh_all;
        uint32_t refresh_source;
        bool cache_logo;
        uint32_t logo_channel;
        char logo_url[IPTV_LOGO_URL_MAX];
        char logo_path[IPTV_PATH_MAX];

        mutexLock(&g_mutex);
        while (!g_worker_stop && !g_refresh_all_requested && !g_refresh_source_requested && !g_logo_requested)
            condvarWait(&g_worker_cond, &g_mutex);
        if (g_worker_stop)
        {
            mutexUnlock(&g_mutex);
            break;
        }
        refresh_all = g_refresh_all_requested;
        refresh_source = g_refresh_source_requested;
        cache_logo = g_logo_requested;
        logo_channel = g_logo_channel_id;
        iptv_copy(logo_url, sizeof(logo_url), g_logo_url);
        iptv_copy(logo_path, sizeof(logo_path), g_logo_path);
        g_refresh_all_requested = false;
        g_refresh_source_requested = 0;
        g_logo_requested = false;
        mutexUnlock(&g_mutex);

        if (refresh_all)
        {
            IptvSource sources[IPTV_MAX_SOURCES];
            int count;
            char message[IPTV_STATUS_MAX] = "IPTV sources refreshed.";
            mutexLock(&g_mutex);
            count = g_source_count;
            memcpy(sources, g_sources, sizeof(sources));
            mutexUnlock(&g_mutex);
            for (int i = 0; i < count; ++i)
            {
                mutexLock(&g_mutex);
                if (g_worker_stop)
                {
                    mutexUnlock(&g_mutex);
                    break;
                }
                g_refreshing_source_id = sources[i].id;
                snprintf(g_status, sizeof(g_status), "Refreshing %s...", sources[i].name);
                mutexUnlock(&g_mutex);
                iptv_refresh_source_files(&sources[i], message, sizeof(message));
            }
            iptv_rebuild_catalog();
            iptv_finish_refresh(message);
        }
        else if (refresh_source)
        {
            IptvSource source;
            char message[IPTV_STATUS_MAX];
            if (iptv_find_source_copy(refresh_source, &source))
            {
                mutexLock(&g_mutex);
                g_refreshing_source_id = source.id;
                snprintf(g_status, sizeof(g_status), "Refreshing %s...", source.name);
                mutexUnlock(&g_mutex);
                iptv_refresh_source_files(&source, message, sizeof(message));
                iptv_rebuild_catalog();
                iptv_finish_refresh(message);
            }
            else
            {
                iptv_finish_refresh("Selected IPTV source no longer exists.");
            }
        }

        if (cache_logo)
            iptv_cache_logo(logo_channel, logo_url, logo_path);
    }
    threadExit();
}

static void iptv_queue_selected_logo_locked(void)
{
    IptvChannel *channel;
    if (!g_worker_started || g_visible_count <= 0 || g_selected_index < 0 || g_selected_index >= g_visible_count)
        return;
    channel = &g_channels[g_visible[g_selected_index]];
    if (!channel->logo_url[0] || channel->logo_cached)
        return;
    g_logo_requested = true;
    g_logo_channel_id = channel->id;
    iptv_copy(g_logo_url, sizeof(g_logo_url), channel->logo_url);
    iptv_copy(g_logo_path, sizeof(g_logo_path), channel->logo_path);
    condvarWakeOne(&g_worker_cond);
}

bool iptv_init(void)
{
    Result rc;

    if (!g_sync_initialized)
    {
        mutexInit(&g_mutex);
        condvarInit(&g_worker_cond);
        g_sync_initialized = true;
    }
    iptv_ensure_directories();
    g_worker_stop = false;
    g_refresh_all_requested = false;
    g_refresh_source_requested = 0;
    g_logo_requested = false;
    g_preinstalled_refresh_needed = false;
    g_preinstalled_changed_count = 0;
    g_channels = calloc(IPTV_MAX_CHANNELS, sizeof(*g_channels));
    if (!g_channels)
        return false;
    avformat_network_init();
    mutexLock(&g_mutex);
    g_initialized = true;
    g_recent_count = iptv_load_id_file(IPTV_RECENT_FILE, g_recent_ids, IPTV_MAX_RECENT);
    mutexUnlock(&g_mutex);
    if (!iptv_rebuild_catalog())
    {
        free(g_channels);
        g_channels = NULL;
        avformat_network_deinit();
        return false;
    }

    rc = threadCreate(&g_worker_thread, iptv_worker, NULL, NULL, IPTV_WORKER_STACK_SIZE, 0x2D, -2);
    if (R_SUCCEEDED(rc))
        rc = threadStart(&g_worker_thread);
    if (R_FAILED(rc))
    {
        if (g_worker_thread.handle)
            threadClose(&g_worker_thread);
        mutexLock(&g_mutex);
        iptv_copy(g_status, sizeof(g_status), "IPTV loaded, but background refresh is unavailable.");
        mutexUnlock(&g_mutex);
        log_warn("[iptv] worker start failed rc=0x%08X\n", rc);
        return true;
    }
    mutexLock(&g_mutex);
    g_worker_started = true;
    if (g_preinstalled_refresh_needed)
    {
        g_refreshing = true;
        g_refresh_all_requested = true;
        if (g_preinstalled_changed_count > 0)
            snprintf(g_status, sizeof(g_status), "Imported %d SD source%s; refreshing...",
                     g_preinstalled_changed_count,
                     g_preinstalled_changed_count == 1 ? "" : "s");
        else
            iptv_copy(g_status, sizeof(g_status), "Refreshing preinstalled IPTV sources...");
        g_preinstalled_refresh_needed = false;
        g_preinstalled_changed_count = 0;
        condvarWakeOne(&g_worker_cond);
    }
    iptv_queue_selected_logo_locked();
    mutexUnlock(&g_mutex);
    return true;
}

void iptv_deinit(void)
{
    if (!g_sync_initialized)
        return;
    mutexLock(&g_mutex);
    g_worker_stop = true;
    condvarWakeAll(&g_worker_cond);
    mutexUnlock(&g_mutex);
    if (g_worker_started)
    {
        threadWaitForExit(&g_worker_thread);
        threadClose(&g_worker_thread);
    }
    mutexLock(&g_mutex);
    g_worker_started = false;
    g_initialized = false;
    g_loaded = false;
    g_refreshing = false;
    g_channel_count = g_source_count = g_visible_count = 0;
    g_status[0] = g_last_name[0] = g_last_url[0] = '\0';
    free(g_channels);
    g_channels = NULL;
    mutexUnlock(&g_mutex);
    avformat_network_deinit();
}

bool iptv_reload(void)
{
    if (!g_initialized || !g_channels)
        return false;
    iptv_ensure_directories();
    if (!iptv_rebuild_catalog())
    {
        iptv_set_status("Failed to rebuild IPTV catalog: out of memory.");
        return false;
    }
    mutexLock(&g_mutex);
    iptv_queue_selected_logo_locked();
    mutexUnlock(&g_mutex);
    return true;
}

void iptv_set_status(const char *status)
{
    if (!g_sync_initialized)
        return;
    mutexLock(&g_mutex);
    iptv_copy(g_status, sizeof(g_status), status);
    mutexUnlock(&g_mutex);
}

bool iptv_get_state(IptvState *out)
{
    if (!out || !g_sync_initialized)
        return false;
    memset(out, 0, sizeof(*out));
    mutexLock(&g_mutex);
    out->initialized = g_initialized;
    out->loaded = g_loaded;
    out->refreshing = g_refreshing;
    out->source_count = g_source_count;
    out->hls_stream_count = g_hls_stream_count;
    out->channel_count = g_channel_count;
    out->visible_count = g_visible_count;
    out->group_count = g_group_count;
    out->logo_cached_count = g_logo_cached_count;
    out->epg_channel_count = g_epg_channel_count;
    out->selected_index = g_selected_index;
    out->source_selected_index = g_source_selected_index;
    for (int i = 0; i < g_channel_count; ++i)
    {
        if (g_channels[i].favorite)
            ++out->favorite_count;
        if (g_channels[i].recent)
            ++out->recent_count;
    }
    if (g_filter_index == 0)
        iptv_copy(out->active_filter, sizeof(out->active_filter), "All channels");
    else if (g_filter_index == 1)
        iptv_copy(out->active_filter, sizeof(out->active_filter), "Favorites");
    else if (g_filter_index == 2)
        iptv_copy(out->active_filter, sizeof(out->active_filter), "Recent");
    else if (g_filter_index - 3 < g_group_count)
        iptv_copy(out->active_filter, sizeof(out->active_filter), g_groups[g_filter_index - 3]);
    iptv_copy(out->search, sizeof(out->search), g_search);
    iptv_copy(out->status, sizeof(out->status), g_status);
    iptv_copy(out->last_name, sizeof(out->last_name), g_last_name);
    iptv_copy(out->last_url, sizeof(out->last_url), g_last_url);
    mutexUnlock(&g_mutex);
    return true;
}

int iptv_get_channel_count(void)
{
    int count;
    mutexLock(&g_mutex);
    count = g_visible_count;
    mutexUnlock(&g_mutex);
    return count;
}

int iptv_get_selected_index(void)
{
    int index;
    mutexLock(&g_mutex);
    index = g_selected_index;
    mutexUnlock(&g_mutex);
    return index;
}

void iptv_set_selected_index(int index)
{
    mutexLock(&g_mutex);
    if (g_visible_count <= 0)
        g_selected_index = 0;
    else
    {
        if (index < 0)
            index = 0;
        if (index >= g_visible_count)
            index = g_visible_count - 1;
        g_selected_index = index;
        iptv_queue_selected_logo_locked();
    }
    mutexUnlock(&g_mutex);
}

void iptv_select_delta(int delta)
{
    int current = iptv_get_selected_index();
    iptv_set_selected_index(current + delta);
}

bool iptv_get_channel(int index, IptvChannel *out)
{
    bool found = false;
    if (!out)
        return false;
    mutexLock(&g_mutex);
    if (index >= 0 && index < g_visible_count)
    {
        *out = g_channels[g_visible[index]];
        found = true;
    }
    mutexUnlock(&g_mutex);
    return found;
}

int iptv_get_source_count(void)
{
    int count;
    mutexLock(&g_mutex);
    count = g_source_count;
    mutexUnlock(&g_mutex);
    return count;
}

int iptv_get_source_selected_index(void)
{
    int index;
    mutexLock(&g_mutex);
    index = g_source_selected_index;
    mutexUnlock(&g_mutex);
    return index;
}

void iptv_set_source_selected_index(int index)
{
    mutexLock(&g_mutex);
    if (g_source_count <= 0)
        g_source_selected_index = 0;
    else
    {
        if (index < 0)
            index = 0;
        if (index >= g_source_count)
            index = g_source_count - 1;
        g_source_selected_index = index;
    }
    mutexUnlock(&g_mutex);
}

void iptv_select_source_delta(int delta)
{
    int current = iptv_get_source_selected_index();
    iptv_set_source_selected_index(current + delta);
}

bool iptv_get_source(int index, IptvSource *out)
{
    bool found = false;
    if (!out)
        return false;
    mutexLock(&g_mutex);
    if (index >= 0 && index < g_source_count)
    {
        *out = g_sources[index];
        out->refreshing = g_refreshing && out->id == g_refreshing_source_id;
        found = true;
    }
    mutexUnlock(&g_mutex);
    return found;
}

static bool iptv_prompt_text(const char *header,
                             const char *subtext,
                             const char *guide,
                             const char *button,
                             const char *initial,
                             char *out,
                             size_t out_size)
{
    SwkbdConfig keyboard;
    Result rc;
    char *trimmed;

    if (!out || out_size == 0)
        return false;
    out[0] = '\0';
    rc = swkbdCreate(&keyboard, 0);
    if (R_FAILED(rc))
        return false;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, header);
    swkbdConfigSetSubText(&keyboard, subtext);
    swkbdConfigSetGuideText(&keyboard, guide);
    swkbdConfigSetOkButtonText(&keyboard, button);
    swkbdConfigSetStringLenMax(&keyboard, out_size > 1 ? (u32)(out_size - 1) : 1);
    if (initial && initial[0])
        swkbdConfigSetInitialText(&keyboard, initial);
    rc = swkbdShow(&keyboard, out, out_size);
    swkbdClose(&keyboard);
    if (R_FAILED(rc))
        return false;
    trimmed = iptv_trim(out);
    if (trimmed != out)
        memmove(out, trimmed, strlen(trimmed) + 1);
    return out[0] != '\0';
}

bool iptv_add_source_url(const char *url)
{
    char name[IPTV_SOURCE_MAX];
    uint32_t id;
    int added_index;

    mutexLock(&g_mutex);
    bool busy = g_refreshing;
    mutexUnlock(&g_mutex);
    if (busy)
    {
        iptv_set_status("Wait for the current IPTV refresh to finish.");
        return false;
    }
    if (!iptv_url_is_remote(url))
    {
        iptv_set_status("Remote source must use HTTP or HTTPS.");
        return false;
    }
    id = iptv_hash_string(url);
    iptv_source_name_from_url(url, name, sizeof(name));
    mutexLock(&g_mutex);
    for (int i = 0; i < g_source_count; ++i)
    {
        if (g_sources[i].id == id || strcasecmp(g_sources[i].url, url) == 0)
        {
            g_source_selected_index = i;
            mutexUnlock(&g_mutex);
            iptv_set_status("IPTV source already exists; refreshing it.");
            return iptv_refresh_selected_source_async();
        }
    }
    if (g_source_count >= IPTV_MAX_SOURCES)
    {
        mutexUnlock(&g_mutex);
        iptv_set_status("IPTV source limit reached.");
        return false;
    }
    IptvSource *source = &g_sources[g_source_count++];
    memset(source, 0, sizeof(*source));
    source->id = id;
    source->enabled = true;
    iptv_copy(source->name, sizeof(source->name), name);
    iptv_copy(source->url, sizeof(source->url), url);
    snprintf(source->cache_path, sizeof(source->cache_path), "%s/%08x.m3u", IPTV_PLAYLIST_CACHE_DIR, id);
    added_index = g_source_count - 1;
    bool saved = iptv_save_sources_locked();
    if (!saved)
    {
        memset(&g_sources[added_index], 0, sizeof(g_sources[added_index]));
        --g_source_count;
    }
    mutexUnlock(&g_mutex);
    if (!saved)
    {
        iptv_set_status("Failed to save IPTV source.");
        return false;
    }
    if (!iptv_reload())
    {
        iptv_set_status("IPTV source was saved, but the catalog could not be reloaded.");
        return false;
    }
    mutexLock(&g_mutex);
    for (int i = 0; i < g_source_count; ++i)
    {
        if (g_sources[i].id == id)
        {
            g_source_selected_index = i;
            break;
        }
    }
    mutexUnlock(&g_mutex);
    return iptv_refresh_selected_source_async();
}

bool iptv_prompt_add_source(void)
{
    char url[IPTV_URL_MAX];

    if (!iptv_prompt_text("Add IPTV playlist",
                          "Paste the address of the M3U channel list",
                          "https://example.com/channels.m3u",
                          "Import",
                          "",
                          url,
                          sizeof(url)))
        return false;
    return iptv_add_source_url(url);
}

bool iptv_prompt_set_source_epg(void)
{
    IptvSource source;
    char url[IPTV_URL_MAX];
    int selected = iptv_get_source_selected_index();

    mutexLock(&g_mutex);
    bool busy = g_refreshing;
    mutexUnlock(&g_mutex);
    if (busy)
    {
        iptv_set_status("Wait for the current IPTV refresh to finish.");
        return false;
    }
    if (!iptv_get_source(selected, &source))
        return false;
    if (source.local)
    {
        iptv_set_status("Local playlist: add url-tvg=\"guide.xml URL\" to the #EXTM3U header.");
        return false;
    }
    if (!iptv_prompt_text(source.epg_url[0] ? "Change programme guide" : "Connect programme guide",
                          "Paste the guide.xml address supplied with this M3U playlist",
                          "https://example.com/guide.xml",
                          "Connect",
                          source.epg_url,
                          url,
                          sizeof(url)))
        return false;
    if (!iptv_url_is_remote(url))
    {
        iptv_set_status("Programme guide address must use HTTP or HTTPS.");
        return false;
    }
    mutexLock(&g_mutex);
    if (selected >= 0 && selected < g_source_count && g_sources[selected].id == source.id)
        iptv_copy(g_sources[selected].epg_url, sizeof(g_sources[selected].epg_url), url);
    bool saved = iptv_save_sources_locked();
    mutexUnlock(&g_mutex);
    if (!saved)
    {
        iptv_set_status("Failed to save the programme guide address.");
        return false;
    }
    return iptv_refresh_selected_source_async();
}

bool iptv_remove_selected_source(void)
{
    IptvSource removed;
    int selected;

    mutexLock(&g_mutex);
    if (g_refreshing)
    {
        mutexUnlock(&g_mutex);
        iptv_set_status("Wait for the current IPTV refresh to finish.");
        return false;
    }
    selected = g_source_selected_index;
    if (selected < 0 || selected >= g_source_count || g_sources[selected].local)
    {
        mutexUnlock(&g_mutex);
        iptv_set_status("Local M3U sources are removed by deleting their SD card file.");
        return false;
    }
    removed = g_sources[selected];
    if (!iptv_remove_preinstalled_source(removed.url))
    {
        mutexUnlock(&g_mutex);
        iptv_set_status("Failed to remove the source from sources.txt.");
        return false;
    }
    memmove(&g_sources[selected], &g_sources[selected + 1], (size_t)(g_source_count - selected - 1) * sizeof(g_sources[0]));
    --g_source_count;
    bool saved = iptv_save_sources_locked();
    mutexUnlock(&g_mutex);
    if (!saved)
    {
        iptv_set_status("Failed to update IPTV source list.");
        return false;
    }
    remove(removed.cache_path);
    char epg_path[IPTV_PATH_MAX];
    snprintf(epg_path, sizeof(epg_path), "%s/%08x.xmltv", IPTV_EPG_CACHE_DIR, removed.id);
    remove(epg_path);
    if (!iptv_reload())
        return false;
    iptv_set_status("IPTV source, playlist cache, and programme guide removed.");
    return true;
}

bool iptv_refresh_selected_source_async(void)
{
    uint32_t id = 0;
    mutexLock(&g_mutex);
    if (!g_worker_started || g_source_count <= 0 || g_refreshing)
    {
        mutexUnlock(&g_mutex);
        iptv_set_status(g_refreshing ? "An IPTV refresh is already running." : "IPTV background worker is unavailable.");
        return false;
    }
    id = g_sources[g_source_selected_index].id;
    g_refreshing = true;
    g_refresh_source_requested = id;
    snprintf(g_status, sizeof(g_status), "Queued refresh for %s.", g_sources[g_source_selected_index].name);
    condvarWakeOne(&g_worker_cond);
    mutexUnlock(&g_mutex);
    return true;
}

bool iptv_refresh_all_async(void)
{
    mutexLock(&g_mutex);
    if (!g_worker_started || g_refreshing)
    {
        mutexUnlock(&g_mutex);
        iptv_set_status(g_refreshing ? "An IPTV refresh is already running." : "IPTV background worker is unavailable.");
        return false;
    }
    mutexUnlock(&g_mutex);

    if (!iptv_reload())
    {
        iptv_set_status("Failed to reload IPTV sources from SD.");
        return false;
    }

    mutexLock(&g_mutex);
    if (!g_worker_started || g_refreshing || g_source_count <= 0)
    {
        bool busy = g_refreshing;
        bool worker_available = g_worker_started;
        mutexUnlock(&g_mutex);
        if (busy)
            iptv_set_status("An IPTV refresh is already running.");
        else if (!worker_available)
            iptv_set_status("IPTV background worker is unavailable.");
        else
            iptv_set_status("No IPTV sources found. Check sources.txt on the Switch SD card.");
        return false;
    }
    g_refreshing = true;
    g_refresh_all_requested = true;
    g_preinstalled_refresh_needed = false;
    g_preinstalled_changed_count = 0;
    iptv_copy(g_status, sizeof(g_status), "Queued refresh for all IPTV sources.");
    condvarWakeOne(&g_worker_cond);
    mutexUnlock(&g_mutex);
    return true;
}

void iptv_cycle_filter(int delta)
{
    uint32_t selected_id = 0;
    mutexLock(&g_mutex);
    if (g_visible_count > 0 && g_selected_index < g_visible_count)
        selected_id = g_channels[g_visible[g_selected_index]].id;
    int count = g_group_count + 3;
    g_filter_index = (g_filter_index + delta) % count;
    if (g_filter_index < 0)
        g_filter_index += count;
    iptv_rebuild_visible_locked(selected_id);
    iptv_queue_selected_logo_locked();
    mutexUnlock(&g_mutex);
}

bool iptv_prompt_search(void)
{
    char search[IPTV_SEARCH_MAX];
    char initial[IPTV_SEARCH_MAX];
    mutexLock(&g_mutex);
    iptv_copy(initial, sizeof(initial), g_search);
    mutexUnlock(&g_mutex);
    if (!iptv_prompt_text("Search IPTV",
                          "Match channel name, group, or tvg-id",
                          "News",
                          "Filter",
                          initial,
                          search,
                          sizeof(search)))
        return false;
    mutexLock(&g_mutex);
    iptv_copy(g_search, sizeof(g_search), search);
    iptv_rebuild_visible_locked(0);
    iptv_queue_selected_logo_locked();
    mutexUnlock(&g_mutex);
    return true;
}

void iptv_clear_search(void)
{
    mutexLock(&g_mutex);
    g_search[0] = '\0';
    iptv_rebuild_visible_locked(0);
    iptv_queue_selected_logo_locked();
    mutexUnlock(&g_mutex);
}

bool iptv_toggle_selected_favorite(void)
{
    uint32_t favorites[IPTV_MAX_CHANNELS];
    int count = 0;
    uint32_t selected_id;

    mutexLock(&g_mutex);
    if (g_visible_count <= 0 || g_selected_index >= g_visible_count)
    {
        mutexUnlock(&g_mutex);
        return false;
    }
    selected_id = g_channels[g_visible[g_selected_index]].id;
    bool make_favorite = !g_channels[g_visible[g_selected_index]].favorite;
    for (int i = 0; i < g_channel_count; ++i)
    {
        if (g_channels[i].id == selected_id)
            g_channels[i].favorite = make_favorite;
        if (g_channels[i].favorite)
            favorites[count++] = g_channels[i].id;
    }
    bool saved = iptv_write_id_file(IPTV_FAVORITES_FILE, favorites, count);
    iptv_rebuild_visible_locked(selected_id);
    snprintf(g_status, sizeof(g_status), "%s favorite.", make_favorite ? "Added to" : "Removed from");
    mutexUnlock(&g_mutex);
    return saved;
}

static bool iptv_play_url_named(const char *url,
                                const char *name,
                                const char *display_title,
                                uint32_t channel_id)
{
    RendererState previous_state;

    if (!iptv_url_is_playable(url))
    {
        iptv_set_status("Invalid IPTV URL.");
        return false;
    }

    previous_state = renderer_get_state();
    if (previous_state != PLAYER_STATE_IDLE && previous_state != PLAYER_STATE_STOPPED)
    {
        // Queue an explicit stop before replacing a live stream so mpv tears
        // down the old demuxer/decoder before opening the next channel.
        if (!renderer_stop())
            log_warn("[iptv] failed to stop previous stream before channel switch state=%d\n", (int)previous_state);
    }

    if (!renderer_set_uri(url, display_title) || !renderer_play())
    {
        iptv_set_status("Failed to start IPTV playback.");
        return false;
    }
    mutexLock(&g_mutex);
    iptv_copy(g_last_url, sizeof(g_last_url), url);
    iptv_copy(g_last_name, sizeof(g_last_name), name && name[0] ? name : "Direct IPTV URL");
    snprintf(g_status, sizeof(g_status), "Playing IPTV: %s", g_last_name);
    if (channel_id)
    {
        int existing = -1;
        for (int i = 0; i < g_recent_count; ++i)
        {
            if (g_recent_ids[i] == channel_id)
            {
                existing = i;
                break;
            }
        }
        if (existing >= 0)
            memmove(&g_recent_ids[1], &g_recent_ids[0], (size_t)existing * sizeof(g_recent_ids[0]));
        else
        {
            int move = g_recent_count < IPTV_MAX_RECENT ? g_recent_count : IPTV_MAX_RECENT - 1;
            memmove(&g_recent_ids[1], &g_recent_ids[0], (size_t)move * sizeof(g_recent_ids[0]));
            if (g_recent_count < IPTV_MAX_RECENT)
                ++g_recent_count;
        }
        g_recent_ids[0] = channel_id;
        for (int i = 0; i < g_channel_count; ++i)
            g_channels[i].recent = iptv_id_in_list(g_recent_ids, g_recent_count, g_channels[i].id);
        iptv_write_id_file(IPTV_RECENT_FILE, g_recent_ids, g_recent_count);
        iptv_rebuild_visible_locked(channel_id);
    }
    mutexUnlock(&g_mutex);
    return true;
}

bool iptv_play_channel(int index)
{
    IptvChannel channel;
    char display_title[IPTV_NAME_MAX + IPTV_EPG_TITLE_MAX + 4];

    if (!iptv_get_channel(index, &channel))
    {
        iptv_set_status("No IPTV channel selected.");
        return false;
    }
    iptv_set_selected_index(index);
    if (channel.now_title[0])
        snprintf(display_title, sizeof(display_title), "%s - %s", channel.name, channel.now_title);
    else
        snprintf(display_title, sizeof(display_title), "%s", channel.name);
    return iptv_play_url_named(channel.url, channel.name, display_title, channel.id);
}

bool iptv_play_url(const char *url)
{
    return iptv_play_url_named(url, "Direct IPTV URL", "Direct IPTV URL", 0);
}

bool iptv_prompt_url(char *out_url, size_t out_url_size)
{
    char initial[IPTV_URL_MAX];
    mutexLock(&g_mutex);
    iptv_copy(initial, sizeof(initial), g_last_url);
    mutexUnlock(&g_mutex);
    if (!iptv_prompt_text("IPTV URL",
                          "Enter a media URL or an M3U playlist URL",
                          "M3U lists open in Channels; media URLs play directly",
                          "Open",
                          initial,
                          out_url,
                          out_url_size))
    {
        iptv_set_status("IPTV URL input closed.");
        return false;
    }
    return true;
}
