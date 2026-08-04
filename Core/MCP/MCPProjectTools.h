#pragma once

// MCP Project Control Tools
//
// MCPDevTools covers the *runtime* loop (pause, step, log, profile, test).
// This family covers the *project* loop: producing platform builds, packaging
// artifacts, persisting scenes to disk, and inspecting the project tree. Together
// they let an agent drive development end to end without shell access.
//
// Everything here is deliberately narrow: no arbitrary command execution, and every
// path is confined to the project root. An MCP endpoint that could run shell
// commands would be a remote code execution primitive on the developer's machine.

#include "MCPTool.h"
#include "SceneSerialization.h"
#include "Core/ECS/Scene.h"
#include "Core/Build/BuildOrchestrator.h"
#include "Core/Build/DedicatedServerBuildService.h"
#include "Core/Build/StoreSubmissionPackager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace Core {
namespace MCP {

    namespace ProjectToolsDetail {

        // Every path argument is resolved against, and confined to, the project
        // root. Without this an agent could read or write anywhere the engine
        // process can reach.
        inline std::filesystem::path ProjectRoot() {
            std::error_code ec;
            auto root = std::filesystem::current_path(ec);
            return ec ? std::filesystem::path(".") : root;
        }

        inline bool ResolveInProject(const std::string& relative,
                                     std::filesystem::path& out,
                                     std::string& error) {
            if (relative.empty()) {
                error = "path must not be empty";
                return false;
            }

            const std::filesystem::path candidate = std::filesystem::path(relative);
            if (candidate.is_absolute()) {
                error = "path must be relative to the project root";
                return false;
            }

            std::error_code ec;
            const auto root = std::filesystem::weakly_canonical(ProjectRoot(), ec);
            auto resolved = std::filesystem::weakly_canonical(ProjectRoot() / candidate, ec);
            if (ec) {
                error = "path could not be resolved";
                return false;
            }

            // Reject anything that escapes the root via .. or a symlink.
            const auto rootStr = root.lexically_normal().generic_string();
            const auto resolvedStr = resolved.lexically_normal().generic_string();
            if (resolvedStr.rfind(rootStr, 0) != 0) {
                error = "path escapes the project root";
                return false;
            }

            out = std::move(resolved);
            return true;
        }

        inline Json StagesToJson(const std::vector<Build::BuildStageStatus>& stages) {
            Json array = Json::array();
            for (const auto& stage : stages) {
                array.push_back({
                    {"name", stage.StageName},
                    {"state", static_cast<int>(stage.State)},
                    {"durationMs", stage.DurationMs},
                    {"message", stage.Message}
                });
            }
            return array;
        }

    } // namespace ProjectToolsDetail

    // ========================================================================
    // BuildForPlatform
    // ========================================================================
    class BuildForPlatformTool : public MCPTool {
    public:
        BuildForPlatformTool()
            : MCPTool("BuildForPlatform",
                      "Run the engine's platform build pipeline for a target platform and "
                      "configuration, returning per-stage status, a reproducible build id, "
                      "and the toolchain signature. Use this to produce a shippable build "
                      "after changing engine or content, and read the stage list to see "
                      "which step failed.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"platform", {{"type", "string"},
                    {"description", "Target platform identifier, e.g. 'windows', 'linux'"}}},
                {"configuration", {{"type", "string"},
                    {"enum", Json::array({"Debug", "Release", "Shipping"})},
                    {"description", "Build configuration (default 'Release')"}}},
                {"buildProfile", {{"type", "string"},
                    {"description", "Named build profile (default 'default')"}}},
                {"cookProfile", {{"type", "string"},
                    {"description", "Named asset cook profile (default 'default')"}}},
                {"includeSymbols", {{"type", "boolean"},
                    {"description", "Emit debug symbols alongside the build"}}},
                {"outputDirectory", {{"type", "string"},
                    {"description", "Project-relative output directory (default 'build/platform')"}}}
            };
            schema.Required = {"platform"};
            return schema;
        }

        bool RequiresScene() const override { return false; }

        bool ValidateArguments(const Json& args, std::string& errorMessage) const override {
            if (!args.contains("platform") || !args["platform"].is_string() ||
                args["platform"].get<std::string>().empty()) {
                errorMessage = "'platform' is required and must be a non-empty string";
                return false;
            }
            if (args.contains("outputDirectory") && args["outputDirectory"].is_string()) {
                std::filesystem::path resolved;
                std::string pathError;
                if (!ProjectToolsDetail::ResolveInProject(
                        args["outputDirectory"].get<std::string>(), resolved, pathError)) {
                    errorMessage = "'outputDirectory' invalid: " + pathError;
                    return false;
                }
            }
            return true;
        }

        ToolResult Execute(const Json& args, ECS::Scene*) override {
            Build::PlatformBuildRequest request;
            request.Platform = args["platform"].get<std::string>();
            request.Configuration = JsonUtils::GetOr<std::string>(args, "configuration", "Release");
            request.BuildProfile = JsonUtils::GetOr<std::string>(args, "buildProfile", "default");
            request.CookProfile = JsonUtils::GetOr<std::string>(args, "cookProfile", "default");
            request.IncludeSymbols = JsonUtils::GetOr<bool>(args, "includeSymbols", false);

            std::filesystem::path outputDir;
            std::string pathError;
            const std::string requested =
                JsonUtils::GetOr<std::string>(args, "outputDirectory", "build/platform");
            if (!ProjectToolsDetail::ResolveInProject(requested, outputDir, pathError)) {
                return ToolResult::Error("Invalid outputDirectory: " + pathError);
            }
            request.OutputDirectory = outputDir;

            auto outcome = Build::BuildForPlatformTarget(request);
            if (!outcome.Ok) {
                return ToolResult::Error("Platform build failed to run: " + outcome.Error);
            }

            const auto& build = outcome.Value;
            Json result;
            result["platform"] = build.Platform;
            result["configuration"] = build.Configuration;
            result["succeeded"] = build.Succeeded;
            result["buildId"] = build.BuildId;
            result["gitCommitHash"] = build.GitCommitHash;
            result["profileHash"] = build.ProfileHash;
            result["cookManifestHash"] = build.CookManifestHash;
            result["toolchainSignature"] = build.ToolchainSignature;
            result["outputDirectory"] = build.OutputDirectory.string();
            result["totalDurationMs"] = build.TotalDurationMs;
            result["stages"] = ProjectToolsDetail::StagesToJson(build.Stages);

            ToolResult toolResult = ToolResult::SuccessJson(result);
            // A build that ran but did not succeed is a failure to the caller.
            toolResult.IsError = !build.Succeeded;
            return toolResult;
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Build for platform";
            // Writes build artifacts and invokes the toolchain.
            a.OpenWorldHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"project.build"}; }
    };

    // ========================================================================
    // SaveScene / LoadScene
    // ========================================================================
    class SaveSceneTool : public MCPTool {
    public:
        SaveSceneTool()
            : MCPTool("SaveScene",
                      "Serialize the active scene to a project-relative asset file so the "
                      "current world state survives a restart and can be diffed or committed. "
                      "Pair with LoadScene to restore a known-good state before testing.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"path", {{"type", "string"},
                    {"description", "Project-relative destination, e.g. 'Assets/Scenes/level1.scene'"}}}
            };
            schema.Required = {"path"};
            return schema;
        }

        bool ValidateArguments(const Json& args, std::string& errorMessage) const override {
            if (!args.contains("path") || !args["path"].is_string()) {
                errorMessage = "'path' is required and must be a string";
                return false;
            }
            std::filesystem::path resolved;
            return ProjectToolsDetail::ResolveInProject(
                args["path"].get<std::string>(), resolved, errorMessage);
        }

        ToolResult Execute(const Json& args, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene bound to the MCP server");
            }

            std::filesystem::path target;
            std::string pathError;
            if (!ProjectToolsDetail::ResolveInProject(args["path"].get<std::string>(), target, pathError)) {
                return ToolResult::Error("Invalid path: " + pathError);
            }

            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);

            std::string error;
            if (!SerializeSceneToAsset(*scene, target, &error)) {
                return ToolResult::Error("Failed to save scene: " + error);
            }

            Json result;
            result["path"] = args["path"];
            result["entityCount"] = scene->GetEntityCount();
            result["sceneName"] = scene->GetName();
            return ToolResult::SuccessJson(result);
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Save scene to disk";
            // Overwrites the destination file if it already exists.
            a.DestructiveHint = true;
            a.IdempotentHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"project.write"}; }
    };

    class LoadSceneTool : public MCPTool {
    public:
        LoadSceneTool()
            : MCPTool("LoadScene",
                      "Replace the active scene's contents with a scene asset from disk. "
                      "Destroys every existing entity first, so save anything worth keeping "
                      "before calling this.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"path", {{"type", "string"},
                    {"description", "Project-relative scene asset to load"}}}
            };
            schema.Required = {"path"};
            return schema;
        }

        bool ValidateArguments(const Json& args, std::string& errorMessage) const override {
            if (!args.contains("path") || !args["path"].is_string()) {
                errorMessage = "'path' is required and must be a string";
                return false;
            }
            std::filesystem::path resolved;
            return ProjectToolsDetail::ResolveInProject(
                args["path"].get<std::string>(), resolved, errorMessage);
        }

        ToolResult Execute(const Json& args, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene bound to the MCP server");
            }

            std::filesystem::path source;
            std::string pathError;
            if (!ProjectToolsDetail::ResolveInProject(args["path"].get<std::string>(), source, pathError)) {
                return ToolResult::Error("Invalid path: " + pathError);
            }
            if (!std::filesystem::exists(source)) {
                return ToolResult::Error("Scene asset does not exist: " + args["path"].get<std::string>());
            }

            std::string error;
            if (!DeserializeSceneFromAsset(source, *scene, &error)) {
                return ToolResult::Error("Failed to load scene: " + error);
            }

            Json result;
            result["path"] = args["path"];
            result["entityCount"] = scene->GetEntityCount();
            result["sceneName"] = scene->GetName();
            return ToolResult::SuccessJson(result);
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "Load scene from disk";
            // Destroys every entity in the active scene before loading.
            a.DestructiveHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"project.write"}; }
    };

    // ========================================================================
    // ListProjectFiles
    // ========================================================================
    class ListProjectFilesTool : public MCPTool {
    public:
        ListProjectFilesTool()
            : MCPTool("ListProjectFiles",
                      "List files under a project-relative directory, optionally filtered by "
                      "extension. Use this to discover scenes, shaders, and assets before "
                      "acting on them. Paths outside the project root are rejected.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = {
                {"directory", {{"type", "string"},
                    {"description", "Project-relative directory (default '.')"}}},
                {"extension", {{"type", "string"},
                    {"description", "Optional extension filter including the dot, e.g. '.scene'"}}},
                {"recursive", {{"type", "boolean"},
                    {"description", "Recurse into subdirectories (default false)"}}},
                {"maxResults", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5000},
                    {"description", "Cap on returned entries (default 500)"}}}
            };
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& args, ECS::Scene*) override {
            const std::string requested = JsonUtils::GetOr<std::string>(args, "directory", ".");

            std::filesystem::path root;
            std::string pathError;
            if (!ProjectToolsDetail::ResolveInProject(requested, root, pathError)) {
                return ToolResult::Error("Invalid directory: " + pathError);
            }
            if (!std::filesystem::is_directory(root)) {
                return ToolResult::Error("Not a directory: " + requested);
            }

            const std::string extension = JsonUtils::GetOr<std::string>(args, "extension", "");
            const bool recursive = JsonUtils::GetOr<bool>(args, "recursive", false);
            const size_t maxResults = static_cast<size_t>(
                std::clamp<int64_t>(JsonUtils::GetOr<int64_t>(args, "maxResults", 500), 1, 5000));

            const auto projectRoot = ProjectToolsDetail::ProjectRoot();
            Json files = Json::array();
            bool truncated = false;

            auto consider = [&](const std::filesystem::directory_entry& entry) {
                if (files.size() >= maxResults) {
                    truncated = true;
                    return;
                }
                if (!entry.is_regular_file()) return;
                if (!extension.empty() && entry.path().extension().string() != extension) return;

                std::error_code relEc;
                auto relative = std::filesystem::relative(entry.path(), projectRoot, relEc);
                files.push_back({
                    {"path", (relEc ? entry.path() : relative).generic_string()},
                    {"sizeBytes", static_cast<uint64_t>(entry.file_size())}
                });
            };

            std::error_code ec;
            if (recursive) {
                for (auto it = std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec) break;
                    consider(*it);
                    if (truncated) break;
                }
            } else {
                for (auto it = std::filesystem::directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::directory_iterator(); it.increment(ec)) {
                    if (ec) break;
                    consider(*it);
                    if (truncated) break;
                }
            }

            Json result;
            result["directory"] = requested;
            result["fileCount"] = files.size();
            // Say so explicitly rather than silently returning a partial listing.
            result["truncated"] = truncated;
            result["files"] = files;
            return ToolResult::SuccessJson(result);
        }

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations a;
            a.Title = "List project files";
            a.ReadOnlyHint = true;
            a.IdempotentHint = true;
            return a;
        }

        std::vector<std::string> RequiredCapabilities() const override { return {"project.read"}; }
    };

    // ========================================================================
    // Factory
    // ========================================================================
    inline std::vector<MCPToolPtr> CreateProjectTools() {
        std::vector<MCPToolPtr> tools;
        tools.push_back(std::make_shared<BuildForPlatformTool>());
        tools.push_back(std::make_shared<SaveSceneTool>());
        tools.push_back(std::make_shared<LoadSceneTool>());
        tools.push_back(std::make_shared<ListProjectFilesTool>());
        return tools;
    }

} // namespace MCP
} // namespace Core
