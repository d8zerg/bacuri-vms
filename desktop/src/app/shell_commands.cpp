#include "app/window_commands.hpp"

namespace bacuri::vms::client
{
using W = WindowCommands;
using K = QKeySequence;
using S = CommandScope;
using P = ParameterType;
void installShellCommands(CommandRegistry& registry)
{
    const auto always = [](const W&) { return true; };
    const auto overview = [](const W& w) { return w.navigation_->page() == Navigation::Overview; };
    const auto session = [overview](const W& w) {
        return overview(w) && w.workspace_->signedIn() && w.workspace_->sessionActions() &&
               w.workspace_->state() != Workspace::SigningOut;
    };
    const auto trigger = [](QString id) { return [id](W& w) { emit w.triggered(id); }; };
    auto keys = [](K::StandardKey standard, K extra = {}) {
        auto result = K::keyBindings(standard);
        if (!extra.isEmpty() && !result.contains(extra))
            result.append(extra);
        return result;
    };
    registry.add(
        {{"palette",
          "Command palette",
          "search",
          {K("Ctrl+K")},
          S::Global,
          P::None,
          always,
          [](W& w) { w.openPalette(); }},
         {"new", "New window", "new-window", keys(K::New), S::Global, P::None, always, trigger("new")},
         {"close", "Close window", "circle-x", keys(K::Close), S::Global, P::None, always, trigger("close")},
         {"back", "Back", "arrow-left", keys(K::Back, K(Qt::Key_Escape)), S::Global, P::None,
          [overview](const W& w) { return !overview(w); }, trigger("back")},
         {"settings", "Settings", "settings-2", keys(K::Preferences, K("Ctrl+,")), S::Global, P::None, always,
          trigger("settings")},
         {"find", "Find", "search", keys(K::Find), S::Global, P::None,
          [overview](const W& w) {
              return overview(w) && w.workspace_->signedIn() && w.workspace_->points()->count();
          },
          trigger("find")},
         {"refresh", "Refresh", "rotate-cw", keys(K::Refresh, K("F5")), S::Global, P::None,
          [session](const W& w) { return session(w) && w.workspace_->state() != Workspace::Loading; },
          [](W& w) { w.workspace_->refresh(); }},
         {"sign-out",
          "Sign out",
          "log-out",
          {K("Ctrl+Shift+Q")},
          S::Global,
          P::None,
          session,
          [](W& w) { w.workspace_->signOut(); }},
         {"bindings", "Keyboard shortcuts", "settings-2", {}, S::Global, P::None, always, trigger("bindings")}});
    registry.add({"publish-keymap",
                  "Publish site keyboard profile",
                  "settings-2",
                  {},
                  S::Global,
                  P::None,
                  [](const W& w) {
                      const auto role = w.workspace_->siteRole(w.navigation_->pointId());
                      return role.value("role") == "administrator" && role.contains("keymap_revision");
                  },
                  [](W& w) { emit w.tool("publish-keymap", {}); }});
    registry.add({"undo",
                  "Undo view change",
                  "rotate-cw",
                  {K("Ctrl+Z")},
                  S::Global,
                  P::None,
                  [](const W& w) { return !w.history_.isEmpty(); },
                  [](W& w) { w.undoView(); }});
}
} // namespace bacuri::vms::client
