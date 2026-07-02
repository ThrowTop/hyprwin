#pragma once

#include <cstdint>

#ifndef HYPRWIN_ENABLE_PERF
#define HYPRWIN_ENABLE_PERF 0
#endif

namespace perf {

enum class CounterId : std::uint8_t {
    KeyboardHook,
    MouseHook,
    OverlayFrame,
    BindDispatch,
    Count,
};

#if HYPRWIN_ENABLE_PERF

class ScopeTimer {
  public:
    explicit ScopeTimer(CounterId id) noexcept;
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
    ~ScopeTimer() noexcept;

  private:
    CounterId m_id;
    std::int64_t m_startTicks = 0;
};

void DumpSummary() noexcept;

#else

class ScopeTimer {
  public:
    explicit ScopeTimer(CounterId) noexcept {}
};

inline void DumpSummary() noexcept {}

#endif

} // namespace perf

#if HYPRWIN_ENABLE_PERF
#define HW_PERF_JOIN_IMPL(a, b) a##b
#define HW_PERF_JOIN(a, b) HW_PERF_JOIN_IMPL(a, b)
#define HW_PERF_SCOPE(id)                                                                                                                                                          \
    ::perf::ScopeTimer HW_PERF_JOIN(_hw_perf_scope_, __LINE__) {                                                                                                                   \
        id                                                                                                                                                                         \
    }
#else
#define HW_PERF_SCOPE(id) (void)0
#endif
