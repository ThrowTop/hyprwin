#pragma once

#include <atomic>
#include <cstdint>

#include <windows.h>

#include "util/geometry.hpp"

namespace hw {

class AtomicRect {
  public:
    AtomicRect() noexcept = default;

    explicit AtomicRect(const vec::i4& rect) noexcept {
        Store(rect);
    }

    void Store(const vec::i4& rect, std::memory_order order = std::memory_order_release) noexcept {
        const std::uint32_t version = m_version.load(std::memory_order_relaxed);
        m_version.store(version + 1U, std::memory_order_relaxed);
        m_leftTop.store(Pack(static_cast<LONG>(rect.x), static_cast<LONG>(rect.y)), std::memory_order_relaxed);
        m_rightBottom.store(Pack(static_cast<LONG>(rect.x2), static_cast<LONG>(rect.y2)), std::memory_order_relaxed);
        m_version.store(version + 2U, order);
    }

    vec::i4 Load(std::memory_order order = std::memory_order_acquire) const noexcept {
        std::uint32_t before = 0;
        std::uint32_t after = 0;
        std::uint64_t leftTop = 0;
        std::uint64_t rightBottom = 0;

        do {
            before = m_version.load(order);
            leftTop = m_leftTop.load(std::memory_order_relaxed);
            rightBottom = m_rightBottom.load(std::memory_order_relaxed);
            after = m_version.load(order);
        } while ((before & 1U) != 0U || before != after);

        const POINT lt = Unpack(leftTop);
        const POINT rb = Unpack(rightBottom);
        return vec::i4{
          static_cast<int>(lt.x),
          static_cast<int>(lt.y),
          static_cast<int>(rb.x),
          static_cast<int>(rb.y),
        };
    }

  private:
    static std::uint64_t Pack(LONG x, LONG y) noexcept {
        return static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32U);
    }

    static POINT Unpack(std::uint64_t value) noexcept {
        const auto x = static_cast<LONG>(static_cast<std::int32_t>(value & 0xFFFF'FFFFULL));
        const auto y = static_cast<LONG>(static_cast<std::int32_t>(value >> 32U));
        return POINT{x, y};
    }

    std::atomic<std::uint64_t> m_leftTop{0};
    std::atomic<std::uint64_t> m_rightBottom{0};
    std::atomic<std::uint32_t> m_version{0};
};

} // namespace hw
