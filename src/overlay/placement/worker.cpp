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

        PlacementResult result{
          .interactionId = request.interactionId,
          .kind = request.kind,
          .target = request.target,
          .rawRect = request.rawRect,
        };

        if (!IsWindow(request.target) || request.rawRect.Width() <= 0 || request.rawRect.Height() <= 0) {
            result.error = ERROR_INVALID_WINDOW_HANDLE;
        } else {
            UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
            int width = request.rawRect.Width();
            int height = request.rawRect.Height();
            if (request.kind == PlacementKind::Park) {
                flags |= SWP_NOSIZE;
                width = 0;
                height = 0;
            }
            if (SetWindowPos(request.target,
                  nullptr,
                  request.rawRect.x,
                  request.rawRect.y,
                  width,
                  height,
                  flags)) {
                result.success = true;
            } else {
                result.error = GetLastError();
            }
        }

        if (request.kind == PlacementKind::Park) {
            RECT actualRawRect{};
            if (GetWindowRect(request.target, &actualRawRect)) {
                result.actualRawRect = vec::i4::FromWin32(actualRawRect);
                result.actualRawRectAvailable = true;
            }
        }

        if (m_publish) {
            m_publish(result);
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
