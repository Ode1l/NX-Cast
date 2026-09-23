/* cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pthread
 * -Iscripts/iptv_test_stubs -Isource scripts/test_iptv_data.c
 * source/iptv/url.c source/iptv/xmltv.c source/player/ui/home.c -lz
 * -fsanitize=address,undefined -o /tmp/test_iptv_data && /tmp/test_iptv_data
 */
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

static bool fail_growth;
static bool fail_allocation;
static void *test_malloc(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}
static void *test_realloc(void *data, size_t size)
{
    return fail_growth ? NULL : realloc(data, size);
}
#define realloc test_realloc
#define malloc test_malloc
#include "../source/iptv/iptv.c"
#undef realloc
#undef malloc

static int play_requests;
static bool owns_iptv;
void log_info(const char *fmt, ...) { (void)fmt; }
void log_warn(const char *fmt, ...) { (void)fmt; }
bool iptv_fetch_to_file(const char *u, const char *d, size_t n,
                        const IptvFetchControl *c, char *e, size_t s)
{ (void)u; (void)d; (void)n; (void)c; (void)e; (void)s; return false; }
uint32_t player_trace_begin_media(const char *r, const char *u, const char *m)
{ (void)r; (void)u; (void)m; return 1; }
uint32_t player_trace_uri_hash(const char *u) { (void)u; return 1; }
bool protocol_coordinator_media_begin(PlayerMediaOwner o, uint64_t t, ProtocolMediaTransaction *p)
{ (void)o; (void)t; memset(p, 0, sizeof(*p)); owns_iptv = true; return true; }
void protocol_coordinator_media_end(ProtocolMediaTransaction *t) { (void)t; }
void protocol_coordinator_media_abort(ProtocolMediaTransaction *t) { (void)t; }
bool protocol_coordinator_media_release_current(PlayerMediaOwner o, uint64_t t)
{ (void)o; (void)t; owns_iptv = false; return true; }
bool player_ownership_matches(PlayerMediaOwner o, uint64_t t)
{ (void)o; (void)t; return owns_iptv; }
const char *player_media_owner_name(PlayerMediaOwner o) { (void)o; return "test"; }
PlayerCommandStatus player_submit_command_async(const PlayerCommandRequest *r)
{ (void)r; ++play_requests; return PLAYER_COMMAND_STATUS_ACCEPTED; }
bool player_command_status_succeeded(PlayerCommandStatus s)
{ return s == PLAYER_COMMAND_STATUS_ACCEPTED; }
const char *player_command_status_name(PlayerCommandStatus s) { (void)s; return "accepted"; }

static void write_playlist(const char *path, int count)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("#EXTM3U\n", file);
    for (int i = 0; i < count; ++i)
        fprintf(file, "#EXTINF:-1 tvg-id=\"id-%d\" group-title=\"Group %d\",Channel %d\nhttps://example.com/%d.ts\n",
                i, i % 100, i, i);
    assert(fclose(file) == 0);
}

int main(void)
{
    char early_name[64];
    int early_count = -1;
    assert(iptv_get_filter(0, early_name, sizeof(early_name), &early_count));
    assert(early_count == 0 && strcmp(early_name, "All channels") == 0);
    assert(!iptv_get_filter(3, early_name, sizeof(early_name), NULL));
    assert(iptv_get_filter_count() == 3);
    assert(iptv_get_filter_index() == 0);
    assert(iptv_get_channel_count() == 0);
    assert(iptv_get_source_count() == 0);
    assert(iptv_get_source_filter() == 0);
    assert(iptv_get_playing_channel_id() == 0);
    iptv_set_filter(1);
    iptv_set_source_filter(1);
    char directory[] = "/tmp/nxcast-iptv-data-XXXXXX";
    char original[4096];
    assert(getcwd(original, sizeof(original)));
    assert(mkdtemp(directory));
    assert(chdir(directory) == 0);
    assert(mkdir("sdmc:", 0700) == 0);
    assert(mkdir("sdmc:/switch", 0700) == 0);
    home_ui_init(NULL, false);
    assert(iptv_init());
    assert(iptv_get_channel_count() == 0);
    assert(iptv_reload()); /* Empty catalogs have no allocated channel buffer. */
    iptv_deinit();

    const char *large = IPTV_ROOT_DIR "/large.m3u8";
    write_playlist(large, 10001);
    uint32_t large_id = iptv_hash_string(large);
    FILE *favorites = fopen(IPTV_FAVORITES_FILE, "wb");
    assert(favorites);
    for (int i = 0; i < 10001; ++i)
    {
        char id[32];
        snprintf(id, sizeof(id), "id-%d", i);
        fprintf(favorites, "%08x\n", iptv_channel_id(large_id, id, ""));
    }
    assert(fclose(favorites) == 0);
    for (int i = 0; i < 40; ++i)
    {
        char path[IPTV_PATH_MAX];
        snprintf(path, sizeof(path), IPTV_ROOT_DIR "/local-%02d.m3u", i);
        write_playlist(path, 1);
    }
    FILE *sources = fopen(IPTV_PREINSTALLED_SOURCES_FILE, "wb");
    assert(sources);
    for (int i = 0; i < 40; ++i)
        fprintf(sources, "Raw source %d|https://example.com/list-%d.m3u\n", i, i);
    assert(fclose(sources) == 0);
    clock_t start = clock();
    assert(iptv_init());
    double load_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    IptvState state;
    assert(iptv_get_state(&state));
    assert(state.channel_count == 10041);
    assert(state.source_count == 81);
    assert(state.group_count == 100);
    assert(state.favorite_count == 10001);
    assert(play_requests == 0);
    assert(iptv_get_playing_channel_id() == 0);
    assert(iptv_get_filter_count() == 103);
    int count;
    char name[IPTV_GROUP_MAX];
    assert(iptv_get_filter(1, name, sizeof(name), &count) && count == 10001);
    mutexLock(&g_mutex);
    iptv_copy(g_search, sizeof(g_search), "Channel 10000");
    iptv_rebuild_visible_locked(0);
    mutexUnlock(&g_mutex);
    assert(iptv_get_filter(0, name, sizeof(name), &count) && count == 1);
    assert(iptv_get_filter(1, name, sizeof(name), &count) && count == 1);
    iptv_clear_search();
    iptv_set_source_filter(large_id);
    assert(iptv_get_source_filter() == large_id);
    assert(iptv_get_channel_count() == 10001);
    iptv_set_filter(3);
    assert(iptv_get_filter_index() == 3);
    assert(iptv_get_filter(3, name, sizeof(name), &count) && count == 101);
    assert(iptv_get_channel_count() == count);
    iptv_set_filter(INT_MAX);
    assert(iptv_get_filter_index() == 0);
    iptv_set_filter(-1);
    assert(iptv_get_filter_index() == 0);
    assert(!iptv_get_filter(103, name, sizeof(name), &count));
    iptv_set_source_filter(UINT32_MAX);
    assert(iptv_get_source_filter() == 0);
    iptv_set_source_filter(large_id);
    iptv_set_filter(1);
    iptv_set_selected_index(10000);
    IptvChannel channel;
    assert(iptv_get_channel(10000, &channel));
    assert(strcmp(channel.name, "Channel 10000") == 0);
    assert(iptv_toggle_selected_favorite());
    assert(iptv_get_channel_count() == 10000);
    assert(iptv_reload());
    assert(iptv_get_state(&state) && state.favorite_count == 10000);
    iptv_set_filter(0);
    assert(iptv_play_channel(10000));
    assert(iptv_get_playing_channel_id() == channel.id);
    owns_iptv = false;
    assert(iptv_get_playing_channel_id() == 0);

    /* Getter cost must be independent of catalog size after rebuilding. */
    start = clock();
    for (int frame = 0; frame < 10000; ++frame)
    {
        assert(iptv_get_state(&state));
        assert(iptv_get_filter(3, name, sizeof(name), &count));
        for (int row = 0; row < 10; ++row)
            assert(iptv_get_channel(row, &channel));
    }
    double read_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;

    fail_growth = true;
    assert(!iptv_reload());
    fail_growth = false;
    fail_allocation = true; /* Publication must also be transactional. */
    assert(!iptv_reload());
    fail_allocation = false;
    assert(iptv_get_channel_count() == 10001);
    assert(iptv_get_state(&state));
    assert(strstr(state.status, "Previous catalog retained"));
    home_ui_toggle_language();
    assert(iptv_get_state(&state));
    assert(strstr(state.status, "已保留原列表"));
    assert(iptv_get_filter(0, name, sizeof(name), &count));
    assert(strcmp(name, "全部频道") == 0);
    IptvSource source;
    assert(iptv_get_source(0, &source));
    assert(strcmp(source.name, "Raw source 0") == 0);
    assert(strcmp(source.status, "尚未缓存，请刷新此来源") == 0);
    home_ui_toggle_language();
    assert(iptv_get_source(0, &source));
    assert(strcmp(source.status, "Not cached - refresh this source") == 0);

    assert(remove(large) == 0);
    write_playlist(IPTV_ROOT_DIR "/new.M3U8", 2);
    assert(iptv_reload());
    assert(iptv_get_source_filter() == 0);
    assert(iptv_get_channel_count() == 42);
    assert(iptv_get_source_count() == 81);
    iptv_deinit();
    assert(iptv_init());
    assert(iptv_get_channel_count() == 42);
    assert(play_requests == 1); /* Startup/reload never submits playback. */
    iptv_deinit();
    assert(iptv_get_filter(0, name, sizeof(name), &count) && count == 0);
    assert(iptv_get_filter_count() == 3);

    size_t capacity = 0;
    bool failed = false;
    void *data = iptv_data_reserve(NULL, &capacity, 16, sizeof(IptvChannel), IPTV_CHANNEL_BYTES, &failed);
    assert(data && !failed);
    void *old = data;
    data = iptv_data_reserve(data, &capacity, SIZE_MAX, sizeof(IptvChannel), IPTV_CHANNEL_BYTES, &failed);
    assert(failed && data == old && capacity == 16);
    free(data);
    capacity = 0;
    failed = false;
    data = iptv_data_reserve(NULL, &capacity, 33, sizeof(IptvSource), 32 * sizeof(IptvSource), &failed);
    assert(failed && data == NULL && capacity == 0);
    printf("IPTV data tests passed: 10041 channels / 81 sources / 100 groups / 10001 favorites.\n"
           "Channel struct: %zu bytes; 16384-slot channel allocation: %.2f MiB; dual catalogs: %.2f MiB.\n"
           "Initial load: %.3f CPU seconds; 10000 frames of state/filter/10-row reads: %.3f CPU seconds.\n",
           sizeof(IptvChannel), 16384.0 * sizeof(IptvChannel) / 1048576,
           32768.0 * sizeof(IptvChannel) / 1048576, load_seconds, read_seconds);

    assert(chdir(original) == 0);
    /* The isolated fixture is intentionally retained for inspection. */
    printf("Fixture: %s\n", directory);
    return 0;
}
