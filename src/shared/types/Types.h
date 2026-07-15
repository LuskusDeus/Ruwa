// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C O M M O N   T Y P E S
// ==========================================================================

#ifndef RUWA_CORE_COMMON_TYPES_H
#define RUWA_CORE_COMMON_TYPES_H

#include <cstdint>
#include <cmath>

namespace aether {

// ==========================================================================
//   V E C T O R 2
// ==========================================================================

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vector2() = default;
    constexpr Vector2(float x_, float y_)
        : x(x_)
        , y(y_)
    {
    }

    constexpr Vector2 operator+(const Vector2& other) const { return { x + other.x, y + other.y }; }

    constexpr Vector2 operator-(const Vector2& other) const { return { x - other.x, y - other.y }; }

    constexpr Vector2 operator*(float scalar) const { return { x * scalar, y * scalar }; }

    constexpr Vector2 operator/(float scalar) const { return { x / scalar, y / scalar }; }

    constexpr Vector2& operator+=(const Vector2& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr Vector2& operator-=(const Vector2& other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    constexpr Vector2& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    constexpr bool operator==(const Vector2& other) const { return x == other.x && y == other.y; }

    float length() const { return std::sqrt(x * x + y * y); }

    Vector2 normalized() const
    {
        float len = length();
        if (len > 0.0f) {
            return { x / len, y / len };
        }
        return { 0.0f, 0.0f };
    }

    static constexpr Vector2 zero() { return { 0.0f, 0.0f }; }
    static constexpr Vector2 one() { return { 1.0f, 1.0f }; }
};

// ==========================================================================
//   R E C T
// ==========================================================================

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_)
        : x(x_)
        , y(y_)
        , width(w_)
        , height(h_)
    {
    }

    constexpr Vector2 position() const { return { x, y }; }
    constexpr Vector2 size() const { return { width, height }; }
    constexpr Vector2 center() const { return { x + width / 2, y + height / 2 }; }

    constexpr float left() const { return x; }
    constexpr float right() const { return x + width; }
    constexpr float top() const { return y; }
    constexpr float bottom() const { return y + height; }

    constexpr bool contains(const Vector2& point) const
    {
        return point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height;
    }

    constexpr bool intersects(const Rect& other) const
    {
        return !(
            other.x > right() || other.right() < x || other.y > bottom() || other.bottom() < y);
    }
};

// ==========================================================================
//   C O L O R
// ==========================================================================

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_)
        , g(g_)
        , b(b_)
        , a(a_)
    {
    }

    static constexpr Color fromRGB(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
    {
        return { r_ / 255.0f, g_ / 255.0f, b_ / 255.0f, a_ / 255.0f };
    }

    static constexpr Color fromHex(uint32_t hex)
    {
        return fromRGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF,
            (hex >> 24) & 0xFF ? (hex >> 24) & 0xFF : 255);
    }

    // Predefined colors
    static constexpr Color white() { return { 1.0f, 1.0f, 1.0f, 1.0f }; }
    static constexpr Color black() { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
    static constexpr Color red() { return { 1.0f, 0.0f, 0.0f, 1.0f }; }
    static constexpr Color green() { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
    static constexpr Color blue() { return { 0.0f, 0.0f, 1.0f, 1.0f }; }
    static constexpr Color transparent() { return { 0.0f, 0.0f, 0.0f, 0.0f }; }
};

// ==========================================================================
//   E X T E N T 2 D
// ==========================================================================

struct Extent2D {
    uint32_t width = 0;
    uint32_t height = 0;

    constexpr Extent2D() = default;
    constexpr Extent2D(uint32_t w, uint32_t h)
        : width(w)
        , height(h)
    {
    }
};

} // namespace aether

#endif // RUWA_CORE_COMMON_TYPES_H
