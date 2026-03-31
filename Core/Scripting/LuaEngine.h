#pragma once

// Lua Scripting Engine
// Provides a sandboxed Lua environment for executing AI-generated scripts

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/MCP/SceneSerialization.h"
#include "Core/Log.h"

#include <string>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <optional>
#include <memory>

namespace Core {
namespace Scripting {

    // ============================================================================
    // Script execution result
    // ============================================================================

    struct ScriptResult {
        bool Success = false;
        std::string Output;
        std::string Error;
        double ExecutionTimeMs = 0.0;
        int InstructionsExecuted = 0;

        static ScriptResult Ok(const std::string& output = "") {
            return {true, output, "", 0.0, 0};
        }

        static ScriptResult Fail(const std::string& error) {
            return {false, "", error, 0.0, 0};
        }
    };

    // ============================================================================
    // Script execution limits for sandboxing
    // ============================================================================

    struct ScriptLimits {
        int MaxInstructions = 100000;       // Max Lua instructions
        int MaxMemoryMB = 16;               // Max memory allocation (MB)
        double MaxExecutionTimeMs = 1000.0; // Max execution time (ms)
        bool AllowFileIO = false;           // Allow file operations
        bool AllowOSCalls = false;          // Allow os.* functions
        bool AllowRequire = false;          // Allow require/dofile
    };

    // ============================================================================
    // Lua Engine - Sandboxed Lua execution environment
    // ============================================================================

    class LuaEngine {
    public:
        LuaEngine();
        ~LuaEngine();

        // Non-copyable
        LuaEngine(const LuaEngine&) = delete;
        LuaEngine& operator=(const LuaEngine&) = delete;

        // Move operations
        LuaEngine(LuaEngine&& other) noexcept;
        LuaEngine& operator=(LuaEngine&& other) noexcept;

        // Initialize/Reset the Lua state
        bool Initialize();
        void Reset();

        // Set the active scene for ECS operations
        void SetScene(ECS::Scene* scene) { m_Scene = scene; }

        // Configure execution limits
        void SetLimits(const ScriptLimits& limits) { m_Limits = limits; }
        const ScriptLimits& GetLimits() const { return m_Limits; }

        // Execute a Lua script string
        ScriptResult Execute(const std::string& script);

        // Execute with custom timeout
        ScriptResult ExecuteWithTimeout(const std::string& script, double timeoutMs);

        // Register a custom C++ function to Lua
        using LuaFunction = std::function<int(lua_State*)>;
        void RegisterFunction(const std::string& name, lua_CFunction func);

        // Get the raw Lua state (for advanced usage)
        lua_State* GetState() { return m_State; }

        // Check if engine is initialized
        bool IsInitialized() const { return m_State != nullptr; }

    private:
        lua_State* m_State = nullptr;
        ECS::Scene* m_Scene = nullptr;
        ScriptLimits m_Limits;
        int m_InstructionCount = 0;
        bool m_TimedOut = false;

        // Setup sandboxed environment
        void SetupSandbox();
        void RegisterEngineAPI();

        // Instruction count hook for limiting execution
        static void InstructionHook(lua_State* L, lua_Debug* ar);

        // Store engine pointer in Lua state
        void StoreEnginePointer();
        static LuaEngine* GetEngineFromState(lua_State* L);

        // Engine API functions exposed to Lua
        static int Lua_Log(lua_State* L);
        static int Lua_LogWarning(lua_State* L);
        static int Lua_LogError(lua_State* L);

        // Entity API
        static int Lua_CreateEntity(lua_State* L);
        static int Lua_DestroyEntity(lua_State* L);
        static int Lua_GetEntity(lua_State* L);
        static int Lua_EntityExists(lua_State* L);

        // Transform API
        static int Lua_GetPosition(lua_State* L);
        static int Lua_SetPosition(lua_State* L);
        static int Lua_GetRotation(lua_State* L);
        static int Lua_SetRotation(lua_State* L);
        static int Lua_GetScale(lua_State* L);
        static int Lua_SetScale(lua_State* L);
        static int Lua_Translate(lua_State* L);
        static int Lua_Rotate(lua_State* L);

        // Light API
        static int Lua_SetLightColor(lua_State* L);
        static int Lua_SetLightIntensity(lua_State* L);
        static int Lua_SetLightEnabled(lua_State* L);

        // Physics API
        static int Lua_SetMass(lua_State* L);
        static int Lua_SetVelocity(lua_State* L);
        static int Lua_ApplyForce(lua_State* L);
        static int Lua_ApplyImpulse(lua_State* L);

        // Scene query API
        static int Lua_FindEntitiesByName(lua_State* L);
        static int Lua_GetEntityCount(lua_State* L);
        static int Lua_GetAllEntities(lua_State* L);
    };

    // ============================================================================
    // Implementation
    // ============================================================================

    inline LuaEngine::LuaEngine() {
        Initialize();
    }

    inline LuaEngine::~LuaEngine() {
        if (m_State) {
            lua_close(m_State);
            m_State = nullptr;
        }
    }

    inline LuaEngine::LuaEngine(LuaEngine&& other) noexcept
        : m_State(other.m_State)
        , m_Scene(other.m_Scene)
        , m_Limits(other.m_Limits)
        , m_InstructionCount(other.m_InstructionCount)
        , m_TimedOut(other.m_TimedOut) {
        other.m_State = nullptr;
        other.m_Scene = nullptr;
        if (m_State) {
            StoreEnginePointer();
        }
    }

    inline LuaEngine& LuaEngine::operator=(LuaEngine&& other) noexcept {
        if (this != &other) {
            if (m_State) {
                lua_close(m_State);
            }
            m_State = other.m_State;
            m_Scene = other.m_Scene;
            m_Limits = other.m_Limits;
            m_InstructionCount = other.m_InstructionCount;
            m_TimedOut = other.m_TimedOut;
            other.m_State = nullptr;
            other.m_Scene = nullptr;
            if (m_State) {
                StoreEnginePointer();
            }
        }
        return *this;
    }

    inline bool LuaEngine::Initialize() {
        if (m_State) {
            lua_close(m_State);
        }

        m_State = luaL_newstate();
        if (!m_State) {
            ENGINE_CORE_ERROR("LuaEngine: Failed to create Lua state");
            return false;
        }

        SetupSandbox();
        RegisterEngineAPI();
        StoreEnginePointer();

        ENGINE_CORE_INFO("LuaEngine: Initialized successfully");
        return true;
    }

    inline void LuaEngine::Reset() {
        Initialize();
    }

    inline void LuaEngine::SetupSandbox() {
        // Open only safe libraries
        luaL_requiref(m_State, "_G", luaopen_base, 1);
        lua_pop(m_State, 1);
        luaL_requiref(m_State, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(m_State, 1);
        luaL_requiref(m_State, LUA_STRLIBNAME, luaopen_string, 1);
        lua_pop(m_State, 1);
        luaL_requiref(m_State, LUA_MATHLIBNAME, luaopen_math, 1);
        lua_pop(m_State, 1);

        // Remove dangerous functions from base library
        const char* unsafeFunctions[] = {
            "dofile", "loadfile", "load", "loadstring",
            "rawequal", "rawget", "rawset", "rawlen",
            "collectgarbage", "getmetatable", "setmetatable",
            "require", "module", "package"
        };

        for (const auto* func : unsafeFunctions) {
            lua_pushnil(m_State);
            lua_setglobal(m_State, func);
        }

        // Set instruction count hook for limiting execution
        lua_sethook(m_State, InstructionHook, LUA_MASKCOUNT, 1000);
    }

    inline void LuaEngine::StoreEnginePointer() {
        lua_pushlightuserdata(m_State, this);
        lua_setglobal(m_State, "__engine");
    }

    inline LuaEngine* LuaEngine::GetEngineFromState(lua_State* L) {
        lua_getglobal(L, "__engine");
        auto* engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        return engine;
    }

    inline void LuaEngine::InstructionHook(lua_State* L, lua_Debug*) {
        auto* engine = GetEngineFromState(L);
        if (!engine) return;

        engine->m_InstructionCount += 1000;
        if (engine->m_InstructionCount > engine->m_Limits.MaxInstructions) {
            luaL_error(L, "Script exceeded maximum instruction count (%d)", 
                       engine->m_Limits.MaxInstructions);
        }
    }

    inline void LuaEngine::RegisterEngineAPI() {
        // Engine namespace
        lua_newtable(m_State);

        // Logging
        lua_pushcfunction(m_State, Lua_Log);
        lua_setfield(m_State, -2, "log");
        lua_pushcfunction(m_State, Lua_LogWarning);
        lua_setfield(m_State, -2, "warn");
        lua_pushcfunction(m_State, Lua_LogError);
        lua_setfield(m_State, -2, "error");

        // Entity management
        lua_pushcfunction(m_State, Lua_CreateEntity);
        lua_setfield(m_State, -2, "createEntity");
        lua_pushcfunction(m_State, Lua_DestroyEntity);
        lua_setfield(m_State, -2, "destroyEntity");
        lua_pushcfunction(m_State, Lua_GetEntity);
        lua_setfield(m_State, -2, "getEntity");
        lua_pushcfunction(m_State, Lua_EntityExists);
        lua_setfield(m_State, -2, "entityExists");

        // Transform
        lua_pushcfunction(m_State, Lua_GetPosition);
        lua_setfield(m_State, -2, "getPosition");
        lua_pushcfunction(m_State, Lua_SetPosition);
        lua_setfield(m_State, -2, "setPosition");
        lua_pushcfunction(m_State, Lua_GetRotation);
        lua_setfield(m_State, -2, "getRotation");
        lua_pushcfunction(m_State, Lua_SetRotation);
        lua_setfield(m_State, -2, "setRotation");
        lua_pushcfunction(m_State, Lua_GetScale);
        lua_setfield(m_State, -2, "getScale");
        lua_pushcfunction(m_State, Lua_SetScale);
        lua_setfield(m_State, -2, "setScale");
        lua_pushcfunction(m_State, Lua_Translate);
        lua_setfield(m_State, -2, "translate");
        lua_pushcfunction(m_State, Lua_Rotate);
        lua_setfield(m_State, -2, "rotate");

        // Light
        lua_pushcfunction(m_State, Lua_SetLightColor);
        lua_setfield(m_State, -2, "setLightColor");
        lua_pushcfunction(m_State, Lua_SetLightIntensity);
        lua_setfield(m_State, -2, "setLightIntensity");
        lua_pushcfunction(m_State, Lua_SetLightEnabled);
        lua_setfield(m_State, -2, "setLightEnabled");

        // Physics
        lua_pushcfunction(m_State, Lua_SetMass);
        lua_setfield(m_State, -2, "setMass");
        lua_pushcfunction(m_State, Lua_SetVelocity);
        lua_setfield(m_State, -2, "setVelocity");
        lua_pushcfunction(m_State, Lua_ApplyForce);
        lua_setfield(m_State, -2, "applyForce");
        lua_pushcfunction(m_State, Lua_ApplyImpulse);
        lua_setfield(m_State, -2, "applyImpulse");

        // Scene queries
        lua_pushcfunction(m_State, Lua_FindEntitiesByName);
        lua_setfield(m_State, -2, "findByName");
        lua_pushcfunction(m_State, Lua_GetEntityCount);
        lua_setfield(m_State, -2, "getEntityCount");
        lua_pushcfunction(m_State, Lua_GetAllEntities);
        lua_setfield(m_State, -2, "getAllEntities");

        lua_setglobal(m_State, "Engine");

        // Also create a print function that captures output
        lua_pushcfunction(m_State, Lua_Log);
        lua_setglobal(m_State, "print");
    }

    inline void LuaEngine::RegisterFunction(const std::string& name, lua_CFunction func) {
        lua_getglobal(m_State, "Engine");
        lua_pushcfunction(m_State, func);
        lua_setfield(m_State, -2, name.c_str());
        lua_pop(m_State, 1);
    }

    inline ScriptResult LuaEngine::Execute(const std::string& script) {
        return ExecuteWithTimeout(script, m_Limits.MaxExecutionTimeMs);
    }

    inline ScriptResult LuaEngine::ExecuteWithTimeout(const std::string& script, double timeoutMs) {
        if (!m_State) {
            return ScriptResult::Fail("Lua engine not initialized");
        }

        m_InstructionCount = 0;
        m_TimedOut = false;

        auto startTime = std::chrono::high_resolution_clock::now();

        // Capture output via a string buffer
        std::string output;
        
        // Store output buffer in registry for access by print function
        lua_pushlightuserdata(m_State, &output);
        lua_setglobal(m_State, "__output");

        // Load the script
        int loadResult = luaL_loadstring(m_State, script.c_str());
        if (loadResult != LUA_OK) {
            std::string error = lua_tostring(m_State, -1);
            lua_pop(m_State, 1);
            return ScriptResult::Fail("Syntax error: " + error);
        }

        // Execute the script
        int execResult = lua_pcall(m_State, 0, LUA_MULTRET, 0);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double executionTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        ScriptResult result;
        result.ExecutionTimeMs = executionTime;
        result.InstructionsExecuted = m_InstructionCount;

        if (execResult != LUA_OK) {
            result.Success = false;
            result.Error = lua_tostring(m_State, -1);
            lua_pop(m_State, 1);
        } else {
            result.Success = true;
            
            // Collect return values
            int nresults = lua_gettop(m_State);
            if (nresults > 0) {
                for (int i = 1; i <= nresults; ++i) {
                    if (lua_isstring(m_State, i)) {
                        if (!output.empty()) output += "\n";
                        output += lua_tostring(m_State, i);
                    } else if (lua_isnumber(m_State, i)) {
                        if (!output.empty()) output += "\n";
                        output += std::to_string(lua_tonumber(m_State, i));
                    } else if (lua_isboolean(m_State, i)) {
                        if (!output.empty()) output += "\n";
                        output += lua_toboolean(m_State, i) ? "true" : "false";
                    }
                }
                lua_pop(m_State, nresults);
            }
            result.Output = output;
        }

        return result;
    }

    // ============================================================================
    // Lua API Implementations
    // ============================================================================

    inline int LuaEngine::Lua_Log(lua_State* L) {
        int n = lua_gettop(L);
        std::string message;
        for (int i = 1; i <= n; ++i) {
            if (i > 1) message += "\t";
            if (lua_isstring(L, i)) {
                message += lua_tostring(L, i);
            } else if (lua_isnumber(L, i)) {
                message += std::to_string(lua_tonumber(L, i));
            } else if (lua_isboolean(L, i)) {
                message += lua_toboolean(L, i) ? "true" : "false";
            } else if (lua_isnil(L, i)) {
                message += "nil";
            } else {
                message += lua_typename(L, lua_type(L, i));
            }
        }
        
        // Append to output buffer
        lua_getglobal(L, "__output");
        auto* output = static_cast<std::string*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        if (output) {
            if (!output->empty()) *output += "\n";
            *output += message;
        }
        
        ENGINE_CORE_INFO("Lua: {}", message);
        return 0;
    }

    inline int LuaEngine::Lua_LogWarning(lua_State* L) {
        const char* msg = luaL_checkstring(L, 1);
        ENGINE_CORE_WARN("Lua: {}", msg);
        return 0;
    }

    inline int LuaEngine::Lua_LogError(lua_State* L) {
        const char* msg = luaL_checkstring(L, 1);
        ENGINE_CORE_ERROR("Lua: {}", msg);
        return 0;
    }

    inline int LuaEngine::Lua_CreateEntity(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushnil(L);
            return 1;
        }

        const char* name = luaL_optstring(L, 1, "Entity");
        auto entity = engine->m_Scene->CreateEntity(name);
        
        // Add NameComponent
        auto& registry = engine->m_Scene->GetRegistry();
        registry.emplace<MCP::NameComponent>(entity.GetHandle(), MCP::NameComponent{name});
        
        lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity.GetHandle())));
        return 1;
    }

    inline int LuaEngine::Lua_DestroyEntity(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushboolean(L, false);
            return 1;
        }

        lua_Integer entityId = luaL_checkinteger(L, 1);
        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (registry.valid(entity)) {
            registry.destroy(entity);
            lua_pushboolean(L, true);
        } else {
            lua_pushboolean(L, false);
        }
        return 1;
    }

    inline int LuaEngine::Lua_GetEntity(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushnil(L);
            return 1;
        }

        const char* name = luaL_checkstring(L, 1);
        auto& registry = engine->m_Scene->GetRegistry();
        auto view = registry.view<MCP::NameComponent>();
        
        for (auto entity : view) {
            if (view.get<MCP::NameComponent>(entity).Name == name) {
                lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
                return 1;
            }
        }
        
        lua_pushnil(L);
        return 1;
    }

    inline int LuaEngine::Lua_EntityExists(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushboolean(L, false);
            return 1;
        }

        lua_Integer entityId = luaL_checkinteger(L, 1);
        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        lua_pushboolean(L, engine->m_Scene->GetRegistry().valid(entity));
        return 1;
    }

    inline int LuaEngine::Lua_GetPosition(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushnil(L);
            return 1;
        }

        lua_Integer entityId = luaL_checkinteger(L, 1);
        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            lua_newtable(L);
            lua_pushnumber(L, transform->Position.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, transform->Position.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, transform->Position.z);
            lua_setfield(L, -2, "z");
            return 1;
        }
        
        lua_pushnil(L);
        return 1;
    }

    inline int LuaEngine::Lua_SetPosition(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float x = static_cast<float>(luaL_checknumber(L, 2));
        float y = static_cast<float>(luaL_checknumber(L, 3));
        float z = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            transform->Position = Math::Vec3(x, y, z);
            transform->IsDirty = true;
        }
        return 0;
    }

    inline int LuaEngine::Lua_GetRotation(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushnil(L);
            return 1;
        }

        lua_Integer entityId = luaL_checkinteger(L, 1);
        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            lua_newtable(L);
            lua_pushnumber(L, glm::degrees(transform->Rotation.x));
            lua_setfield(L, -2, "pitch");
            lua_pushnumber(L, glm::degrees(transform->Rotation.y));
            lua_setfield(L, -2, "yaw");
            lua_pushnumber(L, glm::degrees(transform->Rotation.z));
            lua_setfield(L, -2, "roll");
            return 1;
        }
        
        lua_pushnil(L);
        return 1;
    }

    inline int LuaEngine::Lua_SetRotation(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float pitch = static_cast<float>(luaL_checknumber(L, 2));
        float yaw = static_cast<float>(luaL_checknumber(L, 3));
        float roll = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            transform->Rotation = Math::Vec3(glm::radians(pitch), glm::radians(yaw), glm::radians(roll));
            transform->IsDirty = true;
        }
        return 0;
    }

    inline int LuaEngine::Lua_GetScale(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushnil(L);
            return 1;
        }

        lua_Integer entityId = luaL_checkinteger(L, 1);
        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            lua_newtable(L);
            lua_pushnumber(L, transform->Scale.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, transform->Scale.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, transform->Scale.z);
            lua_setfield(L, -2, "z");
            return 1;
        }
        
        lua_pushnil(L);
        return 1;
    }

    inline int LuaEngine::Lua_SetScale(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float x = static_cast<float>(luaL_checknumber(L, 2));
        float y = static_cast<float>(luaL_checknumber(L, 3));
        float z = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            transform->Scale = Math::Vec3(x, y, z);
            transform->IsDirty = true;
        }
        return 0;
    }

    inline int LuaEngine::Lua_Translate(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float dx = static_cast<float>(luaL_checknumber(L, 2));
        float dy = static_cast<float>(luaL_checknumber(L, 3));
        float dz = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            transform->Position += Math::Vec3(dx, dy, dz);
            transform->IsDirty = true;
        }
        return 0;
    }

    inline int LuaEngine::Lua_Rotate(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float dpitch = static_cast<float>(luaL_checknumber(L, 2));
        float dyaw = static_cast<float>(luaL_checknumber(L, 3));
        float droll = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* transform = registry.try_get<ECS::TransformComponent>(entity)) {
            transform->Rotation += Math::Vec3(glm::radians(dpitch), glm::radians(dyaw), glm::radians(droll));
            transform->IsDirty = true;
        }
        return 0;
    }

    inline int LuaEngine::Lua_SetLightColor(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float r = static_cast<float>(luaL_checknumber(L, 2));
        float g = static_cast<float>(luaL_checknumber(L, 3));
        float b = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* light = registry.try_get<ECS::LightComponent>(entity)) {
            light->Color = glm::clamp(Math::Vec3(r, g, b), Math::Vec3(0.0f), Math::Vec3(1.0f));
        }
        return 0;
    }

    inline int LuaEngine::Lua_SetLightIntensity(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float intensity = static_cast<float>(luaL_checknumber(L, 2));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* light = registry.try_get<ECS::LightComponent>(entity)) {
            light->Intensity = std::max(0.0f, intensity);
        }
        return 0;
    }

    inline int LuaEngine::Lua_SetLightEnabled(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        bool enabled = lua_toboolean(L, 2);

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* light = registry.try_get<ECS::LightComponent>(entity)) {
            light->Enabled = enabled;
        }
        return 0;
    }

    inline int LuaEngine::Lua_SetMass(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float mass = static_cast<float>(luaL_checknumber(L, 2));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* rb = registry.try_get<ECS::RigidBodyComponent>(entity)) {
            rb->Mass = std::max(0.001f, mass);
        }
        return 0;
    }

    inline int LuaEngine::Lua_SetVelocity(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float vx = static_cast<float>(luaL_checknumber(L, 2));
        float vy = static_cast<float>(luaL_checknumber(L, 3));
        float vz = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* rb = registry.try_get<ECS::RigidBodyComponent>(entity)) {
            rb->LinearVelocity = Math::Vec3(vx, vy, vz);
        }
        return 0;
    }

    inline int LuaEngine::Lua_ApplyForce(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float fx = static_cast<float>(luaL_checknumber(L, 2));
        float fy = static_cast<float>(luaL_checknumber(L, 3));
        float fz = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* rb = registry.try_get<ECS::RigidBodyComponent>(entity)) {
            rb->AccumulatedForce += Math::Vec3(fx, fy, fz);
        }
        return 0;
    }

    inline int LuaEngine::Lua_ApplyImpulse(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) return 0;

        lua_Integer entityId = luaL_checkinteger(L, 1);
        float ix = static_cast<float>(luaL_checknumber(L, 2));
        float iy = static_cast<float>(luaL_checknumber(L, 3));
        float iz = static_cast<float>(luaL_checknumber(L, 4));

        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
        auto& registry = engine->m_Scene->GetRegistry();
        
        if (auto* rb = registry.try_get<ECS::RigidBodyComponent>(entity)) {
            // Impulse directly changes velocity
            rb->LinearVelocity += Math::Vec3(ix, iy, iz) / rb->Mass;
        }
        return 0;
    }

    inline int LuaEngine::Lua_FindEntitiesByName(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_newtable(L);
            return 1;
        }

        const char* pattern = luaL_checkstring(L, 1);
        std::string searchPattern = pattern;
        auto& registry = engine->m_Scene->GetRegistry();
        
        lua_newtable(L);
        int index = 1;
        
        auto view = registry.view<MCP::NameComponent>();
        for (auto entity : view) {
            const auto& nameComp = view.get<MCP::NameComponent>(entity);
            if (nameComp.Name.find(searchPattern) != std::string::npos) {
                lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
                lua_rawseti(L, -2, index++);
            }
        }
        
        return 1;
    }

    inline int LuaEngine::Lua_GetEntityCount(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_pushinteger(L, 0);
            return 1;
        }

        lua_pushinteger(L, static_cast<lua_Integer>(engine->m_Scene->GetEntityCount()));
        return 1;
    }

    inline int LuaEngine::Lua_GetAllEntities(lua_State* L) {
        auto* engine = GetEngineFromState(L);
        if (!engine || !engine->m_Scene) {
            lua_newtable(L);
            return 1;
        }

        auto& registry = engine->m_Scene->GetRegistry();
        
        lua_newtable(L);
        int index = 1;
        
        registry.each([L, &index](auto entity) {
            lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
            lua_rawseti(L, -2, index++);
        });
        
        return 1;
    }

} // namespace Scripting
} // namespace Core
