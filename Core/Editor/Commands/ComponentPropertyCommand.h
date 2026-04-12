#pragma once

#include "IEditorCommand.h"

#include <functional>
#include <string>

namespace Core {
namespace Editor {
namespace Commands {

    class ComponentPropertyCommand final : public IEditorCommand {
    public:
        using Callback = std::function<bool()>;

        ComponentPropertyCommand(std::string label, Callback apply, Callback revert);

        bool Apply() override;
        bool Revert() override;
        const std::string& GetLabel() const override;

    private:
        std::string m_Label;
        Callback m_Apply;
        Callback m_Revert;
    };

} // namespace Commands
} // namespace Editor
} // namespace Core

