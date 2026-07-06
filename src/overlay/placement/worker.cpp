#include "overlay/placement/worker.hpp"

#include "log/log.hpp"

#include <utility>

namespace hw {

PlacementWorker::PlacementWorker(PublishFn publish) : m_publish(std::move(publish)) {
    m_worker = std::jthread([this](std::stop_token token) {
        SET_THREAD_NAME("W-POS");
        WorkerLoop(token);
    });
}

PlacementWorker::~PlacementWorker() {
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_cv.notify_one();
    }
}

void PlacementWorker::Submit(PlacementRequest request) noexcept {
    {
        std::lock_guard lock(m_mutex);
        m_pending = request;
    }
    m_cv.notify_one();
}

void PlacementWorker::WaitIdle() noexcept {
    std::unique_lock lock(m_mutex);
    m_idleCv.wait(lock, [this] { return !m_busy && !m_pending.has_value(); });
}

void PlacementWorker::WorkerLoop(std::stop_token token) noexcept {
    while (!token.stop_requested()) {
        PlacementRequest request{};
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, token, [this] { return m_pending.has_value(); });
            if (token.stop_requested()) {
                break;
            }
            request = *m_pending;
            m_pending.reset();
            m_busy = true;
        }

        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
        int width = request.rawRect.Width();
        int height = request.rawRect.Height();
        if (request.kind == PlacementKind::Park) {
            flags |= SWP_NOSIZE;
            width = 0;
            height = 0;
        }
        (void)SetWindowPos(request.target, nullptr, request.rawRect.x, request.rawRect.y, width, height, flags);

        if (m_publish) {
            m_publish(PlacementResult{
              .interactionId = request.interactionId,
              .kind = request.kind,
            });
        }

        {
            std::lock_guard lock(m_mutex);
            m_busy = false;
            if (!m_pending) {
                m_idleCv.notify_all();
            }
        }
    }
}

} // namespace hw
