#include "ComponentPropertyCommand.h"

namespace Core {
namespace Editor {
namespace Commands {

    ComponentPropertyCommand::ComponentPropertyCommand(std::string label, Callback apply, Callback revert)
        : m_Label(std::move(label))
        , m_Apply(std::move(apply))
        , m_Revert(std::move(revert)) {
    }

    bool ComponentPropertyCommand::Apply() {
        return m_Apply ? m_Apply() : false;
    }

    bool ComponentPropertyCommand::Revert() {
        return m_Revert ? m_Revert() : false;
    }

    const std::string& ComponentPropertyCommand::GetLabel() const {
        return m_Label;
    }

} // namespace Commands
} // namespace Editor
} // namespace Core

