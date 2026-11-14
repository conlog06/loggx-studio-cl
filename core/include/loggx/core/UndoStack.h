// Schnappschuss-basiertes Undo/Redo. Projekte sind klein (nur Metadaten,
// keine Mediendaten), deshalb ist eine Kopie pro Schritt billig und robust.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace loggx {

template <typename T>
class UndoStack {
public:
    explicit UndoStack(std::size_t limit = 500) : m_limit(limit) {}

    // Vor einer Änderung mit dem alten Zustand aufrufen.
    void push(T previous) {
        m_undo.push_back(std::move(previous));
        if (m_undo.size() > m_limit) m_undo.erase(m_undo.begin());
        m_redo.clear();
    }
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    bool undo(T& current) {
        if (m_undo.empty()) return false;
        m_redo.push_back(std::move(current));
        current = std::move(m_undo.back());
        m_undo.pop_back();
        return true;
    }
    bool redo(T& current) {
        if (m_redo.empty()) return false;
        m_undo.push_back(std::move(current));
        current = std::move(m_redo.back());
        m_redo.pop_back();
        return true;
    }
    void clear() {
        m_undo.clear();
        m_redo.clear();
    }

private:
    std::size_t m_limit;
    std::vector<T> m_undo;
    std::vector<T> m_redo;
};

}  // namespace loggx
