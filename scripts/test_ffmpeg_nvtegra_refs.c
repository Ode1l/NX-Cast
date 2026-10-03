#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FFMIN(a, b) ((a) < (b) ? (a) : (b))
#define FF_ARRAY_ELEMS(a) ((int)(sizeof(a) / sizeof((a)[0])))

typedef struct H264Picture {
    int id;
} H264Picture;

typedef struct H264Context {
    int short_ref_count;
    H264Picture *short_ref[32];
    H264Picture *long_ref[32];
} H264Context;

static int collect_refs(H264Context *h, H264Picture **output)
{
    H264Picture *refs[17] = {0};
    int num_refs, max, i;

/* Compile the reference-list block extracted from the actual FFmpeg source. */
#include NXCAST_REF_LIST_FILE

    memcpy(output, refs, sizeof(refs));
    return num_refs;
}

int main(void)
{
    H264Picture short_picture = {1}, long_picture = {2}, second_long = {3};
    H264Context h = {0};
    H264Picture *refs[17] = {0};

    assert(collect_refs(&h, refs) == 0);
    h.short_ref_count = 1;
    h.short_ref[0] = &short_picture;
    assert(collect_refs(&h, refs) == 1 && refs[0] == &short_picture);

    h = (H264Context){0};
    h.long_ref[0] = &long_picture;
    assert(collect_refs(&h, refs) == 1 && refs[0] == &long_picture);

    h.long_ref[0] = NULL;
    h.long_ref[7] = &long_picture;
    assert(collect_refs(&h, refs) == 1 && refs[0] == &long_picture);

    h.short_ref_count = 1;
    h.short_ref[0] = &short_picture;
    h.long_ref[15] = &second_long;
    assert(collect_refs(&h, refs) == 3);
    assert(refs[0] == &short_picture && refs[1] == &long_picture &&
           refs[2] == &second_long);

    puts("nvtegra reference-list tests passed");
    return 0;
}
