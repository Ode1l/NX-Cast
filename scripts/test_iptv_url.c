#include "iptv/url.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_url(const char *base, const char *reference,
                       const char *expected)
{
    char output[256];

    assert(iptv_url_resolve(base, reference, output, sizeof(output)));
    assert(strcmp(output, expected) == 0);
}

int main(void)
{
    char exact[5];
    char short_output[4] = "bad";

    expect_url(NULL, "https://cdn.test/live.m3u8",
               "https://cdn.test/live.m3u8");
    expect_url("https://origin.test/list.m3u", "https://cdn.test/live.m3u8",
               "https://cdn.test/live.m3u8");
    expect_url("https://origin.test/path/list.m3u", "//cdn.test/live.ts",
               "https://cdn.test/live.ts");
    expect_url("https://origin.test/path/list.m3u?token=1", "/live/one.ts",
               "https://origin.test/live/one.ts");
    expect_url("https://origin.test/path/list.m3u?token=1", "one.ts",
               "https://origin.test/path/one.ts");
    expect_url("https://origin.test?token=1", "one.ts",
               "https://origin.test/one.ts");
    expect_url("sdmc:/switch/NX-Cast/iptv/list.m3u", "one.ts",
               "sdmc:/switch/NX-Cast/iptv/one.ts");
    expect_url("list.m3u", "one.ts", "one.ts");
    expect_url("sdmc:/list.m3u", "/absolute/one.ts", "/absolute/one.ts");

    assert(iptv_url_resolve(NULL, "four", exact, sizeof(exact)));
    assert(strcmp(exact, "four") == 0);
    assert(!iptv_url_resolve(NULL, "four", short_output,
                             sizeof(short_output)));
    assert(short_output[0] == '\0');
    assert(!iptv_url_resolve(NULL, "", exact, sizeof(exact)));
    assert(exact[0] == '\0');
    assert(!iptv_url_resolve(NULL, "one", NULL, 0u));

    puts("IPTV URL tests passed");
    return 0;
}
