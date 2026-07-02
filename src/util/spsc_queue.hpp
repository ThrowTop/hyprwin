#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // Intentional cache-line alignment on queue indexes.
#endif

namespace util {

template <typename T, std::size_t Size>
class SpscQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");
    static_assert(Size >= 2, "Size must leave room for one sentinel slot");

  public:
    static constexpr std::size_t capacity = Size - 1;

    bool push(const T& item) noexcept(noexcept(std::declval<T&>() = item)) {
        const std::size_t tail = m_tail.value.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & (Size - 1);
        if (next == m_head.value.load(std::memory_order_acquire)) {
            return false;
        }

        m_buffer[tail] = item;
        m_tail.value.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept(noexcept(item = std::declval<T&>())) {
        const std::size_t head = m_head.value.load(std::memory_order_relaxed);
        if (head == m_tail.value.load(std::memory_order_acquire)) {
            return false;
        }

        item = m_buffer[head];
        m_head.value.store((head + 1) & (Size - 1), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return m_head.value.load(std::memory_order_acquire) == m_tail.value.load(std::memory_order_acquire);
    }

  private:
    struct alignas(64) CacheLineAtomic {
        std::atomic<std::size_t> value{0};
    };

    std::array<T, Size> m_buffer{};
    CacheLineAtomic m_head{};
    CacheLineAtomic m_tail{};
};

} // namespace util

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
