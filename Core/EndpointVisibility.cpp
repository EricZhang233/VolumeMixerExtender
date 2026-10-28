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
}
