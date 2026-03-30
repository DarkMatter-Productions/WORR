/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#define NOMINMAX
#include "client/font.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "client/client.h"
#include "common/common.h"
#include "common/files.h"
#include "common/utils.h"
#include "common/zone.h"
#include "shared/shared.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#if USE_SDL3_TTF
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_ttf/SDL_textengine.h>
#endif

enum font_kind_t { FONT_LEGACY, FONT_KFONT, FONT_TTF };

struct kfont_glyph_t {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

#if USE_SDL3_TTF
struct font_ttf_glyph_t {
  bool valid = false;
  int page = -1;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int left = 0;
  int top = 0;
  int bottom = 0;
  int x_skip = 0;
  int advance_26_6 = 0;
};

struct font_ttf_chunk_t {
  bool empty = false;
  std::array<font_ttf_glyph_t, 256> glyphs{};
};

struct font_ttf_page_t {
  qhandle_t handle = 0;
  int width = 0;
  int height = 0;
  byte *pixels = nullptr;
};

struct font_ttf_t {
  TTF_Font *sdl_font = nullptr;
  void *data = nullptr;
  int data_size = 0;
  int pixel_height = 0;
  int baseline = 0;
  int extent = 0;
  int line_skip = 0;
  int fixed_advance_units = 0;
  int fixed_advance_26_6 = 0;
  std::array<font_ttf_chunk_t *, 0x110000 / 256> chunks{};
  std::vector<font_ttf_page_t> pages;
};
#endif

struct font_kfont_t {
  qhandle_t pic = 0;
  int tex_w = 0;
  int tex_h = 0;
  float inv_w = 0.0f;
  float inv_h = 0.0f;
  int line_height = 0;
  std::unordered_map<uint32_t, kfont_glyph_t> glyphs;
};

struct font_s {
  font_kind_t kind = FONT_LEGACY;
  int id = 0;
  int virtual_line_height = CONCHAR_HEIGHT;
  float pixel_scale = 1.0f;
  float unit_scale = 1.0f;
  int fixed_advance = 0;
  float letter_spacing = 0.0f;
  qhandle_t legacy_handle = 0;
#if USE_SDL3_TTF
  font_ttf_t ttf;
#endif
  font_kfont_t kfont;
  font_t *fallback_kfont = nullptr;
  bool registered = false;
};

static std::vector<font_t *> g_fonts;
static int g_font_seq = 0;
static cvar_t *cl_debug_fonts = nullptr;
static cvar_t *cl_font_scale_boost = nullptr;
static cvar_t *cl_font_draw_black_background = nullptr;
static cvar_t *cl_font_fallback_kfont = nullptr;
static cvar_t *cl_font_fallback_legacy = nullptr;

#if USE_SDL3_TTF
static cvar_t *cl_font_ttf_hinting = nullptr;
static bool g_ttf_ready = false;
static font_ttf_chunk_t g_ttf_null_chunk;
#if USE_SDL3_TTF
static TTF_TextEngine *g_ttf_engine = nullptr;
#endif
static const int k_ttf_atlas_size = 512;
static const int k_ttf_atlas_padding = 1;
#endif

static const char *font_safe_str(const char *value) {
  return (value && *value) ? value : "<null>";
}

static bool font_debug_enabled(void) {
  if (!cl_debug_fonts)
    cl_debug_fonts = Cvar_Get("cl_debug_fonts", "1", 0);
  return cl_debug_fonts && cl_debug_fonts->integer;
}

static void font_debug_printf(const char *fmt, ...) {
  if (!font_debug_enabled())
    return;

  char msg[MAXPRINTMSG];
  va_list args;
  va_start(args, fmt);
  Q_vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  Com_Printf("%s", msg);
}

static float font_scale_boost(void) {
  if (!cl_font_scale_boost)
    cl_font_scale_boost = Cvar_Get("cl_font_scale_boost", "1.5", CVAR_ARCHIVE);
  return cl_font_scale_boost ? Cvar_ClampValue(cl_font_scale_boost, 0.5f, 4.0f)
                             : 1.5f;
}

static bool font_draw_black_background_enabled(void) {
	if (!cl_font_draw_black_background) {
		cl_font_draw_black_background =
			Cvar_Get("cl_font_draw_black_background", "1", CVAR_ARCHIVE);
	}
	return cl_font_draw_black_background &&
		   Cvar_ClampInteger(cl_font_draw_black_background, 0, 1) != 0;
}

static float font_draw_scale(const font_t *font, int scale) {
  if (!font)
    return 1.0f;

  int draw_scale = scale > 0 ? scale : 1;
  float pixel_scale = font->pixel_scale > 0.0f ? font->pixel_scale : 1.0f;
  return (font->unit_scale * (float)draw_scale * font_scale_boost()) /
         pixel_scale;
}

static inline bool font_ext_is(const char *path, const char *ext) {
  if (!path || !*path || !ext || !*ext)
    return false;
  const char *file_ext = COM_FileExtension(path);
  if (!file_ext || !*file_ext)
    return false;
  if (*file_ext == '.')
    ++file_ext;
  if (*ext == '.')
    ++ext;
  return Q_strcasecmp(file_ext, ext) == 0;
}

static color_t font_resolve_color(int flags, color_t color) {
  if (flags & (UI_ALTCOLOR | UI_XORCOLOR)) {
    color_t alt = COLOR_RGB(255, 255, 0);
    alt.a = color.a;
    return alt;
  }
  return color;
}

static uint32_t font_read_codepoint(const char **src, size_t *remaining) {
  const unsigned char *text = reinterpret_cast<const unsigned char *>(*src);

  if (!remaining || !*remaining || !*text)
    return 0;

  uint8_t first = text[0];
  if (first < 0x80) {
    *src = reinterpret_cast<const char *>(text + 1);
    (*remaining)--;
    return first;
  }

  int bytes = 7 - Q_log2(first ^ 255);
  if (bytes < 2 || bytes > 4 || static_cast<size_t>(bytes) > *remaining) {
    *src = reinterpret_cast<const char *>(text + 1);
    (*remaining)--;
    return first;
  }

  uint32_t code = first & (127 >> bytes);
  for (int i = 1; i < bytes; ++i) {
    uint8_t cont = text[i];
    if ((cont & 0xC0) != 0x80) {
      *src = reinterpret_cast<const char *>(text + 1);
      (*remaining)--;
      return first;
    }
    code = (code << 6) | (cont & 63);
  }

  *src = reinterpret_cast<const char *>(text + bytes);
  *remaining -= bytes;

  if (code > UNICODE_MAX)
    return first;
  if (code >= 0xD800 && code <= 0xDFFF)
    return first;
  if ((bytes == 2 && code < 0x80) || (bytes == 3 && code < 0x800) ||
      (bytes == 4 && code < 0x10000))
    return first;

  return code;
}

static const char *font_format_char(uint32_t cp, char *buffer, size_t size) {
  if (!buffer || size == 0)
    return "";
  if (cp == ' ') {
    Q_snprintf(buffer, size, "space");
    return buffer;
  }
  if (cp == '\t') {
    Q_snprintf(buffer, size, "\\t");
    return buffer;
  }
  if (cp == '\n') {
    Q_snprintf(buffer, size, "\\n");
    return buffer;
  }
  if (cp == '\r') {
    Q_snprintf(buffer, size, "\\r");
    return buffer;
  }
  if (cp == '\\') {
    Q_snprintf(buffer, size, "\\\\");
    return buffer;
  }
  if (cp == '\'') {
    Q_snprintf(buffer, size, "\\'");
    return buffer;
  }
  if (cp >= 32 && cp < 127 && std::isprint(static_cast<int>(cp))) {
    Q_snprintf(buffer, size, "%c", (char)cp);
    return buffer;
  }
  Q_snprintf(buffer, size, "0x%02X", (unsigned)cp);
  return buffer;
}

static qhandle_t font_register_legacy(const char *path, const char *fallback) {
  qhandle_t handle = 0;
  if (path && *path)
    handle = R_RegisterFont(path);
  if (!handle && fallback && *fallback &&
      (!path || Q_strcasecmp(path, fallback))) {
    handle = R_RegisterFont(fallback);
  }
  return handle;
}

static bool font_load_kfont(font_t *font, const char *filename) {
  if (!font || !filename || !*filename)
    return false;

  char *buffer = nullptr;
  if (FS_LoadFile(filename, (void **)&buffer) < 0)
    return false;

  const char *data = buffer;
  while (true) {
    const char *token = COM_Parse(&data);
    if (!*token)
      break;

    if (!strcmp(token, "texture")) {
      token = COM_Parse(&data);
      font->kfont.pic = R_RegisterFont(va("/%s", token));
    } else if (!strcmp(token, "unicode")) {
      continue;
    } else if (!strcmp(token, "mapchar")) {
      COM_Parse(&data); // "{"
      while (true) {
        token = COM_Parse(&data);
        if (!strcmp(token, "}"))
          break;

        uint32_t codepoint = strtoul(token, nullptr, 10);
        uint32_t x = strtoul(COM_Parse(&data), nullptr, 10);
        uint32_t y = strtoul(COM_Parse(&data), nullptr, 10);
        uint32_t w = strtoul(COM_Parse(&data), nullptr, 10);
        uint32_t h = strtoul(COM_Parse(&data), nullptr, 10);
        COM_Parse(&data); // skip

        kfont_glyph_t glyph;
        glyph.x = (int)x;
        glyph.y = (int)y;
        glyph.w = (int)w;
        glyph.h = (int)h;
        font->kfont.glyphs[codepoint] = glyph;
        font->kfont.line_height = std::max(font->kfont.line_height, (int)h);
      }
    }
  }

  FS_FreeFile(buffer);

  if (!font->kfont.pic)
    return false;

  R_GetPicSize(&font->kfont.tex_w, &font->kfont.tex_h, font->kfont.pic);
  if (font->kfont.tex_w <= 0 || font->kfont.tex_h <= 0)
    return false;

  font->kfont.inv_w = 1.0f / (float)font->kfont.tex_w;
  font->kfont.inv_h = 1.0f / (float)font->kfont.tex_h;
  if (font->kfont.line_height <= 0)
    font->kfont.line_height = CONCHAR_HEIGHT;
  return true;
}

static int font_fixed_advance_scaled(const font_t *font, int scale) {
  if (!font || font->fixed_advance <= 0)
    return 0;
  int draw_scale = scale > 0 ? scale : 1;
  return std::max(
      1, Q_rint((float)font->fixed_advance * (float)draw_scale * font_scale_boost()));
}

#if USE_SDL3_TTF




static inline float ttf_float_26_6(int value) { return (float)value / 64.0f; }

static int font_ttf_hinting_mode(void) {
  if (!cl_font_ttf_hinting)
    cl_font_ttf_hinting = Cvar_Get("cl_font_ttf_hinting", "1", CVAR_ARCHIVE);
  return Cvar_ClampInteger(cl_font_ttf_hinting, 0, 3);
}







static void font_ttf_blit_alpha(byte *dst, int dst_width, int dst_height, int x,
                                int y, const byte *src, int src_pitch, int w,
                                int h) {
  if (!dst || !src || w <= 0 || h <= 0)
    return;
  if (x < 0 || y < 0 || x + w > dst_width || y + h > dst_height)
    return;

  for (int row = 0; row < h; ++row) {
    byte *dst_row = dst + ((y + row) * dst_width) + x;
    const byte *src_row = src + row * src_pitch;
    memcpy(dst_row, src_row, (size_t)w);
  }
}

static int font_ttf_store_page(font_t *font, const byte *alpha_pixels, int width,
                               int height) {
  if (!font || !alpha_pixels || width <= 0 || height <= 0)
    return -1;

  const size_t pixel_count = (size_t)width * (size_t)height;
  const size_t rgba_size = pixel_count * 4;

  font_ttf_page_t page;
  page.width = width;
  page.height = height;
  page.pixels = (byte *)Z_Malloc(rgba_size);

  for (size_t i = 0; i < pixel_count; ++i) {
    const size_t j = i * 4;
    page.pixels[j + 0] = 255;
    page.pixels[j + 1] = 255;
    page.pixels[j + 2] = 255;
    page.pixels[j + 3] = alpha_pixels[i];
  }

  byte *upload = (byte *)Z_Malloc(rgba_size);
  memcpy(upload, page.pixels, rgba_size);

  int page_index = (int)font->ttf.pages.size();
  page.handle = R_RegisterRawImage(
      va("fonts/_ttf_%d_%d", font->id, page_index), width, height, upload,
      IT_FONT,
      static_cast<imageflags_t>(IF_PERMANENT | IF_TRANSPARENT |
                                IF_NO_COLOR_ADJUST));
  if (!page.handle) {
    Z_Free(upload);
    Z_Free(page.pixels);
    return -1;
  }

  font->ttf.pages.push_back(page);
  return page_index;
}

static bool font_ttf_render_bitmap(font_t *font, uint32_t glyph_index,
                                   font_ttf_glyph_t *out_glyph,
                                   std::vector<byte> *out_bitmap,
                                   int *out_pitch) {
  if (!font || !out_glyph || !out_bitmap || !out_pitch || !font->ttf.sdl_font)
    return false;

  int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
  bool have_metrics = TTF_GetGlyphMetrics(font->ttf.sdl_font, glyph_index, &minx,
	&maxx, &miny, &maxy, &advance);

  out_glyph->valid = have_metrics;
  out_glyph->left = minx;
  out_glyph->top = maxy;
  out_glyph->bottom = miny;
  out_glyph->advance_26_6 = advance << 6;
  out_glyph->x_skip = std::max(0, advance);
  out_glyph->w = std::max(0, maxx - minx);
  out_glyph->h = std::max(0, maxy - miny);

  if (have_metrics && out_glyph->w <= 0 && out_glyph->h <= 0) {
    *out_pitch = 0;
    out_bitmap->clear();
    return true;
  }

  SDL_Surface *surface = TTF_GetGlyphImageForIndex(font->ttf.sdl_font, glyph_index, NULL);
  if (!surface) {
    *out_pitch = 0;
    out_bitmap->clear();
    return true;
  }

  out_glyph->valid = true;
  out_glyph->w = surface->w;
  out_glyph->h = surface->h;
  if (!have_metrics && out_glyph->x_skip <= 0) {
	out_glyph->x_skip = surface->w;
	out_glyph->advance_26_6 = surface->w << 6;
  }
  *out_pitch = surface->w;
  out_bitmap->assign((size_t)surface->w * (size_t)surface->h, 0);

  SDL_LockSurface(surface);
  for (int y = 0; y < surface->h; ++y) {
    const byte *src_row = (const byte *)surface->pixels + y * surface->pitch;
    byte *dst_row = out_bitmap->data() + y * (*out_pitch);
    memcpy(dst_row, src_row, surface->w);
  }
  SDL_UnlockSurface(surface);
  SDL_DestroySurface(surface);
  return true;
}

static bool font_ttf_render_chunk(font_t *font, uint32_t chunk_index) {
  if (!font || font->kind != FONT_TTF || !font->ttf.sdl_font)
    return false;
  if (chunk_index >= font->ttf.chunks.size())
    return false;
  if (font->ttf.chunks[chunk_index])
    return font->ttf.chunks[chunk_index] != &g_ttf_null_chunk;

  font_ttf_chunk_t *chunk = new font_ttf_chunk_t();
  bool any_glyph = false;

  std::vector<byte> page_alpha((size_t)k_ttf_atlas_size *
                               (size_t)k_ttf_atlas_size, 0);
  std::vector<int> pending_slots;
  int pen_x = k_ttf_atlas_padding;
  int pen_y = k_ttf_atlas_padding;
  int row_height = 0;

  auto flush_page = [&]() {
    if (pending_slots.empty())
      return;
    int page_index = font_ttf_store_page(font, page_alpha.data(), k_ttf_atlas_size,
                                         k_ttf_atlas_size);
    for (int slot : pending_slots) {
      if (page_index >= 0)
        chunk->glyphs[(size_t)slot].page = page_index;
      else
        chunk->glyphs[(size_t)slot].page = -1;
    }
    pending_slots.clear();
    std::fill(page_alpha.begin(), page_alpha.end(), 0);
    pen_x = k_ttf_atlas_padding;
    pen_y = k_ttf_atlas_padding;
    row_height = 0;
  };

  for (int i = 0; i < 256; ++i) {
    uint32_t glyph_index = chunk_index * 256u + (uint32_t)i;

    font_ttf_glyph_t glyph;
    std::vector<byte> bitmap;
    int bitmap_pitch = 0;
    if (!font_ttf_render_bitmap(font, glyph_index, &glyph, &bitmap, &bitmap_pitch))
      continue;

    any_glyph = true;
    if (font->fixed_advance > 0 && font->ttf.fixed_advance_units > 0) {
      glyph.x_skip = font->ttf.fixed_advance_units;
      glyph.advance_26_6 = font->ttf.fixed_advance_26_6;
    }

    if (glyph.w <= 0 || glyph.h <= 0) {
      chunk->glyphs[(size_t)i] = glyph;
      continue;
    }

    if (glyph.w + (k_ttf_atlas_padding * 2) > k_ttf_atlas_size ||
        glyph.h + (k_ttf_atlas_padding * 2) > k_ttf_atlas_size) {
      chunk->glyphs[(size_t)i] = glyph;
      continue;
    }

    if (pen_x + glyph.w + k_ttf_atlas_padding > k_ttf_atlas_size) {
      pen_x = k_ttf_atlas_padding;
      pen_y += row_height + k_ttf_atlas_padding;
      row_height = 0;
    }

    if (pen_y + glyph.h + k_ttf_atlas_padding > k_ttf_atlas_size)
      flush_page();

    if (pen_y + glyph.h + k_ttf_atlas_padding > k_ttf_atlas_size) {
      chunk->glyphs[(size_t)i] = glyph;
      continue;
    }

    font_ttf_blit_alpha(page_alpha.data(), k_ttf_atlas_size, k_ttf_atlas_size,
                        pen_x, pen_y, bitmap.data(), bitmap_pitch, glyph.w,
                        glyph.h);

    glyph.page = -2;
    glyph.x = pen_x;
    glyph.y = pen_y;
    chunk->glyphs[(size_t)i] = glyph;
    pending_slots.push_back(i);

    pen_x += glyph.w + k_ttf_atlas_padding;
    row_height = std::max(row_height, glyph.h);
  }

  flush_page();

  if (!any_glyph) {
    delete chunk;
    font->ttf.chunks[chunk_index] = &g_ttf_null_chunk;
    return false;
  }

  chunk->empty = false;
  font->ttf.chunks[chunk_index] = chunk;
  return true;
}

static font_ttf_chunk_t *font_ttf_get_chunk(font_t *font, uint32_t chunk_index) {
  if (!font || font->kind != FONT_TTF || chunk_index >= font->ttf.chunks.size())
    return nullptr;

  font_ttf_chunk_t *chunk = font->ttf.chunks[chunk_index];
  if (!chunk) {
    font_ttf_render_chunk(font, chunk_index);
    chunk = font->ttf.chunks[chunk_index];
  }

  if (!chunk || chunk == &g_ttf_null_chunk || chunk->empty)
    return nullptr;
  return chunk;
}

static const font_ttf_glyph_t *font_ttf_get_glyph_by_index(font_t *font, uint32_t glyph_index) {
  if (!font || font->kind != FONT_TTF || !font->ttf.sdl_font) { return nullptr; }
  uint32_t chunk_index = glyph_index / 256u;
  uint32_t chunk_slot = glyph_index & 255u;

  font_ttf_chunk_t *chunk = font_ttf_get_chunk(font, chunk_index);
  if (chunk) {
    const font_ttf_glyph_t &glyph = chunk->glyphs[(size_t)chunk_slot];
    if (glyph.valid)
      return &glyph;
  }

  if (glyph_index != 0)
    return font_ttf_get_glyph_by_index(font, 0);

  return nullptr;
}



static float font_ttf_advance(const font_t *font, const font_ttf_glyph_t *glyph,
                              int scale) {
  if (!font || !glyph)
    return 0.0f;
  if (font->fixed_advance > 0)
    return (float)font_fixed_advance_scaled(font, scale);
  if (glyph->advance_26_6 <= 0)
    return 0.0f;
  return std::max(0.0f, ttf_float_26_6(glyph->advance_26_6) *
                            font_draw_scale(font, scale));
}



static bool font_draw_ttf_glyph_at(const font_t *font, const font_ttf_glyph_t *glyph,
                                   float draw_xf, float draw_yf, int scale, int flags,
                                   color_t color) {
  if (!font || font->kind != FONT_TTF || !glyph || !glyph->valid)
    return false;

  int draw_scale = scale > 0 ? scale : 1;
  float glyph_scale = font_draw_scale(font, draw_scale);

  if (glyph->page >= 0 && glyph->page < (int)font->ttf.pages.size() &&
      glyph->w > 0 && glyph->h > 0) {
    const font_ttf_page_t &page = font->ttf.pages[(size_t)glyph->page];
    if (page.handle) {
      int draw_x = (int)floorf(draw_xf);
      int draw_y = (int)floorf(draw_yf);
      int draw_w = std::max(1, (int)ceilf((float)glyph->w * glyph_scale));
      int draw_h = std::max(1, (int)ceilf((float)glyph->h * glyph_scale));

      float s1 = (float)glyph->x / (float)page.width;
      float t1 = (float)glyph->y / (float)page.height;
      float s2 = (float)(glyph->x + glyph->w) / (float)page.width;
      float t2 = (float)(glyph->y + glyph->h) / (float)page.height;

      if (flags & UI_DROPSHADOW) {
        int shadow = std::max(1, draw_scale);
        color_t black = COLOR_A(color.a);
        R_DrawStretchSubPic(draw_x + shadow, draw_y + shadow, draw_w, draw_h,
                            s1, t1, s2, t2, black, page.handle);
      }

      R_DrawStretchSubPic(draw_x, draw_y, draw_w, draw_h, s1, t1, s2, t2, color,
                          page.handle);
    }
  }

  return true;
}

static void font_free_ttf(font_t *font) {
  if (!font || font->kind != FONT_TTF)
    return;

  for (font_ttf_chunk_t *chunk : font->ttf.chunks) {
    if (chunk && chunk != &g_ttf_null_chunk)
      delete chunk;
  }
  font->ttf.chunks.fill(nullptr);

  for (font_ttf_page_t &page : font->ttf.pages) {
    if (page.handle)
      R_UnregisterImage(page.handle);
    if (page.pixels)
      Z_Free(page.pixels);
  }
  font->ttf.pages.clear();

  if (font->ttf.sdl_font) {
    TTF_CloseFont(font->ttf.sdl_font);
    font->ttf.sdl_font = nullptr;
  }
  if (font->ttf.data) {
    FS_FreeFile(font->ttf.data);
    font->ttf.data = nullptr;
  }
  font->ttf.data_size = 0;
}

static bool font_load_ttf(font_t *font, const char *path) {
  if (!font || !path || !*path || !g_ttf_ready)
    return false;

  void *data = nullptr;
  int len = FS_LoadFile(path, &data);
  if (len <= 0 || !data)
    return false;

  int pixel_height = std::max(1, Q_rint((float)font->virtual_line_height * font->pixel_scale));

  SDL_IOStream *io = SDL_IOFromConstMem(data, (size_t)len);
  if (!io) {
    FS_FreeFile(data);
    return false;
  }

  TTF_Font *sdl_font = TTF_OpenFontIO(io, true, (float)pixel_height);
  if (!sdl_font) {
    FS_FreeFile(data);
    return false;
  }

  font->kind = FONT_TTF;
  font->ttf.sdl_font = sdl_font;
  font->ttf.data = data;
  font->ttf.data_size = len;
  font->ttf.pixel_height = pixel_height;

  int asc = TTF_GetFontAscent(sdl_font);
  int desc = -TTF_GetFontDescent(sdl_font); // descent is negative
  int line = TTF_GetFontLineSkip(sdl_font);
  int extent = std::max(line, asc + desc);
  if (extent <= 0)
    extent = pixel_height;

  font->ttf.line_skip = line;
  font->ttf.extent = extent;
  font->ttf.baseline = std::clamp(asc + 1, 1, extent);
  font->unit_scale = ((float)font->virtual_line_height * font->pixel_scale) / (float)extent;

  if (font->fixed_advance > 0) {
    int minx, maxx, miny, maxy, advance;
    if (TTF_GetGlyphMetrics(sdl_font, 'M', &minx, &maxx, &miny, &maxy, &advance)) {
      int xskip = std::max(1, advance);
      font->ttf.fixed_advance_units = xskip;
      font->ttf.fixed_advance_26_6 = xskip << 6;
      float vscale = (float)font->virtual_line_height / (float)font->ttf.extent;
      font->fixed_advance = std::max(1, Q_rint((float)xskip * vscale));
    }
  }
  return true;
}
#endif

static const char *font_kind_name(font_kind_t kind) {
  switch (kind) {
  case FONT_LEGACY:
    return "legacy";
  case FONT_KFONT:
    return "kfont";
  case FONT_TTF:
    return "ttf";
  default:
    return "unknown";
  }
}

static int font_advance_for_codepoint(const font_t *font, uint32_t codepoint,
                                      int scale) {
  if (!font || !codepoint)
    return 0;

  int draw_scale = scale > 0 ? scale : 1;
  if (font->fixed_advance > 0)
    return font_fixed_advance_scaled(font, draw_scale);

  if (font->kind == FONT_LEGACY) {
    float s = font_draw_scale(font, draw_scale);
    return std::max(1, Q_rint((float)CONCHAR_WIDTH * s));
  }

  if (font->kind == FONT_KFONT) {
    auto it = font->kfont.glyphs.find(codepoint);
    if (it == font->kfont.glyphs.end())
      return 0;
    float s = font_draw_scale(font, draw_scale);
    return std::max(1, Q_rint((float)it->second.w * s));
  }

#if USE_SDL3_TTF
  if (font->kind == FONT_TTF) {
    const font_ttf_glyph_t *glyph =
        font_ttf_get_glyph_by_index(const_cast<font_t *>(font), codepoint);
    if (!glyph)
      return 0;
    return std::max(0, Q_rint(font_ttf_advance(font, glyph, draw_scale)));
  }
#endif

  return 0;
}

static bool font_draw_legacy_glyph(const font_t *font, uint32_t codepoint, int *x,
                                   int y, int scale, int flags, color_t color) {
  if (!font || !font->legacy_handle || !x)
    return false;

  int draw_scale = scale > 0 ? scale : 1;
  float s = font_draw_scale(font, draw_scale);
  int w = std::max(1, Q_rint((float)CONCHAR_WIDTH * s));
  int h = std::max(1, Q_rint((float)CONCHAR_HEIGHT * s));
  int ch = codepoint <= 255 ? (int)codepoint : '?';

  R_DrawStretchChar(*x, y, w, h, flags, ch, color, font->legacy_handle);
  *x += (font->fixed_advance > 0) ? font_fixed_advance_scaled(font, draw_scale)
                                  : w;
  return true;
}

static bool font_draw_kfont_glyph(const font_t *font, uint32_t codepoint, int *x,
                                  int y, int scale, int flags, color_t color) {
  if (!font || font->kind != FONT_KFONT || !font->kfont.pic || !x)
    return false;

  auto it = font->kfont.glyphs.find(codepoint);
  if (it == font->kfont.glyphs.end())
    return false;

  int draw_scale = scale > 0 ? scale : 1;
  float glyph_scale = font_draw_scale(font, draw_scale);
  const kfont_glyph_t &glyph = it->second;
  int w = std::max(1, Q_rint((float)glyph.w * glyph_scale));
  int h = std::max(1, Q_rint((float)glyph.h * glyph_scale));

  float s1 = (float)glyph.x * font->kfont.inv_w;
  float t1 = (float)glyph.y * font->kfont.inv_h;
  float s2 = (float)(glyph.x + glyph.w) * font->kfont.inv_w;
  float t2 = (float)(glyph.y + glyph.h) * font->kfont.inv_h;

  if (flags & UI_DROPSHADOW) {
    int shadow = std::max(1, draw_scale);
    color_t black = COLOR_A(color.a);
    R_DrawStretchSubPic(*x + shadow, y + shadow, w, h, s1, t1, s2, t2, black,
                        font->kfont.pic);
  }

  R_DrawStretchSubPic(*x, y, w, h, s1, t1, s2, t2, color, font->kfont.pic);
  *x += (font->fixed_advance > 0) ? font_fixed_advance_scaled(font, draw_scale)
                                  : w;
  return true;
}

static font_t *font_load_internal(const char *path, int virtual_line_height,
                                  float pixel_scale, int fixed_advance,
                                  const char *fallback_kfont,
                                  const char *fallback_legacy,
                                  bool register_font) {
	if (!cl_font_fallback_kfont)
		cl_font_fallback_kfont =
			Cvar_Get("cl_font_fallback_kfont", "fonts/qfont.kfont", CVAR_ARCHIVE);
	if (!cl_font_fallback_legacy)
		cl_font_fallback_legacy =
			Cvar_Get("cl_font_fallback_legacy", "conchars.png", CVAR_ARCHIVE);

	if ((!fallback_kfont || !*fallback_kfont) &&
		cl_font_fallback_kfont && cl_font_fallback_kfont->string &&
		*cl_font_fallback_kfont->string) {
		fallback_kfont = cl_font_fallback_kfont->string;
	}

	if ((!fallback_legacy || !*fallback_legacy) &&
		cl_font_fallback_legacy && cl_font_fallback_legacy->string &&
		*cl_font_fallback_legacy->string) {
		fallback_legacy = cl_font_fallback_legacy->string;
	}

  if (virtual_line_height <= 0)
    virtual_line_height = CONCHAR_HEIGHT;

  font_t *font = new font_t();
  font->id = ++g_font_seq;
  font->virtual_line_height = virtual_line_height;
  font->pixel_scale = pixel_scale > 0.0f ? pixel_scale : 1.0f;
  font->fixed_advance = fixed_advance > 0 ? fixed_advance : 0;
  font->registered = register_font;

  if (font_ext_is(path, "ttf") || font_ext_is(path, "otf")) {
#if USE_SDL3_TTF
    if (font_load_ttf(font, path)) {
      if (fallback_kfont && *fallback_kfont) {
        font->fallback_kfont = font_load_internal(
            fallback_kfont, virtual_line_height, pixel_scale, fixed_advance,
            nullptr, fallback_legacy, false);
      }
      font->legacy_handle = font_register_legacy(fallback_legacy, fallback_legacy);
      goto loaded;
    }
#endif

    if (fallback_kfont && *fallback_kfont) {
      font->kind = FONT_KFONT;
      if (font_load_kfont(font, fallback_kfont)) {
        font->unit_scale = (float)(virtual_line_height * font->pixel_scale) /
                           (float)font->kfont.line_height;
        font->legacy_handle =
            font_register_legacy(fallback_legacy, fallback_legacy);
        goto loaded;
      }
      font->kfont.glyphs.clear();
      font->kfont.pic = 0;
      font->kfont.tex_w = 0;
      font->kfont.tex_h = 0;
      font->kfont.inv_w = 0.0f;
      font->kfont.inv_h = 0.0f;
      font->kfont.line_height = 0;
    }

    font->kind = FONT_LEGACY;
    font->legacy_handle = font_register_legacy(fallback_legacy, fallback_legacy);
    if (!font->legacy_handle) {
      delete font;
      return nullptr;
    }
    font->unit_scale =
        (float)(virtual_line_height * font->pixel_scale) / (float)CONCHAR_HEIGHT;
  } else if (font_ext_is(path, "kfont")) {
    font->kind = FONT_KFONT;
    if (!font_load_kfont(font, path)) {
      font->kind = FONT_LEGACY;
      font->legacy_handle = font_register_legacy(fallback_legacy, fallback_legacy);
      if (!font->legacy_handle) {
        delete font;
        return nullptr;
      }
      font->unit_scale = (float)(virtual_line_height * font->pixel_scale) /
                         (float)CONCHAR_HEIGHT;
    } else {
      font->unit_scale = (float)(virtual_line_height * font->pixel_scale) /
                         (float)font->kfont.line_height;
      font->legacy_handle = font_register_legacy(fallback_legacy, fallback_legacy);
    }
  } else {
    font->kind = FONT_LEGACY;
    font->legacy_handle = font_register_legacy(path, fallback_legacy);
    if (!font->legacy_handle) {
      delete font;
      return nullptr;
    }
    font->unit_scale =
        (float)(virtual_line_height * font->pixel_scale) / (float)CONCHAR_HEIGHT;
  }

loaded:
  if (register_font)
    g_fonts.push_back(font);

  font_debug_printf("Font: loaded kind=%s path=\"%s\"\n",
                    font_kind_name(font->kind), font_safe_str(path));
  return font;
}

static void font_dump_glyphs_kfont(qhandle_t file, font_t *font) {
  FS_FPrintf(file, "kind: kfont line_height=%d\n", font->kfont.line_height);
  FS_FPrintf(file, "cp hex char x y w h\n");
  for (uint32_t cp = 32; cp <= 126; ++cp) {
    auto it = font->kfont.glyphs.find(cp);
    char disp[16];
    if (it == font->kfont.glyphs.end()) {
      FS_FPrintf(file, "%3u 0x%02X '%s' missing\n", cp, cp,
                 font_format_char(cp, disp, sizeof(disp)));
      continue;
    }
    const kfont_glyph_t &g = it->second;
    FS_FPrintf(file, "%3u 0x%02X '%s' %4d %4d %3d %3d\n", cp, cp,
               font_format_char(cp, disp, sizeof(disp)), g.x, g.y, g.w, g.h);
  }
}

static void font_dump_glyphs_legacy(qhandle_t file) {
  FS_FPrintf(file, "kind: legacy\n");
  FS_FPrintf(file, "cp hex char w h\n");
  for (uint32_t cp = 32; cp <= 126; ++cp) {
    char disp[16];
    FS_FPrintf(file, "%3u 0x%02X '%s' %3d %3d\n", cp, cp,
               font_format_char(cp, disp, sizeof(disp)), CONCHAR_WIDTH,
               CONCHAR_HEIGHT);
  }
}

#if USE_SDL3_TTF
static void font_dump_glyphs_ttf(qhandle_t file, font_t *font) {
  FS_FPrintf(file, "kind: ttf\n");
  FS_FPrintf(file, "pixel_height=%d baseline=%d extent=%d line_skip=%d\n",
             font->ttf.pixel_height, font->ttf.baseline, font->ttf.extent,
             font->ttf.line_skip);
  FS_FPrintf(file, "cp hex char adv left top bottom w h page x y\n");
  for (uint32_t cp = 32; cp <= 126; ++cp) {
    const font_ttf_glyph_t *g = font_ttf_get_glyph_by_index(font, cp); /* cp is not necessarily index but it's just a dump */
    char disp[16];
    if (!g) {
      FS_FPrintf(file, "%3u 0x%02X '%s' missing\n", cp, cp,
                 font_format_char(cp, disp, sizeof(disp)));
      continue;
    }
    FS_FPrintf(file, "%3u 0x%02X '%s' %4d %5d %4d %6d %3d %3d %3d %4d %4d\n", cp,
               cp, font_format_char(cp, disp, sizeof(disp)), g->x_skip, g->left,
               g->top, g->bottom, g->w, g->h, g->page, g->x, g->y);
  }
}
#endif

static void Font_DumpGlyphs_f(void) {
  int line_height = 32;
  const char *font_path = nullptr;
  if (Cmd_Argc() > 1)
    font_path = Cmd_Argv(1);
  if (Cmd_Argc() > 2)
    line_height = Q_clip(atoi(Cmd_Argv(2)), 1, 256);
  if (!font_path || !*font_path)
    font_path = Cvar_VariableString("ui_font");

  char out_path[MAX_OSPATH];
  qhandle_t file = 0;
  for (int i = 0; i < 1000; ++i) {
    Q_snprintf(out_path, sizeof(out_path), "fontdump/glyphs_%03d.txt", i);
    int ret = FS_OpenFile(out_path, &file,
                          FS_MODE_WRITE | FS_FLAG_TEXT | FS_FLAG_EXCL);
    if (file)
      break;
    if (ret != Q_ERR(EEXIST)) {
      Com_EPrintfLoc("$e_auto_73287ca7dcec", out_path, Q_ErrorString(ret));
      return;
    }
  }

  if (!file) {
    Com_Printf("font_dump_glyphs: failed to open output file\n");
    return;
  }

  font_t *font = font_load_internal(font_path, line_height, 1.0f, 0,
                                    "fonts/qfont.kfont", "conchars.png", false);
  if (!font) {
    FS_FPrintf(file, "load failed for path: %s\n", font_safe_str(font_path));
    FS_CloseFile(file);
    return;
  }

  FS_FPrintf(file, "WORR Font Glyph Dump\n");
  FS_FPrintf(file, "path: %s\n", font_safe_str(font_path));
  FS_FPrintf(file, "line_height=%d scale_boost=%.2f\n", line_height,
             font_scale_boost());

  switch (font->kind) {
  case FONT_TTF:
#if USE_SDL3_TTF
    font_dump_glyphs_ttf(file, font);
#else
    font_dump_glyphs_legacy(file);
#endif
    break;
  case FONT_KFONT:
    font_dump_glyphs_kfont(file, font);
    break;
  case FONT_LEGACY:
  default:
    font_dump_glyphs_legacy(file);
    break;
  }

  Font_Free(font);
  if (FS_CloseFile(file))
    Com_EPrintfLoc("$cl_error_writing_file", out_path);
  else
    Com_PrintfLoc("$cl_write_complete", out_path);
}

void Font_Init(void) {
  (void)font_debug_enabled();
  (void)font_scale_boost();
  (void)font_draw_black_background_enabled();
	if (!cl_font_fallback_kfont)
		cl_font_fallback_kfont =
			Cvar_Get("cl_font_fallback_kfont", "fonts/qfont.kfont", CVAR_ARCHIVE);
	if (!cl_font_fallback_legacy)
		cl_font_fallback_legacy =
			Cvar_Get("cl_font_fallback_legacy", "conchars.png", CVAR_ARCHIVE);
#if USE_CLIENT
  Cmd_AddCommand("font_dump_glyphs", Font_DumpGlyphs_f);
#endif

#if USE_SDL3_TTF
  (void)font_ttf_hinting_mode();
  g_ttf_null_chunk.empty = true;
  if (!g_ttf_ready) {
    if (!TTF_Init()) {
      Com_WPrintf("SDL3_ttf init failed, TTF fonts disabled\n");
      g_ttf_ready = false;
    } else {
      g_ttf_engine = TTF_CreateSurfaceTextEngine();
      if (!g_ttf_engine) {
        Com_WPrintf("SDL3_ttf text engine creation failed, TTF fonts disabled\n");
        g_ttf_ready = false;
      } else {
        g_ttf_ready = true;
        font_debug_printf("Font: SDL3_ttf initialized\n");
      }
    }
  }
#else
  font_debug_printf("Font: TTF support disabled at build time\n");
#endif
}

void Font_Shutdown(void) {
#if USE_CLIENT
  Cmd_RemoveCommand("font_dump_glyphs");
#endif

  for (font_t *font : g_fonts)
    Font_Free(font);
  g_fonts.clear();

#if USE_SDL3_TTF
  if (g_ttf_ready) {
    if (g_ttf_engine) {
      TTF_DestroySurfaceTextEngine(g_ttf_engine);
      g_ttf_engine = nullptr;
    }
    TTF_Quit();
    g_ttf_ready = false;
  }
#endif
}

font_t *Font_Load(const char *path, int virtual_line_height, float pixel_scale,
                  int fixed_advance, const char *fallback_kfont,
                  const char *fallback_legacy) {
  return font_load_internal(path, virtual_line_height, pixel_scale,
                            fixed_advance, fallback_kfont, fallback_legacy, true);
}

void Font_Free(font_t *font) {
  if (!font)
    return;

  if (font->registered) {
    auto it = std::find(g_fonts.begin(), g_fonts.end(), font);
    if (it != g_fonts.end())
      g_fonts.erase(it);
  }

#if USE_SDL3_TTF
  if (font->kind == FONT_TTF)
    font_free_ttf(font);
#endif

  if (font->fallback_kfont) {
    Font_Free(font->fallback_kfont);
    font->fallback_kfont = nullptr;
  }

  delete font;
}

void Font_SetLetterSpacing(font_t *font, float spacing) {
  if (!font)
    return;
  font->letter_spacing = spacing > 0.0f ? spacing : 0.0f;
}

static void font_draw_string_black_background(const font_t *font, int x, int y,
                                              int draw_scale, int flags,
                                              size_t max_chars,
                                              const char *string) {
  if (!font || !string || !*string)
    return;

  const int line_height = Font_LineHeight(font, draw_scale);
  if (line_height <= 0)
    return;

  const float pixel_spacing =
      font->pixel_scale > 0.0f ? (1.0f / font->pixel_scale) : 0.0f;
  const int line_step = Q_rint((float)line_height + pixel_spacing);
  const int padding = std::max(1, Q_rint(font_draw_scale(font, draw_scale)));

  size_t remaining = max_chars;
  const char *s = string;
  int line_y = y;
  while (remaining && *s) {
    const char *line_start = s;
    size_t line_len = 0;

    while (remaining && *s) {
      if ((flags & UI_MULTILINE) && *s == '\n')
        break;
      ++s;
      --remaining;
      ++line_len;
    }

    if (line_len > 0) {
      int line_width = Font_MeasureString(font, draw_scale, flags & ~UI_MULTILINE,
                                          line_len, line_start, nullptr);
      if (line_width > 0) {
        R_DrawFill32(x - padding, line_y - padding, line_width + (padding * 2),
                     line_height + (padding * 2), COLOR_BLACK);
      }
    }

    if (!(flags & UI_MULTILINE) || !remaining || *s != '\n')
      break;

    ++s;
    --remaining;
    line_y += line_step;
  }
}

int Font_DrawString(font_t *font, int x, int y, int scale, int flags,
                    size_t max_chars, const char *string, color_t color) {
  if (!font || !string || !*string)
    return x;

  int draw_scale = scale > 0 ? scale : 1;
  float line_height = (float)Font_LineHeight(font, draw_scale);
  float pixel_spacing = font->pixel_scale > 0.0f ? (1.0f / font->pixel_scale) : 0.0f;
  int start_x = x;
  int x_i = x;
  float x_f = (float)x;

  int draw_flags = flags;
  bool use_color_codes = Com_HasColorEscape(string, max_chars);
  color_t base_color = color;
  if (use_color_codes) {
    base_color = font_resolve_color(flags, color);
    draw_flags &= ~(UI_ALTCOLOR | UI_XORCOLOR);
  }

  color_t draw_color = (font->kind == FONT_LEGACY) ? color : font_resolve_color(flags, color);
  if (use_color_codes)
    draw_color = base_color;

  if (font_draw_black_background_enabled()) {
    font_draw_string_black_background(font, x, y, draw_scale, flags, max_chars, string);
  }

  size_t remaining = max_chars;
  const char *s = string;
  while (remaining && *s) {
    if ((flags & UI_MULTILINE) && *s == '\n') {
      x_i = start_x;
      x_f = (float)start_x;
      y += Q_rint(line_height + pixel_spacing);
      ++s;
      --remaining;
      continue;
    }

    if (use_color_codes) {
      color_t parsed;
      if (Com_ParseColorEscape(&s, &remaining, base_color, &parsed)) {
        draw_color = parsed;
        continue;
      }
    }

    const char *seg_start = s;
    size_t seg_len = 0;
    while (remaining && *s) {
      if ((flags & UI_MULTILINE) && *s == '\n') break;
      if (use_color_codes && *s == '^' && remaining > 1 && Com_IsColorEscapeCode(*(s + 1))) break;

      uint32_t cp = font_read_codepoint(&s, &remaining);
      if (!cp) break;
      seg_len = (size_t)(s - seg_start);
    }

    if (seg_len == 0) continue;

#if USE_SDL3_TTF
    if (font->kind == FONT_TTF && g_ttf_engine) {
      bool drew_ttf_segment = false;
      TTF_Text *text_obj = TTF_CreateText(g_ttf_engine, font->ttf.sdl_font, seg_start, seg_len);
      if (text_obj && text_obj->internal && text_obj->internal->ops) {
        float glyph_scale = font_draw_scale(font, draw_scale);
        float baseline_y = (float)y + (float)font->ttf.baseline * glyph_scale;

        for (int i = 0; i < text_obj->internal->num_ops; ++i) {
          TTF_DrawOperation *op = &text_obj->internal->ops[i];
          if (op->cmd == TTF_DRAW_COMMAND_COPY) {
            uint32_t glyph_idx = op->copy.glyph_index;
            const font_ttf_glyph_t *glyph = font_ttf_get_glyph_by_index(font, glyph_idx);
            if (glyph && glyph->valid) {
              float draw_xf = x_f + (float)op->copy.dst.x * glyph_scale;
              float draw_yf = baseline_y + (float)op->copy.dst.y * glyph_scale;

              if (font->fixed_advance > 0) {
                 float centered = ((float)font_fixed_advance_scaled(font, draw_scale) - (glyph->w * glyph_scale)) * 0.5f;
                 draw_xf += centered;
              }

              font_draw_ttf_glyph_at(font, glyph, draw_xf, draw_yf, draw_scale, draw_flags, draw_color);
            }
          }
        }
        x_f += (float)text_obj->internal->w * glyph_scale;
        x_i = Q_rint(x_f);
        drew_ttf_segment = true;
        TTF_DestroyText(text_obj);
      } else {
        if (text_obj) TTF_DestroyText(text_obj);
      }
      if (drew_ttf_segment)
        continue;
    }
#endif

    const char *fs = seg_start;
    size_t f_rem = seg_len;
    while (f_rem > 0) {
      uint32_t codepoint = font_read_codepoint(&fs, &f_rem);
      if (!codepoint) break;

      bool drawn = false;
      if (font->kind == FONT_KFONT) {
        drawn = font_draw_kfont_glyph(font, codepoint, &x_i, y, draw_scale, draw_flags, draw_color);
        x_f = (float)x_i;
      }
      if (!drawn && font->kind == FONT_LEGACY) {
        drawn = font_draw_legacy_glyph(font, codepoint, &x_i, y, draw_scale, draw_flags, draw_color);
        x_f = (float)x_i;
      }
      if (!drawn && font->fallback_kfont) {
        int fallback_x = x_i;
        drawn = font_draw_kfont_glyph(font->fallback_kfont, codepoint, &fallback_x, y, draw_scale, draw_flags, draw_color);
        if (drawn) { x_i = fallback_x; x_f = (float)fallback_x; }
      }
      if (!drawn) {
        int fallback_x = x_i;
        font_draw_legacy_glyph(font, codepoint, &fallback_x, y, draw_scale, draw_flags, draw_color);
        x_i = fallback_x;
        x_f = (float)fallback_x;
      }
    }
  }

  return font->kind == FONT_TTF ? Q_rint(x_f) : x_i;
}

int Font_MeasureString(const font_t *font, int scale, int flags,
                       size_t max_chars, const char *string, int *out_height) {
  if (!font || !string || !*string) {
    if (out_height) *out_height = 0;
    return 0;
  }

  int draw_scale = scale > 0 ? scale : 1;
  float line_height = (float)Font_LineHeight(font, draw_scale);
  float pixel_spacing = font->pixel_scale > 0.0f ? (1.0f / font->pixel_scale) : 0.0f;
  float pen_x = 0.0f;
  float max_width = 0.0f;
  int lines = 1;

  bool use_color_codes = Com_HasColorEscape(string, max_chars);
  size_t remaining = max_chars;
  const char *s = string;
  while (remaining && *s) {
    if ((flags & UI_MULTILINE) && *s == '\n') {
      max_width = std::max(max_width, pen_x);
      pen_x = 0.0f;
      ++lines;
      ++s;
      --remaining;
      continue;
    }

    if (use_color_codes) {
      if (Com_ParseColorEscape(&s, &remaining, COLOR_WHITE, nullptr))
        continue;
    }

    const char *seg_start = s;
    size_t seg_len = 0;
    while (remaining && *s) {
      if ((flags & UI_MULTILINE) && *s == '\n') break;
      if (use_color_codes && *s == '^' && remaining > 1 && Com_IsColorEscapeCode(*(s + 1))) break;
      uint32_t cp = font_read_codepoint(&s, &remaining);
      if (!cp) break;
      seg_len = (size_t)(s - seg_start);
    }

    if (seg_len == 0) continue;

#if USE_SDL3_TTF
    if (font->kind == FONT_TTF && g_ttf_engine) {
      int w = 0, h = 0;
      if (TTF_GetStringSize(font->ttf.sdl_font, seg_start, seg_len, &w, &h)) {
        pen_x += (float)w * font_draw_scale(font, draw_scale);
        max_width = std::max(max_width, pen_x);
        continue;
      }
    }
#endif

    const char *fs = seg_start;
    size_t f_rem = seg_len;
    while (f_rem > 0) {
      uint32_t codepoint = font_read_codepoint(&fs, &f_rem);
      if (!codepoint) break;
      int fallback_advance = 0;
      if (font->fallback_kfont) {
        fallback_advance = font_advance_for_codepoint(font->fallback_kfont, codepoint, draw_scale);
      }
      if (!fallback_advance && font->legacy_handle) {
        fallback_advance = std::max(1, Q_rint((float)CONCHAR_WIDTH * font_draw_scale(font, draw_scale)));
      }
      pen_x += (float)fallback_advance;
    }
    max_width = std::max(max_width, pen_x);
  }

  if (out_height) {
    *out_height = Q_rint((float)lines * line_height + (float)(lines - 1) * pixel_spacing);
  }
  return std::max(0, Q_rint(max_width));
}

int Font_LineHeight(const font_t *font, int scale) {
  if (!font)
    return CONCHAR_HEIGHT * std::max(scale, 1);
  int draw_scale = scale > 0 ? scale : 1;
  return std::max(
      1, Q_rint((float)font->virtual_line_height * (float)draw_scale *
                font_scale_boost()));
}

bool Font_IsLegacy(const font_t *font) { return font && font->kind == FONT_LEGACY; }

qhandle_t Font_LegacyHandle(const font_t *font) {
  return font ? font->legacy_handle : 0;
}
