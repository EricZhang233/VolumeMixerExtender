#pragma once

#include "Foundation.h"

namespace vmex::recording
{
    struct VisibilityState final
    {
        std::wstring id;
        bool visible = true;
        bool owned = false;
    };

    class IVisibilityActions
    {
    public:
        virtual ~IVisibilityActions() = default;
        virtual Status Enumerate(std::vector<VisibilityState>& endpoints) = 0;
        virtual Status Store(std::wstring_view id, bool owned) = 0;
        virtual Status SetVisible(std::wstring_view id, bool visible) = 0;
    };

    Status ChangeVisibility(IVisibilityActions& actions, bool visible, std::vector<VisibilityState>& changes);
    Status RestoreVisibility(IVisibilityActions& actions, const std::vector<VisibilityState>& changes);
    bool IsCableHardwareId(std::wstring_view id);
}
