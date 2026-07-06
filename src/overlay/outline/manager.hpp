#pragma once

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "config/settings.hpp"
#include "overlay/outline/compiler.hpp"

namespace hw::outline {

class Manager {
  public:
    using PublishFn = std::function<void(Update)>;

    explicit Manager(PublishFn publish);
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    ~Manager();

    void ApplySettings(const Settings& settings);

  private:
    struct Request {
        std::filesystem::path source_path;
        std::uint64_t generation = 0;
    };

    void WorkerLoop(std::stop_token token);

    PublishFn m_publish;
    std::mutex m_mutex;
    std::condition_variable_any m_cv;
    std::optional<Request> m_pending;
    std::filesystem::path m_configuredPath;
    std::uint64_t m_generation = 0;
    std::jthread m_worker;
};

} // namespace hw::outline
