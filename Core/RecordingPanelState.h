#pragma once

#include "RecordingMode.h"

namespace vmex::recording
{
    struct PanelState final
    {
        Snapshot recording;
        bool available = false;
    };

    class PanelStateCache final
    {
    public:
        explicit PanelStateCache(std::wstring name = {});
        ~PanelStateCache();
        PanelStateCache(const PanelStateCache&) = delete;
        PanelStateCache& operator=(const PanelStateCache&) = delete;
        Status Start();
        void Stop();
        void Publish(const PanelState& state);
        [[nodiscard]] static bool Read(PanelState& state, std::wstring_view name = {});

    private:
        std::wstring m_name;
        void* m_mapping = nullptr;
        void* m_mutex = nullptr;
        void* m_view = nullptr;
    };
}
