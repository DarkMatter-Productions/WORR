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

#include "ui_rml.h"

#if UI_RML_HAS_RUNTIME

#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "client/keys.h"
#include "client/sound/sound.h"
#include "renderer/renderer.h"
#include "system/system.h"

#ifdef DotProduct
#undef DotProduct
#endif

#ifdef CrossProduct
#undef CrossProduct
#endif

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/URL.h>

#if USE_SDL3_TTF
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static bool ui_rml_core_initialized;
static Rml::Context *ui_rml_context;
static Rml::ElementDocument *ui_rml_document;
static Rml::String ui_rml_active_route;
static Rml::String ui_rml_active_document;
static int ui_rml_context_width;
static int ui_rml_context_height;
static int ui_rml_last_menu_sound = -1;
static unsigned ui_rml_last_menu_sound_msec;
static bool ui_rml_applying_cvar_bindings;
static cvar_t *ui_rml_log_missing_data_models;

static bool UI_Rml_IsMissingDataModelNotice(const Rml::String &message)
{
    return message.find("Could not locate data model") != Rml::String::npos;
}

class UI_Rml_SystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override
    {
        static const unsigned start_msec = Sys_Milliseconds();
        return (Sys_Milliseconds() - start_msec) / 1000.0;
    }

    int TranslateString(Rml::String &translated, const Rml::String &input) override
    {
        translated = input;
        return 0;
    }

    void JoinPath(Rml::String &translated_path,
                  const Rml::String &document_path,
                  const Rml::String &path) override
    {
        if (path.empty()) {
            translated_path.clear();
            return;
        }

        Rml::String normalized_path = Rml::StringUtilities::Replace(path, '\\', '/');

        if (normalized_path[0] == '/') {
            translated_path = normalized_path.substr(1);
            return;
        }

        const size_t drive_pos = normalized_path.find(':');
        const size_t slash_pos = normalized_path.find('/');
        if (drive_pos != Rml::String::npos &&
            (slash_pos == Rml::String::npos || drive_pos < slash_pos)) {
            translated_path = normalized_path;
            return;
        }

        Rml::String base = Rml::StringUtilities::Replace(document_path, '\\', '/');
        const size_t file_start = base.rfind('/');
        if (file_start != Rml::String::npos) {
            base.resize(file_start + 1);
        } else {
            base.clear();
        }

        const Rml::String url_path = Rml::StringUtilities::Replace(base + normalized_path, ':', '|');
        Rml::URL url(url_path);
        translated_path = Rml::StringUtilities::Replace(url.GetPathedFileName(), '|', ':');
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
        if (UI_Rml_IsMissingDataModelNotice(message) &&
            (!ui_rml_log_missing_data_models ||
             !ui_rml_log_missing_data_models->integer)) {
            return true;
        }

        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            Com_EPrintf("RmlUi: %s\n", message.c_str());
            return true;
        case Rml::Log::LT_WARNING:
            Com_WPrintf("RmlUi: %s\n", message.c_str());
            return true;
        default:
            Com_Printf("RmlUi: %s\n", message.c_str());
            return true;
        }
    }
};

class UI_Rml_CoreFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String &path) override
    {
        qhandle_t handle = 0;
        const int64_t len = FS_OpenFile(path.c_str(), &handle, FS_MODE_READ);

        if (len < 0) {
            return 0;
        }

        return static_cast<Rml::FileHandle>(handle);
    }

    void Close(Rml::FileHandle file) override
    {
        if (file) {
            FS_CloseFile(static_cast<qhandle_t>(file));
        }
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
    {
        const int len = FS_Read(buffer, size, static_cast<qhandle_t>(file));
        return len > 0 ? static_cast<size_t>(len) : 0;
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        return FS_Seek(static_cast<qhandle_t>(file), offset, origin) == Q_ERR_SUCCESS;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        const int64_t offset = FS_Tell(static_cast<qhandle_t>(file));
        return offset > 0 ? static_cast<size_t>(offset) : 0;
    }

    size_t Length(Rml::FileHandle file) override
    {
        const int64_t length = FS_Length(static_cast<qhandle_t>(file));
        return length > 0 ? static_cast<size_t>(length) : 0;
    }
};

struct UI_Rml_BitmapGlyph {
    char character;
    const char *rows[7];
};

static const UI_Rml_BitmapGlyph ui_rml_bitmap_glyphs[] = {
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {'C', {"01111", "10000", "10000", "10000", "10000", "10000", "01111"}},
    {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {'G', {"01111", "10000", "10000", "10111", "10001", "10001", "01111"}},
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'I', {"11111", "00100", "00100", "00100", "00100", "00100", "11111"}},
    {'J', {"00111", "00010", "00010", "00010", "10010", "10010", "01100"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
    {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'W', {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}},
    {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
    {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
    {'4', {"10010", "10010", "10010", "11111", "00010", "00010", "00010"}},
    {'5', {"11111", "10000", "10000", "11110", "00001", "00001", "11110"}},
    {'6', {"01110", "10000", "10000", "11110", "10001", "10001", "01110"}},
    {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {'9', {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}},
    {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
    {',', {"00000", "00000", "00000", "00000", "01100", "01100", "01000"}},
    {':', {"00000", "01100", "01100", "00000", "01100", "01100", "00000"}},
    {';', {"00000", "01100", "01100", "00000", "01100", "01100", "01000"}},
    {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    {'_', {"00000", "00000", "00000", "00000", "00000", "00000", "11111"}},
    {'/', {"00001", "00010", "00010", "00100", "01000", "01000", "10000"}},
    {'\\', {"10000", "01000", "01000", "00100", "00010", "00010", "00001"}},
    {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
    {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}},
    {'[', {"01110", "01000", "01000", "01000", "01000", "01000", "01110"}},
    {']', {"01110", "00010", "00010", "00010", "00010", "00010", "01110"}},
    {'+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"}},
    {'=', {"00000", "00000", "11111", "00000", "11111", "00000", "00000"}},
    {'!', {"00100", "00100", "00100", "00100", "00100", "00000", "00100"}},
    {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}},
};

static const char *const ui_rml_missing_glyph[7] = {
    "11111", "10001", "00001", "00010", "00100", "00000", "00100",
};

static unsigned char UI_Rml_NormalizeBitmapGlyph(unsigned char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return static_cast<unsigned char>(ch - ('a' - 'A'));
    }

    return ch;
}

static const char *const *UI_Rml_FindBitmapGlyph(unsigned char ch)
{
    ch = UI_Rml_NormalizeBitmapGlyph(ch);

    for (const UI_Rml_BitmapGlyph &glyph : ui_rml_bitmap_glyphs) {
        if (static_cast<unsigned char>(glyph.character) == ch) {
            return glyph.rows;
        }
    }

    return ui_rml_missing_glyph;
}

static void UI_Rml_AddBitmapQuad(Rml::Mesh &mesh,
                                 float x0,
                                 float y0,
                                 float x1,
                                 float y1,
                                 Rml::ColourbPremultiplied colour)
{
    const int index = static_cast<int>(mesh.vertices.size());

    mesh.vertices.push_back(Rml::Vertex{{x0, y0}, colour, {0.0f, 0.0f}});
    mesh.vertices.push_back(Rml::Vertex{{x1, y0}, colour, {1.0f, 0.0f}});
    mesh.vertices.push_back(Rml::Vertex{{x1, y1}, colour, {1.0f, 1.0f}});
    mesh.vertices.push_back(Rml::Vertex{{x0, y1}, colour, {0.0f, 1.0f}});

    mesh.indices.push_back(index);
    mesh.indices.push_back(index + 1);
    mesh.indices.push_back(index + 2);
    mesh.indices.push_back(index);
    mesh.indices.push_back(index + 2);
    mesh.indices.push_back(index + 3);
}

class UI_Rml_SmokeFontEngineInterface final : public Rml::FontEngineInterface {
public:
    UI_Rml_SmokeFontEngineInterface()
    {
        SetMetrics(16);
    }

    bool LoadFontFace(const Rml::String &file_name,
                      int face_index,
                      bool fallback_face,
                      Rml::Style::FontWeight weight) override
    {
        (void)file_name;
        (void)face_index;
        (void)fallback_face;
        (void)weight;
        return true;
    }

    bool LoadFontFace(Rml::Span<const Rml::byte> data,
                      int face_index,
                      const Rml::String &family,
                      Rml::Style::FontStyle style,
                      Rml::Style::FontWeight weight,
                      bool fallback_face) override
    {
        (void)data;
        (void)face_index;
        (void)family;
        (void)style;
        (void)weight;
        (void)fallback_face;
        return true;
    }

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String &family,
                                          Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight,
                                          int size) override
    {
        (void)family;
        (void)style;
        (void)weight;
        SetMetrics(size > 0 ? size : 16);
        return 1;
    }

    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle,
                                              const Rml::FontEffectList &font_effects) override
    {
        (void)handle;
        (void)font_effects;
        return 1;
    }

    const Rml::FontMetrics &GetFontMetrics(Rml::FontFaceHandle handle) override
    {
        (void)handle;
        return metrics;
    }

    int GetStringWidth(Rml::FontFaceHandle handle,
                       Rml::StringView string,
                       const Rml::TextShapingContext &text_shaping_context,
                       Rml::Character prior_character = Rml::Character::Null) override
    {
        (void)handle;
        (void)text_shaping_context;
        (void)prior_character;
        return CalculateStringWidth(string);
    }

    int GenerateString(Rml::RenderManager &render_manager,
                       Rml::FontFaceHandle face_handle,
                       Rml::FontEffectsHandle font_effects_handle,
                       Rml::StringView string,
                       Rml::Vector2f position,
                       Rml::ColourbPremultiplied colour,
                       float opacity,
                       const Rml::TextShapingContext &text_shaping_context,
                       Rml::TexturedMeshList &mesh_list) override
    {
        (void)render_manager;
        (void)face_handle;
        (void)font_effects_handle;
        (void)text_shaping_context;

        float draw_opacity = opacity;
        if (draw_opacity < 0.0f) {
            draw_opacity = 0.0f;
        } else if (draw_opacity > 1.0f) {
            draw_opacity = 1.0f;
        }

        Rml::TexturedMesh text_mesh;
        const int scale = GlyphScale();
        const int advance = GlyphAdvance();
        const float top = position.y - metrics.ascent;
        const Rml::ColourbPremultiplied draw_colour = colour * draw_opacity;
        int cursor_x = 0;
        int glyph_count = 0;

        for (char raw_ch : string) {
            unsigned char ch = static_cast<unsigned char>(raw_ch);
            if (ch == '\r' || ch == '\n' || ch == '\t') {
                ch = ' ';
            }

            if (ch == ' ') {
                cursor_x += SpaceAdvance();
                continue;
            }

            const char *const *rows = UI_Rml_FindBitmapGlyph(ch);
            for (int row = 0; row < UI_RML_BITMAP_FONT_ROWS; ++row) {
                for (int column = 0; column < UI_RML_BITMAP_FONT_COLUMNS; ++column) {
                    if (rows[row][column] != '1') {
                        continue;
                    }

                    const float x0 = position.x + static_cast<float>(cursor_x + column * scale);
                    const float y0 = top + static_cast<float>(row * scale);
                    UI_Rml_AddBitmapQuad(
                        text_mesh.mesh,
                        x0,
                        y0,
                        x0 + static_cast<float>(scale),
                        y0 + static_cast<float>(scale),
                        draw_colour);
                }
            }

            ++glyph_count;
            cursor_x += advance;
        }

        if (text_mesh.mesh) {
            mesh_list.push_back(text_mesh);
        }

        if (glyph_count > 0) {
            ++generated_strings;
            generated_glyphs += glyph_count;

            if (!reported_geometry) {
                reported_geometry = true;
                Com_Printf(
                    "RmlUi smoke font engine generated glyph geometry: strings=%d glyphs=%d size=%d.\n",
                    generated_strings,
                    generated_glyphs,
                    metrics.size);
            }
        }

        return CalculateStringWidth(string);
    }

    int GetVersion(Rml::FontFaceHandle handle) override
    {
        (void)handle;
        return 2;
    }

private:
    static constexpr int UI_RML_BITMAP_FONT_COLUMNS = 5;
    static constexpr int UI_RML_BITMAP_FONT_ROWS = 7;

    int GlyphScale() const
    {
        return max(1, metrics.size / UI_RML_BITMAP_FONT_ROWS);
    }

    int GlyphAdvance() const
    {
        return UI_RML_BITMAP_FONT_COLUMNS * GlyphScale() + max(1, GlyphScale());
    }

    int SpaceAdvance() const
    {
        return max(1, GlyphAdvance() / 2);
    }

    int CalculateStringWidth(Rml::StringView string) const
    {
        int width = 0;

        for (char raw_ch : string) {
            unsigned char ch = static_cast<unsigned char>(raw_ch);
            if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
                width += SpaceAdvance();
                continue;
            }

            width += GlyphAdvance();
        }

        return width;
    }

    void SetMetrics(int size)
    {
        metrics.size = size;
        metrics.ascent = size * 0.8f;
        metrics.descent = size * 0.2f;
        metrics.line_spacing = size * 1.25f;
        metrics.x_height = size * 0.5f;
        metrics.underline_position = size * 0.08f;
        metrics.underline_thickness = max(1, size / 12);
        metrics.has_ellipsis = false;
    }

    Rml::FontMetrics metrics = {};
    int generated_strings = 0;
    int generated_glyphs = 0;
    bool reported_geometry = false;
};

#if USE_SDL3_TTF
class UI_Rml_TtfFontEngineInterface final : public Rml::FontEngineInterface {
private:
    struct UI_Rml_TtfFace;
    struct UI_Rml_TtfSizedFace;
    struct UI_Rml_TtfFontCandidate {
        const char *path = nullptr;
        bool quake2_rerelease = false;
    };

public:
    void Initialize() override
    {
        if (ttf_initialized) {
            return;
        }

        if (!TTF_Init()) {
            Com_WPrintf("RmlUi TTF font engine could not initialize SDL3_ttf: %s\n",
                        SDL_GetError());
            return;
        }

        ttf_initialized = true;
        fallback_metrics = MetricsForSize(16);

        LoadDefaultFace("WORR Display", {
            { "fonts/RussoOne-Regular.ttf", true },
            { "fonts/Montserrat-Regular.ttf", true },
            { "fonts/NotoSansKR-Regular.otf", true },
            { "fonts/AtkinsonHyperLegible-Regular.otf", true },
            { "C:/Windows/Fonts/segoeui.ttf", false },
            { "C:/Windows/Fonts/arial.ttf", false },
        });

        LoadDefaultFace("WORR UI", {
            { "fonts/Montserrat-Regular.ttf", true },
            { "fonts/NotoSansKR-Regular.otf", true },
            { "fonts/NotoSansJP-Regular.otf", true },
            { "fonts/AtkinsonHyperLegible-Regular.otf", true },
            { "fonts/RussoOne-Regular.ttf", true },
            { "C:/Windows/Fonts/segoeui.ttf", false },
            { "C:/Windows/Fonts/arial.ttf", false },
            { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", false },
            { "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", false },
            { "/System/Library/Fonts/Supplemental/Arial.ttf", false },
        });

        LoadDefaultFace("WORR Mono", {
            { "fonts/RobotoMono-Regular.ttf", true },
            { "fonts/RobotoMono-Medium.ttf", true },
            { "fonts/RobotoMono-Bold.ttf", true },
            { "C:/Windows/Fonts/consola.ttf", false },
            { "C:/Windows/Fonts/cour.ttf", false },
            { "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", false },
            { "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf", false },
            { "/System/Library/Fonts/Menlo.ttc", false },
        });

        if (faces.empty()) {
            Com_WPrintf("RmlUi TTF font engine found no usable UI fonts; menu text will be unavailable.\n");
        }
    }

    void Shutdown() override
    {
        ReleaseFontResources();
        faces.clear();

        if (ttf_initialized) {
            TTF_Quit();
            ttf_initialized = false;
        }
    }

    bool LoadFontFace(const Rml::String &file_name,
                      int face_index,
                      bool fallback_face,
                      Rml::Style::FontWeight weight) override
    {
        (void)face_index;

        std::vector<Rml::byte> data;
        if (!ReadFontFile(file_name, data)) {
            return false;
        }

        const Rml::String family = FontFamilyFromPath(file_name);
        return RegisterFontFace(std::move(data),
                                family.empty() ? Rml::String("WORR UI") : family,
                                Rml::Style::FontStyle::Normal,
                                weight,
                                fallback_face,
                                file_name);
    }

    bool LoadFontFace(Rml::Span<const Rml::byte> data,
                      int face_index,
                      const Rml::String &family,
                      Rml::Style::FontStyle style,
                      Rml::Style::FontWeight weight,
                      bool fallback_face) override
    {
        (void)face_index;

        if (data.empty()) {
            return false;
        }

        std::vector<Rml::byte> copy(data.data(), data.data() + data.size());
        return RegisterFontFace(std::move(copy),
                                family.empty() ? Rml::String("WORR UI") : family,
                                style,
                                weight,
                                fallback_face,
                                "memory");
    }

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String &family,
                                          Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight,
                                          int size) override
    {
        if (!ttf_initialized || faces.empty()) {
            return 0;
        }

        const size_t face_index = ResolveFaceIndex(family, style, weight);
        UI_Rml_TtfSizedFace *face = OpenSizedFace(face_index, size);
        return reinterpret_cast<Rml::FontFaceHandle>(face);
    }

    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle,
                                              const Rml::FontEffectList &font_effects) override
    {
        (void)handle;
        (void)font_effects;
        return 1;
    }

    const Rml::FontMetrics &GetFontMetrics(Rml::FontFaceHandle handle) override
    {
        const UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(handle);
        return face ? face->metrics : fallback_metrics;
    }

    int GetStringWidth(Rml::FontFaceHandle handle,
                       Rml::StringView string,
                       const Rml::TextShapingContext &text_shaping_context,
                       Rml::Character prior_character = Rml::Character::Null) override
    {
        (void)text_shaping_context;
        (void)prior_character;

        UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(handle);
        return face ? MeasureString(face, string) : 0;
    }

    int GenerateString(Rml::RenderManager &render_manager,
                       Rml::FontFaceHandle face_handle,
                       Rml::FontEffectsHandle font_effects_handle,
                       Rml::StringView string,
                       Rml::Vector2f position,
                       Rml::ColourbPremultiplied colour,
                       float opacity,
                       const Rml::TextShapingContext &text_shaping_context,
                       Rml::TexturedMeshList &mesh_list) override
    {
        (void)font_effects_handle;
        (void)text_shaping_context;

        UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(face_handle);
        if (!face || string.empty()) {
            return 0;
        }

        const std::string text = NormalizeText(string);
        const int measured_width = MeasureString(face, text);
        if (measured_width <= 0) {
            return 0;
        }

        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *surface =
            TTF_RenderText_Blended(face->font, text.c_str(), text.size(), white);
        if (!surface) {
            return measured_width;
        }

        if (surface->format != SDL_PIXELFORMAT_ARGB8888) {
            SDL_Surface *converted =
                SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
            SDL_DestroySurface(surface);
            surface = converted;
        }

        if (!surface || surface->w <= 0 || surface->h <= 0 || !surface->pixels ||
            surface->pitch < 0 ||
            static_cast<size_t>(surface->pitch) <
                static_cast<size_t>(surface->w) * sizeof(uint32_t)) {
            if (surface) {
                SDL_DestroySurface(surface);
            }
            return measured_width;
        }

        const size_t width = static_cast<size_t>(surface->w);
        const size_t height = static_cast<size_t>(surface->h);
        if (height > (std::numeric_limits<size_t>::max)() / width / 4) {
            SDL_DestroySurface(surface);
            return measured_width;
        }

        auto pixels = std::make_shared<std::vector<Rml::byte>>(width * height * 4);
        for (size_t y = 0; y < height; ++y) {
            const auto *src = reinterpret_cast<const uint32_t *>(
                static_cast<const uint8_t *>(surface->pixels) + y * surface->pitch);
            Rml::byte *dst = pixels->data() + y * width * 4;

            for (size_t x = 0; x < width; ++x) {
                const Rml::byte alpha =
                    static_cast<Rml::byte>((src[x] >> 24) & 0xffu);
                dst[x * 4 + 0] = alpha;
                dst[x * 4 + 1] = alpha;
                dst[x * 4 + 2] = alpha;
                dst[x * 4 + 3] = alpha;
            }
        }

        const Rml::Vector2i texture_dimensions(surface->w, surface->h);
        SDL_DestroySurface(surface);

        Rml::CallbackTexture callback_texture =
            render_manager.MakeCallbackTexture(
                [pixels, texture_dimensions](
                    const Rml::CallbackTextureInterface &texture_interface) {
                    return texture_interface.GenerateTexture(
                        Rml::Span<const Rml::byte>(pixels->data(), pixels->size()),
                        texture_dimensions);
                });

        if (!callback_texture) {
            return measured_width;
        }

        Rml::TexturedMesh text_mesh;
        text_mesh.texture = callback_texture;

        float draw_opacity = opacity;
        if (draw_opacity < 0.0f) {
            draw_opacity = 0.0f;
        } else if (draw_opacity > 1.0f) {
            draw_opacity = 1.0f;
        }

        const float x0 = position.x;
        const float y0 = position.y - face->metrics.ascent;
        const float x1 = x0 + static_cast<float>(texture_dimensions.x);
        const float y1 = y0 + static_cast<float>(texture_dimensions.y);
        AddTexturedQuad(text_mesh.mesh, x0, y0, x1, y1, colour * draw_opacity);

        generated_textures.push_back(std::move(callback_texture));
        mesh_list.push_back(std::move(text_mesh));

        ++generated_strings;
        if (!reported_geometry) {
            reported_geometry = true;
            Com_Printf("RmlUi TTF font engine generated text texture: strings=%d size=%d source='%s'.\n",
                       generated_strings,
                       face->metrics.size,
                       faces[face->face_index].source.c_str());
        }

        return measured_width;
    }

    int GetVersion(Rml::FontFaceHandle handle) override
    {
        const UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(handle);
        return face ? face->version : font_version;
    }

    void ReleaseFontResources() override
    {
        generated_textures.clear();
        sized_face_cache.clear();
        sized_faces.clear();
        ++font_version;
        reported_geometry = false;
        generated_strings = 0;
    }

private:
    struct UI_Rml_TtfFace {
        Rml::String family;
        Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal;
        Rml::Style::FontWeight weight = Rml::Style::FontWeight::Normal;
        bool fallback = false;
        std::vector<Rml::byte> data;
        Rml::String source;
    };

    struct UI_Rml_TtfSizedFace {
        ~UI_Rml_TtfSizedFace()
        {
            if (font) {
                TTF_CloseFont(font);
                font = nullptr;
            }
        }

        size_t face_index = 0;
        int size = 16;
        int version = 1;
        TTF_Font *font = nullptr;
        Rml::FontMetrics metrics = {};
    };

    static Rml::FontMetrics MetricsForSize(int size)
    {
        if (size <= 0) {
            size = 16;
        }

        Rml::FontMetrics metrics = {};
        metrics.size = size;
        metrics.ascent = size * 0.8f;
        metrics.descent = size * 0.2f;
        metrics.line_spacing = size * 1.25f;
        metrics.x_height = size * 0.5f;
        metrics.underline_position = size * 0.08f;
        metrics.underline_thickness = max(1, size / 12);
        metrics.has_ellipsis = true;
        return metrics;
    }

    static Rml::FontMetrics MetricsForFont(TTF_Font *font, int size)
    {
        Rml::FontMetrics metrics = MetricsForSize(size);

        const int ascent = TTF_GetFontAscent(font);
        const int descent = max(0, -TTF_GetFontDescent(font));
        const int height = TTF_GetFontHeight(font);
        const int line_skip = TTF_GetFontLineSkip(font);
        int x_width = 0;
        int x_height = 0;

        metrics.ascent = static_cast<float>(max(1, ascent));
        metrics.descent = static_cast<float>(descent);
        metrics.line_spacing = static_cast<float>(
            max(max(1, line_skip), max(height, ascent + descent)));

        if (TTF_GetStringSize(font, "x", 1, &x_width, &x_height) && x_height > 0) {
            metrics.x_height = static_cast<float>(x_height);
        }

        return metrics;
    }

    static std::string NormalizeFamily(const Rml::String &family)
    {
        std::string key;
        key.reserve(family.size());

        for (char raw_ch : family) {
            unsigned char ch = static_cast<unsigned char>(raw_ch);
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<unsigned char>(ch - 'A' + 'a');
            }
            key.push_back(static_cast<char>(ch));
        }

        return key;
    }

    static std::string NormalizeText(Rml::StringView string)
    {
        std::string text;
        text.reserve(string.size());

        for (char ch : string) {
            if (ch == '\r' || ch == '\n' || ch == '\t') {
                text.push_back(' ');
            } else {
                text.push_back(ch);
            }
        }

        return text;
    }

    static Rml::String FontFamilyFromPath(const Rml::String &file_name)
    {
        Rml::String normalized = Rml::StringUtilities::Replace(file_name, '\\', '/');
        const size_t start = normalized.find_last_of('/');
        const size_t end = normalized.find_last_of('.');
        const size_t stem_start = start == Rml::String::npos ? 0 : start + 1;

        if (end == Rml::String::npos || end <= stem_start) {
            return normalized.substr(stem_start);
        }

        return normalized.substr(stem_start, end - stem_start);
    }

    static bool IsAbsoluteDiskPath(const Rml::String &path)
    {
        if (path.size() >= 2 && path[1] == ':') {
            return true;
        }

        return !path.empty() && (path[0] == '/' || path[0] == '\\');
    }

    static bool ReadFontFile(const Rml::String &path, std::vector<Rml::byte> &data)
    {
        data.clear();

        if (path.empty()) {
            return false;
        }

        if (!IsAbsoluteDiskPath(path)) {
            void *loaded = nullptr;
            const int len = FS_LoadFile(path.c_str(), &loaded);
            if (len > 0 && loaded) {
                const auto *begin = static_cast<const Rml::byte *>(loaded);
                data.assign(begin, begin + len);
                FS_FreeFile(loaded);
                return true;
            }
            if (loaded) {
                FS_FreeFile(loaded);
            }
        }

        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file) {
            return false;
        }

        file.seekg(0, std::ios::end);
        const std::streamoff len = file.tellg();
        if (len <= 0 ||
            static_cast<unsigned long long>(len) >
                (std::numeric_limits<size_t>::max)()) {
            return false;
        }

        file.seekg(0, std::ios::beg);
        data.resize(static_cast<size_t>(len));
        return static_cast<bool>(
            file.read(reinterpret_cast<char *>(data.data()), len));
    }

    static void AddTexturedQuad(Rml::Mesh &mesh,
                                float x0,
                                float y0,
                                float x1,
                                float y1,
                                Rml::ColourbPremultiplied colour)
    {
        const int index = static_cast<int>(mesh.vertices.size());

        mesh.vertices.push_back(Rml::Vertex{{x0, y0}, colour, {0.0f, 0.0f}});
        mesh.vertices.push_back(Rml::Vertex{{x1, y0}, colour, {1.0f, 0.0f}});
        mesh.vertices.push_back(Rml::Vertex{{x1, y1}, colour, {1.0f, 1.0f}});
        mesh.vertices.push_back(Rml::Vertex{{x0, y1}, colour, {0.0f, 1.0f}});

        mesh.indices.push_back(index);
        mesh.indices.push_back(index + 1);
        mesh.indices.push_back(index + 2);
        mesh.indices.push_back(index);
        mesh.indices.push_back(index + 2);
        mesh.indices.push_back(index + 3);
    }

    bool ValidateFontBytes(const std::vector<Rml::byte> &data) const
    {
        if (!ttf_initialized || data.empty()) {
            return false;
        }

        SDL_IOStream *io = SDL_IOFromConstMem(data.data(), data.size());
        if (!io) {
            return false;
        }

        TTF_Font *font = TTF_OpenFontIO(io, true, 16.0f);
        if (!font) {
            return false;
        }

        TTF_CloseFont(font);
        return true;
    }

    bool RegisterFontFace(std::vector<Rml::byte> data,
                          const Rml::String &family,
                          Rml::Style::FontStyle style,
                          Rml::Style::FontWeight weight,
                          bool fallback_face,
                          const Rml::String &source)
    {
        if (!ValidateFontBytes(data)) {
            return false;
        }

        UI_Rml_TtfFace face = {};
        face.family = family;
        face.style = style;
        face.weight = weight;
        face.fallback = fallback_face;
        face.data = std::move(data);
        face.source = source;
        faces.push_back(std::move(face));
        return true;
    }

    bool LoadDefaultFace(const Rml::String &family,
                         std::initializer_list<UI_Rml_TtfFontCandidate> candidates)
    {
        for (const UI_Rml_TtfFontCandidate &candidate : candidates) {
            std::vector<Rml::byte> data;
            if (!candidate.path || !ReadFontFile(candidate.path, data)) {
                continue;
            }

            Rml::String source = candidate.path;
            if (candidate.quake2_rerelease) {
                source = Rml::String("Quake II Rerelease: ") + candidate.path;
            }

            if (!RegisterFontFace(std::move(data),
                                  family,
                                  Rml::Style::FontStyle::Normal,
                                  Rml::Style::FontWeight::Normal,
                                  false,
                                  source)) {
                continue;
            }

            if (candidate.quake2_rerelease) {
                Com_Printf("RmlUi TTF font face '%s' loaded from Quake II Rerelease font %s.\n",
                           family.c_str(),
                           candidate.path);
            } else {
                Com_WPrintf("RmlUi TTF font face '%s' fell back to non-rerelease font %s.\n",
                            family.c_str(),
                            candidate.path);
            }
            return true;
        }

        Com_WPrintf("RmlUi TTF font face '%s' could not be loaded from Quake II Rerelease or fallback fonts.\n",
                    family.c_str());
        return false;
    }

    size_t ResolveFaceIndex(const Rml::String &family,
                            Rml::Style::FontStyle style,
                            Rml::Style::FontWeight weight) const
    {
        const std::string requested = NormalizeFamily(family);
        const bool wants_mono =
            requested.find("mono") != std::string::npos ||
            requested.find("console") != std::string::npos ||
            requested.find("courier") != std::string::npos;

        size_t fallback_index = 0;
        for (size_t i = 0; i < faces.size(); ++i) {
            const UI_Rml_TtfFace &face = faces[i];
            if (face.fallback) {
                fallback_index = i;
            }

            if (face.style == style && face.weight == weight &&
                NormalizeFamily(face.family) == requested) {
                return i;
            }
        }

        for (size_t i = 0; i < faces.size(); ++i) {
            const std::string available = NormalizeFamily(faces[i].family);
            if ((wants_mono && available.find("mono") != std::string::npos) ||
                (!wants_mono && available == "worr ui")) {
                return i;
            }
        }

        return fallback_index;
    }

    UI_Rml_TtfSizedFace *OpenSizedFace(size_t face_index, int size)
    {
        if (face_index >= faces.size()) {
            return nullptr;
        }

        size = max(1, min(size, 256));
        const std::string key =
            std::to_string(face_index) + ":" + std::to_string(size);

        auto found = sized_face_cache.find(key);
        if (found != sized_face_cache.end()) {
            return found->second;
        }

        const UI_Rml_TtfFace &face = faces[face_index];
        SDL_IOStream *io = SDL_IOFromConstMem(face.data.data(), face.data.size());
        if (!io) {
            return nullptr;
        }

        TTF_Font *font = TTF_OpenFontIO(io, true, static_cast<float>(size));
        if (!font) {
            return nullptr;
        }

        TTF_SetFontHinting(font, TTF_HINTING_LIGHT);

        auto sized_face = std::make_unique<UI_Rml_TtfSizedFace>();
        sized_face->face_index = face_index;
        sized_face->size = size;
        sized_face->version = font_version;
        sized_face->font = font;
        sized_face->metrics = MetricsForFont(font, size);

        UI_Rml_TtfSizedFace *ptr = sized_face.get();
        sized_faces.push_back(std::move(sized_face));
        sized_face_cache.emplace(key, ptr);
        return ptr;
    }

    UI_Rml_TtfSizedFace *SizedFaceFromHandle(Rml::FontFaceHandle handle)
    {
        return reinterpret_cast<UI_Rml_TtfSizedFace *>(handle);
    }

    const UI_Rml_TtfSizedFace *SizedFaceFromHandle(Rml::FontFaceHandle handle) const
    {
        return reinterpret_cast<const UI_Rml_TtfSizedFace *>(handle);
    }

    int MeasureString(UI_Rml_TtfSizedFace *face, Rml::StringView string) const
    {
        return MeasureString(face, NormalizeText(string));
    }

    int MeasureString(UI_Rml_TtfSizedFace *face, const std::string &text) const
    {
        if (!face || !face->font || text.empty()) {
            return 0;
        }

        int width = 0;
        int height = 0;
        if (TTF_GetStringSize(face->font, text.c_str(), text.size(), &width, &height)) {
            return max(0, width);
        }

        return static_cast<int>(text.size()) * max(1, face->size / 2);
    }

    bool ttf_initialized = false;
    int font_version = 1;
    Rml::FontMetrics fallback_metrics = {};
    std::vector<UI_Rml_TtfFace> faces;
    std::vector<std::unique_ptr<UI_Rml_TtfSizedFace>> sized_faces;
    std::vector<Rml::CallbackTexture> generated_textures;
    std::unordered_map<std::string, UI_Rml_TtfSizedFace *> sized_face_cache;
    int generated_strings = 0;
    bool reported_geometry = false;
};
#endif

static UI_Rml_SystemInterface ui_rml_system_interface;
static UI_Rml_CoreFileInterface ui_rml_file_interface;
#if USE_SDL3_TTF
static UI_Rml_TtfFontEngineInterface ui_rml_font_interface;
#else
static UI_Rml_SmokeFontEngineInterface ui_rml_font_interface;
#endif
static constexpr float UI_RML_REFERENCE_WIDTH = 960.0f;
static constexpr float UI_RML_REFERENCE_HEIGHT = 720.0f;

static Rml::Element *UI_Rml_FindCommandElement(Rml::Element *element)
{
    for (Rml::Element *current = element; current; current = current->GetParentNode()) {
        if (!current->GetAttribute<Rml::String>("data-route-target", "").empty() ||
            !current->GetAttribute<Rml::String>("data-route", "").empty() ||
            !current->GetAttribute<Rml::String>("data-command-cvar", "").empty() ||
            !current->GetAttribute<Rml::String>("data-command", "").empty()) {
            return current;
        }
    }

    return nullptr;
}

static bool UI_Rml_QueueCommand(const char *command)
{
    if (!command || !command[0]) {
        return false;
    }

    Cbuf_AddText(&cmd_buffer, command);
    Cbuf_AddText(&cmd_buffer, "\n");
    return true;
}

static bool UI_Rml_QueueRouteOpen(const Rml::String &route_id)
{
    if (route_id.empty() || !UI_Rml_DocumentForRoute(route_id.c_str())) {
        return false;
    }

    return UI_Rml_QueueCommand(va("ui_rml_runtime_open %s", route_id.c_str()));
}

static bool UI_Rml_QueueRoutePopup(const Rml::String &route_id)
{
    if (route_id.empty() || !UI_Rml_DocumentForRoute(route_id.c_str())) {
        return false;
    }

    return UI_Rml_QueueCommand(va("ui_rml_runtime_popup %s", route_id.c_str()));
}

static Rml::String UI_Rml_RouteTargetForElement(Rml::Element *element,
                                                const Rml::String &command)
{
    Rml::String route_target =
        element->GetAttribute<Rml::String>("data-route-target", "");

    if (!route_target.empty()) {
        return route_target;
    }

    if (command == "ui.open_route") {
        return element->GetAttribute<Rml::String>("data-route", "");
    }

    return "";
}

static Rml::String UI_Rml_CommandFirstArgumentAfterToken(const Rml::String &command,
                                                        const char *token)
{
    size_t position = strlen(token);

    while (position < command.size() &&
           (command[position] == ' ' || command[position] == '\t')) {
        position++;
    }

    if (position >= command.size() || command[position] == ';') {
        return "";
    }

    size_t end = position;
    while (end < command.size() &&
           command[end] != ' ' &&
           command[end] != '\t' &&
           command[end] != ';') {
        end++;
    }

    return command.substr(position, end - position);
}

static bool UI_Rml_CommandStartsWithToken(const Rml::String &command,
                                          const char *token)
{
    const size_t token_length = strlen(token);

    if (command.size() < token_length ||
        command.compare(0, token_length, token) != 0) {
        return false;
    }

    if (command.size() == token_length) {
        return true;
    }

    return command[token_length] == ';' ||
           command[token_length] == ' ' ||
           command[token_length] == '\t';
}

static const char *UI_Rml_CommandTailAfterToken(const Rml::String &command,
                                                const char *token)
{
    size_t position = strlen(token);

    while (position < command.size() &&
           (command[position] == ' ' || command[position] == '\t')) {
        position++;
    }

    if (position >= command.size() || command[position] != ';') {
        return nullptr;
    }

    position++;
    while (position < command.size() &&
           (command[position] == ' ' || command[position] == '\t')) {
        position++;
    }

    if (position >= command.size()) {
        return nullptr;
    }

    return command.c_str() + position;
}

static int UI_Rml_MenuSoundForName(const Rml::String &sound_name)
{
    if (sound_name.empty() ||
        !Q_stricmp(sound_name.c_str(), "none") ||
        !Q_stricmp(sound_name.c_str(), "silent")) {
        return -1;
    }

    if (!Q_stricmp(sound_name.c_str(), "open") ||
        !Q_stricmp(sound_name.c_str(), "in") ||
        !Q_stricmp(sound_name.c_str(), "activate")) {
        return UI_FEEDBACK_OPEN;
    }

    if (!Q_stricmp(sound_name.c_str(), "move") ||
        !Q_stricmp(sound_name.c_str(), "change")) {
        return UI_FEEDBACK_MOVE;
    }

    if (!Q_stricmp(sound_name.c_str(), "back") ||
        !Q_stricmp(sound_name.c_str(), "close") ||
        !Q_stricmp(sound_name.c_str(), "cancel") ||
        !Q_stricmp(sound_name.c_str(), "out")) {
        return UI_FEEDBACK_CLOSE;
    }

    if (!Q_stricmp(sound_name.c_str(), "alert") ||
        !Q_stricmp(sound_name.c_str(), "beep") ||
        !Q_stricmp(sound_name.c_str(), "confirm")) {
        return UI_FEEDBACK_ALERT;
    }

    return UI_FEEDBACK_OPEN;
}

static void UI_Rml_PlayMenuFeedbackSound(uiFeedbackSound_t sound)
{
    const unsigned now = Sys_Milliseconds();

    if (ui_rml_last_menu_sound == sound &&
        now - ui_rml_last_menu_sound_msec < 75) {
        return;
    }

    ui_rml_last_menu_sound = sound;
    ui_rml_last_menu_sound_msec = now;
    UI_StartFeedbackSound(sound);
}

static void UI_Rml_PlayElementMenuSound(Rml::Element *element,
                                        const Rml::String &command,
                                        const Rml::String &route_target)
{
    const Rml::String explicit_sound =
        element->GetAttribute<Rml::String>("data-menu-sound", "");
    int sound = -1;

    if (!explicit_sound.empty()) {
        sound = UI_Rml_MenuSoundForName(explicit_sound);
    } else if (command == "ui.close" ||
               command == "ui.back" ||
               UI_Rml_CommandStartsWithToken(command, "popmenu")) {
        sound = UI_FEEDBACK_CLOSE;
    } else if (command == "ui.popup" ||
               UI_Rml_CommandStartsWithToken(command, "pushpopup")) {
        sound = UI_FEEDBACK_ALERT;
    } else if (!route_target.empty() ||
               command == "ui.open_route" ||
               UI_Rml_CommandStartsWithToken(command, "pushmenu")) {
        sound = UI_FEEDBACK_OPEN;
    } else if (!command.empty()) {
        sound = UI_FEEDBACK_OPEN;
    }

    if (sound >= 0) {
        UI_Rml_PlayMenuFeedbackSound(static_cast<uiFeedbackSound_t>(sound));
    }
}

static Rml::Element *UI_Rml_DocumentBody(Rml::ElementDocument *document)
{
    if (!document) {
        return nullptr;
    }

    Rml::Element *body = document->QuerySelector("body");
    if (body) {
        return body;
    }

    return document->GetFirstChild();
}

static bool UI_Rml_ElementBoolAttribute(Rml::Element *element,
                                        const char *name)
{
    if (!element || !element->HasAttribute(name)) {
        return false;
    }

    const Rml::String value = element->GetAttribute<Rml::String>(name, "");
    if (value.empty() ||
        !Q_stricmp(value.c_str(), "1") ||
        !Q_stricmp(value.c_str(), "true") ||
        !Q_stricmp(value.c_str(), "yes") ||
        !Q_stricmp(value.c_str(), "on")) {
        return true;
    }

    return false;
}

static int UI_Rml_ElementIntAttribute(Rml::Element *element,
                                      const char *name,
                                      int fallback)
{
    if (!element || !element->HasAttribute(name)) {
        return fallback;
    }

    const Rml::String value = element->GetAttribute<Rml::String>(name, "");
    if (value.empty()) {
        return fallback;
    }

    return static_cast<int>(strtol(value.c_str(), nullptr, 10));
}

static Rml::String UI_Rml_EscapeTextForInnerRml(const char *text)
{
    Rml::String escaped;

    if (!text) {
        return escaped;
    }

    for (const char *cursor = text; *cursor; cursor++) {
        switch (*cursor) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        default:
            escaped += *cursor;
            break;
        }
    }

    return escaped;
}

static void UI_Rml_SetElementInnerText(Rml::Element *element,
                                       const char *text)
{
    if (!element) {
        return;
    }

    const Rml::String escaped = UI_Rml_EscapeTextForInnerRml(text);
    if (element->GetInnerRML() != escaped) {
        element->SetInnerRML(escaped);
    }
}

static Rml::String UI_Rml_FormatCvarDisplayText(cvar_t *var)
{
    Rml::String text = (var && var->string) ? var->string : "";
    const bool numeric =
        text.find_first_not_of("+-0123456789.") == Rml::String::npos &&
        text.find('.') != Rml::String::npos;

    if (!numeric) {
        return text;
    }

    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }

    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }

    return text.empty() ? "0" : text;
}

static bool UI_Rml_CvarHasDisplayText(cvar_t *var)
{
    return var && var->string && var->string[0] != '\0';
}

static Rml::String UI_Rml_TrimString(Rml::String value)
{
    size_t start = 0;
    while (start < value.size() &&
           static_cast<unsigned char>(value[start]) <= ' ') {
        start++;
    }

    size_t end = value.size();
    while (end > start &&
           static_cast<unsigned char>(value[end - 1]) <= ' ') {
        end--;
    }

    return value.substr(start, end - start);
}

static bool UI_Rml_ParseDouble(const Rml::String &value, double *out)
{
    if (value.empty()) {
        return false;
    }

    char *end = nullptr;
    const double parsed = strtod(value.c_str(), &end);
    if (!end || *end != '\0') {
        return false;
    }

    if (out) {
        *out = parsed;
    }
    return true;
}

static bool UI_Rml_CvarStringTruthy(const char *value)
{
    if (!value || !value[0]) {
        return false;
    }

    if (!Q_stricmp(value, "0") ||
        !Q_stricmp(value, "false") ||
        !Q_stricmp(value, "no") ||
        !Q_stricmp(value, "off")) {
        return false;
    }

    return true;
}

static bool UI_Rml_ConditionTermMatches(Rml::String term)
{
    term = UI_Rml_TrimString(term);
    if (term.empty()) {
        return true;
    }

    bool negate = false;
    if (term[0] == '!') {
        negate = true;
        term = UI_Rml_TrimString(term.substr(1));
    }

    size_t operator_pos = term.find("!=");
    bool not_equal = operator_pos != Rml::String::npos;
    if (operator_pos == Rml::String::npos) {
        operator_pos = term.find('=');
    }

    bool matches = false;

    if (operator_pos == Rml::String::npos) {
        cvar_t *var = Cvar_FindVar(term.c_str());
        matches = var && UI_Rml_CvarStringTruthy(var->string);
    } else {
        const size_t operator_len = not_equal ? 2 : 1;
        const Rml::String name = UI_Rml_TrimString(term.substr(0, operator_pos));
        const Rml::String expected =
            UI_Rml_TrimString(term.substr(operator_pos + operator_len));
        const char *actual_text = Cvar_VariableString(name.c_str());
        double actual_number = 0.0;
        double expected_number = 0.0;

        if (UI_Rml_ParseDouble(actual_text, &actual_number) &&
            UI_Rml_ParseDouble(expected, &expected_number)) {
            matches = fabs(actual_number - expected_number) < 0.000001;
        } else {
            matches = !Q_stricmp(actual_text, expected.c_str());
        }

        if (not_equal) {
            matches = !matches;
        }
    }

    return negate ? !matches : matches;
}

static bool UI_Rml_ConditionExpressionMatches(const Rml::String &expression)
{
    if (expression.empty()) {
        return true;
    }

    size_t start = 0;
    while (start <= expression.size()) {
        const size_t end = expression.find(';', start);
        const Rml::String term =
            end == Rml::String::npos
                ? expression.substr(start)
                : expression.substr(start, end - start);

        if (!UI_Rml_ConditionTermMatches(term)) {
            return false;
        }

        if (end == Rml::String::npos) {
            break;
        }
        start = end + 1;
    }

    return true;
}

static Rml::String UI_Rml_ElementCondition(Rml::Element *element,
                                           const char *first,
                                           const char *second)
{
    if (!element) {
        return "";
    }

    Rml::String value = element->GetAttribute<Rml::String>(first, "");
    if (value.empty() && second) {
        value = element->GetAttribute<Rml::String>(second, "");
    }
    return value;
}

static void UI_Rml_ApplyElementConditions(Rml::Element *element)
{
    if (!element) {
        return;
    }

    bool visible = true;
    const Rml::String visible_if =
        UI_Rml_ElementCondition(element, "data-visible-if", nullptr);
    const Rml::String show_if =
        UI_Rml_ElementCondition(element, "data-show-if", nullptr);
    const bool has_visible_condition =
        !visible_if.empty() || !show_if.empty();
    if (!visible_if.empty()) {
        visible = visible && UI_Rml_ConditionExpressionMatches(visible_if);
    }
    if (!show_if.empty()) {
        visible = visible && UI_Rml_ConditionExpressionMatches(show_if);
    }

    if (has_visible_condition) {
        if (visible) {
            element->RemoveProperty("display");
        } else {
            element->SetProperty("display", "none");
        }
    }

    const Rml::String enable_if =
        UI_Rml_ElementCondition(element, "data-enable-if", "data-enabled-if");
    if (!enable_if.empty()) {
        const bool enabled = UI_Rml_ConditionExpressionMatches(enable_if);
        if (enabled) {
            element->RemoveAttribute("disabled");
        } else {
            element->SetAttribute("disabled", "");
        }
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_ApplyElementConditions(element->GetChild(child_index));
    }
}

static bool UI_Rml_ElementIsCheckbox(Rml::Element *element)
{
    if (!element || Q_stricmp(element->GetTagName().c_str(), "input")) {
        return false;
    }

    const Rml::String type = element->GetAttribute<Rml::String>("type", "");
    return !Q_stricmp(type.c_str(), "checkbox");
}

static bool UI_Rml_CvarValueToChecked(Rml::Element *element, cvar_t *var)
{
    const int bit = UI_Rml_ElementIntAttribute(element, "data-bit", -1);
    const bool negate = UI_Rml_ElementBoolAttribute(element, "data-negate");
    bool checked = false;

    if (var) {
        if (bit >= 0 && bit < 31) {
            checked = (var->integer & (1 << bit)) != 0;
        } else {
            checked = var->integer != 0;
        }
    }

    return negate ? !checked : checked;
}

static void UI_Rml_SetCheckboxValue(Rml::Element *element, bool checked)
{
    if (!element) {
        return;
    }

    if (checked) {
        if (!element->HasAttribute("checked")) {
            element->SetAttribute("checked", "");
        }
    } else if (element->HasAttribute("checked")) {
        element->RemoveAttribute("checked");
    }
}

static void UI_Rml_ApplyCvarToControl(Rml::Element *element, cvar_t *var)
{
    if (!element || !var) {
        return;
    }

    auto *control =
        rmlui_dynamic_cast<Rml::ElementFormControl *>(element);
    if (!control) {
        return;
    }

    if (UI_Rml_ElementIsCheckbox(element)) {
        UI_Rml_SetCheckboxValue(element, UI_Rml_CvarValueToChecked(element, var));
        return;
    }

    control->SetValue(var->string ? var->string : "");
}

static void UI_Rml_ApplyCvarToProgress(Rml::Element *element, cvar_t *var)
{
    if (!element || !var ||
        Q_stricmp(element->GetTagName().c_str(), "progress")) {
        return;
    }

    element->SetAttribute("value", var->string ? var->string : "0");
}

static void UI_Rml_ApplyCvarToText(Rml::Element *element, cvar_t *var)
{
    if (!element || !var) {
        return;
    }

    const Rml::String text = UI_Rml_FormatCvarDisplayText(var);
    UI_Rml_SetElementInnerText(element, text.c_str());
}

static void UI_Rml_ApplyCvarToMeter(Rml::Element *element, cvar_t *var)
{
    if (!element || !var) {
        return;
    }

    double value = 0.0;
    double minimum = 0.0;
    double maximum = 1.0;

    if (!UI_Rml_ParseDouble(var->string ? var->string : "", &value) ||
        !UI_Rml_ParseDouble(
            element->GetAttribute<Rml::String>("data-meter-min", "0"),
            &minimum) ||
        !UI_Rml_ParseDouble(
            element->GetAttribute<Rml::String>("data-meter-max", "1"),
            &maximum) ||
        maximum <= minimum) {
        element->SetProperty("width", "0%");
        return;
    }

    double ratio = (value - minimum) / (maximum - minimum);
    if (ratio < 0.0) {
        ratio = 0.0;
    } else if (ratio > 1.0) {
        ratio = 1.0;
    }
    element->SetProperty("width", va("%.3f%%", ratio * 100.0));
}

static Rml::String UI_Rml_DataBindCvarName(Rml::Element *element)
{
    if (!element) {
        return "";
    }

    const Rml::String data_bind =
        element->GetAttribute<Rml::String>("data-bind", "");
    static const char prefix[] = "cvars.";
    const size_t prefix_length = sizeof(prefix) - 1;

    if (data_bind.size() <= prefix_length ||
        data_bind.compare(0, prefix_length, prefix)) {
        return "";
    }

    return data_bind.substr(prefix_length);
}

static void UI_Rml_RefreshCvarBindings(Rml::Element *element,
                                       const Rml::String *only_cvar,
                                       bool include_controls)
{
    if (!element) {
        return;
    }

    const Rml::String data_cvar =
        element->GetAttribute<Rml::String>("data-cvar", "");
    if (!data_cvar.empty() &&
        (!only_cvar || data_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(data_cvar.c_str())) {
            if (include_controls) {
                UI_Rml_ApplyCvarToControl(element, var);
            }
            UI_Rml_ApplyCvarToProgress(element, var);
        }
    }

    const Rml::String meter_cvar =
        element->GetAttribute<Rml::String>("data-meter-cvar", "");
    if (!meter_cvar.empty() &&
        (!only_cvar || meter_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(meter_cvar.c_str())) {
            UI_Rml_ApplyCvarToMeter(element, var);
        }
    }

    const Rml::String bind_cvar =
        element->GetAttribute<Rml::String>("data-bind-cvar", "");
    if (!bind_cvar.empty() &&
        (!only_cvar || bind_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(bind_cvar.c_str())) {
            if (UI_Rml_CvarHasDisplayText(var)) {
                UI_Rml_ApplyCvarToText(element, var);
            }
        }
    }

    const Rml::String label_cvar =
        element->GetAttribute<Rml::String>("data-label-cvar", "");
    if (!label_cvar.empty() &&
        (!only_cvar || label_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(label_cvar.c_str())) {
            if (UI_Rml_CvarHasDisplayText(var)) {
                UI_Rml_ApplyCvarToText(element, var);
            }
        }
    }

    const Rml::String data_bind_cvar = UI_Rml_DataBindCvarName(element);
    if (!data_bind_cvar.empty() &&
        (!only_cvar || data_bind_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(data_bind_cvar.c_str())) {
            if (UI_Rml_CvarHasDisplayText(var)) {
                UI_Rml_ApplyCvarToText(element, var);
            }
        }
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_RefreshCvarBindings(element->GetChild(child_index),
                                   only_cvar,
                                   include_controls);
    }
}

static void UI_Rml_ApplyDocumentCvarBindings(Rml::ElementDocument *document)
{
    if (!document) {
        return;
    }

    ui_rml_applying_cvar_bindings = true;
    UI_Rml_RefreshCvarBindings(document, nullptr, true);
    UI_Rml_ApplyElementConditions(document);
    ui_rml_applying_cvar_bindings = false;
}

static void UI_Rml_RefreshDocumentCvarDisplays(Rml::ElementDocument *document)
{
    if (!document) {
        return;
    }

    ui_rml_applying_cvar_bindings = true;
    UI_Rml_RefreshCvarBindings(document, nullptr, false);
    UI_Rml_ApplyElementConditions(document);
    ui_rml_applying_cvar_bindings = false;
}

static cvar_t *UI_Rml_SetCvarInteger(const Rml::String &name,
                                     cvar_t *var,
                                     int value)
{
    if (var) {
        Cvar_SetInteger(var, value, FROM_MENU);
        return var;
    }

    const std::string value_text = std::to_string(value);
    return Cvar_SetEx(name.c_str(), value_text.c_str(), FROM_MENU);
}

static cvar_t *UI_Rml_SetCvarFloat(const Rml::String &name,
                                   cvar_t *var,
                                   float value)
{
    if (var) {
        Cvar_SetValue(var, value, FROM_MENU);
        return var;
    }

    return Cvar_SetEx(name.c_str(), va("%g", value), FROM_MENU);
}

static cvar_t *UI_Rml_SetCvarString(const Rml::String &name,
                                    cvar_t *var,
                                    const Rml::String &value)
{
    if (var) {
        Cvar_SetByVar(var, value.c_str(), FROM_MENU);
        return var;
    }

    return Cvar_SetEx(name.c_str(), value.c_str(), FROM_MENU);
}

static Rml::Element *UI_Rml_FindCvarControlElement(Rml::Element *element)
{
    for (Rml::Element *current = element; current; current = current->GetParentNode()) {
        if (current->GetAttribute<Rml::String>("data-cvar", "").empty()) {
            continue;
        }

        if (rmlui_dynamic_cast<Rml::ElementFormControl *>(current)) {
            return current;
        }
    }

    return nullptr;
}

static void UI_Rml_SetCvarFromControl(Rml::Element *element)
{
    if (ui_rml_applying_cvar_bindings || !element) {
        return;
    }

    auto *control =
        rmlui_dynamic_cast<Rml::ElementFormControl *>(element);
    if (!control) {
        return;
    }

    const Rml::String cvar_name =
        element->GetAttribute<Rml::String>("data-cvar", "");
    if (cvar_name.empty()) {
        return;
    }

    cvar_t *var = Cvar_FindVar(cvar_name.c_str());

    if (UI_Rml_ElementIsCheckbox(element)) {
        const int bit = UI_Rml_ElementIntAttribute(element, "data-bit", -1);
        const bool negate = UI_Rml_ElementBoolAttribute(element, "data-negate");
        const bool visible_checked = element->HasAttribute("checked");
        const bool cvar_checked = negate ? !visible_checked : visible_checked;

        if (bit >= 0 && bit < 31) {
            const int mask = 1 << bit;
            int next_value = var ? var->integer : 0;
            if (cvar_checked) {
                next_value |= mask;
            } else {
                next_value &= ~mask;
            }
            var = UI_Rml_SetCvarInteger(cvar_name, var, next_value);
        } else {
            var = UI_Rml_SetCvarInteger(cvar_name, var, cvar_checked ? 1 : 0);
        }
    } else {
        const Rml::String value = control->GetValue();
        const Rml::String type = element->GetAttribute<Rml::String>("type", "");

        if (UI_Rml_ElementBoolAttribute(element, "data-integer")) {
            var = UI_Rml_SetCvarInteger(cvar_name,
                                        var,
                                        static_cast<int>(strtol(value.c_str(), nullptr, 10)));
        } else if (!Q_stricmp(type.c_str(), "range")) {
            var = UI_Rml_SetCvarFloat(cvar_name,
                                      var,
                                      static_cast<float>(atof(value.c_str())));
        } else {
            var = UI_Rml_SetCvarString(cvar_name, var, value);
        }
    }

    if (!ui_rml_document || !var) {
        return;
    }

    ui_rml_applying_cvar_bindings = true;
    UI_Rml_RefreshCvarBindings(ui_rml_document, &cvar_name, true);
    UI_Rml_ApplyElementConditions(ui_rml_document);
    ui_rml_applying_cvar_bindings = false;
}

class UI_Rml_CvarEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override
    {
        if (event.GetId() != Rml::EventId::Change) {
            return;
        }

        UI_Rml_SetCvarFromControl(
            UI_Rml_FindCvarControlElement(event.GetTargetElement()));
    }
};

static UI_Rml_CvarEventListener ui_rml_cvar_event_listener;

static void UI_Rml_AttachElementCvarListeners(Rml::Element *element)
{
    if (!element) {
        return;
    }

    if (!element->GetAttribute<Rml::String>("data-cvar", "").empty() &&
        rmlui_dynamic_cast<Rml::ElementFormControl *>(element)) {
        element->AddEventListener(Rml::EventId::Change,
                                  &ui_rml_cvar_event_listener);
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_AttachElementCvarListeners(element->GetChild(child_index));
    }
}

static void UI_Rml_ApplyDocumentAudioHints(const char *route_id,
                                           Rml::ElementDocument *document)
{
    Rml::String music;
    Rml::String open_sound;
    Rml::Element *body = UI_Rml_DocumentBody(document);

    if (!document) {
        return;
    }

    open_sound = document->GetAttribute<Rml::String>("data-menu-sound-open", "");
    if (open_sound.empty() && body) {
        open_sound = body->GetAttribute<Rml::String>("data-menu-sound-open", "");
    }

    if (!open_sound.empty()) {
        const int sound = UI_Rml_MenuSoundForName(open_sound);

        if (sound >= 0) {
            UI_Rml_PlayMenuFeedbackSound(static_cast<uiFeedbackSound_t>(sound));
            Com_Printf("RmlUi route '%s' requested menu open sound '%s'.\n",
                       route_id && route_id[0] ? route_id : "<unknown>",
                       open_sound.c_str());
        }
    }

    music = document->GetAttribute<Rml::String>("data-menu-music", "");
    if (music.empty() && body) {
        music = body->GetAttribute<Rml::String>("data-menu-music", "");
    }

    if (music.empty() ||
        !Q_stricmp(music.c_str(), "none") ||
        !Q_stricmp(music.c_str(), "silent")) {
        return;
    }

    if (!Q_stricmp(music.c_str(), "menu") ||
        !Q_stricmp(music.c_str(), "auto")) {
        OGG_Play();
        Com_Printf("RmlUi route '%s' requested menu music cue '%s'.\n",
                   route_id && route_id[0] ? route_id : "<unknown>",
                   music.c_str());
        return;
    }

    if (!Q_stricmp(music.c_str(), "off") ||
        !Q_stricmp(music.c_str(), "stop")) {
        OGG_Stop();
        Com_Printf("RmlUi route '%s' requested music stop cue '%s'.\n",
                   route_id && route_id[0] ? route_id : "<unknown>",
                   music.c_str());
    }
}

static bool UI_Rml_ElementIsInteractionTag(Rml::Element *element)
{
    if (!element) {
        return false;
    }

    const Rml::String &tag = element->GetTagName();

    return !Q_stricmp(tag.c_str(), "button") ||
           !Q_stricmp(tag.c_str(), "input") ||
           !Q_stricmp(tag.c_str(), "select") ||
           !Q_stricmp(tag.c_str(), "textarea");
}

static bool UI_Rml_ElementWantsInteractionAudio(Rml::Element *element)
{
    return element &&
           (UI_Rml_ElementIsInteractionTag(element) ||
            element->HasAttribute("data-command") ||
            element->HasAttribute("data-command-cvar") ||
            element->HasAttribute("data-route") ||
            element->HasAttribute("data-route-target") ||
            element->HasAttribute("data-cvar") ||
            element->HasAttribute("data-event-click") ||
            element->HasAttribute("data-event-change") ||
            element->HasAttribute("data-menu-sound-focus") ||
            element->HasAttribute("data-menu-sound-change"));
}

class UI_Rml_AudioEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override
    {
        if (ui_rml_applying_cvar_bindings) {
            return;
        }

        const Rml::EventId event_id = event.GetId();

        if (event_id != Rml::EventId::Focus &&
            event_id != Rml::EventId::Change) {
            return;
        }

        Rml::Element *element = event.GetTargetElement();
        if (!UI_Rml_ElementWantsInteractionAudio(element)) {
            return;
        }

        const char *attribute =
            event_id == Rml::EventId::Focus
                ? "data-menu-sound-focus"
                : "data-menu-sound-change";
        const Rml::String sound_name =
            element->GetAttribute<Rml::String>(attribute, "");
        const int sound = sound_name.empty()
                              ? UI_FEEDBACK_MOVE
                              : UI_Rml_MenuSoundForName(sound_name);

        if (sound >= 0) {
            UI_Rml_PlayMenuFeedbackSound(static_cast<uiFeedbackSound_t>(sound));
        }
    }
};

static UI_Rml_AudioEventListener ui_rml_audio_event_listener;

static void UI_Rml_AttachElementAudioListeners(Rml::Element *element)
{
    if (!element) {
        return;
    }

    if (UI_Rml_ElementWantsInteractionAudio(element)) {
        element->AddEventListener(Rml::EventId::Focus,
                                  &ui_rml_audio_event_listener);
        element->AddEventListener(Rml::EventId::Change,
                                  &ui_rml_audio_event_listener);
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_AttachElementAudioListeners(element->GetChild(child_index));
    }
}

class UI_Rml_CommandEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override
    {
        Rml::Element *element = UI_Rml_FindCommandElement(event.GetTargetElement());
        if (!element) {
            return;
        }

        Rml::String command =
            element->GetAttribute<Rml::String>("data-command", "");
        const Rml::String command_cvar =
            element->GetAttribute<Rml::String>("data-command-cvar", "");
        if (command.empty() && !command_cvar.empty()) {
            command = Cvar_VariableString(command_cvar.c_str());
        }
        const Rml::String route_target =
            UI_Rml_RouteTargetForElement(element, command);
        UI_Rml_PlayElementMenuSound(element, command, route_target);

        if (command == "ui.popup" ||
            UI_Rml_CommandStartsWithToken(command, "pushpopup")) {
            Rml::String popup_target = route_target;

            if (popup_target.empty()) {
                popup_target = UI_Rml_CommandFirstArgumentAfterToken(command, "pushpopup");
            }

            if (UI_Rml_QueueRoutePopup(popup_target)) {
                event.StopPropagation();
                return;
            }
        }

        if (UI_Rml_QueueRouteOpen(route_target)) {
            event.StopPropagation();
            return;
        }

        if (command == "ui.close") {
            UI_Rml_QueueCommand("ui_rml_runtime_close");
            event.StopPropagation();
            return;
        }

        if (command == "ui.back" ||
            UI_Rml_CommandStartsWithToken(command, "popmenu")) {
            const char *tail = UI_Rml_CommandTailAfterToken(command, "popmenu");

            UI_Rml_QueueCommand("ui_rml_runtime_back");
            UI_Rml_QueueCommand(tail);
            event.StopPropagation();
            return;
        }

        if (UI_Rml_CommandStartsWithToken(command, "forcemenuoff")) {
            const char *tail = UI_Rml_CommandTailAfterToken(command, "forcemenuoff");

            UI_Rml_QueueCommand("ui_rml_runtime_close");
            UI_Rml_QueueCommand(tail);
            event.StopPropagation();
            return;
        }

        if (command == "ui.open_route") {
            event.StopPropagation();
            return;
        }

        if (UI_Rml_QueueCommand(command.c_str())) {
            event.StopPropagation();
        }
    }
};

static UI_Rml_CommandEventListener ui_rml_command_event_listener;

static void UI_Rml_InstallCoreInterfaces(void)
{
    const ui_rml_renderer_interface_t *renderer = UI_Rml_RendererInterface();

    Rml::SetSystemInterface(&ui_rml_system_interface);
    Rml::SetFileInterface(&ui_rml_file_interface);
    Rml::SetFontEngineInterface(&ui_rml_font_interface);

    if (renderer && renderer->NativeRenderInterface) {
        void *native_render_interface = renderer->NativeRenderInterface();

        if (native_render_interface) {
            Rml::SetRenderInterface(
                static_cast<Rml::RenderInterface *>(native_render_interface));
        }
    }
}

static bool UI_Rml_CompiledRuntimeCanOpenRoutes(void)
{
    return UI_Rml_RendererIsAvailable();
}

static const char *UI_Rml_CompiledRuntimeName(void)
{
    static char runtime_name[64];
    const Rml::String version = Rml::GetVersion();

    Q_snprintf(runtime_name, sizeof(runtime_name), "RmlUi %s", version.c_str());
    return runtime_name;
}

static bool UI_Rml_CompiledRuntimeInit(void)
{
    if (ui_rml_core_initialized) {
        return true;
    }

    ui_rml_log_missing_data_models =
        Cvar_Get("ui_rml_log_missing_data_models", "0", 0);

    UI_Rml_InstallCoreInterfaces();

    if (!Rml::Initialise()) {
        Com_Printf("RmlUi core initialization failed; keeping legacy UI fallback active.\n");
        return false;
    }

    ui_rml_core_initialized = true;
    Com_Printf("RmlUi core initialized through %s; guarded route context rendering is available (renderer='%s', family='%s').\n",
               UI_Rml_CompiledRuntimeName(),
               UI_Rml_RendererName(),
               UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
    return true;
}

static Rml::Vector2i UI_Rml_CompiledRuntimeDimensions(int width, int height)
{
    if (width <= 0) {
        width = VIRTUAL_SCREEN_WIDTH;
    }

    if (height <= 0) {
        height = VIRTUAL_SCREEN_HEIGHT;
    }

    return Rml::Vector2i(width, height);
}

static float UI_Rml_CompiledRuntimeCanvasPixelScale(void)
{
    const int framebuffer_width =
        r_config.width > 0 ? r_config.width : (int)UI_RML_REFERENCE_WIDTH;
    const int framebuffer_height =
        r_config.height > 0 ? r_config.height : (int)UI_RML_REFERENCE_HEIGHT;
    const float scale_x = (float)framebuffer_width / UI_RML_REFERENCE_WIDTH;
    const float scale_y = (float)framebuffer_height / UI_RML_REFERENCE_HEIGHT;
    float scale = min(scale_x, scale_y);

    if (scale < 1.0f) {
        scale = 1.0f;
    }

    return scale;
}

static int UI_Rml_CompiledRuntimeRendererBaseScaleInt(void)
{
    const int framebuffer_width =
        r_config.width > 0 ? r_config.width : VIRTUAL_SCREEN_WIDTH;
    const int framebuffer_height =
        r_config.height > 0 ? r_config.height : VIRTUAL_SCREEN_HEIGHT;
    float scale_x = (float)framebuffer_width / VIRTUAL_SCREEN_WIDTH;
    float scale_y = (float)framebuffer_height / VIRTUAL_SCREEN_HEIGHT;
    float base_scale = max(scale_x, scale_y);
    int base_scale_int;

    if (base_scale < 1.0f) {
        base_scale = 1.0f;
    }

    base_scale_int = (int)base_scale;
    if (base_scale_int < 1) {
        base_scale_int = 1;
    }

    return base_scale_int;
}

static float UI_Rml_CompiledRuntimeRendererDrawScale(void)
{
    const float canvas_scale = UI_Rml_CompiledRuntimeCanvasPixelScale();

    if (canvas_scale <= 0.0f) {
        return (float)UI_Rml_CompiledRuntimeRendererBaseScaleInt();
    }

    return (float)UI_Rml_CompiledRuntimeRendererBaseScaleInt() / canvas_scale;
}

static Rml::Vector2i UI_Rml_CompiledRuntimeCurrentDimensions(void)
{
    const int framebuffer_width =
        r_config.width > 0 ? r_config.width : (int)UI_RML_REFERENCE_WIDTH;
    const int framebuffer_height =
        r_config.height > 0 ? r_config.height : (int)UI_RML_REFERENCE_HEIGHT;
    float scale = UI_Rml_CompiledRuntimeCanvasPixelScale();

    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    return UI_Rml_CompiledRuntimeDimensions(
        max(1, Q_rint((float)framebuffer_width / scale)),
        max(1, Q_rint((float)framebuffer_height / scale)));
}

static bool UI_Rml_CompiledRuntimeEnsureContext(int width, int height)
{
    const Rml::Vector2i dimensions = UI_Rml_CompiledRuntimeDimensions(width, height);

    if (!ui_rml_core_initialized) {
        return false;
    }

    if (!ui_rml_context) {
        ui_rml_context = Rml::CreateContext("worr_ui", dimensions);

        if (!ui_rml_context) {
            Com_Printf("RmlUi failed to create the guarded route context; keeping legacy UI fallback active.\n");
            return false;
        }
    } else if (ui_rml_context_width != dimensions.x ||
               ui_rml_context_height != dimensions.y) {
        ui_rml_context->SetDimensions(dimensions);
    }

    ui_rml_context_width = dimensions.x;
    ui_rml_context_height = dimensions.y;
    return true;
}

static bool UI_Rml_CompiledRuntimeRouteIsAllowed(const char *route_id)
{
    return route_id && route_id[0];
}

static int UI_Rml_CompiledRuntimeModifiers(void)
{
    int modifiers = 0;

    if (Key_IsDown(K_CTRL)) {
        modifiers |= Rml::Input::KM_CTRL;
    }
    if (Key_IsDown(K_SHIFT)) {
        modifiers |= Rml::Input::KM_SHIFT;
    }
    if (Key_IsDown(K_ALT)) {
        modifiers |= Rml::Input::KM_ALT;
    }

    return modifiers;
}

static Rml::Input::KeyIdentifier UI_Rml_CompiledRuntimeKeyIdentifier(int key)
{
    if (key >= 'a' && key <= 'z') {
        key -= 'a' - 'A';
    }

    if (key >= 'A' && key <= 'Z') {
        return static_cast<Rml::Input::KeyIdentifier>(
            Rml::Input::KI_A + (key - 'A'));
    }

    if (key >= '0' && key <= '9') {
        return static_cast<Rml::Input::KeyIdentifier>(
            Rml::Input::KI_0 + (key - '0'));
    }

    switch (key) {
    case K_SPACE:
        return Rml::Input::KI_SPACE;
    case K_BACKSPACE:
        return Rml::Input::KI_BACK;
    case K_TAB:
        return Rml::Input::KI_TAB;
    case K_ENTER:
        return Rml::Input::KI_RETURN;
    case K_ESCAPE:
        return Rml::Input::KI_ESCAPE;
    case K_DEL:
        return Rml::Input::KI_DELETE;
    case K_UPARROW:
        return Rml::Input::KI_UP;
    case K_DOWNARROW:
        return Rml::Input::KI_DOWN;
    case K_LEFTARROW:
        return Rml::Input::KI_LEFT;
    case K_RIGHTARROW:
        return Rml::Input::KI_RIGHT;
    case K_INS:
        return Rml::Input::KI_INSERT;
    case K_HOME:
        return Rml::Input::KI_HOME;
    case K_END:
        return Rml::Input::KI_END;
    case K_PGUP:
        return Rml::Input::KI_PRIOR;
    case K_PGDN:
        return Rml::Input::KI_NEXT;
    case K_PAUSE:
        return Rml::Input::KI_PAUSE;
    case K_CAPSLOCK:
        return Rml::Input::KI_CAPITAL;
    case K_NUMLOCK:
        return Rml::Input::KI_NUMLOCK;
    case K_SCROLLOCK:
        return Rml::Input::KI_SCROLL;
    case K_SHIFT:
    case K_LSHIFT:
    case K_RSHIFT:
        return Rml::Input::KI_LSHIFT;
    case K_CTRL:
    case K_LCTRL:
    case K_RCTRL:
        return Rml::Input::KI_LCONTROL;
    case K_ALT:
    case K_LALT:
    case K_RALT:
        return Rml::Input::KI_LMENU;
    case K_F1:
    case K_F2:
    case K_F3:
    case K_F4:
    case K_F5:
    case K_F6:
    case K_F7:
    case K_F8:
    case K_F9:
    case K_F10:
    case K_F11:
    case K_F12:
        return static_cast<Rml::Input::KeyIdentifier>(
            Rml::Input::KI_F1 + (key - K_F1));
    case K_KP_ENTER:
        return Rml::Input::KI_NUMPADENTER;
    case K_KP_SLASH:
        return Rml::Input::KI_DIVIDE;
    case K_KP_MULTIPLY:
        return Rml::Input::KI_MULTIPLY;
    case K_KP_MINUS:
        return Rml::Input::KI_SUBTRACT;
    case K_KP_PLUS:
        return Rml::Input::KI_ADD;
    case K_KP_INS:
        return Rml::Input::KI_NUMPAD0;
    case K_KP_END:
        return Rml::Input::KI_NUMPAD1;
    case K_KP_DOWNARROW:
        return Rml::Input::KI_NUMPAD2;
    case K_KP_PGDN:
        return Rml::Input::KI_NUMPAD3;
    case K_KP_LEFTARROW:
        return Rml::Input::KI_NUMPAD4;
    case K_KP_5:
        return Rml::Input::KI_NUMPAD5;
    case K_KP_RIGHTARROW:
        return Rml::Input::KI_NUMPAD6;
    case K_KP_HOME:
        return Rml::Input::KI_NUMPAD7;
    case K_KP_UPARROW:
        return Rml::Input::KI_NUMPAD8;
    case K_KP_PGUP:
        return Rml::Input::KI_NUMPAD9;
    case K_KP_DEL:
        return Rml::Input::KI_DECIMAL;
    case ';':
    case ':':
        return Rml::Input::KI_OEM_1;
    case '=':
    case '+':
        return Rml::Input::KI_OEM_PLUS;
    case ',':
    case '<':
        return Rml::Input::KI_OEM_COMMA;
    case '-':
    case '_':
        return Rml::Input::KI_OEM_MINUS;
    case '.':
    case '>':
        return Rml::Input::KI_OEM_PERIOD;
    case '/':
    case '?':
        return Rml::Input::KI_OEM_2;
    case '`':
    case '~':
        return Rml::Input::KI_OEM_3;
    case '[':
    case '{':
        return Rml::Input::KI_OEM_4;
    case '\\':
    case '|':
        return Rml::Input::KI_OEM_5;
    case ']':
    case '}':
        return Rml::Input::KI_OEM_6;
    case '\'':
    case '"':
        return Rml::Input::KI_OEM_7;
    default:
        return Rml::Input::KI_UNKNOWN;
    }
}

static int UI_Rml_CompiledRuntimeMouseButtonIndex(int key)
{
    if (key >= K_MOUSE1 && key <= K_MOUSE8) {
        return key - K_MOUSE1;
    }

    return -1;
}

static void UI_Rml_CompiledRuntimeCloseRoute(void);
static void UI_Rml_CompiledRuntimeCloseActiveDocument(void);

static bool UI_Rml_CompiledRuntimeProbeRoute(const char *route_id, const char *document_path)
{
    Rml::String contents;

    if (!document_path || !document_path[0]) {
        Com_Printf("RmlUi runtime file probe failed for route '%s': no document path.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!Rml::GetFileInterface() ||
        !Rml::GetFileInterface()->LoadFile(document_path, contents)) {
        Com_Printf("RmlUi runtime file probe failed for route '%s': %s.\n",
                   route_id ? route_id : "<null>",
                   document_path);
        return false;
    }

    Com_Printf("RmlUi runtime file probe OK: route '%s' loaded %s (%zu bytes) through WORR filesystem.\n",
               route_id ? route_id : "<null>",
               document_path,
               contents.size());
    return true;
}

static void UI_Rml_CompiledRuntimeShutdown(void)
{
    if (!ui_rml_core_initialized) {
        return;
    }

    UI_Rml_CompiledRuntimeCloseRoute();

    if (ui_rml_context) {
        Rml::RemoveContext("worr_ui");
        ui_rml_context = NULL;
        ui_rml_context_width = 0;
        ui_rml_context_height = 0;
    }

    Rml::Shutdown();
    ui_rml_core_initialized = false;
    ui_rml_log_missing_data_models = NULL;
}

static bool UI_Rml_CompiledRuntimeOpenRoute(const char *route_id, const char *document_path)
{
    if (!UI_Rml_RendererIsAvailable()) {
        Com_Printf("RmlUi route '%s' resolved to '%s', but no native renderer bridge is available (renderer='%s', family='%s').\n",
                   route_id ? route_id : "<null>",
                   document_path ? document_path : "<null>",
                   UI_Rml_RendererName(),
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
        return false;
    }

    if (UI_Rml_RendererFamily() != UI_RML_RENDERER_FAMILY_OPENGL) {
        Com_Printf("RmlUi route '%s' resolved to '%s', but renderer family '%s' does not have a guarded native context path yet.\n",
                   route_id ? route_id : "<null>",
                   document_path ? document_path : "<null>",
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
        return false;
    }

    if (!UI_Rml_CompiledRuntimeRouteIsAllowed(route_id)) {
        Com_Printf("RmlUi route '%s' is outside the guarded sample-route allow-list.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!document_path || !document_path[0]) {
        Com_Printf("RmlUi route '%s' cannot open without a document path.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    const Rml::Vector2i initial_dimensions =
        UI_Rml_CompiledRuntimeCurrentDimensions();
    if (!UI_Rml_CompiledRuntimeEnsureContext(initial_dimensions.x,
                                            initial_dimensions.y)) {
        return false;
    }

    Rml::String document_contents;
    if (!Rml::GetFileInterface() ||
        !Rml::GetFileInterface()->LoadFile(document_path, document_contents)) {
        Com_Printf("RmlUi route '%s' failed to read document '%s'.\n",
                   route_id ? route_id : "<null>",
                   document_path);
        return false;
    }

    Rml::ElementDocument *document =
        ui_rml_context->LoadDocumentFromMemory(document_contents, document_path);
    if (!document) {
        Com_Printf("RmlUi route '%s' failed to load document '%s'.\n",
                   route_id ? route_id : "<null>",
                   document_path);
        return false;
    }

    UI_Rml_CompiledRuntimeCloseActiveDocument();

    ui_rml_document = document;
    ui_rml_document->AddEventListener(Rml::EventId::Click, &ui_rml_command_event_listener);
    ui_rml_document->Show();
    ui_rml_active_route = route_id ? route_id : "";
    ui_rml_active_document = document_path;
    UI_Rml_ApplyDocumentCvarBindings(ui_rml_document);
    ui_rml_context->Update();
    UI_Rml_AttachElementCvarListeners(ui_rml_document);
    UI_Rml_AttachElementAudioListeners(ui_rml_document);
    UI_Rml_ApplyDocumentAudioHints(route_id, ui_rml_document);

    Com_Printf("RmlUi route '%s' opened document '%s' in guarded context '%s'.\n",
               ui_rml_active_route.c_str(),
               ui_rml_active_document.c_str(),
               "worr_ui");
    return true;
}

static void UI_Rml_CompiledRuntimeCloseActiveDocument(void)
{
    if (ui_rml_document) {
        Rml::ElementDocument *document = ui_rml_document;
        ui_rml_document = NULL;
        document->Close();
    }
}

static void UI_Rml_CompiledRuntimeCloseRoute(void)
{
    UI_Rml_CompiledRuntimeCloseActiveDocument();

    if (ui_rml_context) {
        ui_rml_context->Update();
    }

    ui_rml_active_route.clear();
    ui_rml_active_document.clear();
}

static bool UI_Rml_CompiledRuntimeUpdate(int width, int height, unsigned realtime)
{
    (void)realtime;

    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    if (!UI_Rml_CompiledRuntimeEnsureContext(width, height)) {
        return false;
    }

    UI_Rml_RefreshDocumentCvarDisplays(ui_rml_document);
    (void)ui_rml_context->Update();
    return true;
}

static bool UI_Rml_CompiledRuntimeRender(void)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    R_SetClipRect(NULL);
    R_SetScale(UI_Rml_CompiledRuntimeRendererDrawScale());
    return ui_rml_context->Render();
}

static bool UI_Rml_CompiledRuntimeKeyEvent(int key, bool down)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    const int modifiers = UI_Rml_CompiledRuntimeModifiers();
    const int button = UI_Rml_CompiledRuntimeMouseButtonIndex(key);
    if (button >= 0) {
        if (down) {
            (void)ui_rml_context->ProcessMouseButtonDown(button, modifiers);
        } else {
            (void)ui_rml_context->ProcessMouseButtonUp(button, modifiers);
        }
        return true;
    }

    if (down) {
        switch (key) {
        case K_MWHEELUP:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(0.0f, -1.0f), modifiers);
            return true;
        case K_MWHEELDOWN:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(0.0f, 1.0f), modifiers);
            return true;
        case K_MWHEELLEFT:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(-1.0f, 0.0f), modifiers);
            return true;
        case K_MWHEELRIGHT:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(1.0f, 0.0f), modifiers);
            return true;
        default:
            break;
        }
    }

    const Rml::Input::KeyIdentifier key_identifier =
        UI_Rml_CompiledRuntimeKeyIdentifier(key);
    if (key_identifier == Rml::Input::KI_UNKNOWN) {
        return false;
    }

    if (down) {
        (void)ui_rml_context->ProcessKeyDown(key_identifier, modifiers);
    } else {
        (void)ui_rml_context->ProcessKeyUp(key_identifier, modifiers);
    }

    return true;
}

static bool UI_Rml_CompiledRuntimeCharEvent(int key)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    if (key < 32 || (key >= 0x7f && key < 0xa0) || key > UNICODE_MAX ||
        (key >= 0xd800 && key <= 0xdfff)) {
        return false;
    }

    (void)ui_rml_context->ProcessTextInput(static_cast<Rml::Character>(key));
    return true;
}

static bool UI_Rml_CompiledRuntimeMouseEvent(int x, int y)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    (void)ui_rml_context->ProcessMouseMove(x, y, UI_Rml_CompiledRuntimeModifiers());
    return true;
}

void UI_Rml_RegisterCompiledRuntime(void)
{
    static const ui_rml_runtime_interface_t runtime = {
        UI_Rml_CompiledRuntimeInit,
        UI_Rml_CompiledRuntimeShutdown,
        UI_Rml_CompiledRuntimeOpenRoute,
        UI_Rml_CompiledRuntimeCloseRoute,
        UI_Rml_CompiledRuntimeUpdate,
        UI_Rml_CompiledRuntimeRender,
        UI_Rml_CompiledRuntimeKeyEvent,
        UI_Rml_CompiledRuntimeCharEvent,
        UI_Rml_CompiledRuntimeMouseEvent,
        UI_Rml_CompiledRuntimeProbeRoute,
        UI_Rml_CompiledRuntimeName,
        UI_Rml_CompiledRuntimeCanOpenRoutes,
    };

    UI_Rml_SetRuntimeInterface(&runtime);
}

#endif
