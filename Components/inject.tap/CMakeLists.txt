add_library(vmex_tap SHARED
    Tap.cpp
)

set_target_properties(vmex_tap PROPERTIES
    PREFIX ""
)

target_link_options(vmex_tap PRIVATE
    "/EXPORT:DllGetClassObject,PRIVATE"
    "/EXPORT:DllCanUnloadNow,PRIVATE"
)

if(NOT DEFINED VMEX_CPPWINRT_DIR)
    set(VMEX_CPPWINRT_DIR "${WindowsSdkDir}Include/${WindowsSDKVersion}cppwinrt")
endif()
target_include_directories(vmex_tap PRIVATE "${VMEX_CPPWINRT_DIR}")

target_link_libraries(vmex_tap PRIVATE
    vmex_core
    WindowsApp.lib
    Mmdevapi.lib
    Ole32.lib
    Uuid.lib
    Version.lib
)

vmex_add_version_resource(vmex_tap "VolumeMixerExtender XAML diagnostics tap" "vmex_tap" "vmex_tap.dll" VFT_DLL)
