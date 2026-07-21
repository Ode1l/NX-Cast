#include "protocol/dlna/control/soap_writer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_valid_output(void)
{
    SoapActionOutput output = {0};

    assert(soap_writer_element_text(&output, "Title", "A&B <C>"));
    assert(strcmp(output.output_xml,
                  "<Title>A&amp;B &lt;C&gt;</Title>") == 0);
    assert(output.output_len == strlen(output.output_xml));
    soap_writer_clear(&output);
    assert(output.output_len == 0u && output.output_xml[0] == '\0');
    assert(soap_writer_appendf(&output, "%s:%d", "volume", 42));
    assert(strcmp(output.output_xml, "volume:42") == 0);
    soap_writer_dispose(&output);
}

static void test_overflow_rejected(void)
{
    SoapActionOutput output = {0};
    SoapActionOutput invalid = {
        .output_len = 1u,
        .output_cap = 1u,
    };
    char *large = malloc(20001u);

    assert(large);
    memset(large, '&', 20000u);
    large[20000] = '\0';
    assert(!soap_writer_append_len(&output, "x", SIZE_MAX));
    assert(output.output_xml == NULL && output.output_len == 0u);
    assert(!soap_writer_append_escaped(&output, large));
    assert(output.output_xml == NULL && output.output_len == 0u);
    assert(!soap_writer_append_raw(&invalid, "x"));
    free(large);
    soap_writer_dispose(&output);
}

int main(void)
{
    test_valid_output();
    test_overflow_rejected();
    puts("SOAP writer tests passed");
    return 0;
}
