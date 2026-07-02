#include "log/log.hpp"

#include "win/native.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

#include <windows.h>

namespace logging {
namespace {

#ifndef NDEBUG
    thread_local std::string t_thread_name;
#endif

    const char* tag(Level level) {
        switch (level) {
            case Level::Trace:
                return "TRACE";
            case Level::Debug:
                return "DEBUG";
            case Level::Info:
                return "INFO";
            case Level::Warn:
                return "WARN";
            case Level::Error:
                return "ERROR";
            case Level::Critical:
                return "CRIT";
            case Level::Off:
                return "OFF";
        }
        return "OFF";
    }

    std::string prefix(const Message& message) {
        const auto local_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::floor<std::chrono::milliseconds>(message.timestamp)}.get_local_time();

        std::string out = std::format("{:%d %H:%M:%S} [{}] ", local_time, tag(message.level));
        if (message.thread_name.empty()) {
            out += std::format("[tid {}] ", message.thread_id);
        } else {
            out += std::format("[{}] ", message.thread_name);
        }
        return out;
    }

    class Logger {
      public:
        static Logger& instance() {
            static Logger logger;
            return logger;
        }

        void init(Options options) {
            bool open_after_init = false;
            std::unique_lock lock(m_mutex);
            if (m_running) {
                return;
            }

            m_options = std::move(options);
            m_fileWrote = false;
            m_sequence = 0;
            m_ring.clear();
            if (m_options.ring_capacity == 0) {
                m_options.ring_capacity = 1;
            }

            m_flushEach.store(m_options.flush_each, std::memory_order_relaxed);

            if (!m_options.file_path.empty()) {
                m_filePath = m_options.file_path;
                std::lock_guard file_lock(m_fileMutex);
                m_file.emplace(m_options.file_path, std::ios::out | std::ios::trunc | std::ios::binary);
                if (!*m_file) {
                    m_file.reset();
                    m_filePath.clear();
                }
            }

            open_after_init = m_options.console;
            m_running = true;
            m_initialized.store(true, std::memory_order_release);
            m_worker = std::jthread([this](std::stop_token token) {
                SET_THREAD_NAME("LOG");
                run(token);
            });
            lock.unlock();

            if (open_after_init) {
                open_viewer();
            }
        }

        void shutdown() {
            std::unique_lock lock(m_mutex);
            if (!m_running) {
                return;
            }

            m_running = false;
            lock.unlock();
            m_cv.notify_all();

            if (m_worker.joinable()) {
                m_worker.join();
            }

            m_viewer.close();

            std::lock_guard cleanup_lock(m_mutex);
            {
                std::lock_guard file_lock(m_fileMutex);
                if (m_file) {
                    m_file->flush();
                    m_file->close();
                    m_file.reset();
                }
            }
            if (!m_fileWrote && !m_filePath.empty()) {
                std::error_code ec;
                std::filesystem::remove(m_filePath, ec);
            }
            m_filePath.clear();
            m_ring.clear();

            m_initialized.store(false, std::memory_order_release);
            m_flushEach.store(false, std::memory_order_relaxed);
        }

        void flush() {
            std::lock_guard lock(m_fileMutex);
            if (m_file) {
                m_file->flush();
            }
        }

        [[nodiscard]] std::string file_path() const {
            std::lock_guard lock(m_mutex);
            return m_filePath;
        }

        bool open_viewer() {
            std::vector<RenderedMessage> initial;
            std::string current_file_path;
            {
                std::lock_guard lock(m_mutex);
                if (m_viewer.is_open()) {
                    return true;
                }
                initial.assign(m_ring.begin(), m_ring.end());
                current_file_path = m_filePath;
            }
            return m_viewer.open(std::move(initial), std::move(current_file_path), {});
        }

        void close_viewer() {
            m_viewer.close_async();
        }

        [[nodiscard]] bool viewer_open() const noexcept {
            return m_viewer.is_open();
        }

        void write_text(Level level, std::string text) {
            if (!m_initialized.load(std::memory_order_acquire)) {
                return;
            }

            Message message{
              .level = level,
              .text = std::move(text),
              .thread_id = GetCurrentThreadId(),
              .thread_name = std::string{get_thread_name()},
              .timestamp = std::chrono::system_clock::now(),
            };

            {
                std::lock_guard lock(m_mutex);
                m_queue.push_back(std::move(message));
            }
            m_cv.notify_one();
        }

        void viewer_print(std::string text) {
            if (!m_viewer.is_open()) {
                return;
            }
            m_viewer.append(RenderedMessage{
              .sequence = 0,
              .level = Level::Off,
              .line = std::move(text) + '\n',
            });
        }

      private:
        Logger() = default;

        void push_ring(RenderedMessage message) {
            if (m_ring.size() >= m_options.ring_capacity) {
                m_ring.pop_front();
            }
            m_ring.push_back(std::move(message));
        }

        void run(std::stop_token token) {
            std::deque<Message> local;
            for (;;) {
                {
                    std::unique_lock lock(m_mutex);
                    m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });
                    if (!m_running && m_queue.empty()) {
                        break;
                    }
                    local.swap(m_queue);
                }

                while (!local.empty()) {
                    const Message& message = local.front();
                    RenderedMessage rendered{
                      .sequence = ++m_sequence,
                      .level = message.level,
                      .line = prefix(message) + message.text + '\n',
                    };

                    {
                        std::lock_guard lock(m_mutex);
                        push_ring(rendered);
                    }

                    if (m_viewer.is_open()) {
                        m_viewer.append(rendered);
                    }

                    {
                        std::lock_guard file_lock(m_fileMutex);
                        if (m_file) {
                            m_file->write(rendered.line.data(), static_cast<std::streamsize>(rendered.line.size()));
                            m_fileWrote = true;
                            if (m_flushEach.load(std::memory_order_acquire) || message.level >= Level::Warn) {
                                m_file->flush();
                            }
                        }
                    }

                    local.pop_front();
                }

                if (token.stop_requested()) {
                    break;
                }
            }
        }

        std::atomic<bool> m_initialized{false};
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<Message> m_queue;
        std::deque<RenderedMessage> m_ring;
        std::jthread m_worker;
        mutable std::mutex m_fileMutex;
        std::optional<std::ofstream> m_file;
        std::string m_filePath;
        Options m_options;
        std::atomic<bool> m_running{false};
        std::atomic<bool> m_flushEach{false};
        bool m_fileWrote = false;
        std::uint64_t m_sequence = 0;
        detail::LogViewer m_viewer;
    };

} // namespace

std::string make_run_log_file_path(std::string_view prefix, std::size_t keep_files) {
    namespace fs = std::filesystem;

    const fs::path module_directory = win::GetModuleDirectory();
    fs::path directory = (module_directory.empty() ? fs::current_path() : module_directory) / "logs";
    std::error_code ec;
    fs::create_directories(directory, ec);
    ec.clear();

    const std::string prefix_text(prefix);
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto local_time = std::chrono::zoned_time{std::chrono::current_zone(), now}.get_local_time();
    const std::string stem = std::format("{}_{:%Y%m%d_%H%M%S}", prefix_text, local_time);

    fs::path path = directory / (stem + ".log");
    for (int suffix = 1; fs::exists(path) && suffix < 100; ++suffix) {
        path = directory / (stem + '_' + std::to_string(suffix) + ".log");
    }

    struct LogFile {
        fs::path path;
        fs::file_time_type write_time{};
    };

    std::vector<LogFile> logs;
    const std::string file_prefix = prefix_text + '_';
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        const fs::path entry_path = entry.path();
        const std::string filename = entry_path.filename().string();
        if (!filename.starts_with(file_prefix) || entry_path.extension() != ".log") {
            continue;
        }
        logs.push_back(LogFile{.path = entry_path, .write_time = entry.last_write_time(ec)});
        ec.clear();
    }

    std::ranges::sort(logs, [](const LogFile& lhs, const LogFile& rhs) {
        if (lhs.write_time == rhs.write_time) {
            return lhs.path.filename().string() > rhs.path.filename().string();
        }
        return lhs.write_time > rhs.write_time;
    });

    const std::size_t old_files_to_keep = keep_files > 0 ? keep_files - 1 : 0;
    for (std::size_t i = old_files_to_keep; i < logs.size(); ++i) {
        fs::remove(logs[i].path, ec);
        ec.clear();
    }

    return path.string();
}

void init(Options options) {
    Logger::instance().init(std::move(options));
}
void shutdown() {
    Logger::instance().shutdown();
}
void flush() {
    Logger::instance().flush();
}
std::string file_path() {
    return Logger::instance().file_path();
}
bool open_viewer() {
    return Logger::instance().open_viewer();
}
void close_viewer() {
    Logger::instance().close_viewer();
}
bool viewer_open() noexcept {
    return Logger::instance().viewer_open();
}

namespace detail {
    void write_text(Level level, std::string text) {
        Logger::instance().write_text(level, std::move(text));
    }
    void viewer_print(std::string text) {
        Logger::instance().viewer_print(std::move(text));
    }
} // namespace detail

#ifndef NDEBUG
namespace detail {
    void set_os_thread_name(std::string_view name) noexcept {
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        static const auto set_thread_description = reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
        if (!set_thread_description) {
            return;
        }

        std::wstring wide_name;
        wide_name.reserve(name.size());
        for (const char c : name) {
            wide_name.push_back(static_cast<unsigned char>(c));
        }
        (void)set_thread_description(GetCurrentThread(), wide_name.c_str());
    }
} // namespace detail

void set_thread_name(std::string_view name) {
    t_thread_name = name;
    detail::set_os_thread_name(name);
    LOG_TRACE("thread name set: tid={} name={}", GetCurrentThreadId(), t_thread_name);
}

std::string_view get_thread_name() noexcept {
    return t_thread_name;
}
#endif

} // namespace logging
