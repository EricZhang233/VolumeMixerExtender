set(VMEX_TEXT_YAML "${CMAKE_SOURCE_DIR}/text.yaml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VMEX_TEXT_YAML}")

file(READ "${VMEX_TEXT_YAML}" VMEX_TEXT_YAML_CONTENT)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/TextResources.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TextResources.g.h"
    @ONLY)

add_library(vmex_core STATIC
    App.cpp
    AudioDeviceManager.cpp
    CliTypes.cpp
    CommandRegistry.cpp
    ConfigService.cpp
    EndpointPolicyService.cpp
    Foundation.cpp
    InjectionService.cpp
    Log.cpp
    Logger.cpp
    PayloadResources.cpp
    Platform.cpp
    Strings.cpp
    TextEncoding.cpp
    TextService.cpp
)

target_include_directories(vmex_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(vmex_core PUBLIC ole32 oleaut32)
