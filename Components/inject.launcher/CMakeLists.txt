add_library(vmex_launcher SHARED
    Launcher.cpp
)

set_target_properties(vmex_launcher PROPERTIES
    PREFIX ""
)

target_link_libraries(vmex_launcher PRIVATE vmex_core)

vmex_add_version_resource(vmex_launcher "VolumeMixerExtender injection launcher" "vmex_launcher" "vmex_launcher.dll" VFT_DLL)
