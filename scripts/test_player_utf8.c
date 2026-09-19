#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "player/ui/utf8.h"

int main(void)
{
    char output[32];
    static const char chinese[] = "中文频道";
    static const char mixed[] = "CCTV 中文";
    static const char invalid[] = {'A', (char)0xe4, 'B', '\0'};

    assert(player_utf8_copy_prefix(output, sizeof(output), chinese, 6u) == 6u);
    assert(strcmp(output, "中文") == 0);
    assert(player_utf8_copy_prefix(output, sizeof(output), chinese, 7u) == 6u);
    assert(strcmp(output, "中文") == 0);
    assert(player_utf8_copy_prefix(output, sizeof(output), mixed,
                                   sizeof(mixed)) == strlen(mixed));
    assert(strcmp(output, mixed) == 0);
    assert(player_utf8_copy_prefix(output, 5u, chinese, sizeof(chinese)) == 3u);
    assert(strcmp(output, "中") == 0);
    assert(player_utf8_copy_prefix(output, sizeof(output), invalid,
                                   sizeof(invalid)) == 1u);
    assert(strcmp(output, "A") == 0);
    assert(player_utf8_copy_prefix(output, sizeof(output), NULL, 8u) == 0u);
    assert(output[0] == '\0');

    puts("player UTF-8 tests passed");
    return 0;
}
