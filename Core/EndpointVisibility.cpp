#include "EndpointVisibility.h"

#include "Strings.h"

namespace vmex::recording
{
    bool IsCableHardwareId(std::wstring_view id)
    {
        return strings::EqualsIgnoreCase(id, L"VBAudioVACWDM") ||
            strings::EqualsIgnoreCase(id, L"ROOT\\VBAudioVACWDM");
    }

    Status ChangeVisibility(IVisibilityActions& actions, bool visible, std::vector<VisibilityState>& changes)
    {
        std::vector<VisibilityState> endpoints;
        const auto enumerated = actions.Enumerate(endpoints);
        if (!enumerated) return enumerated;
        for (const auto& endpoint : endpoints)
        {
            if (visible && !endpoint.owned) continue;
            if (!visible && !endpoint.visible) continue;
            changes.push_back(endpoint);
            if (!visible)
            {
                const auto stored = actions.Store(endpoint.id, true);
                if (!stored) { changes.pop_back(); return stored; }
            }
            if (endpoint.visible != visible)
            {
                const auto changed = actions.SetVisible(endpoint.id, visible);
                if (!changed) return changed;
            }
            if (visible)
            {
                const auto stored = actions.Store(endpoint.id, false);
                if (!stored) return stored;
            }
        }
        return Status::Ok();
    }

    Status RestoreVisibility(IVisibilityActions& actions, const std::vector<VisibilityState>& changes)
    {
        Status result;
        for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        {
            auto restored = actions.Store(it->id, true);
            if (restored) restored = actions.SetVisible(it->id, it->visible);
            if (restored) restored = actions.Store(it->id, it->owned);
            if (!restored) result = restored;
        }
        return result;
    }

    Status MaintainStandbyVisibility(IVisibilityActions& actions, bool recordingActive, bool restartRequired,
        const std::function<Status()>& protectDefaults)
    {
        if (recordingActive) return Status::Ok();
        const auto protectedDefaults = protectDefaults();
        if (!protectedDefaults || restartRequired) return protectedDefaults;
        std::vector<VisibilityState> changes;
        return ChangeVisibility(actions, false, changes);
    }

    Status MaintainDriverVisibility(IVisibilityActions& actions, bool install,
        const std::function<Status()>& operation, const std::function<bool()>& restartRequired)
    {
        std::vector<VisibilityState> changes;
        const auto rollback = [&](Status result)
        {
            if (restartRequired()) return result;
            const auto restored = RestoreVisibility(actions, changes);
            return restored ? result : Status::Failed(result.detail + L"; " + restored.detail);
        };
        if (!install)
        {
            const auto hidden = ChangeVisibility(actions, false, changes);
            if (!hidden) return rollback(hidden);
        }
        Status result;
        try { result = operation(); }
        catch (...) { result = Status::Failed(L"driver-operation-exception"); }
        if (!result) return rollback(result);
        if (install && !restartRequired())
        {
            const auto hidden = ChangeVisibility(actions, false, changes);
            if (!hidden) return rollback(hidden);
        }
        return result;
    }
}
