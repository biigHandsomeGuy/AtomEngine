#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "SceneGraph.h"

struct Scene;

namespace AtomEditor
{
    class CommandHistory
    {
    public:
        using Action = std::function<void(Scene&)>;
        static constexpr size_t MaxCommands = 128;

        void RecordApplied(std::string label, Action undo, Action redo)
        {
            m_Commands.erase(m_Commands.begin() + static_cast<std::ptrdiff_t>(m_Cursor), m_Commands.end());
            m_Commands.push_back({ std::move(label), std::move(undo), std::move(redo) });
            m_Cursor = m_Commands.size();
            if (m_Commands.size() > MaxCommands)
            {
                m_Commands.erase(m_Commands.begin());
                --m_Cursor;
            }
        }

        bool Undo(Scene& scene)
        {
            if (!CanUndo())
                return false;
            m_Commands[--m_Cursor].Undo(scene);
            scene.MarkModified();
            return true;
        }

        bool Redo(Scene& scene)
        {
            if (!CanRedo())
                return false;
            m_Commands[m_Cursor++].Redo(scene);
            scene.MarkModified();
            return true;
        }

        bool CanUndo() const { return m_Cursor > 0; }
        bool CanRedo() const { return m_Cursor < m_Commands.size(); }
        const std::string& UndoLabel() const { return m_Commands[m_Cursor - 1].Label; }
        const std::string& RedoLabel() const { return m_Commands[m_Cursor].Label; }

        void Clear()
        {
            m_Commands.clear();
            m_Cursor = 0;
        }

    private:
        struct Command
        {
            std::string Label;
            Action Undo;
            Action Redo;
        };

        std::vector<Command> m_Commands;
        size_t m_Cursor = 0;
    };
}
