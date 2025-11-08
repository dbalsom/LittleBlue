#include "DebuggerManager.h"
#include <algorithm>
#include <ranges>
#include <imgui/imgui.h>

void DebuggerManager::addWindow(const std::string &name, std::unique_ptr<DebuggerWindow> w, bool *open) {
    Entry e;
    e.window = std::move(w);
    e.visible = open ? *open : false;
    e.externalOpen = open;
    // Use insert_or_assign to avoid attempting to copy Entry which contains unique_ptr
    _windows.insert_or_assign(name, std::move(e));
}

bool DebuggerManager::removeWindow(const std::string &name) {
    return _windows.erase(name) > 0;
}

void DebuggerManager::setVisible(const std::string &name, bool v) {
    const auto it = _windows.find(name);
    if (it == _windows.end()) return;
    it->second.visible = v;
}

bool DebuggerManager::isVisible(const std::string &name) const {
    const auto it = _windows.find(name);
    if (it == _windows.end()) return false;
    return it->second.visible;
}

bool DebuggerManager::toggleVisible(const std::string &name) {
    const auto it = _windows.find(name);
    if (it == _windows.end()) return false;
    it->second.visible = !it->second.visible;
    return it->second.visible;
}

void DebuggerManager::drawMenuItems() {
    // Menu items should be drawn within an ImGui menu.
    for (auto & [fst, snd] : _windows) {
        bool v = snd.externalOpen ? *snd.externalOpen : snd.visible;
        if (ImGui::MenuItem(fst.c_str(), nullptr, &v)) {
            if (snd.externalOpen)
                *snd.externalOpen = v;
            else
                snd.visible = v;
        }
    }
}

void DebuggerManager::showAll() {
    // Iterate in insertion order would be nicer but unordered_map doesn't provide it — that's OK here.
    for (auto& val : _windows | std::views::values) {
        bool shouldShow = val.externalOpen ? *val.externalOpen : val.visible;
        if (shouldShow && val.window) {
            // Pass pointer to visibility so the window's close button can toggle it.
            // If externalOpen exists, pass that pointer; otherwise pass address of internal flag.
            bool *openPtr = val.externalOpen ? val.externalOpen : &val.visible;
            val.window->show(openPtr);
        }
    }
}

std::vector<std::string> DebuggerManager::names() const {
    std::vector<std::string> out;
    out.reserve(_windows.size());
    for (auto &p : _windows) out.push_back(p.first);
    std::ranges::sort(out);
    return out;
}
