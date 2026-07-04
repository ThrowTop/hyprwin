#include "overlay/outline/manager.hpp"

#include <memory>
#include <utility>

#include "log/log.hpp"
#include "overlay/outline/cache.hpp"

namespace hw::outline {

Manager::Manager(PublishFn publish) : m_publish(std::move(publish)) {
    m_worker = std::jthread([this](std::stop_token token) {
        SET_THREAD_NAME("SHD");
        WorkerLoop(token);
    });
}

Manager::~Manager() {
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_cv.notify_one();
    }
}

void Manager::ApplySettings(const Settings& settings) {
    std::filesystem::path next = settings.shader_path;

    Update update = UseBuiltInShader{};
    bool publishBuiltIn = false;

    {
        std::lock_guard lock(m_mutex);
        if (next.empty() && m_configuredPath.empty()) {
            return;
        }

        m_configuredPath = next;
        ++m_generation;

        if (next.empty()) {
            m_pending.reset();
            update = UseBuiltInShader{.generation = m_generation};
            publishBuiltIn = true;
        } else {
            m_pending = Request{.source_path = next, .generation = m_generation};
            m_cv.notify_one();
        }
    }

    if (publishBuiltIn && m_publish) {
        m_publish(std::move(update));
    }
}

void Manager::WorkerLoop(std::stop_token token) {
    Compiler compiler;

    const auto isStale = [this](std::uint64_t generation) {
        std::lock_guard lock(m_mutex);
        return generation != m_generation;
    };

    while (!token.stop_requested()) {
        Request request{};
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, token, [&] { return m_pending.has_value(); });
            if (token.stop_requested()) {
                break;
            }
            request = *m_pending;
            m_pending.reset();
        }

        const CompileRequest compileRequest{
          .source_path = request.source_path,
          .config_directory = request.source_path.parent_path(),
          .abi_version = kShaderAbiVersion,
#ifdef NDEBUG
          .debug = false,
#else
          .debug = true,
#endif
          .generation = request.generation,
        };

        if (cache::LookupResult cached = cache::Load(compileRequest); cached.hit) {
            if (isStale(compileRequest.generation)) {
                continue;
            }

            auto bytecode = std::make_shared<Bytecode>(std::move(cached.bytecode));
            LOG_TRACE("shader: cache hit generation={} path={} bytes={}", compileRequest.generation, request.source_path.string(), bytecode->bytes.size());
            if (m_publish) {
                m_publish(InstallPixelShader{
                  .bytecode = std::move(bytecode),
                  .generation = compileRequest.generation,
                });
            }
            continue;
        }

        LOG_DEBUG("shader: compiling {} generation={}", request.source_path.string(), request.generation);
        CompileResult result = compiler.Compile(compileRequest);

        if (isStale(result.generation)) {
            continue;
        }

        if (!result.success) {
            LOG_ERROR("shader: compile failed generation={} path={}\n{}", result.generation, result.source_path.string(), result.diagnostics);
            continue;
        }

        auto bytecode = std::make_shared<Bytecode>(std::move(result.bytecode));
        cache::Store(compileRequest, *bytecode);
        LOG_DEBUG("shader: compile succeeded generation={} bytes={}", result.generation, bytecode->bytes.size());
        if (m_publish) {
            m_publish(InstallPixelShader{
              .bytecode = std::move(bytecode),
              .generation = result.generation,
            });
        }
    }
}

} // namespace hw::outline
