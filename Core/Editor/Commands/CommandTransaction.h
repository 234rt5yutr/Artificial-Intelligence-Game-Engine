#pragma once

#include "IEditorCommand.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Core {
namespace Editor {
namespace Commands {

    struct CommandTransaction {
        std::string Label;
        std::vector<std::unique_ptr<IEditorCommand>> Commands;
        std::chrono::system_clock::time_point Timestamp = std::chrono::system_clock::now();

        bool Apply() {
            for (const auto& command : Commands) {
                if (!command || !command->Apply()) {
                    return false;
                }
            }
            return true;
        }

        bool Revert() {
            for (auto it = Commands.rbegin(); it != Commands.rend(); ++it) {
                if (!(*it) || !(*it)->Revert()) {
                    return false;
                }
            }
            return true;
        }

        bool Empty() const {
            return Commands.empty();
        }

        void AddCommand(std::unique_ptr<IEditorCommand> command) {
            if (command) {
                Commands.push_back(std::move(command));
            }
        }
    };

} // namespace Commands
} // namespace Editor
} // namespace Core

