#pragma once

#include <stdbool.h>

#include <switch.h>

#include "player/view.h"

typedef struct
{
    PlayerViewStatus status;
    Framebuffer framebuffer;
    bool framebuffer_ready;
} ViewContext;

bool frontend_connect(ViewContext *ctx);
bool frontend_open(ViewContext *ctx);
void frontend_close(ViewContext *ctx, bool restore_console);
bool frontend_render(ViewContext *ctx);
