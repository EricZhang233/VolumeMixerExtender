cmake_minimum_required(VERSION 3.21)

set(VMEX_VERSION_SOURCE "${CMAKE_CURRENT_LIST_DIR}/Version.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VMEX_VERSION_SOURCE}")

file(READ "${VMEX_VERSION_SOURCE}" VMEX_VERSION_HEADER)

string(REGEX MATCH "VMEX_VERSION_STRING[ \t]+\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"" _ "${VMEX_VERSION_HEADER}")
if(CMAKE_MATCH_1 STREQUAL "")
    message(FATAL_ERROR "Version.h must declare VMEX_VERSION_STRING as \"MAJOR.MINOR.PATCH\"")
endif()

set(VMEX_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(VMEX_VERSION_MINOR "${CMAKE_MATCH_2}")
set(VMEX_VERSION_PATCH "${CMAKE_MATCH_3}")
set(VMEX_VERSION "${VMEX_VERSION_MAJOR}.${VMEX_VERSION_MINOR}.${VMEX_VERSION_PATCH}")
set(VMEX_VERSION_COMMA "${VMEX_VERSION_MAJOR},${VMEX_VERSION_MINOR},${VMEX_VERSION_PATCH},0")

string(REGEX MATCH "VMEX_PRODUCT_NAME[ \t]+\"([^\"]+)\"" _ "${VMEX_VERSION_HEADER}")
if(CMAKE_MATCH_1 STREQUAL "")
    message(FATAL_ERROR "Version.h must declare VMEX_PRODUCT_NAME")
endif()
set(VMEX_PRODUCT_NAME "${CMAKE_MATCH_1}")

project(VolumeMixerExtender VERSION "${VMEX_VERSION}" LANGUAGES CXX RC)

function(vmex_add_version_resource target description internal_name original_name file_type)
    set(VMEX_RC_DESCRIPTION "${description}")
    set(VMEX_RC_INTERNAL_NAME "${internal_name}")
    set(VMEX_RC_ORIGINAL_NAME "${original_name}")
    set(VMEX_RC_FILE_TYPE "${file_type}")

    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/VersionResources.rc")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/VersionResources.rc.in" "${output}" @ONLY)
    target_sources(${target} PRIVATE "${output}")
endfunction()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VMEX_MIN_WINDOWS_BUILD 26300)
set(VMEX_MIN_WINDOWS_NAME "26H2")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/obj/lib/$<CONFIG>")

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc /MP /guard:cf)
    add_link_options(/guard:cf)
    add_compile_definitions(
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        NTDDI_VERSION=0x0A000010
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        VMEX_MIN_WINDOWS_BUILD=${VMEX_MIN_WINDOWS_BUILD}
    )
endif()

add_subdirectory(Core)
add_subdirectory(Components/inject.launcher)
add_subdirectory(Components/inject.tap)

set(VMEX_PAYLOAD_RC "${CMAKE_BINARY_DIR}/generated/PayloadResources.rc")

add_custom_command(
    OUTPUT "${VMEX_PAYLOAD_RC}"
    COMMAND "${CMAKE_COMMAND}"
        "-DVMEX_RC_OUTPUT=${VMEX_PAYLOAD_RC}"
        "-DVMEX_PAYLOAD_LAUNCHER=$<TARGET_FILE:vmex_launcher>"
        "-DVMEX_PAYLOAD_TAP=$<TARGET_FILE:vmex_tap>"
        -P "${CMAKE_SOURCE_DIR}/cmake/GeneratePayloadRc.cmake"
    DEPENDS "$<TARGET_FILE:vmex_launcher>" "$<TARGET_FILE:vmex_tap>"
    VERBATIM)

add_executable(vmex
    CliService.cpp
    Program.cpp
    "${CMAKE_SOURCE_DIR}/app.manifest"
    "${VMEX_PAYLOAD_RC}"
)

set_target_properties(vmex PROPERTIES VS_DPI_AWARE "PerMonitor")

add_dependencies(vmex vmex_launcher vmex_tap)

target_link_libraries(vmex PRIVATE vmex_core)

vmex_add_version_resource(vmex "VolumeMixerExtender command line interface" "vmex" "vmex.exe" VFT_APP)

set(VMEX_PACKAGE_NAME "${VMEX_PRODUCT_NAME}_${VMEX_VERSION}")
set(VMEX_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/output")

add_custom_command(TARGET vmex POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DVMEX_PACKAGE_DIR=${VMEX_PACKAGE_DIR}"
        "-DVMEX_PACKAGE_NAME=${VMEX_PACKAGE_NAME}"
        "-DVMEX_EXECUTABLE=$<TARGET_FILE:vmex>"
        "-DVMEX_CONFIG=$<CONFIG>"
        -P "${CMAKE_SOURCE_DIR}/cmake/Package.cmake"
    VERBATIM)

add_custom_target(package
    COMMAND "${CMAKE_COMMAND}"
        "-DVMEX_PACKAGE_DIR=${VMEX_PACKAGE_DIR}"
        "-DVMEX_PACKAGE_NAME=${VMEX_PACKAGE_NAME}"
        "-DVMEX_EXECUTABLE=$<TARGET_FILE:vmex>"
        -P "${CMAKE_SOURCE_DIR}/cmake/Package.cmake"
    DEPENDS vmex
    VERBATIM)
