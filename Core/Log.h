#pragma once

#include <memory>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/ringbuffer_sink.h>

namespace Engine {
    class Log {
    public:
        static void Init();

        inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
        inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

        // Most recent formatted log lines, newest last. Backed by an in-memory ring
        // buffer so tooling (MCP, editor console) can read engine output without
        // scraping stdout. Returns empty before Init().
        static std::vector<std::string> GetRecentMessages(size_t count = 200);
        static size_t GetRecentMessageCapacity();

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
        static std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> s_RingBuffer;
    };
}

// Core log macros
#define ENGINE_CORE_TRACE(...)    ::Engine::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define ENGINE_CORE_INFO(...)     ::Engine::Log::GetCoreLogger()->info(__VA_ARGS__)
#define ENGINE_CORE_WARN(...)     ::Engine::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define ENGINE_CORE_ERROR(...)    ::Engine::Log::GetCoreLogger()->error(__VA_ARGS__)
#define ENGINE_CORE_CRITICAL(...) ::Engine::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define ENGINE_TRACE(...)         ::Engine::Log::GetClientLogger()->trace(__VA_ARGS__)
#define ENGINE_INFO(...)          ::Engine::Log::GetClientLogger()->info(__VA_ARGS__)
#define ENGINE_WARN(...)          ::Engine::Log::GetClientLogger()->warn(__VA_ARGS__)
#define ENGINE_ERROR(...)         ::Engine::Log::GetClientLogger()->error(__VA_ARGS__)
#define ENGINE_CRITICAL(...)      ::Engine::Log::GetClientLogger()->critical(__VA_ARGS__)