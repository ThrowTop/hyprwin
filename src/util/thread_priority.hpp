#pragma once

#include <avrt.h>
#include <windows.h>

namespace util {

class ScopedThreadPriorityBoost {
  public:
    ScopedThreadPriorityBoost() noexcept {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        DWORD taskIndex = 0;
        m_mmcssHandle = AvSetMmThreadCharacteristicsW(L"Games", &taskIndex);

        THREAD_POWER_THROTTLING_STATE state{};
        state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        state.StateMask = 0;
        SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
    }

    ScopedThreadPriorityBoost(const ScopedThreadPriorityBoost&) = delete;
    ScopedThreadPriorityBoost& operator=(const ScopedThreadPriorityBoost&) = delete;

    ~ScopedThreadPriorityBoost() {
        if (m_mmcssHandle) {
            AvRevertMmThreadCharacteristics(m_mmcssHandle);
        }
    }

  private:
    HANDLE m_mmcssHandle = nullptr;
};

} // namespace util
