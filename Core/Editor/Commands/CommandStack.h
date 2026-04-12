#pragma once

#include "CommandTransaction.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Core {
namespace Editor {
namespace Commands {

    class CommandStack {
    public:
        bool PushAndApply(CommandTransaction transaction);
        bool Execute(std::unique_ptr<IEditorCommand> command, std::string label);
        bool Undo();
        bool Redo();
        void Clear();

        void SetMaxHistory(std::size_t maxHistory) { m_MaxHistory = maxHistory; }
        std::size_t GetMaxHistory() const { return m_MaxHistory; }

        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }
        std::size_t GetUndoCount() const { return m_UndoStack.size(); }
        std::size_t GetRedoCount() const { return m_RedoStack.size(); }

        const std::vector<CommandTransaction>& GetUndoStack() const { return m_UndoStack; }
        const std::vector<CommandTransaction>& GetRedoStack() const { return m_RedoStack; }

    private:
        std::size_t m_MaxHistory = 256;
        std::vector<CommandTransaction> m_UndoStack;
        std::vector<CommandTransaction> m_RedoStack;
    };

} // namespace Commands
} // namespace Editor
} // namespace Core

