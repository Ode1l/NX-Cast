#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player/seek_target.h"

static int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static void check_parse(const char *text, int expected)
{
    int actual = -1;

    CHECK(player_seek_target_parse_ms(text, &actual));
    CHECK(actual == expected);
}

int main(void)
{
    int value = 7;
    char *formatted;

    check_parse("00:00:1268", 1268000);
    check_parse("01:02:03.456", 3723456);
    check_parse("12.3456", 12346);
    check_parse("2147483.647", INT_MAX);
    CHECK(!player_seek_target_parse_ms("2147483.648", &value));
    CHECK(!player_seek_target_parse_ms("9223372036854775807:00:00", &value));
    CHECK(!player_seek_target_parse_ms("inf", &value));
    CHECK(!player_seek_target_parse_ms("nan", &value));
    CHECK(!player_seek_target_parse_ms("-1", &value));
    CHECK(!player_seek_target_parse_ms(NULL, &value));
    CHECK(!player_seek_target_parse_ms("1", NULL));

    formatted = player_seek_target_format_hhmmss_alloc(3723456);
    CHECK(formatted != NULL);
    CHECK(formatted && strcmp(formatted, "01:02:03") == 0);
    free(formatted);

    if (g_failures != 0)
    {
        fprintf(stderr, "seek target tests failed: %d\n", g_failures);
        return 1;
    }
    puts("seek target tests passed");
    return 0;
}
