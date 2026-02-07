/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/display/vga_int.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/util.h"
#include "renderer.h"

#include <math.h>

#ifdef __ANDROID__
#include <android/log.h>
#include "qemu/thread.h"
#include <string.h>
#include "system/memory.h"
#endif

#ifdef __ANDROID__
static void check_gl_error_android(const char *context)
{
    GLenum err;
    int count = 0;
    while ((err = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "GL error 0x%X in %s\n", err, context);
        if (++count >= 10) {
            break;
        }
    }
}
#define GL_ASSERT_NO_ERROR(context) check_gl_error_android(context)
#else
#define GL_ASSERT_NO_ERROR(context) assert(glGetError() == GL_NO_ERROR)
#endif

#ifdef __ANDROID__
static QemuMutex g_android_readback_mutex;
static bool g_android_readback_mutex_init = false;
static uint8_t *g_android_readback_buf = NULL;
static size_t g_android_readback_size = 0;
static int g_android_readback_w = 0;
static int g_android_readback_h = 0;
static bool g_android_readback_ready = false;

static bool android_force_cpu_blit(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("XEMU_ANDROID_FORCE_CPU_BLIT");
        if (!env) {
            cached = 1;
        } else if (!strcmp(env, "1") || !strcmp(env, "true") ||
                   !strcmp(env, "TRUE")) {
            cached = 1;
        } else {
            cached = 0;
        }
    }
    return cached == 1;
}

static bool android_readback_is_black(const uint8_t *buf, size_t size,
                                      int width, int height)
{
    if (!buf || size < 4 || width <= 0 || height <= 0) {
        return false;
    }
    size_t tl_off = 0;
    size_t mid_off = ((size_t)(height / 2) * (size_t)width +
                      (size_t)(width / 2)) * 4;
    size_t br_off = ((size_t)(height - 1) * (size_t)width +
                     (size_t)(width - 1)) * 4;
    uint32_t tl = 0, mid = 0, br = 0;
    if (tl_off + 4 <= size) {
        memcpy(&tl, buf + tl_off, sizeof(tl));
    }
    if (mid_off + 4 <= size) {
        memcpy(&mid, buf + mid_off, sizeof(mid));
    }
    if (br_off + 4 <= size) {
        memcpy(&br, buf + br_off, sizeof(br));
    }
    bool black_tl = (tl == 0x00000000u || tl == 0xff000000u);
    bool black_mid = (mid == 0x00000000u || mid == 0xff000000u);
    bool black_br = (br == 0x00000000u || br == 0xff000000u);
    return black_tl && black_mid && black_br;
}

static void android_store_readback(NV2AState *d, SurfaceBinding *surface,
                                   int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    if (!g_android_readback_mutex_init) {
        qemu_mutex_init(&g_android_readback_mutex);
        g_android_readback_mutex_init = true;
    }
    size_t needed = (size_t)width * (size_t)height * 4;
    qemu_mutex_lock(&g_android_readback_mutex);
    if (needed > g_android_readback_size) {
        g_android_readback_buf = g_realloc(g_android_readback_buf, needed);
        g_android_readback_size = needed;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_android_readback_buf);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    bool display_black = android_readback_is_black(
        g_android_readback_buf, g_android_readback_size, width, height);
    bool used_surface = false;
    if (display_black && d && surface && surface->gl_buffer) {
        PGRAPHState *pg = &d->pgraph;
        PGRAPHGLState *r = pg->gl_renderer_state;
        unsigned int surf_w = surface->width;
        unsigned int surf_h = surface->height;
        pgraph_apply_scaling_factor(pg, &surf_w, &surf_h);
        if (surf_w > 0 && surf_h > 0) {
            size_t surf_needed = (size_t)surf_w * (size_t)surf_h * 4;
            if (surf_needed > g_android_readback_size) {
                g_android_readback_buf = g_realloc(g_android_readback_buf,
                                                   surf_needed);
                g_android_readback_size = surf_needed;
            }
            GLint prev_fbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, r->disp_rndr.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, surface->gl_buffer, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, surf_w, surf_h, GL_RGBA, GL_UNSIGNED_BYTE,
                         g_android_readback_buf);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, r->gl_display_buffer, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
            g_android_readback_w = (int)surf_w;
            g_android_readback_h = (int)surf_h;
            used_surface = true;
            static unsigned int surf_log = 0;
            if ((surf_log++ % 120) == 0) {
                __android_log_print(ANDROID_LOG_WARN, "xemu-android",
                                    "android readback: display black, using surface vram=0x%x size=%ux%u",
                                    (unsigned)surface->vram_addr,
                                    surf_w, surf_h);
            }
        }
    }
    if (!used_surface) {
        g_android_readback_w = width;
        g_android_readback_h = height;
    }
    g_android_readback_ready = true;
    qemu_mutex_unlock(&g_android_readback_mutex);
}

bool nv2a_android_copy_readback(uint8_t **buffer, size_t *buffer_size,
                                int *width, int *height)
{
    if (!android_force_cpu_blit()) {
        return false;
    }
    if (!g_android_readback_mutex_init || !g_android_readback_ready) {
        return false;
    }
    qemu_mutex_lock(&g_android_readback_mutex);
    size_t needed = (size_t)g_android_readback_w * (size_t)g_android_readback_h * 4;
    if (!g_android_readback_buf || needed == 0) {
        qemu_mutex_unlock(&g_android_readback_mutex);
        return false;
    }
    if (*buffer_size < needed) {
        *buffer = g_realloc(*buffer, needed);
        *buffer_size = needed;
    }
    memcpy(*buffer, g_android_readback_buf, needed);
    *width = g_android_readback_w;
    *height = g_android_readback_h;
    qemu_mutex_unlock(&g_android_readback_mutex);
    return true;
}

#endif

void pgraph_gl_init_display(NV2AState *d)
{
    struct PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

#ifdef __ANDROID__
    glo_set_current(g_nv2a_context_render);
#else
    glo_set_current(g_nv2a_context_display);
#endif

    glGenTextures(1, &r->gl_display_buffer);
    r->gl_display_buffer_internal_format = 0;
    r->gl_display_buffer_width = 0;
    r->gl_display_buffer_height = 0;
    r->gl_display_buffer_format = 0;
    r->gl_display_buffer_type = 0;

    const char *vs =
#ifdef __ANDROID__
        "#version 300 es\n"
        "precision highp float;\n"
#else
        "#version 330\n"
#endif
        "void main()\n"
        "{\n"
        "    float x = -1.0 + float((gl_VertexID & 1) << 2);\n"
        "    float y = -1.0 + float((gl_VertexID & 2) << 1);\n"
        "    gl_Position = vec4(x, y, 0, 1);\n"
        "}\n";
    /* FIXME: improve interlace handling, pvideo */

    const char *fs =
#ifdef __ANDROID__
        "#version 300 es\n"
        "precision highp float;\n"
        "precision highp int;\n"
#else
        "#version 330\n"
#endif
        "uniform sampler2D tex;\n"
        "uniform bool pvideo_enable;\n"
        "uniform sampler2D pvideo_tex;\n"
        "uniform vec2 pvideo_in_pos;\n"
        "uniform vec4 pvideo_pos;\n"
        "uniform vec3 pvideo_scale;\n"
        "uniform bool pvideo_color_key_enable;\n"
        "uniform vec3 pvideo_color_key;\n"
        "uniform vec2 display_size;\n"
        "uniform float line_offset;\n"
        "uniform bool clip_enable;\n"
        "uniform vec4 clip_rect;\n"
        "layout(location = 0) out vec4 out_Color;\n"
        "void main()\n"
        "{\n"
        "    vec2 uv = gl_FragCoord.xy/display_size;\n"
        "    vec2 texSize = vec2(textureSize(tex, 0));\n"
        "    vec2 texCoord;\n"
        "    if (clip_enable) {\n"
        "        vec2 pixel = vec2(clip_rect.x + uv.x * clip_rect.z,\n"
        "                         clip_rect.y + (1.0 - uv.y) * clip_rect.w);\n"
        "        texCoord = pixel / texSize;\n"
        "    } else {\n"
        "        float rel = display_size.y/texSize.y/line_offset;\n"
        "        texCoord = vec2(uv.x, rel*(1.0f - uv.y));\n"
        "    }\n"
        "    out_Color.rgba = texture(tex, texCoord);\n"
        "    if (pvideo_enable) {\n"
        "        vec2 screenCoord = gl_FragCoord.xy - 0.5;\n"
        "        vec4 output_region = vec4(pvideo_pos.xy, pvideo_pos.xy + pvideo_pos.zw);\n"
        "        bvec4 clip = bvec4(lessThan(screenCoord, output_region.xy),\n"
        "                           greaterThan(screenCoord, output_region.zw));\n"
        "        if (!any(clip) && (!pvideo_color_key_enable || out_Color.rgb == pvideo_color_key)) {\n"
        "            vec2 out_xy = (screenCoord - pvideo_pos.xy) * pvideo_scale.z;\n"
        "            vec2 in_st = (pvideo_in_pos + out_xy * pvideo_scale.xy) / vec2(textureSize(pvideo_tex, 0));\n"
        "            in_st.y *= -1.0;\n"
        "            out_Color.rgba = texture(pvideo_tex, in_st);\n"
        "        }\n"
        "    }\n"
        "}\n";

    r->disp_rndr.prog = pgraph_gl_compile_shader(vs, fs);
    r->disp_rndr.tex_loc = glGetUniformLocation(r->disp_rndr.prog, "tex");
    r->disp_rndr.pvideo_enable_loc = glGetUniformLocation(r->disp_rndr.prog, "pvideo_enable");
    r->disp_rndr.pvideo_tex_loc = glGetUniformLocation(r->disp_rndr.prog, "pvideo_tex");
    r->disp_rndr.pvideo_in_pos_loc = glGetUniformLocation(r->disp_rndr.prog, "pvideo_in_pos");
    r->disp_rndr.pvideo_pos_loc = glGetUniformLocation(r->disp_rndr.prog, "pvideo_pos");
    r->disp_rndr.pvideo_scale_loc = glGetUniformLocation(r->disp_rndr.prog, "pvideo_scale");
    r->disp_rndr.pvideo_color_key_enable_loc = glGetUniformLocation(r->disp_rndr.prog, "pvideo_color_key_enable");
    r->disp_rndr.pvideo_color_key_loc = glGetUniformLocation(r->disp_rndr.prog, "pvideo_color_key");
    r->disp_rndr.display_size_loc = glGetUniformLocation(r->disp_rndr.prog, "display_size");
    r->disp_rndr.line_offset_loc = glGetUniformLocation(r->disp_rndr.prog, "line_offset");
    r->disp_rndr.clip_rect_loc = glGetUniformLocation(r->disp_rndr.prog, "clip_rect");
    r->disp_rndr.clip_enable_loc = glGetUniformLocation(r->disp_rndr.prog, "clip_enable");

    glGenVertexArrays(1, &r->disp_rndr.vao);
    glBindVertexArray(r->disp_rndr.vao);
    glGenBuffers(1, &r->disp_rndr.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->disp_rndr.vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW);
    glGenFramebuffers(1, &r->disp_rndr.fbo);
    glGenTextures(1, &r->disp_rndr.pvideo_tex);
    GL_ASSERT_NO_ERROR("pgraph_gl_init_display");

    glo_set_current(g_nv2a_context_render);
}

void pgraph_gl_finalize_display(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

#ifdef __ANDROID__
    glo_set_current(g_nv2a_context_render);
#else
    glo_set_current(g_nv2a_context_display);
#endif

    glDeleteTextures(1, &r->gl_display_buffer);
    r->gl_display_buffer = 0;

    glDeleteProgram(r->disp_rndr.prog);
    r->disp_rndr.prog = 0;

    glDeleteVertexArrays(1, &r->disp_rndr.vao);
    r->disp_rndr.vao = 0;

    glDeleteBuffers(1, &r->disp_rndr.vbo);
    r->disp_rndr.vbo = 0;

    glDeleteFramebuffers(1, &r->disp_rndr.fbo);
    r->disp_rndr.fbo = 0;

    glDeleteTextures(1, &r->disp_rndr.pvideo_tex);
    r->disp_rndr.pvideo_tex = 0;

    glo_set_current(g_nv2a_context_render);
}

static uint8_t *convert_texture_data__CR8YB8CB8YA8(const uint8_t *data,
                                                   unsigned int width,
                                                   unsigned int height,
                                                   unsigned int pitch)
{
    uint8_t *converted_data = (uint8_t *)g_malloc(width * height * 4);
    int x, y;
    for (y = 0; y < height; y++) {
        const uint8_t *line = &data[y * pitch];
        const uint32_t row_offset = y * width;
        for (x = 0; x < width; x++) {
            uint8_t *pixel = &converted_data[(row_offset + x) * 4];
            convert_yuy2_to_rgb(line, x, &pixel[0], &pixel[1], &pixel[2]);
            pixel[3] = 255;
        }
    }
    return converted_data;
}

static float pvideo_calculate_scale(unsigned int din_dout,
                                           unsigned int output_size)
{
    float calculated_in = din_dout * (output_size - 1);
    calculated_in = floorf(calculated_in / (1 << 20) + 0.5f);
    return (calculated_in + 1.0f) / output_size;
}

static void render_display_pvideo_overlay(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    // FIXME: This check against PVIDEO_SIZE_IN does not match HW behavior.
    // Many games seem to pass this value when initializing or tearing down
    // PVIDEO. On its own, this generally does not result in the overlay being
    // hidden, however there are certain games (e.g., Ultimate Beach Soccer)
    // that use an unknown mechanism to hide the overlay without explicitly
    // stopping it.
    // Since the value seems to be set to 0xFFFFFFFF only in cases where the
    // content is not valid, it is probably good enough to treat it as an
    // implicit stop.
    bool enabled = (d->pvideo.regs[NV_PVIDEO_BUFFER] & NV_PVIDEO_BUFFER_0_USE)
        && d->pvideo.regs[NV_PVIDEO_SIZE_IN] != 0xFFFFFFFF;
    glUniform1ui(r->disp_rndr.pvideo_enable_loc, enabled);
    if (!enabled) {
        return;
    }

    hwaddr base = d->pvideo.regs[NV_PVIDEO_BASE];
    hwaddr limit = d->pvideo.regs[NV_PVIDEO_LIMIT];
    hwaddr offset = d->pvideo.regs[NV_PVIDEO_OFFSET];

    int in_width =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN], NV_PVIDEO_SIZE_IN_WIDTH);
    int in_height =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN], NV_PVIDEO_SIZE_IN_HEIGHT);

    int in_s = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_S);
    int in_t = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_T);

    int in_pitch =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT], NV_PVIDEO_FORMAT_PITCH);
    int in_color =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT], NV_PVIDEO_FORMAT_COLOR);

    unsigned int out_width =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT], NV_PVIDEO_SIZE_OUT_WIDTH);
    unsigned int out_height =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT], NV_PVIDEO_SIZE_OUT_HEIGHT);

    float scale_x = 1.0f;
    float scale_y = 1.0f;
    unsigned int ds_dx = d->pvideo.regs[NV_PVIDEO_DS_DX];
    unsigned int dt_dy = d->pvideo.regs[NV_PVIDEO_DT_DY];
    if (ds_dx != NV_PVIDEO_DIN_DOUT_UNITY) {
        scale_x = pvideo_calculate_scale(ds_dx, out_width);
    }
    if (dt_dy != NV_PVIDEO_DIN_DOUT_UNITY) {
        scale_y = pvideo_calculate_scale(dt_dy, out_height);
    }

    // On HW, setting NV_PVIDEO_SIZE_IN larger than NV_PVIDEO_SIZE_OUT results
    // in them being capped to the output size, content is not scaled. This is
    // particularly important as NV_PVIDEO_SIZE_IN may be set to 0xFFFFFFFF
    // during initialization or teardown.
    if (in_width > out_width) {
        in_width = floorf((float)out_width * scale_x + 0.5f);
    }
    if (in_height > out_height) {
        in_height = floorf((float)out_height * scale_y + 0.5f);
    }

    /* TODO: support other color formats */
    assert(in_color == NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8);

    unsigned int out_x =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT], NV_PVIDEO_POINT_OUT_X);
    unsigned int out_y =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT], NV_PVIDEO_POINT_OUT_Y);

    unsigned int color_key_enabled =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT], NV_PVIDEO_FORMAT_DISPLAY);
    glUniform1ui(r->disp_rndr.pvideo_color_key_enable_loc,
                 color_key_enabled);

    unsigned int color_key = d->pvideo.regs[NV_PVIDEO_COLOR_KEY] & 0xFFFFFF;
    glUniform3f(r->disp_rndr.pvideo_color_key_loc,
                GET_MASK(color_key, NV_PVIDEO_COLOR_KEY_RED) / 255.0,
                GET_MASK(color_key, NV_PVIDEO_COLOR_KEY_GREEN) / 255.0,
                GET_MASK(color_key, NV_PVIDEO_COLOR_KEY_BLUE) / 255.0);

    assert(offset + in_pitch * in_height <= limit);
    hwaddr end = base + offset + in_pitch * in_height;
    assert(end <= memory_region_size(d->vram));

    pgraph_apply_scaling_factor(pg, &out_x, &out_y);
    pgraph_apply_scaling_factor(pg, &out_width, &out_height);

    // Translate for the GL viewport origin.
    out_y = MAX(r->gl_display_buffer_height - 1 - (int)(out_y + out_height), 0);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, r->disp_rndr.pvideo_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    uint8_t *tex_rgba = convert_texture_data__CR8YB8CB8YA8(
        d->vram_ptr + base + offset, in_width, in_height, in_pitch);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, in_width, in_height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, tex_rgba);
    g_free(tex_rgba);
    glUniform1i(r->disp_rndr.pvideo_tex_loc, 1);
    glUniform2f(r->disp_rndr.pvideo_in_pos_loc, in_s / 16.f, in_t / 8.f);
    glUniform4f(r->disp_rndr.pvideo_pos_loc,
                out_x, out_y, out_width, out_height);
    glUniform3f(r->disp_rndr.pvideo_scale_loc,
                scale_x, scale_y, 1.0f / pg->surface_scale_factor);
}

static void render_display(NV2AState *d, SurfaceBinding *surface)
{
    struct PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    unsigned int width, height;
    VGADisplayParams vga_display_params;
    d->vga.get_resolution(&d->vga, (int*)&width, (int*)&height);
    d->vga.get_params(&d->vga, &vga_display_params);
    float line_offset = vga_display_params.line_offset
                            ? (float)surface->pitch /
                                  (float)vga_display_params.line_offset
                            : 1.0f;

    /* Adjust viewport height for interlaced mode, used only in 1080i */
    if (d->vga.cr[NV_PRMCIO_INTERLACE_MODE] != NV_PRMCIO_INTERLACE_MODE_DISABLED) {
        height *= 2;
    }

    pgraph_apply_scaling_factor(pg, &width, &height);

    glBindFramebuffer(GL_FRAMEBUFFER, r->disp_rndr.fbo);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->gl_display_buffer);

    GLint disp_ifmt = surface->fmt.gl_internal_format;
    GLenum disp_fmt = surface->fmt.gl_format;
    GLenum disp_type = surface->fmt.gl_type;
#ifdef __ANDROID__
    // Use a GLES-compatible renderable format for the display buffer.
    disp_ifmt = GL_RGBA8;
    disp_fmt = GL_RGBA;
    disp_type = GL_UNSIGNED_BYTE;
#endif

    bool recreate = (
        disp_ifmt != r->gl_display_buffer_internal_format
        || width != r->gl_display_buffer_width
        || height != r->gl_display_buffer_height
        || disp_fmt != r->gl_display_buffer_format
        || disp_type != r->gl_display_buffer_type
        );

    if (recreate) {
        /* XXX: There's apparently a bug in some Intel OpenGL drivers for
         * Windows that will leak this texture when its orphaned after use in
         * another context, apparently regardless of which thread it's created
         * or released on.
         *
         * Driver: 27.20.100.8729 9/11/2020 W10 x64
         * Track: https://community.intel.com/t5/Graphics/OpenGL-Windows-drivers-for-Intel-HD-630-leaking-GPU-memory-when/td-p/1274423
         */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        r->gl_display_buffer_internal_format = disp_ifmt;
        r->gl_display_buffer_width = width;
        r->gl_display_buffer_height = height;
        r->gl_display_buffer_format = disp_fmt;
        r->gl_display_buffer_type = disp_type;
        glTexImage2D(GL_TEXTURE_2D, 0,
            r->gl_display_buffer_internal_format,
            r->gl_display_buffer_width,
            r->gl_display_buffer_height,
            0,
            r->gl_display_buffer_format,
            r->gl_display_buffer_type,
            NULL);
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, r->gl_display_buffer, 0);
    GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, DrawBuffers);
    GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "xemu-android",
                            "render_display: framebuffer incomplete 0x%X (fmt=%d ifmt=%d type=%d size=%ux%u)",
                            fb_status,
                            r->gl_display_buffer_format,
                            r->gl_display_buffer_internal_format,
                            r->gl_display_buffer_type,
                            r->gl_display_buffer_width,
                            r->gl_display_buffer_height);
#else
        fprintf(stderr,
                "render_display: framebuffer incomplete 0x%X (fmt=%d ifmt=%d type=%d size=%ux%u)\n",
                fb_status,
                r->gl_display_buffer_format,
                r->gl_display_buffer_internal_format,
                r->gl_display_buffer_type,
                r->gl_display_buffer_width,
                r->gl_display_buffer_height);
#endif
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glBindTexture(GL_TEXTURE_2D, surface->gl_buffer);
#ifdef __ANDROID__
    bool clip_enable = false;
    float clip_x = 0.0f;
    float clip_y = 0.0f;
    float clip_w = 0.0f;
    float clip_h = 0.0f;
    if (android_force_cpu_blit()) {
        clip_x = (float)surface->shape.clip_x;
        clip_y = (float)surface->shape.clip_y;
        clip_w = (float)surface->shape.clip_width;
        clip_h = (float)surface->shape.clip_height;
        if (clip_w > 0.0f && clip_h > 0.0f) {
            clip_enable = true;
            static unsigned int clip_log = 0;
            if ((clip_log++ % 120) == 0) {
                GLint tex_w = 0;
                GLint tex_h = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tex_w);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &tex_h);
                __android_log_print(ANDROID_LOG_WARN, "xemu-android",
                                    "render_display: clip rect x=%g y=%g w=%g h=%g tex=%dx%d disp=%ux%u",
                                    clip_x, clip_y, clip_w, clip_h,
                                    (int)tex_w, (int)tex_h,
                                    width, height);
            }
        }
    }
#endif
    glBindVertexArray(r->disp_rndr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->disp_rndr.vbo);
    glUseProgram(r->disp_rndr.prog);
    glProgramUniform1i(r->disp_rndr.prog, r->disp_rndr.tex_loc, 0);
    glUniform2f(r->disp_rndr.display_size_loc, width, height);
    glUniform1f(r->disp_rndr.line_offset_loc, line_offset);
#ifdef __ANDROID__
    glUniform1i(r->disp_rndr.clip_enable_loc, clip_enable ? 1 : 0);
    glUniform4f(r->disp_rndr.clip_rect_loc, clip_x, clip_y, clip_w, clip_h);
#endif
    render_display_pvideo_overlay(d);

    glViewport(0, 0, width, height);
    glColorMask(true, true, true, true);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);

#ifdef __ANDROID__
    if (android_force_cpu_blit()) {
        android_store_readback(d, surface, width, height);
    }
#endif

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, 0, 0);
}

static void gl_fence(void)
{
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    int result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT,
                                         (GLuint64)(5000000000));
    assert(result == GL_CONDITION_SATISFIED || result == GL_ALREADY_SIGNALED);
    glDeleteSync(fence);
}

void pgraph_gl_sync(NV2AState *d)
{
    VGADisplayParams vga_display_params;
    d->vga.get_params(&d->vga, &vga_display_params);

    PGRAPHState *pg = &d->pgraph;
    unsigned int disp_w = 0;
    unsigned int disp_h = 0;
    d->vga.get_resolution(&d->vga, (int *)&disp_w, (int *)&disp_h);
    if (d->vga.cr[NV_PRMCIO_INTERLACE_MODE] != NV_PRMCIO_INTERLACE_MODE_DISABLED) {
        disp_h *= 2;
    }

    SurfaceBinding *surface =
        pgraph_gl_surface_get_within(d, d->pcrtc.start + vga_display_params.line_offset);
    if (surface == NULL || !surface->color || !surface->width || !surface->height) {
        qemu_event_set(&d->pgraph.sync_complete);
        return;
    }

    /* FIXME: Sanity check surface dimensions */

    /* Wait for queued commands to complete */
#ifdef __ANDROID__
    bool force_upload = !tcg_enabled();
    if (android_force_cpu_blit() && surface->draw_time == 0) {
        force_upload = true;
        static unsigned int no_draw_log = 0;
        if ((no_draw_log++ % 120) == 0) {
            __android_log_print(ANDROID_LOG_WARN, "xemu-android",
                                "pgraph_gl_sync: forcing upload (no draws yet) vram=0x%x",
                                (unsigned)surface->vram_addr);
        }
    }
    pgraph_gl_upload_surface_data(d, surface, force_upload);
#else
    pgraph_gl_upload_surface_data(d, surface, !tcg_enabled());
#endif
    gl_fence();
    GL_ASSERT_NO_ERROR("pgraph_gl_sync: upload fence");

    /* Render framebuffer */
#ifdef __ANDROID__
    glo_set_current(g_nv2a_context_render);
    render_display(d, surface);
    gl_fence();
    GL_ASSERT_NO_ERROR("pgraph_gl_sync: render fence");
#else
    /* Render framebuffer in display context */
    glo_set_current(g_nv2a_context_display);
    render_display(d, surface);
    gl_fence();
    GL_ASSERT_NO_ERROR("pgraph_gl_sync: render fence");

    /* Switch back to original context */
    glo_set_current(g_nv2a_context_render);
#endif

    qatomic_set(&d->pgraph.sync_pending, false);
    qemu_event_set(&d->pgraph.sync_complete);
}

int pgraph_gl_get_framebuffer_surface(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qemu_mutex_lock(&d->pfifo.lock);
    // FIXME: Possible race condition with pgraph, consider lock

    VGADisplayParams vga_display_params;
    d->vga.get_params(&d->vga, &vga_display_params);

    unsigned int disp_w = 0;
    unsigned int disp_h = 0;
    d->vga.get_resolution(&d->vga, (int *)&disp_w, (int *)&disp_h);
    if (d->vga.cr[NV_PRMCIO_INTERLACE_MODE] != NV_PRMCIO_INTERLACE_MODE_DISABLED) {
        disp_h *= 2;
    }

    SurfaceBinding *surface = pgraph_gl_surface_get_within(
        d, d->pcrtc.start + vga_display_params.line_offset);
    if (surface == NULL || !surface->color) {
        qemu_mutex_unlock(&d->pfifo.lock);
        return 0;
    }

    assert(surface->color);
    assert(surface->fmt.gl_attachment == GL_COLOR_ATTACHMENT0);
    assert(surface->fmt.gl_format == GL_RGBA
        || surface->fmt.gl_format == GL_RGB
        || surface->fmt.gl_format == GL_BGR
        || surface->fmt.gl_format == GL_BGRA
        );

    surface->frame_time = pg->frame_time;
    qemu_event_reset(&d->pgraph.sync_complete);
    qatomic_set(&pg->sync_pending, true);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
    qemu_event_wait(&d->pgraph.sync_complete);

    return r->gl_display_buffer;
}
