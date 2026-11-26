#pragma once

#include <utility>

namespace bacuri::vms::platform
{
// Runs an action when the enclosing scope ends, also when the scope is left through an exception.
template <class Action> class ScopeExit
{
  public:
    explicit ScopeExit(Action action) : action_{std::move(action)} {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit()
    {
        action_();
    }

  private:
    Action action_;
};
} // namespace bacuri::vms::platform
