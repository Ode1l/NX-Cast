#include "player/render/imgui_overlay.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include <imgui.h>
#include <switch.h>

extern "C" {
#include "log/log.h"
#include "player/ui/layout.h"
#include "player/ui/overlay.h"
}

extern "C" {
#include "imgui_shaders.inc"
}

namespace
{
constexpr uint32_t kVtxBufferInitialSize = 1024U * 1024U;
constexpr uint32_t kIdxBufferInitialSize = 512U * 1024U;
constexpr uint32_t kImageCount = FRONTEND_DK3D_FRAMEBUFFER_COUNT;
constexpr uint32_t kDescriptorCount = 1;

struct VertUbo
{
    float proj[4][4];
};

struct FragUbo
{
    uint32_t font;
    uint32_t padding[3];
};

struct Buffer
{
    DkMemBlock mem = nullptr;
    uint32_t size = 0;
};

bool g_initialized = false;
bool g_failed = false;
bool g_pl_initialized = false;
bool g_context_created = false;
DkDevice g_device = nullptr;
DkMemBlock g_code_mem = nullptr;
DkMemBlock g_ubo_mem = nullptr;
DkMemBlock g_font_image_mem = nullptr;
DkMemBlock g_descriptor_mem = nullptr;
DkImage g_font_image;
DkShader g_shaders[2];
Buffer g_vtx[kImageCount];
Buffer g_idx[kImageCount];
DkResHandle g_font_texture_handle = 0;

constexpr float kPi = 3.14159265358979323846f;

uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

uint32_t align_size(size_t value, uint32_t alignment)
{
    return align_up((uint32_t)value, alignment);
}

DkMemBlock make_memblock(DkDevice device, uint32_t size, uint32_t flags)
{
    DkMemBlockMaker maker;

    dkMemBlockMakerDefaults(&maker, device, align_up(size, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    return dkMemBlockCreate(&maker);
}

void destroy_buffer(Buffer &buffer)
{
    if (buffer.mem)
        dkMemBlockDestroy(buffer.mem);
    buffer.mem = nullptr;
    buffer.size = 0;
}

void destroy_resources()
{
    for (uint32_t i = 0; i < kImageCount; ++i)
    {
        destroy_buffer(g_vtx[i]);
        destroy_buffer(g_idx[i]);
    }

    if (g_descriptor_mem)
        dkMemBlockDestroy(g_descriptor_mem);
    if (g_font_image_mem)
        dkMemBlockDestroy(g_font_image_mem);
    if (g_ubo_mem)
        dkMemBlockDestroy(g_ubo_mem);
    if (g_code_mem)
        dkMemBlockDestroy(g_code_mem);

    g_descriptor_mem = nullptr;
    g_font_image_mem = nullptr;
    g_ubo_mem = nullptr;
    g_code_mem = nullptr;
    g_font_texture_handle = 0;
    memset(&g_font_image, 0, sizeof(g_font_image));
    memset(g_shaders, 0, sizeof(g_shaders));
}

bool load_embedded_shaders(DkDevice device)
{
    const unsigned char *shader_data[2] = {
        nxcast_imgui_vsh_dksh,
        nxcast_imgui_fsh_dksh,
    };
    const unsigned int shader_size[2] = {
        nxcast_imgui_vsh_dksh_len,
        nxcast_imgui_fsh_dksh_len,
    };
    uint32_t offsets[2] = {0, 0};
    uint32_t code_size = DK_SHADER_CODE_UNUSABLE_SIZE;

    for (uint32_t i = 0; i < 2; ++i)
    {
        offsets[i] = code_size;
        code_size = align_up(code_size + shader_size[i], DK_SHADER_CODE_ALIGNMENT);
    }

    g_code_mem = make_memblock(device,
                               code_size,
                               DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    if (!g_code_mem)
        return false;

    auto *dst = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(g_code_mem));
    for (uint32_t i = 0; i < 2; ++i)
    {
        DkShaderMaker maker;

        memcpy(dst + offsets[i], shader_data[i], shader_size[i]);
        dkShaderMakerDefaults(&maker, g_code_mem, offsets[i]);
        dkShaderInitialize(&g_shaders[i], &maker);
        if (!dkShaderIsValid(&g_shaders[i]))
            return false;
    }

    return true;
}

#if defined(NXCAST_USE_PACKAGED_FONT) && NXCAST_USE_PACKAGED_FONT
bool load_packaged_font(ImGuiIO &io)
{
    static const char *kFontPath = "sdmc:/switch/NX-Cast/fonts/switch_font.ttf";
    FILE *file = fopen(kFontPath, "rb");
    if (!file)
        return false;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }

    void *data = malloc((size_t)size);
    if (!data)
    {
        fclose(file);
        return false;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size)
    {
        free(data);
        fclose(file);
        return false;
    }
    fclose(file);

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = true;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    if (!io.Fonts->AddFontFromMemoryTTF(data,
                                        (int)size,
                                        22.0f,
                                        &cfg,
                                        io.Fonts->GetGlyphRangesDefault()))
    {
        free(data);
        return false;
    }

    log_info("[player-imgui] loaded packaged font path=%s bytes=%ld\n", kFontPath, size);
    return true;
}
#endif

void load_switch_fonts(ImGuiIO &io)
{
    PlFontData standard;
    PlFontData extended;
    ImFontConfig cfg;
    ImWchar extended_range[] = {0xe000, 0xe152, 0};

#if defined(NXCAST_USE_PACKAGED_FONT) && NXCAST_USE_PACKAGED_FONT
    if (load_packaged_font(io))
        return;
#endif

    if (!g_pl_initialized && R_SUCCEEDED(plInitialize(PlServiceType_User)))
        g_pl_initialized = true;

    if (!g_pl_initialized ||
        R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard)) ||
        R_FAILED(plGetSharedFontByType(&extended, PlSharedFontType_NintendoExt)))
    {
        io.Fonts->AddFontDefault();
        return;
    }

    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    io.Fonts->AddFontFromMemoryTTF(standard.address,
                                   standard.size,
                                   22.0f,
                                   &cfg,
                                   io.Fonts->GetGlyphRangesDefault());

    cfg.MergeMode = true;
    io.Fonts->AddFontFromMemoryTTF(extended.address,
                                   extended.size,
                                   22.0f,
                                   &cfg,
                                   extended_range);
}

bool create_imgui_context()
{
    ImGuiContext *ctx = ImGui::CreateContext();
    if (!ctx)
        return false;

    g_context_created = true;
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendRendererName = "nxcast-imgui-deko3d";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    load_switch_fonts(io);
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 16.0f;
    style.FrameRounding = 12.0f;
    style.GrabRounding = 12.0f;
    style.ScrollbarRounding = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    return true;
}

bool create_font_texture(DkDevice device, DkQueue queue, DkCmdBuf cmdbuf)
{
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    DkMemBlock upload_mem = nullptr;
    DkImageLayoutMaker layout_maker;
    DkImageLayout layout;
    DkImageView font_view;
    DkCopyBuf copy_src;
    DkImageRect copy_rect;
    DkSampler sampler;
    DkSamplerDescriptor *sampler_desc = nullptr;
    DkImageDescriptor *image_desc = nullptr;

    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0)
        return false;

    upload_mem = make_memblock(device,
                               (uint32_t)(width * height),
                               DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!upload_mem)
        return false;
    memcpy(dkMemBlockGetCpuAddr(upload_mem), pixels, (size_t)width * (size_t)height);

    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.flags = 0;
    layout_maker.format = DkImageFormat_R8_Unorm;
    layout_maker.dimensions[0] = (uint32_t)width;
    layout_maker.dimensions[1] = (uint32_t)height;
    dkImageLayoutInitialize(&layout, &layout_maker);

    g_font_image_mem = make_memblock(device,
                                     align_up((uint32_t)dkImageLayoutGetSize(&layout),
                                              std::max<uint32_t>(dkImageLayoutGetAlignment(&layout), DK_MEMBLOCK_ALIGNMENT)),
                                     DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!g_font_image_mem)
    {
        dkMemBlockDestroy(upload_mem);
        return false;
    }

    dkImageInitialize(&g_font_image, &layout, g_font_image_mem, 0);
    dkImageViewDefaults(&font_view, &g_font_image);

    g_descriptor_mem = make_memblock(device,
                                     kDescriptorCount * (sizeof(DkSamplerDescriptor) + sizeof(DkImageDescriptor)),
                                     DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!g_descriptor_mem)
    {
        dkMemBlockDestroy(upload_mem);
        return false;
    }

    sampler_desc = static_cast<DkSamplerDescriptor *>(dkMemBlockGetCpuAddr(g_descriptor_mem));
    image_desc = reinterpret_cast<DkImageDescriptor *>(sampler_desc + kDescriptorCount);

    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Linear;
    sampler.magFilter = DkFilter_Linear;
    sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
    dkSamplerDescriptorInitialize(&sampler_desc[0], &sampler);
    dkImageDescriptorInitialize(&image_desc[0], &font_view, false, false);

    g_font_texture_handle = dkMakeTextureHandle(0, 0);
    io.Fonts->SetTexID((ImTextureID)g_font_texture_handle);

    copy_src.addr = dkMemBlockGetGpuAddr(upload_mem);
    copy_src.rowLength = 0;
    copy_src.imageHeight = 0;
    copy_rect.x = 0;
    copy_rect.y = 0;
    copy_rect.z = 0;
    copy_rect.width = (uint32_t)width;
    copy_rect.height = (uint32_t)height;
    copy_rect.depth = 1;

    dkCmdBufClear(cmdbuf);
    dkCmdBufCopyBufferToImage(cmdbuf, &copy_src, &font_view, &copy_rect, 0);
    dkQueueSubmitCommands(queue, dkCmdBufFinishList(cmdbuf));
    dkQueueWaitIdle(queue);
    dkMemBlockDestroy(upload_mem);
    return true;
}

bool ensure_buffer(DkDevice device, Buffer &buffer, uint32_t needed, uint32_t initial_size)
{
    uint32_t target_size;

    if (buffer.mem && buffer.size >= needed)
        return true;

    destroy_buffer(buffer);
    target_size = std::max(initial_size, align_up(needed + needed / 8U, DK_MEMBLOCK_ALIGNMENT));
    buffer.mem = make_memblock(device,
                               target_size,
                               DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!buffer.mem)
        return false;
    buffer.size = align_up(target_size, DK_MEMBLOCK_ALIGNMENT);
    return true;
}

void setup_render_state(DkCmdBuf cmdbuf, ImDrawData *draw_data, uint32_t width, uint32_t height)
{
    const float left = draw_data->DisplayPos.x;
    const float right = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float top = draw_data->DisplayPos.y;
    const float bottom = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    VertUbo vert = {};
    FragUbo frag = {};
    DkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    DkRasterizerState rasterizer;
    DkColorState color;
    DkColorWriteState color_write;
    DkBlendState blend;
    DkDepthStencilState depth;
    DkVtxAttribState attrs[3] = {};
    DkVtxBufferState buffers[1] = {};
    const DkShader *shaders[2] = {&g_shaders[0], &g_shaders[1]};
    DkGpuAddr ubo_addr = dkMemBlockGetGpuAddr(g_ubo_mem);
    uint32_t vert_size = align_size(sizeof(VertUbo), DK_UNIFORM_BUF_ALIGNMENT);
    uint32_t frag_size = align_size(sizeof(FragUbo), DK_UNIFORM_BUF_ALIGNMENT);

    vert.proj[0][0] = 2.0f / (right - left);
    vert.proj[1][1] = 2.0f / (top - bottom);
    vert.proj[2][2] = 0.5f;
    vert.proj[3][0] = (right + left) / (left - right);
    vert.proj[3][1] = (top + bottom) / (bottom - top);
    vert.proj[3][2] = 0.5f;
    vert.proj[3][3] = 1.0f;
    frag.font = 1;

    dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);
    dkCmdBufBindShaders(cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindUniformBuffer(cmdbuf, DkStage_Vertex, 0, ubo_addr, vert_size);
    dkCmdBufPushConstants(cmdbuf, ubo_addr, vert_size, 0, sizeof(vert), &vert);
    dkCmdBufBindUniformBuffer(cmdbuf, DkStage_Fragment, 0, ubo_addr + vert_size, frag_size);
    dkCmdBufPushConstants(cmdbuf, ubo_addr + vert_size, frag_size, 0, sizeof(frag), &frag);

    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cmdbuf, &rasterizer);

    dkColorStateDefaults(&color);
    dkColorStateSetBlendEnable(&color, 0, true);
    dkCmdBufBindColorState(cmdbuf, &color);

    dkColorWriteStateDefaults(&color_write);
    dkCmdBufBindColorWriteState(cmdbuf, &color_write);

    dkBlendStateDefaults(&blend);
    dkBlendStateSetFactors(&blend,
                           DkBlendFactor_SrcAlpha,
                           DkBlendFactor_InvSrcAlpha,
                           DkBlendFactor_InvSrcAlpha,
                           DkBlendFactor_Zero);
    dkCmdBufBindBlendState(cmdbuf, 0, &blend);

    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = false;
    depth.depthWriteEnable = false;
    dkCmdBufBindDepthStencilState(cmdbuf, &depth);

    attrs[0].bufferId = 0;
    attrs[0].offset = offsetof(ImDrawVert, pos);
    attrs[0].size = DkVtxAttribSize_2x32;
    attrs[0].type = DkVtxAttribType_Float;
    attrs[1].bufferId = 0;
    attrs[1].offset = offsetof(ImDrawVert, uv);
    attrs[1].size = DkVtxAttribSize_2x32;
    attrs[1].type = DkVtxAttribType_Float;
    attrs[2].bufferId = 0;
    attrs[2].offset = offsetof(ImDrawVert, col);
    attrs[2].size = DkVtxAttribSize_4x8;
    attrs[2].type = DkVtxAttribType_Unorm;
    buffers[0].stride = sizeof(ImDrawVert);
    buffers[0].divisor = 0;
    dkCmdBufBindVtxAttribState(cmdbuf, attrs, 3);
    dkCmdBufBindVtxBufferState(cmdbuf, buffers, 1);
}

const char *state_text(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_LOADING:
        return "Loading";
    case PLAYER_STATE_BUFFERING:
        return "Buffering";
    case PLAYER_STATE_SEEKING:
        return "Seeking";
    case PLAYER_STATE_PAUSED:
        return "Paused";
    case PLAYER_STATE_PLAYING:
        return "Playing";
    case PLAYER_STATE_ERROR:
        return "Error";
    case PLAYER_STATE_STOPPED:
        return "Stopped";
    case PLAYER_STATE_IDLE:
    default:
        return "Ready";
    }
}

bool text_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

bool is_busy_state(PlayerState state)
{
    return state == PLAYER_STATE_LOADING ||
           state == PLAYER_STATE_BUFFERING ||
           state == PLAYER_STATE_SEEKING;
}

bool is_runtime_state(PlayerState state)
{
    return state == PLAYER_STATE_LOADING ||
           state == PLAYER_STATE_BUFFERING ||
           state == PLAYER_STATE_SEEKING ||
           state == PLAYER_STATE_PLAYING ||
           state == PLAYER_STATE_PAUSED;
}

PlayerState display_state_from_context(PlayerState context_state, PlayerState overlay_state)
{
    return is_runtime_state(context_state) ? context_state : overlay_state;
}

void draw_centered_text(ImDrawList *draw, const char *text, ImVec2 center, ImU32 color)
{
    ImVec2 size = ImGui::CalcTextSize(text ? text : "");
    draw->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), color, text ? text : "");
}

void draw_pause_icon(ImDrawList *draw, ImVec2 center, float size, ImU32 color)
{
    const float w = size * 0.18f;
    const float h = size * 0.52f;
    const float gap = size * 0.14f;
    draw->AddRectFilled(ImVec2(center.x - gap - w, center.y - h * 0.5f),
                        ImVec2(center.x - gap, center.y + h * 0.5f),
                        color,
                        w * 0.35f);
    draw->AddRectFilled(ImVec2(center.x + gap, center.y - h * 0.5f),
                        ImVec2(center.x + gap + w, center.y + h * 0.5f),
                        color,
                        w * 0.35f);
}

void draw_play_icon(ImDrawList *draw, ImVec2 center, float size, ImU32 color)
{
    draw->AddTriangleFilled(ImVec2(center.x - size * 0.16f, center.y - size * 0.28f),
                            ImVec2(center.x - size * 0.16f, center.y + size * 0.28f),
                            ImVec2(center.x + size * 0.34f, center.y),
                            color);
}

void draw_seek_icon(ImDrawList *draw, ImVec2 center, int direction, ImU32 color)
{
    const float size = 44.0f;
    const float step = 22.0f * (float)direction;
    for (int i = 0; i < 2; ++i)
    {
        float x = center.x + (float)i * step - step * 0.5f;
        if (direction > 0)
        {
            draw->AddTriangleFilled(ImVec2(x - size * 0.28f, center.y - size * 0.34f),
                                    ImVec2(x - size * 0.28f, center.y + size * 0.34f),
                                    ImVec2(x + size * 0.28f, center.y),
                                    color);
        }
        else
        {
            draw->AddTriangleFilled(ImVec2(x + size * 0.28f, center.y - size * 0.34f),
                                    ImVec2(x + size * 0.28f, center.y + size * 0.34f),
                                    ImVec2(x - size * 0.28f, center.y),
                                    color);
        }
    }
}

void draw_spinner(ImDrawList *draw, ImVec2 center, float radius, ImU32 color)
{
    const float t = (float)ImGui::GetTime() * 5.0f;
    const int segments = 12;
    for (int i = 0; i < segments; ++i)
    {
        float a = t + ((float)i / (float)segments) * kPi * 2.0f;
        float alpha = (float)(i + 1) / (float)segments;
        ImU32 c = IM_COL32(245, 248, 255, (int)(255.0f * alpha));
        if (color != 0)
            c = IM_COL32((color >> IM_COL32_R_SHIFT) & 0xFF,
                         (color >> IM_COL32_G_SHIFT) & 0xFF,
                         (color >> IM_COL32_B_SHIFT) & 0xFF,
                         (int)(255.0f * alpha));
        ImVec2 p(center.x + cosf(a) * radius, center.y + sinf(a) * radius);
        draw->AddCircleFilled(p, 4.0f + alpha * 3.0f, c);
    }
}

void draw_center_control(ImDrawList *draw,
                         const PlayerUiOverlayBar &bar,
                         PlayerState display_state,
                         float width,
                         float height)
{
    ImVec2 center(width * 0.5f, height * 0.5f);
    const ImU32 panel = IM_COL32(14, 17, 24, 186);
    const ImU32 ring = IM_COL32(255, 255, 255, 54);
    const ImU32 text = IM_COL32(246, 248, 255, 255);
    const ImU32 muted = IM_COL32(192, 200, 214, 230);

    if (bar.focus == PLAYER_UI_OVERLAY_FOCUS_SEEK)
    {
        char label[128];

        draw->AddRectFilled(ImVec2(center.x - 154.0f, center.y - 58.0f),
                            ImVec2(center.x + 154.0f, center.y + 58.0f),
                            panel,
                            28.0f);
        draw->AddRect(ImVec2(center.x - 153.0f, center.y - 57.0f),
                      ImVec2(center.x + 153.0f, center.y + 57.0f),
                      ring,
                      28.0f,
                      0,
                      2.0f);

        if (bar.seek_delta_ms != 0)
        {
            const int seconds = bar.seek_delta_ms / 1000;
            snprintf(label, sizeof(label), "%+d s", seconds);
            draw_seek_icon(draw, ImVec2(center.x, center.y - 14.0f), seconds >= 0 ? 1 : -1, text);
            draw_centered_text(draw, label, ImVec2(center.x, center.y + 34.0f), text);
        }
        else
        {
            snprintf(label, sizeof(label), "%s", bar.center[0] ? bar.center : "--:-- / --:--");
            draw_centered_text(draw, label, ImVec2(center.x, center.y), text);
            draw_centered_text(draw, "Release to seek", ImVec2(center.x, center.y + 32.0f), muted);
        }
        return;
    }

    if (bar.focus == PLAYER_UI_OVERLAY_FOCUS_VOLUME)
    {
        char label[64];

        snprintf(label, sizeof(label), "%s", bar.right[0] ? bar.right : "VOLUME");
        draw->AddRectFilled(ImVec2(center.x - 124.0f, center.y - 48.0f),
                            ImVec2(center.x + 124.0f, center.y + 48.0f),
                            panel,
                            26.0f);
        draw_centered_text(draw, label, center, text);
        return;
    }

    if (is_busy_state(display_state))
    {
        draw->AddCircleFilled(center, 58.0f, panel);
        draw->AddCircle(center, 58.0f, ring, 0, 2.0f);
        draw_spinner(draw, center, 27.0f, text);
        draw_centered_text(draw, state_text(display_state), ImVec2(center.x, center.y + 76.0f), muted);
        return;
    }

    if (display_state != PLAYER_STATE_PAUSED &&
        bar.focus != PLAYER_UI_OVERLAY_FOCUS_PLAY &&
        bar.focus != PLAYER_UI_OVERLAY_FOCUS_PAUSE)
    {
        return;
    }

    draw->AddCircleFilled(center, 58.0f, panel);
    draw->AddCircle(center, 58.0f, ring, 0, 2.0f);
    if (display_state == PLAYER_STATE_PAUSED || bar.focus == PLAYER_UI_OVERLAY_FOCUS_PLAY)
        draw_play_icon(draw, center, 74.0f, text);
    else
        draw_pause_icon(draw, center, 74.0f, text);
}

void draw_progress_bar(ImDrawList *draw, const PlayerUiOverlayBar &bar, PlayerState context_state, float width, float height)
{
    PlayerUiLayout layout;
    const PlayerState display_state = display_state_from_context(context_state, bar.state);
    if (!player_ui_layout_compute((int)width, (int)height, &layout))
        return;

    const float bottom_h = (float)layout.bottom_height;
    const float progress_x = (float)layout.progress_x;
    const float progress_w = (float)layout.progress_width;
    const float progress_y = (float)layout.progress_y;
    const float progress_h = (float)layout.progress_height;
    const float fill = std::max(0.0f, std::min(1.0f, bar.progress_permille / 1000.0f));
    const ImU32 bg_top = IM_COL32(2, 4, 9, 180);
    const ImU32 bg_bottom = IM_COL32(11, 13, 20, 238);
    const ImU32 text = IM_COL32(238, 242, 250, 255);
    const ImU32 muted = IM_COL32(160, 168, 182, 255);
    const char *center_text = bar.center[0] ? bar.center : state_text(bar.state);
    const char *hint_text = bar.hint;
    const char *right_text = bar.right;

    draw_center_control(draw, bar, display_state, width, height);

    draw->AddRectFilledMultiColor(ImVec2(0.0f, height - bottom_h - 28.0f),
                                  ImVec2(width, height),
                                  IM_COL32(2, 4, 9, 0),
                                  IM_COL32(2, 4, 9, 0),
                                  bg_bottom,
                                  bg_bottom);
    draw->AddRectFilled(ImVec2(0.0f, height - bottom_h),
                        ImVec2(width, height),
                        bg_top);

    draw->AddRectFilled(ImVec2(progress_x, progress_y),
                        ImVec2(progress_x + progress_w, progress_y + progress_h),
                        IM_COL32(68, 74, 88, 255),
                        progress_h * 0.5f);
    draw->AddRectFilled(ImVec2(progress_x, progress_y),
                        ImVec2(progress_x + progress_w * fill, progress_y + progress_h),
                        IM_COL32(245, 248, 255, 255),
                        progress_h * 0.5f);
    draw->AddCircleFilled(ImVec2(progress_x + progress_w * fill, progress_y + progress_h * 0.5f),
                          13.0f,
                          IM_COL32(255, 255, 255, 255));

    ImVec2 status_pos(progress_x, progress_y + 26.0f);
    draw->AddText(status_pos, text, center_text);
    status_pos.x += ImGui::CalcTextSize(center_text).x + 42.0f;
    if (hint_text[0])
    {
        draw->AddText(status_pos, muted, hint_text);
        status_pos.x += ImGui::CalcTextSize(hint_text).x + 42.0f;
    }
    if (right_text[0])
        draw->AddText(status_pos, text, right_text);
}

void draw_loading_message(ImDrawList *draw, const PlayerUiOverlayMessage &message, PlayerState context_state, float width, float height)
{
    ImVec2 center(width * 0.5f, height * 0.5f);
    const char *label = message.line1[0] ? message.line1 : state_text(context_state);

    draw->AddCircleFilled(center, 64.0f, IM_COL32(13, 17, 25, 198));
    draw->AddCircle(center, 64.0f, IM_COL32(255, 255, 255, 54), 0, 2.0f);
    draw_spinner(draw, center, 31.0f, IM_COL32(246, 248, 255, 255));
    draw_centered_text(draw, label, ImVec2(center.x, center.y + 86.0f), IM_COL32(198, 206, 222, 238));
}

void draw_message(ImDrawList *draw, const PlayerUiOverlayMessage &message, PlayerState context_state, float width, float height)
{
    ImVec2 center(width * 0.5f, height * 0.5f);
    ImVec2 min(center.x - 190.0f, center.y - 58.0f);
    ImVec2 max(center.x + 190.0f, center.y + 58.0f);

    if (is_busy_state(context_state) ||
        text_eq(message.title, "LOADING") ||
        text_eq(message.title, "BUFFERING") ||
        text_eq(message.title, "SEEKING"))
    {
        draw_loading_message(draw, message, context_state, width, height);
        return;
    }

    draw->AddRectFilled(min, max, IM_COL32(18, 22, 30, 232), 18.0f);
    draw->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f),
                  ImVec2(max.x - 1.0f, max.y - 1.0f),
                  IM_COL32(82, 92, 112, 180),
                  18.0f,
                  0,
                  2.0f);
    draw->AddText(ImVec2(min.x + 28.0f, min.y + 24.0f),
                  IM_COL32(244, 247, 255, 255),
                  message.title);
    if (message.line1[0])
        draw->AddText(ImVec2(min.x + 28.0f, min.y + 58.0f),
                      IM_COL32(168, 176, 190, 255),
                      message.line1);
}

void draw_home_text(ImDrawList *draw, float x, float y, float size, ImU32 color, const char *text)
{
    draw->AddText(ImGui::GetFont(), size, ImVec2(x, y), color, text ? text : "");
}

const char *home_ready_text(bool ready)
{
    return ready ? "OK" : "WAIT";
}

ImU32 home_status_color(bool ready)
{
    return ready ? IM_COL32(0, 170, 132, 255) : IM_COL32(234, 80, 70, 255);
}

void draw_home_grid(ImDrawList *draw, float width, float height)
{
    const ImU32 bg = IM_COL32(242, 238, 230, 255);
    const ImU32 grid = IM_COL32(222, 218, 210, 84);
    const float step = 42.0f;

    draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width, height), bg);
    for (float x = 0.0f; x < width; x += step)
        draw->AddLine(ImVec2(x, 0.0f), ImVec2(x, height), grid, 1.0f);
    for (float y = 0.0f; y < height; y += step)
        draw->AddLine(ImVec2(0.0f, y), ImVec2(width, y), grid, 1.0f);

    draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(22.0f, height), IM_COL32(0, 185, 212, 255));
    draw->AddRectFilled(ImVec2(width - 22.0f, 0.0f), ImVec2(width, height), IM_COL32(255, 91, 81, 255));
}

void draw_home_status_chip(ImDrawList *draw, float x, float y, const char *label, bool ready)
{
    const ImU32 panel = IM_COL32(255, 255, 255, 210);
    const ImU32 border = IM_COL32(18, 18, 20, 42);
    const ImU32 text = IM_COL32(18, 18, 20, 255);
    const ImU32 muted = IM_COL32(95, 92, 86, 255);

    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + 176.0f, y + 42.0f), panel, 18.0f);
    draw->AddRect(ImVec2(x, y), ImVec2(x + 176.0f, y + 42.0f), border, 18.0f, 0, 1.0f);
    draw->AddCircleFilled(ImVec2(x + 22.0f, y + 21.0f), 7.0f, home_status_color(ready));
    draw_home_text(draw, x + 38.0f, y + 8.0f, 18.0f, text, label);
    draw_home_text(draw, x + 122.0f, y + 8.0f, 18.0f, muted, home_ready_text(ready));
}

void draw_home_cast_arcs(ImDrawList *draw, ImVec2 origin, float scale, ImU32 color)
{
    const float thickness = 12.0f * scale;
    for (int i = 0; i < 3; ++i)
    {
        float radius = (28.0f + (float)i * 34.0f) * scale;
        draw->PathArcTo(origin, radius, -kPi * 0.5f, 0.0f, 28);
        draw->PathStroke(color, false, thickness);
    }
    draw->AddRectFilled(ImVec2(origin.x - thickness * 0.5f, origin.y - thickness * 0.5f),
                        ImVec2(origin.x + thickness * 0.5f, origin.y + thickness * 0.5f),
                        color,
                        thickness * 0.2f);
}

void draw_home_phone(ImDrawList *draw, ImVec2 min, ImVec2 size, ImU32 color)
{
    draw->AddRect(ImVec2(min.x, min.y),
                  ImVec2(min.x + size.x, min.y + size.y),
                  color,
                  24.0f,
                  0,
                  8.0f);
    draw->AddLine(ImVec2(min.x + size.x * 0.36f, min.y + size.y - 36.0f),
                  ImVec2(min.x + size.x * 0.64f, min.y + size.y - 36.0f),
                  color,
                  7.0f);
}

void draw_home_monitor(ImDrawList *draw, ImVec2 min, ImVec2 size, ImU32 color)
{
    draw->AddRect(ImVec2(min.x, min.y),
                  ImVec2(min.x + size.x, min.y + size.y),
                  color,
                  18.0f,
                  0,
                  8.0f);
    draw->AddRectFilled(ImVec2(min.x + size.x * 0.44f, min.y + size.y),
                        ImVec2(min.x + size.x * 0.56f, min.y + size.y + 30.0f),
                        color);
    draw->AddRectFilled(ImVec2(min.x + size.x * 0.28f, min.y + size.y + 30.0f),
                        ImVec2(min.x + size.x * 0.72f, min.y + size.y + 40.0f),
                        color);
}

void draw_home_switch(ImDrawList *draw, ImVec2 min, ImVec2 size)
{
    const ImU32 black = IM_COL32(12, 12, 12, 255);
    const ImU32 cyan = IM_COL32(0, 185, 212, 255);
    const ImU32 red = IM_COL32(255, 91, 81, 255);
    const float natural_w = 82.0f;
    const float natural_h = 44.0f;
    const float scale = std::min(size.x / natural_w, size.y / natural_h);
    const float ox = min.x + (size.x - natural_w * scale) * 0.5f;
    const float oy = min.y + (size.y - natural_h * scale) * 0.5f;

    auto p = [&](float x, float y) {
        return ImVec2(ox + x * scale, oy + y * scale);
    };
    auto rr = [&](float value) {
        return value * scale;
    };

    draw->AddRectFilled(p(1.7f, 2.4f),
                        p(14.6f, 41.6f),
                        black,
                        rr(9.0f),
                        ImDrawFlags_RoundCornersLeft);
    draw->AddRectFilled(p(2.5f, 3.1f),
                        p(13.8f, 40.9f),
                        cyan,
                        rr(8.0f),
                        ImDrawFlags_RoundCornersLeft);

    draw->AddRectFilled(p(67.4f, 2.4f),
                        p(80.3f, 41.6f),
                        black,
                        rr(9.0f),
                        ImDrawFlags_RoundCornersRight);
    draw->AddRectFilled(p(68.2f, 3.1f),
                        p(79.5f, 40.9f),
                        red,
                        rr(8.0f),
                        ImDrawFlags_RoundCornersRight);

    draw->AddRectFilled(p(15.6f, 2.4f), p(66.4f, 5.6f), black);
    draw->AddRectFilled(p(15.6f, 2.4f), p(18.8f, 11.6f), black);
    draw->AddRectFilled(p(63.2f, 2.4f), p(66.4f, 41.6f), black);
    draw->AddRectFilled(p(45.0f, 38.4f), p(66.4f, 41.6f), black);
    {
        const ImVec2 origin = p(16.9f, 40.2f);
        const float arc_thickness = rr(3.65f);
        const float radii[] = {rr(5.8f), rr(14.1f), rr(22.4f)};
        for (float radius : radii)
        {
            draw->PathArcTo(origin, radius, -kPi * 0.5f, 0.0f, 28);
            draw->PathStroke(black, false, arc_thickness);
        }
        draw->AddCircleFilled(origin, arc_thickness * 0.56f, black, 18);
    }

    draw->AddCircleFilled(p(5.5f, 23.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(11.6f, 23.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(8.6f, 20.1f), rr(1.5f), black);
    draw->AddCircleFilled(p(8.6f, 12.3f), rr(3.3f), black);
    draw->AddCircleFilled(p(8.6f, 26.3f), rr(1.5f), black);
    draw->AddRectFilled(p(9.5f, 29.4f), p(11.9f, 31.7f), black, rr(0.35f));

    draw->AddCircleFilled(p(79.0f, 12.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(72.9f, 12.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(76.0f, 9.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(76.0f, 23.5f), rr(3.3f), black);
    draw->AddCircleFilled(p(76.0f, 15.3f), rr(1.5f), black);
    draw->AddCircleFilled(p(73.9f, 30.7f), rr(1.6f), black);
}

void draw_home_screen(ImDrawList *draw, const PlayerHomeViewState &home, float width, float height)
{
    const ImU32 black = IM_COL32(14, 14, 16, 255);
    const ImU32 muted = IM_COL32(78, 76, 70, 255);
    const ImU32 card = IM_COL32(255, 255, 255, 205);
    const ImU32 border = IM_COL32(17, 17, 18, 50);
    const ImU32 red = IM_COL32(255, 91, 81, 255);
    const float x0 = 72.0f;
    const float top = 56.0f;
    char error_preview[128];

    if (home.has_error && home.error_line[0])
        snprintf(error_preview, sizeof(error_preview), "%.96s", home.error_line);
    else
        snprintf(error_preview, sizeof(error_preview), "No error recorded.");

    draw_home_grid(draw, width, height);
    draw_home_text(draw, x0, top, 24.0f, black, "NX-CAST / HOME");
    draw_home_text(draw, width - 244.0f, top, 18.0f, muted, "DLNA RECEIVER");
    draw_home_text(draw, x0, top + 70.0f, 54.0f, black, "CAST MEDIA TO YOUR SWITCH");
    draw_home_text(draw, x0, top + 128.0f, 23.0f, muted, "Open a video app, choose NX-Cast, and let the Switch receive the stream.");

    draw_home_status_chip(draw, x0, 220.0f, "Storage", home.storage_ready);
    draw_home_status_chip(draw, x0 + 188.0f, 220.0f, "Network", home.network_ready);
    draw_home_status_chip(draw, x0 + 376.0f, 220.0f, "DLNA", home.dlna_running);
    draw_home_status_chip(draw, x0 + 564.0f, 220.0f, "Player", home.video_ready);

    if (home.has_error && home.error_line[0])
    {
        draw->AddRectFilled(ImVec2(width - 396.0f, 216.0f),
                            ImVec2(width - x0, 264.0f),
                            IM_COL32(255, 230, 226, 238),
                            18.0f);
        draw_home_text(draw, width - 372.0f, 228.0f, 17.0f, red, "Diagnostics");
        draw_home_text(draw, width - 264.0f, 228.0f, 17.0f, muted, "error captured");
    }
    else
    {
        draw_home_text(draw, width - 390.0f, 232.0f, 18.0f, muted, "Waiting for a stream.");
    }

    draw->AddRectFilled(ImVec2(x0, 274.0f), ImVec2(width - x0, 504.0f), card, 30.0f);
    draw->AddRect(ImVec2(x0, 274.0f), ImVec2(width - x0, 504.0f), border, 30.0f, 0, 1.0f);

    draw_home_phone(draw, ImVec2(x0 + 52.0f, 304.0f), ImVec2(94.0f, 158.0f), black);
    draw_home_monitor(draw, ImVec2(x0 + 196.0f, 320.0f), ImVec2(166.0f, 96.0f), black);
    draw_home_text(draw, x0 + 96.0f, 474.0f, 19.0f, black, "PHONE / DESKTOP");

    draw->AddTriangleFilled(ImVec2(x0 + 406.0f, 388.0f),
                            ImVec2(x0 + 406.0f, 418.0f),
                            ImVec2(x0 + 438.0f, 403.0f),
                            red);
    draw_home_cast_arcs(draw, ImVec2(x0 + 506.0f, 446.0f), 1.0f, black);
    draw_home_text(draw, x0 + 482.0f, 474.0f, 19.0f, black, "DLNA");
    draw->AddTriangleFilled(ImVec2(x0 + 628.0f, 388.0f),
                            ImVec2(x0 + 628.0f, 418.0f),
                            ImVec2(x0 + 660.0f, 403.0f),
                            red);

    draw_home_switch(draw, ImVec2(width - x0 - 348.0f, 304.0f), ImVec2(292.0f, 168.0f));
    draw_home_text(draw, width - x0 - 234.0f, 474.0f, 19.0f, black, "NX-CAST");

    draw->AddRectFilled(ImVec2(x0, 528.0f), ImVec2(width * 0.55f, height - 24.0f), card, 24.0f);
    draw->AddRect(ImVec2(x0, 528.0f), ImVec2(width * 0.55f, height - 24.0f), border, 24.0f, 0, 1.0f);
    draw_home_text(draw, x0 + 28.0f, 552.0f, 21.0f, black, "QUICK START");
    draw_home_text(draw, x0 + 28.0f, 584.0f, 18.0f, muted, "1. Keep both devices on the same Wi-Fi.");
    draw_home_text(draw, x0 + 28.0f, 612.0f, 18.0f, muted, "2. Select NX-Cast from Cast / DLNA / renderer.");
    draw_home_text(draw, x0 + 28.0f, 640.0f, 18.0f, muted, "3. Playback opens automatically.");

    draw->AddRectFilled(ImVec2(width * 0.58f, 528.0f), ImVec2(width - x0, height - 24.0f), card, 24.0f);
    draw->AddRect(ImVec2(width * 0.58f, 528.0f), ImVec2(width - x0, height - 24.0f), border, 24.0f, 0, 1.0f);
    draw_home_text(draw, width * 0.58f + 28.0f, 552.0f, 21.0f, black, "CONTROLS");
    draw_home_text(draw, width * 0.58f + 28.0f, 584.0f, 18.0f, muted, "A Play/Pause     L/R Seek 10s");
    draw_home_text(draw, width * 0.58f + 28.0f, 610.0f, 18.0f, muted, "Touch UI, drag timeline, release to seek.");
    draw_home_text(draw, width * 0.58f + 28.0f, 642.0f, 21.0f, black, "DIAGNOSTICS");
    draw_home_text(draw,
                   width * 0.58f + 28.0f,
                   672.0f,
                   17.0f,
                   home.has_error ? red : muted,
                   error_preview);

}

bool render_draw_data(ViewContext *ctx, int slot)
{
    ImDrawData *draw_data = ImGui::GetDrawData();
    uint32_t width;
    uint32_t height;
    DkCmdBuf cmdbuf;
    DkImageView target_view;
    DkGpuAddr descriptor_addr;
    size_t vtx_offset = 0;
    size_t idx_offset = 0;
    DkResHandle bound_texture = 0;

    if (!draw_data || draw_data->CmdListsCount <= 0)
        return false;
    if (slot < 0 || slot >= (int)kImageCount)
        return false;

    width = (uint32_t)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    height = (uint32_t)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (width == 0 || height == 0)
        return false;

    if (!ensure_buffer(ctx->dk3d_device,
                       g_vtx[slot],
                       (uint32_t)(draw_data->TotalVtxCount * sizeof(ImDrawVert)),
                       kVtxBufferInitialSize) ||
        !ensure_buffer(ctx->dk3d_device,
                       g_idx[slot],
                       (uint32_t)(draw_data->TotalIdxCount * sizeof(ImDrawIdx)),
                       kIdxBufferInitialSize))
    {
        return false;
    }

    cmdbuf = ctx->dk3d_overlay_cmdbuf;
    dkCmdBufClear(cmdbuf);
    dkImageViewDefaults(&target_view, &ctx->dk3d_framebuffers[slot]);
    dkCmdBufBindRenderTarget(cmdbuf, &target_view, nullptr);

    descriptor_addr = dkMemBlockGetGpuAddr(g_descriptor_mem);
    dkCmdBufBindSamplerDescriptorSet(cmdbuf, descriptor_addr, kDescriptorCount);
    dkCmdBufBindImageDescriptorSet(cmdbuf,
                                   descriptor_addr + kDescriptorCount * sizeof(DkSamplerDescriptor),
                                   kDescriptorCount);
    dkCmdBufBarrier(cmdbuf, DkBarrier_None, DkInvalidateFlags_Descriptors);
    setup_render_state(cmdbuf, draw_data, width, height);

    dkCmdBufBindVtxBuffer(cmdbuf, 0, dkMemBlockGetGpuAddr(g_vtx[slot].mem), g_vtx[slot].size);
    dkCmdBufBindIdxBuffer(cmdbuf, DkIdxFormat_Uint16, dkMemBlockGetGpuAddr(g_idx[slot].mem));

    for (int n = 0; n < draw_data->CmdListsCount; ++n)
    {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        size_t vtx_size = (size_t)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
        size_t idx_size = (size_t)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
        auto *vtx_dst = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(g_vtx[slot].mem)) + vtx_offset;
        auto *idx_dst = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(g_idx[slot].mem)) + idx_offset;

        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_size);
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_size);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i)
        {
            const ImDrawCmd *cmd = &cmd_list->CmdBuffer[cmd_i];
            ImVec2 clip_min((cmd->ClipRect.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x,
                            (cmd->ClipRect.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y);
            ImVec2 clip_max((cmd->ClipRect.z - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x,
                            (cmd->ClipRect.w - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y);
            DkScissor scissor;
            DkResHandle texture = (DkResHandle)cmd->GetTexID();

            if (cmd->UserCallback)
                continue;
            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                continue;
            if (clip_min.x < 0.0f)
                clip_min.x = 0.0f;
            if (clip_min.y < 0.0f)
                clip_min.y = 0.0f;
            if (clip_max.x > (float)width)
                clip_max.x = (float)width;
            if (clip_max.y > (float)height)
                clip_max.y = (float)height;

            scissor.x = (uint32_t)clip_min.x;
            scissor.y = (uint32_t)clip_min.y;
            scissor.width = (uint32_t)(clip_max.x - clip_min.x);
            scissor.height = (uint32_t)(clip_max.y - clip_min.y);
            dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);

            if (texture != bound_texture)
            {
                bound_texture = texture;
                dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, 0, texture);
            }

            dkCmdBufDrawIndexed(cmdbuf,
                                DkPrimitive_Triangles,
                                cmd->ElemCount,
                                1,
                                cmd->IdxOffset + (uint32_t)(idx_offset / sizeof(ImDrawIdx)),
                                cmd->VtxOffset + (int32_t)(vtx_offset / sizeof(ImDrawVert)),
                                0);
        }

        vtx_offset += vtx_size;
        idx_offset += idx_size;
    }

    dkQueueSubmitCommands(ctx->dk3d_queue, dkCmdBufFinishList(cmdbuf));
    return true;
}
} // namespace

extern "C" bool frontend_imgui_overlay_init(ViewContext *ctx)
{
    uint32_t ubo_size;

    if (g_initialized)
        return true;
    if (g_failed)
        return false;
    if (!ctx || !ctx->dk3d_device || !ctx->dk3d_queue || !ctx->dk3d_overlay_cmdbuf)
        return false;

    g_device = ctx->dk3d_device;
    if (!create_imgui_context())
        goto fail;
    if (!load_embedded_shaders(ctx->dk3d_device))
        goto fail;

    ubo_size = align_size(sizeof(VertUbo), DK_UNIFORM_BUF_ALIGNMENT) +
               align_size(sizeof(FragUbo), DK_UNIFORM_BUF_ALIGNMENT);
    g_ubo_mem = make_memblock(ctx->dk3d_device,
                              ubo_size,
                              DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!g_ubo_mem)
        goto fail;
    if (!create_font_texture(ctx->dk3d_device, ctx->dk3d_queue, ctx->dk3d_overlay_cmdbuf))
        goto fail;

    g_initialized = true;
    log_info("[player-imgui] initialized deko3d overlay\n");
    return true;

fail:
    log_warn("[player-imgui] initialization failed; falling back to C overlay\n");
    frontend_imgui_overlay_shutdown();
    g_failed = true;
    return false;
}

extern "C" void frontend_imgui_overlay_shutdown(void)
{
    destroy_resources();
    if (g_context_created)
    {
        ImGui::DestroyContext();
        g_context_created = false;
    }
    if (g_pl_initialized)
    {
        plExit();
        g_pl_initialized = false;
    }
    g_initialized = false;
    g_device = nullptr;
}

extern "C" bool frontend_imgui_home_render(ViewContext *ctx, int slot)
{
    ImGuiIO *io;
    ImDrawList *draw;
    PlayerHomeViewState fallback = {};
    const PlayerHomeViewState *home;

    if (!ctx || slot < 0 || slot >= (int)kImageCount)
        return false;
    if (!frontend_imgui_overlay_init(ctx))
        return false;

    home = ctx->home_state_valid ? &ctx->home_state : &fallback;
    io = &ImGui::GetIO();
    io->DisplaySize = ImVec2((float)ctx->status.display_width, (float)ctx->status.display_height);
    io->DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io->DeltaTime = 1.0f / 60.0f;
    io->MouseDown[0] = false;

    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    draw_home_screen(draw, *home, io->DisplaySize.x, io->DisplaySize.y);
    ImGui::Render();

    if (!render_draw_data(ctx, slot))
        return false;

    ctx->dk3d_overlay_dirty = true;
    return true;
}

extern "C" bool frontend_imgui_overlay_render(ViewContext *ctx, int slot)
{
    PlayerUiOverlaySnapshot overlay;
    ImGuiIO *io;
    ImDrawList *draw;

    if (!ctx || slot < 0 || slot >= (int)kImageCount)
        return false;
    if (!frontend_imgui_overlay_init(ctx))
        return false;
    if (!player_ui_overlay_get_snapshot(&overlay) || overlay.kind == PLAYER_UI_OVERLAY_NONE)
        return false;

    io = &ImGui::GetIO();
    io->DisplaySize = ImVec2((float)ctx->status.display_width, (float)ctx->status.display_height);
    io->DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io->DeltaTime = 1.0f / 60.0f;
    io->MouseDown[0] = false;

    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    if (overlay.kind == PLAYER_UI_OVERLAY_MESSAGE)
        draw_message(draw, overlay.message, ctx->status.player_state, io->DisplaySize.x, io->DisplaySize.y);
    else
        draw_progress_bar(draw, overlay.bar, ctx->status.player_state, io->DisplaySize.x, io->DisplaySize.y);
    ImGui::Render();

    if (!render_draw_data(ctx, slot))
        return false;

    ctx->dk3d_overlay_dirty = true;
    return true;
}
