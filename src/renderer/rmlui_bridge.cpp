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

#include "shared/shared.h"
#include "renderer/renderer.h"
#include "renderer/ui_scale.h"

#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL

extern "C" {
#include "../rend_gl/gl.h"
}

#endif

#if UI_RML_HAS_RUNTIME

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef DotProduct
#undef DotProduct
#endif

#ifdef CrossProduct
#undef CrossProduct
#endif

#include <RmlUi/Core/RenderInterface.h>

#endif

#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL

namespace {

struct R_RmlUiSvgColor {
    Rml::byte r = 255;
    Rml::byte g = 255;
    Rml::byte b = 255;
    Rml::byte a = 255;
};

struct R_RmlUiSvgPoint {
    double x = 0.0;
    double y = 0.0;
};

struct R_RmlUiSvgShape {
    enum Type {
        Line,
        Polyline,
        Polygon,
        Rect,
        Circle
    } type = Polyline;

    std::vector<R_RmlUiSvgPoint> points;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double r = 0.0;
    double stroke_width = 1.0;
    bool closed = false;
    bool has_fill = false;
    bool has_stroke = false;
    R_RmlUiSvgColor fill;
    R_RmlUiSvgColor stroke;
};

struct R_RmlUiSvgDocument {
    int width = 32;
    int height = 32;
    double view_x = 0.0;
    double view_y = 0.0;
    double view_w = 32.0;
    double view_h = 32.0;
    std::vector<R_RmlUiSvgShape> shapes;
};

static bool R_RmlUiSvgEndsWithSvg(const Rml::String &source)
{
    const size_t length = source.size();
    if (length < 4) {
        return false;
    }

    const char *suffix = source.c_str() + length - 4;
    return !Q_stricmp(suffix, ".svg");
}

static void R_RmlUiSvgSkipSeparators(const char *&cursor)
{
    while (*cursor &&
           (std::isspace(static_cast<unsigned char>(*cursor)) ||
            *cursor == ',')) {
        ++cursor;
    }
}

static bool R_RmlUiSvgParseDouble(const char *&cursor, double *out)
{
    R_RmlUiSvgSkipSeparators(cursor);

    char *end = nullptr;
    const double value = std::strtod(cursor, &end);
    if (end == cursor) {
        return false;
    }

    cursor = end;
    *out = value;
    return true;
}

static bool R_RmlUiSvgAttributeNameMatch(const Rml::String &tag,
                                         size_t pos,
                                         const char *name,
                                         size_t name_length)
{
    if (pos > 0) {
        const char left = tag[pos - 1];
        if (std::isalnum(static_cast<unsigned char>(left)) ||
            left == '-' || left == '_') {
            return false;
        }
    }

    const size_t right_pos = pos + name_length;
    if (right_pos >= tag.size()) {
        return false;
    }

    const char right = tag[right_pos];
    return std::isspace(static_cast<unsigned char>(right)) || right == '=';
}

static Rml::String R_RmlUiSvgAttribute(const Rml::String &tag, const char *name)
{
    const size_t name_length = std::strlen(name);
    size_t pos = 0;

    while ((pos = tag.find(name, pos)) != Rml::String::npos) {
        if (!R_RmlUiSvgAttributeNameMatch(tag, pos, name, name_length)) {
            pos += name_length;
            continue;
        }

        pos += name_length;
        while (pos < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[pos]))) {
            ++pos;
        }

        if (pos >= tag.size() || tag[pos] != '=') {
            continue;
        }

        ++pos;
        while (pos < tag.size() &&
               std::isspace(static_cast<unsigned char>(tag[pos]))) {
            ++pos;
        }

        if (pos >= tag.size()) {
            return "";
        }

        const char quote = tag[pos];
        if (quote != '"' && quote != '\'') {
            return "";
        }

        const size_t value_start = ++pos;
        const size_t value_end = tag.find(quote, value_start);
        if (value_end == Rml::String::npos) {
            return "";
        }

        return tag.substr(value_start, value_end - value_start);
    }

    return "";
}

static double R_RmlUiSvgAttributeDouble(const Rml::String &tag,
                                        const char *name,
                                        double fallback)
{
    const Rml::String value = R_RmlUiSvgAttribute(tag, name);
    if (value.empty()) {
        return fallback;
    }

    const char *cursor = value.c_str();
    double parsed = fallback;
    return R_RmlUiSvgParseDouble(cursor, &parsed) ? parsed : fallback;
}

static int R_RmlUiSvgAttributeInt(const Rml::String &tag,
                                  const char *name,
                                  int fallback)
{
    const double parsed = R_RmlUiSvgAttributeDouble(
        tag, name, static_cast<double>(fallback));
    if (parsed < 1.0) {
        return fallback;
    }
    if (parsed > 256.0) {
        return 256;
    }
    return static_cast<int>(parsed + 0.5);
}

static bool R_RmlUiSvgHexNibble(char c, int *out)
{
    if (c >= '0' && c <= '9') {
        *out = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = 10 + c - 'a';
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = 10 + c - 'A';
        return true;
    }
    return false;
}

static bool R_RmlUiSvgParseColor(const Rml::String &value,
                                 double opacity,
                                 R_RmlUiSvgColor *out)
{
    if (value.empty() || !Q_stricmp(value.c_str(), "none")) {
        return false;
    }

    if (value[0] != '#') {
        return false;
    }

    if (opacity < 0.0) {
        opacity = 0.0;
    } else if (opacity > 1.0) {
        opacity = 1.0;
    }

    int r0 = 0, r1 = 0, g0 = 0, g1 = 0, b0 = 0, b1 = 0;
    if (value.size() == 4 &&
        R_RmlUiSvgHexNibble(value[1], &r0) &&
        R_RmlUiSvgHexNibble(value[2], &g0) &&
        R_RmlUiSvgHexNibble(value[3], &b0)) {
        out->r = static_cast<Rml::byte>(r0 * 17);
        out->g = static_cast<Rml::byte>(g0 * 17);
        out->b = static_cast<Rml::byte>(b0 * 17);
        out->a = static_cast<Rml::byte>(opacity * 255.0 + 0.5);
        return true;
    }

    if (value.size() == 7 &&
        R_RmlUiSvgHexNibble(value[1], &r0) &&
        R_RmlUiSvgHexNibble(value[2], &r1) &&
        R_RmlUiSvgHexNibble(value[3], &g0) &&
        R_RmlUiSvgHexNibble(value[4], &g1) &&
        R_RmlUiSvgHexNibble(value[5], &b0) &&
        R_RmlUiSvgHexNibble(value[6], &b1)) {
        out->r = static_cast<Rml::byte>(r0 * 16 + r1);
        out->g = static_cast<Rml::byte>(g0 * 16 + g1);
        out->b = static_cast<Rml::byte>(b0 * 16 + b1);
        out->a = static_cast<Rml::byte>(opacity * 255.0 + 0.5);
        return true;
    }

    return false;
}

static void R_RmlUiSvgApplyPaint(R_RmlUiSvgShape *shape,
                                 const Rml::String &tag)
{
    const double opacity = R_RmlUiSvgAttributeDouble(tag, "opacity", 1.0);
    const double fill_opacity =
        R_RmlUiSvgAttributeDouble(tag, "fill-opacity", opacity);
    const double stroke_opacity =
        R_RmlUiSvgAttributeDouble(tag, "stroke-opacity", opacity);
    const Rml::String fill = R_RmlUiSvgAttribute(tag, "fill");
    const Rml::String stroke = R_RmlUiSvgAttribute(tag, "stroke");

    shape->has_fill =
        R_RmlUiSvgParseColor(fill, fill_opacity, &shape->fill);
    shape->has_stroke =
        R_RmlUiSvgParseColor(stroke, stroke_opacity, &shape->stroke);
    shape->stroke_width =
        R_RmlUiSvgAttributeDouble(tag, "stroke-width", shape->stroke_width);

    if (shape->stroke_width <= 0.0) {
        shape->has_stroke = false;
    }
}

static Rml::String R_RmlUiSvgTagName(const Rml::String &tag)
{
    size_t pos = 0;
    while (pos < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[pos]))) {
        ++pos;
    }

    if (pos < tag.size() &&
        (tag[pos] == '/' || tag[pos] == '!' || tag[pos] == '?')) {
        return "";
    }

    const size_t name_start = pos;
    while (pos < tag.size() &&
           !std::isspace(static_cast<unsigned char>(tag[pos])) &&
           tag[pos] != '/') {
        ++pos;
    }

    return tag.substr(name_start, pos - name_start);
}

static std::vector<R_RmlUiSvgPoint> R_RmlUiSvgParsePoints(
    const Rml::String &points)
{
    std::vector<R_RmlUiSvgPoint> parsed;
    const char *cursor = points.c_str();

    while (*cursor) {
        double x = 0.0;
        double y = 0.0;
        if (!R_RmlUiSvgParseDouble(cursor, &x) ||
            !R_RmlUiSvgParseDouble(cursor, &y)) {
            break;
        }

        parsed.push_back(R_RmlUiSvgPoint{x, y});
    }

    return parsed;
}

static bool R_RmlUiSvgParsePath(const Rml::String &path,
                                R_RmlUiSvgShape *shape)
{
    const char *cursor = path.c_str();
    char command = 0;
    R_RmlUiSvgPoint current;
    bool have_current = false;

    while (*cursor) {
        R_RmlUiSvgSkipSeparators(cursor);
        if (!*cursor) {
            break;
        }

        if (std::isalpha(static_cast<unsigned char>(*cursor))) {
            command = *cursor++;
        }

        const char lower = static_cast<char>(
            std::tolower(static_cast<unsigned char>(command)));
        const bool relative = lower == command;

        if (lower == 'z') {
            shape->closed = true;
            if (*cursor) {
                ++cursor;
            }
            continue;
        }

        if (lower == 'm' || lower == 'l') {
            double x = 0.0;
            double y = 0.0;
            if (!R_RmlUiSvgParseDouble(cursor, &x) ||
                !R_RmlUiSvgParseDouble(cursor, &y)) {
                return false;
            }

            if (relative && have_current) {
                x += current.x;
                y += current.y;
            }

            current = {x, y};
            have_current = true;
            shape->points.push_back(current);
            if (lower == 'm') {
                command = relative ? 'l' : 'L';
            }
            continue;
        }

        if (lower == 'h') {
            double x = 0.0;
            if (!have_current || !R_RmlUiSvgParseDouble(cursor, &x)) {
                return false;
            }
            if (relative) {
                x += current.x;
            }
            current.x = x;
            shape->points.push_back(current);
            continue;
        }

        if (lower == 'v') {
            double y = 0.0;
            if (!have_current || !R_RmlUiSvgParseDouble(cursor, &y)) {
                return false;
            }
            if (relative) {
                y += current.y;
            }
            current.y = y;
            shape->points.push_back(current);
            continue;
        }

        return false;
    }

    return shape->points.size() >= 2;
}

static bool R_RmlUiSvgParseTag(const Rml::String &tag,
                               R_RmlUiSvgDocument *document)
{
    const Rml::String name = R_RmlUiSvgTagName(tag);
    if (name.empty()) {
        return true;
    }

    if (!Q_stricmp(name.c_str(), "svg")) {
        document->width = R_RmlUiSvgAttributeInt(tag, "width", document->width);
        document->height = R_RmlUiSvgAttributeInt(tag, "height", document->height);

        const Rml::String view_box = R_RmlUiSvgAttribute(tag, "viewBox");
        if (!view_box.empty()) {
            const char *cursor = view_box.c_str();
            double values[4] = {};
            if (R_RmlUiSvgParseDouble(cursor, &values[0]) &&
                R_RmlUiSvgParseDouble(cursor, &values[1]) &&
                R_RmlUiSvgParseDouble(cursor, &values[2]) &&
                R_RmlUiSvgParseDouble(cursor, &values[3]) &&
                values[2] > 0.0 && values[3] > 0.0) {
                document->view_x = values[0];
                document->view_y = values[1];
                document->view_w = values[2];
                document->view_h = values[3];
            }
        } else {
            document->view_w = static_cast<double>(document->width);
            document->view_h = static_cast<double>(document->height);
        }
        return true;
    }

    R_RmlUiSvgShape shape;

    if (!Q_stricmp(name.c_str(), "line")) {
        shape.type = R_RmlUiSvgShape::Line;
        shape.points.push_back({
            R_RmlUiSvgAttributeDouble(tag, "x1", 0.0),
            R_RmlUiSvgAttributeDouble(tag, "y1", 0.0)});
        shape.points.push_back({
            R_RmlUiSvgAttributeDouble(tag, "x2", 0.0),
            R_RmlUiSvgAttributeDouble(tag, "y2", 0.0)});
    } else if (!Q_stricmp(name.c_str(), "polyline") ||
               !Q_stricmp(name.c_str(), "polygon")) {
        shape.type = !Q_stricmp(name.c_str(), "polygon") ?
            R_RmlUiSvgShape::Polygon : R_RmlUiSvgShape::Polyline;
        shape.closed = shape.type == R_RmlUiSvgShape::Polygon;
        shape.points = R_RmlUiSvgParsePoints(
            R_RmlUiSvgAttribute(tag, "points"));
        if (shape.points.size() < 2) {
            return true;
        }
    } else if (!Q_stricmp(name.c_str(), "rect")) {
        shape.type = R_RmlUiSvgShape::Rect;
        shape.x = R_RmlUiSvgAttributeDouble(tag, "x", 0.0);
        shape.y = R_RmlUiSvgAttributeDouble(tag, "y", 0.0);
        shape.width = R_RmlUiSvgAttributeDouble(tag, "width", 0.0);
        shape.height = R_RmlUiSvgAttributeDouble(tag, "height", 0.0);
        if (shape.width <= 0.0 || shape.height <= 0.0) {
            return true;
        }
    } else if (!Q_stricmp(name.c_str(), "circle")) {
        shape.type = R_RmlUiSvgShape::Circle;
        shape.cx = R_RmlUiSvgAttributeDouble(tag, "cx", 0.0);
        shape.cy = R_RmlUiSvgAttributeDouble(tag, "cy", 0.0);
        shape.r = R_RmlUiSvgAttributeDouble(tag, "r", 0.0);
        if (shape.r <= 0.0) {
            return true;
        }
    } else if (!Q_stricmp(name.c_str(), "path")) {
        shape.type = R_RmlUiSvgShape::Polyline;
        if (!R_RmlUiSvgParsePath(R_RmlUiSvgAttribute(tag, "d"), &shape)) {
            return true;
        }
        if (shape.closed) {
            shape.type = R_RmlUiSvgShape::Polygon;
        }
    } else {
        return true;
    }

    R_RmlUiSvgApplyPaint(&shape, tag);
    if (shape.has_fill || shape.has_stroke) {
        document->shapes.push_back(std::move(shape));
    }

    return true;
}

static bool R_RmlUiSvgParseDocument(const std::string &svg,
                                    R_RmlUiSvgDocument *document)
{
    size_t open = svg.find('<');
    while (open != std::string::npos) {
        const size_t close = svg.find('>', open + 1);
        if (close == std::string::npos) {
            break;
        }

        const Rml::String tag(svg.data() + open + 1, close - open - 1);
        if (!R_RmlUiSvgParseTag(tag, document)) {
            return false;
        }

        open = svg.find('<', close + 1);
    }

    return document->width > 0 && document->height > 0 &&
           document->view_w > 0.0 && document->view_h > 0.0 &&
           !document->shapes.empty();
}

static double R_RmlUiSvgDistanceToSegment(R_RmlUiSvgPoint p,
                                          R_RmlUiSvgPoint a,
                                          R_RmlUiSvgPoint b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    double t = 0.0;

    if (length_squared > 0.0) {
        t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / length_squared;
        if (t < 0.0) {
            t = 0.0;
        } else if (t > 1.0) {
            t = 1.0;
        }
    }

    const double closest_x = a.x + t * dx;
    const double closest_y = a.y + t * dy;
    const double dist_x = p.x - closest_x;
    const double dist_y = p.y - closest_y;
    return std::sqrt(dist_x * dist_x + dist_y * dist_y);
}

static bool R_RmlUiSvgPointInPolygon(R_RmlUiSvgPoint p,
                                     const std::vector<R_RmlUiSvgPoint> &points)
{
    bool inside = false;
    const size_t count = points.size();
    if (count < 3) {
        return false;
    }

    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const R_RmlUiSvgPoint a = points[i];
        const R_RmlUiSvgPoint b = points[j];
        const bool crosses =
            ((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }

    return inside;
}

static bool R_RmlUiSvgShapeFillHit(const R_RmlUiSvgShape &shape,
                                   R_RmlUiSvgPoint p)
{
    switch (shape.type) {
    case R_RmlUiSvgShape::Rect:
        return p.x >= shape.x && p.x <= shape.x + shape.width &&
               p.y >= shape.y && p.y <= shape.y + shape.height;
    case R_RmlUiSvgShape::Circle: {
        const double dx = p.x - shape.cx;
        const double dy = p.y - shape.cy;
        return dx * dx + dy * dy <= shape.r * shape.r;
    }
    case R_RmlUiSvgShape::Polygon:
        return R_RmlUiSvgPointInPolygon(p, shape.points);
    default:
        return false;
    }
}

static bool R_RmlUiSvgShapeStrokeHit(const R_RmlUiSvgShape &shape,
                                     R_RmlUiSvgPoint p)
{
    const double half_width = shape.stroke_width * 0.5;

    if (shape.type == R_RmlUiSvgShape::Circle) {
        const double dx = p.x - shape.cx;
        const double dy = p.y - shape.cy;
        const double distance = std::sqrt(dx * dx + dy * dy);
        return std::fabs(distance - shape.r) <= half_width;
    }

    if (shape.type == R_RmlUiSvgShape::Rect) {
        const bool in_outer =
            p.x >= shape.x - half_width &&
            p.x <= shape.x + shape.width + half_width &&
            p.y >= shape.y - half_width &&
            p.y <= shape.y + shape.height + half_width;
        const bool in_inner =
            p.x > shape.x + half_width &&
            p.x < shape.x + shape.width - half_width &&
            p.y > shape.y + half_width &&
            p.y < shape.y + shape.height - half_width;
        return in_outer && !in_inner;
    }

    if (shape.points.size() < 2) {
        return false;
    }

    for (size_t i = 1; i < shape.points.size(); ++i) {
        if (R_RmlUiSvgDistanceToSegment(
                p, shape.points[i - 1], shape.points[i]) <= half_width) {
            return true;
        }
    }

    if (shape.closed &&
        R_RmlUiSvgDistanceToSegment(
            p, shape.points.back(), shape.points.front()) <= half_width) {
        return true;
    }

    return false;
}

static void R_RmlUiSvgComposite(std::vector<float> &rgba,
                                size_t offset,
                                R_RmlUiSvgColor color)
{
    const float src_a = static_cast<float>(color.a) / 255.0f;
    if (src_a <= 0.0f) {
        return;
    }

    const float inv_a = 1.0f - src_a;
    rgba[offset + 0] = (static_cast<float>(color.r) / 255.0f) * src_a +
                       rgba[offset + 0] * inv_a;
    rgba[offset + 1] = (static_cast<float>(color.g) / 255.0f) * src_a +
                       rgba[offset + 1] * inv_a;
    rgba[offset + 2] = (static_cast<float>(color.b) / 255.0f) * src_a +
                       rgba[offset + 2] * inv_a;
    rgba[offset + 3] = src_a + rgba[offset + 3] * inv_a;
}

static Rml::byte R_RmlUiSvgFloatToByte(float value)
{
    if (value < 0.0f) {
        value = 0.0f;
    } else if (value > 1.0f) {
        value = 1.0f;
    }
    return static_cast<Rml::byte>(value * 255.0f + 0.5f);
}

static bool R_RmlUiSvgRasterize(const R_RmlUiSvgDocument &document,
                                std::vector<Rml::byte> *out_rgba)
{
    static constexpr int supersample = 3;
    const int high_width = document.width * supersample;
    const int high_height = document.height * supersample;

    if (high_width <= 0 || high_height <= 0 ||
        high_width > 768 || high_height > 768) {
        return false;
    }

    std::vector<float> high(
        static_cast<size_t>(high_width) *
        static_cast<size_t>(high_height) * 4u, 0.0f);

    for (int y = 0; y < high_height; ++y) {
        const double svg_y =
            document.view_y +
            (static_cast<double>(y) + 0.5) *
            document.view_h / static_cast<double>(high_height);

        for (int x = 0; x < high_width; ++x) {
            const double svg_x =
                document.view_x +
                (static_cast<double>(x) + 0.5) *
                document.view_w / static_cast<double>(high_width);
            const R_RmlUiSvgPoint p{svg_x, svg_y};
            const size_t offset =
                (static_cast<size_t>(y) * static_cast<size_t>(high_width) +
                 static_cast<size_t>(x)) * 4u;

            for (const R_RmlUiSvgShape &shape : document.shapes) {
                if (shape.has_fill &&
                    R_RmlUiSvgShapeFillHit(shape, p)) {
                    R_RmlUiSvgComposite(high, offset, shape.fill);
                }

                if (shape.has_stroke &&
                    R_RmlUiSvgShapeStrokeHit(shape, p)) {
                    R_RmlUiSvgComposite(high, offset, shape.stroke);
                }
            }
        }
    }

    out_rgba->assign(
        static_cast<size_t>(document.width) *
        static_cast<size_t>(document.height) * 4u, 0);

    for (int y = 0; y < document.height; ++y) {
        for (int x = 0; x < document.width; ++x) {
            float accum[4] = {};

            for (int sy = 0; sy < supersample; ++sy) {
                for (int sx = 0; sx < supersample; ++sx) {
                    const int high_x = x * supersample + sx;
                    const int high_y = y * supersample + sy;
                    const size_t src =
                        (static_cast<size_t>(high_y) *
                         static_cast<size_t>(high_width) +
                         static_cast<size_t>(high_x)) * 4u;
                    accum[0] += high[src + 0];
                    accum[1] += high[src + 1];
                    accum[2] += high[src + 2];
                    accum[3] += high[src + 3];
                }
            }

            const float scale = 1.0f /
                static_cast<float>(supersample * supersample);
            const size_t dst =
                (static_cast<size_t>(y) *
                 static_cast<size_t>(document.width) +
                 static_cast<size_t>(x)) * 4u;
            (*out_rgba)[dst + 0] = R_RmlUiSvgFloatToByte(accum[0] * scale);
            (*out_rgba)[dst + 1] = R_RmlUiSvgFloatToByte(accum[1] * scale);
            (*out_rgba)[dst + 2] = R_RmlUiSvgFloatToByte(accum[2] * scale);
            (*out_rgba)[dst + 3] = R_RmlUiSvgFloatToByte(accum[3] * scale);
        }
    }

    return true;
}

static bool R_RmlUiSvgLoadFile(const Rml::String &source,
                               std::vector<Rml::byte> *out_rgba,
                               Rml::Vector2i *out_dimensions)
{
    void *loaded = nullptr;
    const int length = FS_LoadFile(source.c_str(), &loaded);
    if (length <= 0 || !loaded) {
        if (loaded) {
            FS_FreeFile(loaded);
        }
        return false;
    }

    std::string svg(static_cast<const char *>(loaded),
                    static_cast<size_t>(length));
    FS_FreeFile(loaded);

    R_RmlUiSvgDocument document;
    if (!R_RmlUiSvgParseDocument(svg, &document) ||
        !R_RmlUiSvgRasterize(document, out_rgba)) {
        return false;
    }

    *out_dimensions = {document.width, document.height};
    return true;
}

} // namespace

class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        if (vertices.empty() || indices.empty() ||
            vertices.size() > static_cast<size_t>(TESS_MAX_VERTICES) ||
            indices.size() > static_cast<size_t>(TESS_MAX_INDICES)) {
            return {};
        }

        auto geometry = std::make_unique<R_RmlUiCompiledGeometry>();
        geometry->vertices.reserve(vertices.size());
        geometry->indices.reserve(indices.size());

        for (const Rml::Vertex &vertex : vertices) {
            const Rml::Colourb colour = vertex.colour.ToNonPremultiplied();
            glVertexDesc2D_t out = {};

            out.xy[0] = vertex.position.x;
            out.xy[1] = vertex.position.y;
            out.st[0] = vertex.tex_coord.x;
            out.st[1] = vertex.tex_coord.y;
            out.c = COLOR_U32_RGBA(colour.red, colour.green, colour.blue, colour.alpha);
            geometry->vertices.push_back(out);
        }

        for (int index : indices) {
            if (index < 0 || static_cast<size_t>(index) >= vertices.size()) {
                return {};
            }

            geometry->indices.push_back(static_cast<glIndex_t>(index));
        }

        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override
    {
        auto *compiled = reinterpret_cast<R_RmlUiCompiledGeometry *>(geometry);
        if (!compiled || compiled->vertices.empty() || compiled->indices.empty()) {
            return;
        }

        if (compiled->vertices.size() > static_cast<size_t>(TESS_MAX_VERTICES) ||
            compiled->indices.size() > static_cast<size_t>(TESS_MAX_INDICES)) {
            return;
        }

        const GLuint texnum = TextureForHandle(texture);

        if (tess.numverts || tess.numindices) {
            GL_Flush2D();
        }

        tess.texnum[TMU_TEXTURE] = texnum;
        tess.flags |= GLS_BLEND_BLEND | GLS_SHADE_SMOOTH;

        auto *dst_vertices = reinterpret_cast<glVertexDesc2D_t *>(tess.vertices);
        dst_vertices += tess.numverts;

        for (const glVertexDesc2D_t &src : compiled->vertices) {
            *dst_vertices = src;
            dst_vertices->xy[0] += translation.x;
            dst_vertices->xy[1] += translation.y;
            ++dst_vertices;
        }

        glIndex_t *dst_indices = tess.indices + tess.numindices;
        for (glIndex_t index : compiled->indices) {
            *dst_indices++ = static_cast<glIndex_t>(tess.numverts + index);
        }

        tess.numverts += static_cast<int>(compiled->vertices.size());
        tess.numindices += static_cast<int>(compiled->indices.size());
        GL_Flush2D();
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        auto *compiled = reinterpret_cast<R_RmlUiCompiledGeometry *>(geometry);
        delete compiled;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
                                   const Rml::String &source) override
    {
        texture_dimensions = {};

        if (source.empty()) {
            return {};
        }

        if (R_RmlUiSvgEndsWithSvg(source)) {
            std::vector<Rml::byte> svg_rgba;
            Rml::Vector2i svg_dimensions;

            if (R_RmlUiSvgLoadFile(source, &svg_rgba, &svg_dimensions)) {
                Rml::TextureHandle handle = GenerateTexture(
                    Rml::Span<const Rml::byte>(
                        svg_rgba.data(), svg_rgba.size()),
                    svg_dimensions);
                if (handle) {
                    texture_dimensions = svg_dimensions;
                    Com_Printf("RmlUi OpenGL SVG texture generated: source='%s' size=%dx%d.\n",
                               source.c_str(),
                               svg_dimensions.x,
                               svg_dimensions.y);
                    return handle;
                }
            }

            Com_WPrintf("RmlUi OpenGL SVG texture failed: source='%s'.\n",
                        source.c_str());
            return {};
        }

        const image_t *image = IMG_Find(source.c_str(), IT_PIC, IF_NONE);
        if (!image || image == R_NOTEXTURE || !image->texnum ||
            !image->width || !image->height) {
            return {};
        }

        texture_dimensions = {image->width, image->height};
        return RegisterTexture(image->texnum, false);
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i source_dimensions) override
    {
        if (source.empty() ||
            source_dimensions.x <= 0 ||
            source_dimensions.y <= 0) {
            return {};
        }

        const size_t width = static_cast<size_t>(source_dimensions.x);
        const size_t height = static_cast<size_t>(source_dimensions.y);
        if (width > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()) ||
            height > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()) ||
            width > (std::numeric_limits<size_t>::max)() / height / 4) {
            return {};
        }

        const size_t byte_count = width * height * 4;
        if (source.size() < byte_count) {
            return {};
        }

        std::vector<Rml::byte> rgba(source.data(), source.data() + byte_count);
        UnpremultiplyTexture(rgba);

        GLuint texnum = 0;
        qglGenTextures(1, &texnum);
        if (!texnum) {
            return {};
        }

        GL_ForceTexture(TMU_TEXTURE, texnum);
        qglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                      static_cast<GLsizei>(width),
                      static_cast<GLsizei>(height),
                      0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ClampToEdge());
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ClampToEdge());

        c.texUploads++;
        c.textureUploadBytes += byte_count;

        return RegisterTexture(texnum, true);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        auto it = r_rmlui_textures.find(texture);
        if (it == r_rmlui_textures.end()) {
            return;
        }

        if (it->second.owned && it->second.texnum) {
            GL_Flush2D();
            for (GLuint &bound : gls.texnums) {
                if (bound == it->second.texnum) {
                    bound = 0;
                }
            }
            qglDeleteTextures(1, &it->second.texnum);
        }

        r_rmlui_textures.erase(it);
    }

    void EnableScissorRegion(bool enable) override
    {
        GL_Flush2D();

        if (enable) {
            qglEnable(GL_SCISSOR_TEST);
            draw.scissor = true;
            return;
        }

        qglDisable(GL_SCISSOR_TEST);
        draw.scissor = false;
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        GL_Flush2D();

        const int x = region.Left();
        const int y = region.Top();
        const int width = region.Width();
        const int height = region.Height();
        clipRect_t clip = {};
        clip.left = x;
        clip.top = y;
        clip.right = x + width;
        clip.bottom = y + height;

        if (clip.right <= clip.left || clip.bottom <= clip.top) {
            qglScissor(0, 0, 0, 0);
            return;
        }

        clipRect_t pixel_clip = {};
        if (!R_UIScaleClipToPixels(&clip, draw.base_scale, draw.scale,
                                   r_config.width, r_config.height,
                                   &pixel_clip)) {
            qglScissor(0, 0, 0, 0);
            return;
        }

        qglEnable(GL_SCISSOR_TEST);
        qglScissor(pixel_clip.left, r_config.height - pixel_clip.bottom,
                   pixel_clip.right - pixel_clip.left,
                   pixel_clip.bottom - pixel_clip.top);
        draw.scissor = true;
    }

private:
    struct R_RmlUiCompiledGeometry {
        std::vector<glVertexDesc2D_t> vertices;
        std::vector<glIndex_t> indices;
    };

    struct R_RmlUiTexture {
        GLuint texnum = 0;
        bool owned = false;
    };

    static GLenum ClampToEdge()
    {
        return (gl_config.caps & QGL_CAP_TEXTURE_CLAMP_TO_EDGE) ?
            GL_CLAMP_TO_EDGE : GL_CLAMP;
    }

    static void UnpremultiplyTexture(std::vector<Rml::byte> &rgba)
    {
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            const unsigned alpha = rgba[i + 3];
            if (!alpha) {
                rgba[i] = 0;
                rgba[i + 1] = 0;
                rgba[i + 2] = 0;
                continue;
            }

            rgba[i] = static_cast<Rml::byte>(
                min(255u, (static_cast<unsigned>(rgba[i]) * 255u) / alpha));
            rgba[i + 1] = static_cast<Rml::byte>(
                min(255u, (static_cast<unsigned>(rgba[i + 1]) * 255u) / alpha));
            rgba[i + 2] = static_cast<Rml::byte>(
                min(255u, (static_cast<unsigned>(rgba[i + 2]) * 255u) / alpha));
        }
    }

    static Rml::TextureHandle RegisterTexture(GLuint texnum, bool owned)
    {
        if (!texnum) {
            return {};
        }

        const Rml::TextureHandle handle = r_rmlui_next_texture_handle++;
        r_rmlui_textures.emplace(handle, R_RmlUiTexture{texnum, owned});
        return handle;
    }

    static GLuint TextureForHandle(Rml::TextureHandle texture)
    {
        if (!texture) {
            return TEXNUM_WHITE;
        }

        auto it = r_rmlui_textures.find(texture);
        if (it == r_rmlui_textures.end() || !it->second.texnum) {
            return TEXNUM_WHITE;
        }

        return it->second.texnum;
    }

    static std::unordered_map<Rml::TextureHandle, R_RmlUiTexture> r_rmlui_textures;
    static Rml::TextureHandle r_rmlui_next_texture_handle;
};

std::unordered_map<Rml::TextureHandle, R_RmlUiOpenGLRenderInterface::R_RmlUiTexture>
    R_RmlUiOpenGLRenderInterface::r_rmlui_textures;
Rml::TextureHandle R_RmlUiOpenGLRenderInterface::r_rmlui_next_texture_handle = 1;

static R_RmlUiOpenGLRenderInterface r_rmlui_opengl_render_interface;

#endif

#if UI_RML_HAS_RUNTIME && defined(RENDERER_VULKAN_LEGACY)

class R_RmlUiVulkanRenderInterface final : public Rml::RenderInterface {};

#endif

#if UI_RML_HAS_RUNTIME && defined(RENDERER_VULKAN_RTX)

class R_RmlUiRtxVkptRenderInterface final : public Rml::RenderInterface {};

#endif

extern "C" renderer_rmlui_family_t R_RmlUiRendererFamily(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return R_RENDERER_RMLUI_FAMILY_OPENGL;
#elif defined(RENDERER_VULKAN_LEGACY)
    return R_RENDERER_RMLUI_FAMILY_VULKAN;
#elif defined(RENDERER_VULKAN_RTX)
    return R_RENDERER_RMLUI_FAMILY_RTX_VKPT;
#else
    return R_RENDERER_RMLUI_FAMILY_NONE;
#endif
}

extern "C" const char *R_RmlUiRendererName(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return "OpenGL RmlUi render-interface primitives";
#elif defined(RENDERER_VULKAN_LEGACY)
    return "Vulkan RmlUi render-interface inactive";
#elif defined(RENDERER_VULKAN_RTX)
    return "RTX/vkpt RmlUi render-interface inactive";
#else
    return "none";
#endif
}

extern "C" bool R_RmlUiCanRender(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return true;
#else
    return false;
#endif
}

extern "C" void *R_RmlUiNativeRenderInterface(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return &r_rmlui_opengl_render_interface;
#else
    return NULL;
#endif
}
