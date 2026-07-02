#pragma once

#include <format>

#include <windows.h>

namespace vec {

struct i4;

struct i2 {
    int x = 0;
    int y = 0;

    [[nodiscard]] i2 Offset(int dx, int dy) const noexcept {
        return i2{x + dx, y + dy};
    }

    [[nodiscard]] i2 operator+(const i2& o) const noexcept {
        return i2{x + o.x, y + o.y};
    }
    [[nodiscard]] i2 operator-(const i2& o) const noexcept {
        return i2{x - o.x, y - o.y};
    }
    [[nodiscard]] i2 operator*(int s) const noexcept {
        return i2{x * s, y * s};
    }
    [[nodiscard]] i2 operator/(int s) const noexcept {
        return i2{x / s, y / s};
    }

    i2& operator+=(const i2& o) noexcept {
        x += o.x;
        y += o.y;
        return *this;
    }
    i2& operator-=(const i2& o) noexcept {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    i2& operator*=(int s) noexcept {
        x *= s;
        y *= s;
        return *this;
    }
    i2& operator/=(int s) noexcept {
        x /= s;
        y /= s;
        return *this;
    }

    bool operator==(const i2&) const = default;

    [[nodiscard]] POINT ToWin32() const noexcept {
        return POINT{x, y};
    }

    [[nodiscard]] static i2 FromWin32(POINT point) noexcept {
        return i2{point.x, point.y};
    }
};

struct i4 {
    int x = 0;
    int y = 0;
    int x2 = 0;
    int y2 = 0;

    [[nodiscard]] int Width() const noexcept {
        return x2 - x;
    }
    [[nodiscard]] int Height() const noexcept {
        return y2 - y;
    }

    [[nodiscard]] i2 Pos() const noexcept {
        return i2{x, y};
    }
    [[nodiscard]] i2 Size() const noexcept {
        return i2{Width(), Height()};
    }
    [[nodiscard]] i2 Center() const noexcept {
        return i2{x + Width() / 2, y + Height() / 2};
    }

    [[nodiscard]] i4 Offset(int dx, int dy) const noexcept {
        return i4{x + dx, y + dy, x2 + dx, y2 + dy};
    }

    [[nodiscard]] i4 Inset(int amount) const noexcept {
        return Inset(amount, amount);
    }

    [[nodiscard]] i4 Inset(int ix, int iy) const noexcept {
        return i4{x + ix, y + iy, x2 - ix, y2 - iy};
    }

    [[nodiscard]] i4 WithPos(int nx, int ny) const noexcept {
        return i4{nx, ny, nx + Width(), ny + Height()};
    }

    [[nodiscard]] i4 WithSize(int width, int height) const noexcept {
        return i4{x, y, x + width, y + height};
    }

    [[nodiscard]] i4 operator+(const i4& o) const noexcept {
        return i4{x + o.x, y + o.y, x2 + o.x2, y2 + o.y2};
    }
    [[nodiscard]] i4 operator-(const i4& o) const noexcept {
        return i4{x - o.x, y - o.y, x2 - o.x2, y2 - o.y2};
    }
    [[nodiscard]] i4 operator*(int s) const noexcept {
        return i4{x * s, y * s, x2 * s, y2 * s};
    }
    [[nodiscard]] i4 operator/(int s) const noexcept {
        return i4{x / s, y / s, x2 / s, y2 / s};
    }

    i4& operator+=(const i4& o) noexcept {
        x += o.x;
        y += o.y;
        x2 += o.x2;
        y2 += o.y2;
        return *this;
    }
    i4& operator-=(const i4& o) noexcept {
        x -= o.x;
        y -= o.y;
        x2 -= o.x2;
        y2 -= o.y2;
        return *this;
    }
    i4& operator*=(int s) noexcept {
        x *= s;
        y *= s;
        x2 *= s;
        y2 *= s;
        return *this;
    }
    i4& operator/=(int s) noexcept {
        x /= s;
        y /= s;
        x2 /= s;
        y2 /= s;
        return *this;
    }

    bool operator==(const i4&) const = default;

    [[nodiscard]] RECT ToWin32() const noexcept {
        return RECT{
          static_cast<LONG>(x),
          static_cast<LONG>(y),
          static_cast<LONG>(x2),
          static_cast<LONG>(y2),
        };
    }

    [[nodiscard]] static i4 FromWin32(const RECT& rect) noexcept {
        return i4{
          static_cast<int>(rect.left),
          static_cast<int>(rect.top),
          static_cast<int>(rect.right),
          static_cast<int>(rect.bottom),
        };
    }

    [[nodiscard]] static i4 FromXywh(int nx, int ny, int width, int height) noexcept {
        return i4{nx, ny, nx + width, ny + height};
    }
};

} // namespace vec

template <>
struct std::formatter<vec::i2> : std::formatter<std::string_view> {
    auto format(const vec::i2& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "point({}, {})", v.x, v.y);
    }
};

template <>
struct std::formatter<vec::i4> : std::formatter<std::string_view> {
    auto format(const vec::i4& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "rect({}, {}, {}, {})", v.x, v.y, v.x2, v.y2);
    }
};
