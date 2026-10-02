add_library(vmex_tap SHARED
    Tap.cpp
)

set_target_properties(vmex_tap PROPERTIES
    PREFIX ""
)

target_link_libraries(vmex_tap PRIVATE vmex_core)

vmex_add_version_resource(vmex_tap "VolumeMixerExtender XAML diagnostics tap" "vmex_tap" "vmex_tap.dll" VFT_DLL)
