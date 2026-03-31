#pragma once

// MCP Tool Base Class
// Abstract interface for implementing MCP tools that can be called by AI agents

#include "MCPTypes.h"
#include <string>
#include <functional>
#include <memory>

namespace Core {

// Forward declarations
namespace ECS {
    class Scene;
}

namespace MCP {

    // Forward declaration
    class MCPServer;

    // Base class for all MCP tools
    class MCPTool {
    public:
        MCPTool(const std::string& name, const std::string& description)
            : m_Name(name), m_Description(description) {}
        
        virtual ~MCPTool() = default;

        // Get tool definition for listing
        virtual ToolDefinition GetDefinition() const {
            ToolDefinition def;
            def.Name = m_Name;
            def.Description = m_Description;
            def.InputSchema = GetInputSchema();
            return def;
        }

        // Get the input schema for this tool
        virtual ToolInputSchema GetInputSchema() const = 0;

        // Execute the tool with given arguments
        // Returns the result of the tool execution
        virtual ToolResult Execute(const Json& arguments, ECS::Scene* scene) = 0;

        // Accessors
        const std::string& GetName() const { return m_Name; }
        const std::string& GetDescription() const { return m_Description; }

        // Optional: Validate arguments before execution
        virtual bool ValidateArguments(const Json& arguments, std::string& errorMessage) const {
            (void)arguments;
            (void)errorMessage;
            return true;
        }

        // Optional: Check if tool requires scene access
        virtual bool RequiresScene() const { return true; }

        // Optional: Check if tool is available in current context
        virtual bool IsAvailable() const { return true; }

    protected:
        std::string m_Name;
        std::string m_Description;
    };

    // Smart pointer type for tools
    using MCPToolPtr = std::shared_ptr<MCPTool>;

    // Tool factory function type
    using MCPToolFactory = std::function<MCPToolPtr()>;

    // Helper macro for creating simple tools with lambda execution
    class LambdaTool : public MCPTool {
    public:
        using ExecuteFunc = std::function<ToolResult(const Json&, ECS::Scene*)>;

        LambdaTool(const std::string& name, 
                   const std::string& description,
                   const ToolInputSchema& schema,
                   ExecuteFunc executeFunc,
                   bool requiresScene = true)
            : MCPTool(name, description)
            , m_Schema(schema)
            , m_ExecuteFunc(std::move(executeFunc))
            , m_RequiresScene(requiresScene) {}

        ToolInputSchema GetInputSchema() const override { return m_Schema; }
        
        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            return m_ExecuteFunc(arguments, scene);
        }

        bool RequiresScene() const override { return m_RequiresScene; }

    private:
        ToolInputSchema m_Schema;
        ExecuteFunc m_ExecuteFunc;
        bool m_RequiresScene;
    };

    // Helper to create a lambda-based tool
    inline MCPToolPtr CreateLambdaTool(
        const std::string& name,
        const std::string& description,
        const ToolInputSchema& schema,
        LambdaTool::ExecuteFunc executeFunc,
        bool requiresScene = true)
    {
        return std::make_shared<LambdaTool>(name, description, schema, 
                                             std::move(executeFunc), requiresScene);
    }

} // namespace MCP
} // namespace Core
