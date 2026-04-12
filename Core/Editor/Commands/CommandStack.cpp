#include "CommandStack.h"

#include <cstddef>

namespace Core {
namespace Editor {
namespace Commands {

    bool CommandStack::PushAndApply(CommandTransaction transaction) {
        if (transaction.Empty()) {
            return false;
        }

        if (!transaction.Apply()) {
            return false;
        }

        m_RedoStack.clear();
        m_UndoStack.push_back(std::move(transaction));

        if (m_UndoStack.size() > m_MaxHistory) {
            const std::size_t removeCount = m_UndoStack.size() - m_MaxHistory;
            m_UndoStack.erase(m_UndoStack.begin(), m_UndoStack.begin() + static_cast<std::ptrdiff_t>(removeCount));
        }

        return true;
    }

    bool CommandStack::Execute(std::unique_ptr<IEditorCommand> command, std::string label) {
        CommandTransaction transaction;
        transaction.Label = std::move(label);
        transaction.AddCommand(std::move(command));
        return PushAndApply(std::move(transaction));
    }

    bool CommandStack::Undo() {
        if (m_UndoStack.empty()) {
            return false;
        }

        CommandTransaction transaction = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();

        if (!transaction.Revert()) {
            return false;
        }

        m_RedoStack.push_back(std::move(transaction));
        return true;
    }

    bool CommandStack::Redo() {
        if (m_RedoStack.empty()) {
            return false;
        }

        CommandTransaction transaction = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();

        if (!transaction.Apply()) {
            return false;
        }

        m_UndoStack.push_back(std::move(transaction));
        return true;
    }

    void CommandStack::Clear() {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }

} // namespace Commands
} // namespace Editor
} // namespace Core

