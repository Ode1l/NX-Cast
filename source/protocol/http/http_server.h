#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char *method;
    const char *raw_path;
    const char *path;
    const char *host;
    const char *request;
    size_t request_len;
    const char *client_ip;
    uint16_t client_port;
} HttpRequestContext;

typedef bool (*HttpRequestHandler)(const HttpRequestContext *ctx,
                                   char *response,
                                   size_t response_size,
                                   size_t *response_len,
                                   void *user_data);

typedef struct
{
    uint16_t port;
    HttpRequestHandler handler;
    void *user_data;
} HttpServerConfig;

bool http_server_start(const HttpServerConfig *config);
void http_server_stop(void);
bool http_server_is_running(void);
