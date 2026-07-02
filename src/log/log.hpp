#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define LOG_LEVEL_TRACE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_WARN 3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL_CRITICAL 5
#define LOG_LEVEL_OFF 6

#ifndef LOG_ACTIVE_LEVEL
#define LOG_ACTIVE_LEVEL LOG_LEVEL_TRACE
#endif

namespace logging {
enum class Level : int { Trace = 0, Debug, Info, Warn, Error, Critical, Off };

struct Options {
    bool console = true;
    std::string file_path;
    bool flush_each = false;
    std::size_t ring_capacity = 999;
};

struct Message {
    Level level{};
    std::string text;
    std::uint32_t thread_id = 0;
    std::string thread_name;
    std::chrono::system_clock::time_point timestamp;
};

struct RenderedMessage {
    std::uint64_t sequence = 0;
    Level level{};
    std::string line;
};

namespace detail {
    class LogViewer {
      public:
        ~LogViewer();
        LogViewer() = default;
        LogViewer(const LogViewer&) = delete;
        LogViewer& operator=(const LogViewer&) = delete;

        bool open(std::vector<RenderedMessage> initial, std::string log_file_path, std::function<void()> on_closed);
        void close();
        void close_async();
        [[nodiscard]] bool is_open() const noexcept;
        void append(RenderedMessage message);

      private:
        struct Impl;
        std::shared_ptr<Impl> m_impl;
    };

    void write_text(Level level, std::string text);
    void viewer_print(std::string text);
} // namespace detail

void init(Options options);
void shutdown();
void flush();
[[nodiscard]] std::string file_path();
bool open_viewer();
void close_viewer();
[[nodiscard]] bool viewer_open() noexcept;
[[nodiscard]] std::string make_run_log_file_path(std::string_view prefix = "hyprwin", std::size_t keep_files = 5);

#ifndef NDEBUG
void set_thread_name(std::string_view name);
[[nodiscard]] std::string_view get_thread_name() noexcept;
#else
inline void set_thread_name(std::string_view) {}
[[nodiscard]] inline std::string_view get_thread_name() noexcept {
    return {};
}
#endif

template <class... Args>
inline void write(Level level, std::format_string<Args...> fmt, Args&&... args) {
    detail::write_text(level, std::format(fmt, std::forward<Args>(args)...));
}
} // namespace logging

#ifndef NDEBUG
#define SET_THREAD_NAME(name) ::logging::set_thread_name(name)
#else
#define SET_THREAD_NAME(name) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_TRACE
#define LOG_TRACE(...) ::logging::write(::logging::Level::Trace, __VA_ARGS__)
#else
#define LOG_TRACE(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(...) ::logging::write(::logging::Level::Debug, __VA_ARGS__)
#else
#define LOG_DEBUG(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(...) ::logging::write(::logging::Level::Info, __VA_ARGS__)
#else
#define LOG_INFO(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(...) ::logging::write(::logging::Level::Warn, __VA_ARGS__)
#else
#define LOG_WARN(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(...) ::logging::write(::logging::Level::Error, __VA_ARGS__)
#else
#define LOG_ERROR(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_CRITICAL
#define LOG_CRITICAL(...) ::logging::write(::logging::Level::Critical, __VA_ARGS__)
#else
#define LOG_CRITICAL(...) (void)0
#endif