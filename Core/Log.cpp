#include "Log.h"
#include "Core/Profile.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>
#include <vector>

namespace Engine {

    namespace {
        // Enough history for an agent or console to see what led up to a failure
        // without holding the whole session in memory.
        constexpr size_t RING_BUFFER_CAPACITY = 2048;
    }

    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> Log::s_RingBuffer;

    void Log::Init() {
        PROFILE_FUNCTION();
        spdlog::set_pattern("%^[%T] %n: %v%$");

        // Second sink keeps recent output readable in-process; stdout alone cannot
        // be queried by tooling once it has scrolled away.
        s_RingBuffer = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(RING_BUFFER_CAPACITY);
        s_RingBuffer->set_pattern("[%T] [%l] %n: %v");

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("%^[%T] %n: %v%$");

        const std::vector<spdlog::sink_ptr> sinks{ consoleSink, s_RingBuffer };

        s_CoreLogger = std::make_shared<spdlog::logger>("ENGINE", sinks.begin(), sinks.end());
        s_CoreLogger->set_level(spdlog::level::trace);

        s_ClientLogger = std::make_shared<spdlog::logger>("APP", sinks.begin(), sinks.end());
        s_ClientLogger->set_level(spdlog::level::trace);
    }

    std::vector<std::string> Log::GetRecentMessages(size_t count) {
        if (!s_RingBuffer) {
            return {};
        }
        return s_RingBuffer->last_formatted(std::min(count, RING_BUFFER_CAPACITY));
    }

    size_t Log::GetRecentMessageCapacity() {
        return RING_BUFFER_CAPACITY;
    }

}
