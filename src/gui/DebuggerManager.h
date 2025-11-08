#pragma once

#include "DebuggerWindow.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Simple manager for ImGui debugger windows. Windows are registered by name.

class DebuggerManager {
public:
    DebuggerManager() = default;
    ~DebuggerManager() = default;

    // Add a window; name should be unique. Ownership is transferred.
    // If a window with the same name exists it will be replaced.
    // 'open' optionally points to an external boolean that controls the window's open state.
    // If provided, the manager will use it for visibility and pass its address to the window's show() call.
    void addWindow(const std::string &name, std::unique_ptr<DebuggerWindow> w, bool *open = nullptr);

    // Remove a window by name. Returns true if removed.
    bool removeWindow(const std::string &name);

    // Set/get visibility for a named window. If the name doesn't exist, setVisible will do nothing.
    void setVisible(const std::string &name, bool v);
    bool isVisible(const std::string &name) const;

    // Toggle visibility (returns new state). If name doesn't exist returns false.
    bool toggleVisible(const std::string &name);

    // Draw menu items for each registered window. Call this from inside an already-open "Debug" menu.
    // Each item will be a menu entry that toggles visibility.
    void drawMenuItems();

    // Show all visible windows (call from main UI loop after menu/menubar handling).
    void showAll();

    // Return a list of registered window names (const reference for inspection)
    std::vector<std::string> names() const;

private:
    struct Entry {
        std::unique_ptr<DebuggerWindow> window;
        bool visible{false};
        bool *externalOpen{nullptr};
    };

    std::unordered_map<std::string, Entry> _windows;
};
