// ScriptingSystemsTests: Unit tests for LuaEngine, sandboxing,
// execution limits, ECS bindings, transform and physics integration.

#include "Core/Scripting/LuaEngine.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    using namespace Core::Scripting;
    using namespace Core::ECS;

    Engine::Log::Init();

    // 1. Engine Initialization and Basic Execution
    {
        LuaEngine engine;
        CHECK(engine.IsInitialized());

        ScriptResult res = engine.Execute("return 40 + 2");
        CHECK(res.Success);
        CHECK(res.Output.find("42") != std::string::npos);

        ScriptResult printRes = engine.Execute("print('Hello from Lua')");
        CHECK(printRes.Success);
        CHECK(printRes.Output.find("Hello from Lua") != std::string::npos);
    }

    // 2. Sandboxing and Security Checks
    {
        LuaEngine engine;
        
        // Ensure os, io, debug, require, dofile, loadfile are nil / blocked
        ScriptResult res = engine.Execute("return os == nil and io == nil and debug == nil and require == nil and dofile == nil and loadfile == nil");
        CHECK(res.Success);
        CHECK(res.Output.find("true") != std::string::npos);

        // Disallow file access attempts
        ScriptResult ioRes = engine.Execute("if io then io.open('test.txt', 'w') end");
        CHECK(ioRes.Success); // Won't crash and won't execute io

        // Syntax error handling
        ScriptResult syntaxRes = engine.Execute("function invalid (");
        CHECK(!syntaxRes.Success);
        CHECK(!syntaxRes.Error.empty());
    }

    // 3. Execution Instruction Limits and Timeout
    {
        LuaEngine engine;
        ScriptLimits limits;
        limits.MaxInstructions = 5000;
        limits.MaxExecutionTimeMs = 500.0;
        engine.SetLimits(limits);

        // Infinite loop should trigger instruction limit
        ScriptResult loopRes = engine.Execute("while true do end");
        CHECK(!loopRes.Success);
        CHECK(loopRes.Error.find("exceeded") != std::string::npos);
    }

    // 4. Custom Function Registration
    {
        LuaEngine engine;
        
        static int customCalls = 0;
        auto customFunc = [](lua_State* L) -> int {
            customCalls++;
            double a = luaL_checknumber(L, 1);
            double b = luaL_checknumber(L, 2);
            lua_pushnumber(L, a * b);
            return 1;
        };

        engine.RegisterFunction("customMultiply", customFunc);

        ScriptResult res = engine.Execute("return Engine.customMultiply(6, 7)");
        CHECK(res.Success);
        CHECK(customCalls == 1);
        CHECK(res.Output.find("42") != std::string::npos);
    }

    // 5. ECS Scene and Entity Creation / Query
    {
        Scene scene("ScriptingTestScene");
        LuaEngine engine;
        engine.SetScene(&scene);

        // Create entity via Lua
        ScriptResult res = engine.Execute(R"(
            local heroId = Engine.createEntity("HeroPlayer")
            local monsterId = Engine.createEntity("GoblinMonster")
            return heroId ~= nil and monsterId ~= nil
        )");
        CHECK(res.Success);
        CHECK(scene.GetEntityCount() == 2);

        // Query entities
        ScriptResult queryRes = engine.Execute(R"(
            local count = Engine.getEntityCount()
            local hero = Engine.getEntity("HeroPlayer")
            local exists = Engine.entityExists(hero)
            return count == 2 and exists
        )");
        CHECK(queryRes.Success);
        CHECK(queryRes.Output.find("true") != std::string::npos);

        // Find by pattern
        ScriptResult findRes = engine.Execute(R"(
            local goblins = Engine.findByName("Goblin")
            return #goblins == 1
        )");
        CHECK(findRes.Success);
        CHECK(findRes.Output.find("true") != std::string::npos);
    }

    // 6. Transform Manipulation
    {
        Scene scene("TransformTestScene");
        LuaEngine engine;
        engine.SetScene(&scene);

        auto entity = scene.CreateEntity("Actor");
        auto& tf = entity.AddComponent<TransformComponent>();
        tf.Position = Core::Math::Vec3(1.0f, 2.0f, 3.0f);
        tf.Scale = Core::Math::Vec3(1.0f, 1.0f, 1.0f);

        // Query and modify transform in Lua
        ScriptResult res = engine.Execute(R"(
            local actor = Engine.getEntity("Actor")
            local pos = Engine.getPosition(actor)
            Engine.setPosition(actor, pos.x + 10.0, pos.y + 5.0, pos.z - 2.0)
            Engine.setScale(actor, 2.0, 2.0, 2.0)
            Engine.translate(actor, 1.0, 0.0, 0.0)
        )");
        CHECK(res.Success);

        auto& updatedTf = entity.GetComponent<TransformComponent>();
        CHECK(std::fabs(updatedTf.Position.x - 12.0f) < 0.001f);
        CHECK(std::fabs(updatedTf.Position.y - 7.0f) < 0.001f);
        CHECK(std::fabs(updatedTf.Position.z - 1.0f) < 0.001f);
        CHECK(std::fabs(updatedTf.Scale.x - 2.0f) < 0.001f);
        CHECK(updatedTf.IsDirty);
    }

    // 7. Light Component API
    {
        Scene scene("LightTestScene");
        LuaEngine engine;
        engine.SetScene(&scene);

        auto entity = scene.CreateEntity("Lamp");
        auto& light = entity.AddComponent<LightComponent>();
        light.Color = Core::Math::Vec3(0.0f);
        light.Intensity = 1.0f;
        light.Enabled = false;

        ScriptResult res = engine.Execute(R"(
            local lamp = Engine.getEntity("Lamp")
            Engine.setLightColor(lamp, 1.0, 0.5, 0.2)
            Engine.setLightIntensity(lamp, 5.5)
            Engine.setLightEnabled(lamp, true)
        )");
        CHECK(res.Success);

        auto& updatedLight = entity.GetComponent<LightComponent>();
        CHECK(std::fabs(updatedLight.Color.r - 1.0f) < 0.001f);
        CHECK(std::fabs(updatedLight.Color.g - 0.5f) < 0.001f);
        CHECK(std::fabs(updatedLight.Color.b - 0.2f) < 0.001f);
        CHECK(std::fabs(updatedLight.Intensity - 5.5f) < 0.001f);
        CHECK(updatedLight.Enabled == true);
    }

    // 8. Physics RigidBody API
    {
        Scene scene("PhysicsTestScene");
        LuaEngine engine;
        engine.SetScene(&scene);

        auto entity = scene.CreateEntity("Crate");
        auto& rb = entity.AddComponent<RigidBodyComponent>();
        rb.Mass = 10.0f;
        rb.LinearVelocity = Core::Math::Vec3(0.0f);
        rb.AccumulatedForce = Core::Math::Vec3(0.0f);

        ScriptResult res = engine.Execute(R"(
            local crate = Engine.getEntity("Crate")
            Engine.setMass(crate, 20.0)
            Engine.setVelocity(crate, 1.0, 2.0, 3.0)
            Engine.applyForce(crate, 100.0, 0.0, 0.0)
            Engine.applyImpulse(crate, 0.0, 40.0, 0.0)
        )");
        CHECK(res.Success);

        auto& updatedRb = entity.GetComponent<RigidBodyComponent>();
        CHECK(std::fabs(updatedRb.Mass - 20.0f) < 0.001f);
        CHECK(std::fabs(updatedRb.LinearVelocity.x - 1.0f) < 0.001f);
        // Impulse of 40 on mass 20 increases velocity.y by 2.0 -> total 4.0
        CHECK(std::fabs(updatedRb.LinearVelocity.y - 4.0f) < 0.001f);
        CHECK(std::fabs(updatedRb.LinearVelocity.z - 3.0f) < 0.001f);
        CHECK(std::fabs(updatedRb.AccumulatedForce.x - 100.0f) < 0.001f);
    }

    // 9. Entity Destruction via Lua
    {
        Scene scene("DestructionTestScene");
        LuaEngine engine;
        engine.SetScene(&scene);

        auto entity = scene.CreateEntity("Temporary");
        CHECK(scene.GetEntityCount() == 1);

        ScriptResult res = engine.Execute(R"(
            local temp = Engine.getEntity("Temporary")
            local destroyed = Engine.destroyEntity(temp)
            return destroyed
        )");
        CHECK(res.Success);
        CHECK(res.Output.find("true") != std::string::npos);
        CHECK(scene.GetEntityCount() == 0);
    }

    std::printf("ScriptingSystemsTests: ALL TESTS PASSED!\n");
    return 0;
}
