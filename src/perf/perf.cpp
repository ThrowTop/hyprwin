#include "perf/perf.hpp"

#if HYPRWIN_ENABLE_PERF

#include "log/log.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include <windows.h>

namespace perf {
namespace {
    constexpr std::size_t kCounterCount = static_cast<std::size_t>(CounterId::Count);
    constexpr std::size_t kBucketCount = 35;

    struct Counter {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> total_ns{0};
        std::atomic<std::uint64_t> max_ns{0};
        std::array<std::atomic<std::uint64_t>, kBucketCount> buckets{};
    };

    std::array<Counter, kCounterCount> g_counters;
    std::atomic<std::int64_t> g_frequency{0};

    std::int64_t Frequency() noexcept {
        std::int64_t cached = g_frequency.load(std::memory_order_acquire);
        if (cached > 0) {
            return cached;
        }

        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency)) {
            return 0;
        }

        cached = frequency.QuadPart;
        g_frequency.store(cached, std::memory_order_release);
        return cached;
    }

    std::uint64_t TicksToNs(std::int64_t ticks) noexcept {
        const std::int64_t frequency = Frequency();
        if (frequency <= 0 || ticks <= 0) {
            return 0;
        }

        return static_cast<std::uint64_t>((static_cast<long double>(ticks) * 1000000000.0L) / static_cast<long double>(frequency));
    }

    std::size_t BucketForNs(std::uint64_t ns) noexcept {
        std::uint64_t upper = 1;
        for (std::size_t bucket = 0; bucket + 1 < kBucketCount; ++bucket) {
            if (ns <= upper) {
                return bucket;
            }
            upper <<= 1;
        }
        return kBucketCount - 1;
    }

    std::uint64_t BucketUpperNs(std::size_t bucket) noexcept {
        if (bucket + 1 >= kBucketCount) {
            return 1ull << (kBucketCount - 1);
        }
        return 1ull << bucket;
    }

    void Record(CounterId id, std::uint64_t ns) noexcept {
        auto& counter = g_counters[static_cast<std::size_t>(id)];
        counter.count.fetch_add(1, std::memory_order_relaxed);
        counter.total_ns.fetch_add(ns, std::memory_order_relaxed);
        counter.buckets[BucketForNs(ns)].fetch_add(1, std::memory_order_relaxed);

        std::uint64_t current = counter.max_ns.load(std::memory_order_relaxed);
        while (ns > current && !counter.max_ns.compare_exchange_weak(current, ns, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    const char* CounterName(CounterId id) noexcept {
        switch (id) {
            case CounterId::KeyboardHook:
                return "keyboard_hook";
            case CounterId::MouseHook:
                return "mouse_hook";
            case CounterId::OverlayFrame:
                return "overlay_frame";
            case CounterId::BindDispatch:
                return "bind_dispatch";
            case CounterId::Count:
                break;
        }
        return "unknown";
    }

    std::uint64_t PercentileNs(const std::array<std::uint64_t, kBucketCount>& buckets, std::uint64_t count, std::uint64_t pct) noexcept {
        if (count == 0) {
            return 0;
        }

        const std::uint64_t target = std::max<std::uint64_t>(1, (count * pct + 99) / 100);
        std::uint64_t seen = 0;
        for (std::size_t bucket = 0; bucket < buckets.size(); ++bucket) {
            seen += buckets[bucket];
            if (seen >= target) {
                return BucketUpperNs(bucket);
            }
        }
        return 0;
    }

    std::string FormatDuration(std::uint64_t ns) {
        char buffer[32]{};
        if (ns == 0) {
            return "0ns";
        }
        if (ns < 1000) {
            std::snprintf(buffer, sizeof(buffer), "%lluns", static_cast<unsigned long long>(ns));
        } else if (ns < 1000000) {
            std::snprintf(buffer, sizeof(buffer), "%.2fus", static_cast<double>(ns) / 1000.0);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.3fms", static_cast<double>(ns) / 1000000.0);
        }
        return buffer;
    }

    void DumpCounter(CounterId id) {
        const auto& counter = g_counters[static_cast<std::size_t>(id)];
        const std::uint64_t count = counter.count.load(std::memory_order_relaxed);
        if (count == 0) {
            return;
        }

        std::array<std::uint64_t, kBucketCount> buckets{};
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            buckets[i] = counter.buckets[i].load(std::memory_order_relaxed);
        }

        const std::uint64_t total_ns = counter.total_ns.load(std::memory_order_relaxed);
        const std::uint64_t avg_ns = total_ns / count;
        const std::uint64_t max_ns = counter.max_ns.load(std::memory_order_relaxed);
        const std::uint64_t p50_ns = PercentileNs(buckets, count, 50);
        const std::uint64_t p95_ns = PercentileNs(buckets, count, 95);
        const std::uint64_t p99_ns = PercentileNs(buckets, count, 99);

        const double avg_fps = avg_ns > 0 ? 1e9 / static_cast<double>(avg_ns) : 0.0;
        LOG_WARN("[PERF] {} count={} avg={} ({:.1f}fps) p50<={} p95<={} p99<={} max={}",
          CounterName(id),
          count,
          FormatDuration(avg_ns),
          avg_fps,
          FormatDuration(p50_ns),
          FormatDuration(p95_ns),
          FormatDuration(p99_ns),
          FormatDuration(max_ns));
    }

} // namespace

ScopeTimer::ScopeTimer(CounterId id) noexcept : m_id(id) {
    LARGE_INTEGER now{};
    if (QueryPerformanceCounter(&now)) {
        m_startTicks = now.QuadPart;
    }
}

ScopeTimer::~ScopeTimer() noexcept {
    if (m_startTicks == 0) {
        return;
    }

    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
        return;
    }

    Record(m_id, TicksToNs(now.QuadPart - m_startTicks));
}

void DumpSummary() noexcept {
    LOG_WARN("[PERF] performance counters enabled; summary uses optimized instrumentation");
    DumpCounter(CounterId::KeyboardHook);
    DumpCounter(CounterId::MouseHook);
    DumpCounter(CounterId::OverlayFrame);
    DumpCounter(CounterId::BindDispatch);
}

} // namespace perf

#endif
