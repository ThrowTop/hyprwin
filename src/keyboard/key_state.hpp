#pragma once

#include <array>
#include <cstdint>

#include <windows.h>

namespace hw {

class KeyState {
  public:
    [[nodiscard]] bool IsSet(UINT vk) const noexcept {
        return (m_bits[vk >> 6] & (1ull << (vk & 63))) != 0;
    }

    void Set(UINT vk) noexcept {
        m_bits[vk >> 6] |= (1ull << (vk & 63));
    }

    void Clear(UINT vk) noexcept {
        m_bits[vk >> 6] &= ~(1ull << (vk & 63));
    }

    void ClearAll() noexcept {
        for (std::uint64_t& bits : m_bits) {
            bits = 0;
        }
    }

  private:
    std::array<std::uint64_t, 4> m_bits{};
};

} // namespace hw
