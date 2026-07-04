#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include <windows.h>

#include "util/geometry.hpp"

namespace hw {

enum class PlacementKind {
    Park,
    Live,
    Commit,
    Restore,
};

struct PlacementRequest {
    std::uint64_t interactionId = 0;
    PlacementKind kind = PlacementKind::Live;
    HWND target = nullptr;
    vec::i4 rawRect{};
};

struct PlacementResult {
    std::uint64_t interactionId = 0;
    PlacementKind kind = PlacementKind::Live;
    HWND target = nullptr;
    vec::i4 rawRect{};
    vec::i4 actualRawRect{};
    bool actualRawRectAvailable = false;
    bool success = false;
    DWORD error = ERROR_SUCCESS;
};

class PlacementWorker {
  public:
    using PublishFn = std::function<void(const PlacementResult&)>;

    explicit PlacementWorker(PublishFn publish);
    PlacementWorker(const PlacementWorker&) = delete;
    PlacementWorker& operator=(const PlacementWorker&) = delete;
    ~PlacementWorker();

    void Submit(PlacementRequest request) noexcept;
    void WaitIdle() noexcept;

  private:
    void WorkerLoop(std::stop_token token) noexcept;

    PublishFn m_publish;
    std::mutex m_mutex;
    std::condition_variable_any m_cv;
    std::condition_variable m_idleCv;
    std::optional<PlacementRequest> m_pending;
    bool m_busy = false;
    std::jthread m_worker;
};

} // namespace hw
