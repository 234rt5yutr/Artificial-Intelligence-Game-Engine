#pragma once

#include <string>

namespace Core {
namespace Editor {
namespace Commands {

    class IEditorCommand {
    public:
        virtual ~IEditorCommand() = default;

        virtual bool Apply() = 0;
        virtual bool Revert() = 0;
        virtual const std::string& GetLabel() const = 0;
    };

} // namespace Commands
} // namespace Editor
} // namespace Core

