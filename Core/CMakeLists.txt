set(VMEX_TEXT_YAML "${CMAKE_SOURCE_DIR}/text.yaml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VMEX_TEXT_YAML}")

file(READ "${VMEX_TEXT_YAML}" VMEX_TEXT_YAML_CONTENT)

string(REPLACE "\n" "\n)VMEXTEXT\"\nLR\"VMEXTEXT(" VMEX_TEXT_YAML_CHUNKS "${VMEX_TEXT_YAML_CONTENT}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/TextResources.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TextResources.g.h"
    @ONLY)

add_library(vmex_core SHARED
    AboutService.cpp
    App.cpp
    AppIcons.cpp
    AudioDeviceManager.cpp
    AudioMirror.cpp
    AudioRoutingCleanup.cpp
    AutostartEntry.cpp
    CliTypes.cpp
    CommandRegistry.cpp
    ConfigService.cpp
    EndpointPolicyService.cpp
    EndpointVisibility.cpp
    Foundation.cpp
    HostPresence.cpp
    InjectionMonitor.cpp
    InjectionService.cpp
    Log.cpp
    Logger.cpp
    ManagementService.cpp
    ManagementExports.cpp
    MaintenanceWorkflow.cpp
    RecordingMode.cpp
    RecordingBackend.cpp
    RecordingGuard.cpp
    RecordingClient.cpp
    RecordingDriverService.cpp
    RecordingEndpointLock.cpp
    PayloadDeployment.cpp
    PayloadResources.cpp
    Platform.cpp
    Runtime.cpp
    StartMenuEntry.cpp
    Strings.cpp
    TapCommand.cpp
    TapPipeServer.cpp
    TextEncoding.cpp
    TextService.cpp
    UninstallService.cpp
    UserSettings.cpp
)

set_target_properties(vmex_core PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    PREFIX ""
)

target_include_directories(vmex_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(vmex_core PUBLIC
    advapi32
    ole32
    oleaut32
    mmdevapi
    shell32
    setupapi
    cfgmgr32
    crypt32
    wintrust
    newdev
    uuid
    version
    windowscodecs
    WindowsApp.lib
)
