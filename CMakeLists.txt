cmake_minimum_required(VERSION 3.21)

set(VMEX_VERSION_SOURCE "${CMAKE_CURRENT_LIST_DIR}/Version.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VMEX_VERSION_SOURCE}")

file(READ "${VMEX_VERSION_SOURCE}" VMEX_VERSION_HEADER)

string(REGEX MATCH "VMEX_VERSION_STRING[ \t]+\"([0-9]+)\\.([0-9]+)\\.([0-9]+)(\\.([0-9]+))?\"" _ "${VMEX_VERSION_HEADER}")
if(_ STREQUAL "")
    message(FATAL_ERROR "Version.h must declare VMEX_VERSION_STRING as \"MAJOR.MINOR.PATCH\" or \"MAJOR.MINOR.PATCH.BUILD\"")
endif()

set(VMEX_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(VMEX_VERSION_MINOR "${CMAKE_MATCH_2}")
set(VMEX_VERSION_PATCH "${CMAKE_MATCH_3}")
set(VMEX_VERSION_TWEAK "${CMAKE_MATCH_5}")
set(VMEX_VERSION "${VMEX_VERSION_MAJOR}.${VMEX_VERSION_MINOR}.${VMEX_VERSION_PATCH}")
set(VMEX_VERSION_COMMA "${VMEX_VERSION_MAJOR},${VMEX_VERSION_MINOR},${VMEX_VERSION_PATCH},0")
if(NOT VMEX_VERSION_TWEAK STREQUAL "")
    string(APPEND VMEX_VERSION ".${VMEX_VERSION_TWEAK}")
    set(VMEX_VERSION_COMMA "${VMEX_VERSION_MAJOR},${VMEX_VERSION_MINOR},${VMEX_VERSION_PATCH},${VMEX_VERSION_TWEAK}")
endif()

string(REGEX MATCH "VMEX_PRODUCT_NAME[ \t]+\"([^\"]+)\"" _ "${VMEX_VERSION_HEADER}")
if(CMAKE_MATCH_1 STREQUAL "")
    message(FATAL_ERROR "Version.h must declare VMEX_PRODUCT_NAME")
endif()
set(VMEX_PRODUCT_NAME "${CMAKE_MATCH_1}")

project(VolumeMixerExtender VERSION "${VMEX_VERSION}" LANGUAGES CXX RC)

include(CTest)

function(vmex_add_version_resource target description internal_name original_name file_type)
    set(VMEX_RC_DESCRIPTION "${description}")
    set(VMEX_RC_INTERNAL_NAME "${internal_name}")
    set(VMEX_RC_ORIGINAL_NAME "${original_name}")
    set(VMEX_RC_FILE_TYPE "${file_type}")

    set(VMEX_RC_ICON_LINE "")
    if(ARGN)
        set(VMEX_RC_ICON_LINE "1 ICON \"${ARGN}\"")
    endif()

    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}-VersionResources.rc")
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
vmex_add_version_resource(vmex_core "VolumeMixerExtender core" "vmex_core" "vmex_core.dll" VFT_DLL)
add_subdirectory(Components/inject.launcher)
add_subdirectory(Components/inject.tap)

if(BUILD_TESTING)
    add_executable(vmex_tests Test/CoreTests.cpp)
    target_link_libraries(vmex_tests PRIVATE vmex_core)
    foreach(suite uninstall audio commands resources config pipe)
        add_test(NAME "core.${suite}" COMMAND vmex_tests "${suite}")
        set_tests_properties("core.${suite}" PROPERTIES TIMEOUT 15)
    endforeach()
endif()

set(VMEX_DIAGNOSTICS_DLL "" CACHE FILEPATH "Path to xamldiagnostics.dll to ship")
if(NOT VMEX_DIAGNOSTICS_DLL)
    file(GLOB VMEX_DIAGNOSTICS_CANDIDATES
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/XamlDiagnostics/xamldiagnostics.dll"
        "C:/Program Files (x86)/Windows Kits/10/bin/x64/XamlDiagnostics/xamldiagnostics.dll"
        "C:/Program Files/Windows Kits/10/bin/*/x64/XamlDiagnostics/xamldiagnostics.dll"
        "C:/Program Files/Windows Kits/10/bin/x64/XamlDiagnostics/xamldiagnostics.dll")
    list(SORT VMEX_DIAGNOSTICS_CANDIDATES ORDER DESCENDING)
    if(VMEX_DIAGNOSTICS_CANDIDATES)
        list(GET VMEX_DIAGNOSTICS_CANDIDATES 0 VMEX_DIAGNOSTICS_DLL)
    endif()
endif()
if(NOT EXISTS "${VMEX_DIAGNOSTICS_DLL}")
    message(FATAL_ERROR "xamldiagnostics.dll was not found; set VMEX_DIAGNOSTICS_DLL to its path")
endif()

add_executable(vmex WIN32
    Program.cpp
    "${CMAKE_SOURCE_DIR}/app.manifest"
)

add_executable(vmex_cli
    CliService.cpp
    CliProgram.cpp
    "${CMAKE_SOURCE_DIR}/app.manifest"
)

foreach(vmex_shell IN ITEMS vmex vmex_cli)
    add_custom_command(TARGET ${vmex_shell} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${VMEX_DIAGNOSTICS_DLL}" "$<TARGET_FILE_DIR:${vmex_shell}>"
        VERBATIM)
endforeach()

find_program(VMEX_DOTNET dotnet REQUIRED)
set(VMEX_MANAGER_EXECUTABLE "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/vmex_manager.exe")
add_custom_target(vmex_manager ALL
    COMMAND "${VMEX_DOTNET}" publish "${CMAKE_SOURCE_DIR}/Manager/Manager.csproj"
        -c $<CONFIG> "-p:Version=${VMEX_VERSION}" "-p:FileVersion=${VMEX_VERSION}"
        "-p:AssemblyVersion=${VMEX_VERSION}" "-p:InformationalVersion=${VMEX_VERSION}"
        -o "${CMAKE_BINARY_DIR}/manager/$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_BINARY_DIR}/manager/$<CONFIG>/vmex_manager.exe" "${VMEX_MANAGER_EXECUTABLE}"
    DEPENDS vmex_core
    VERBATIM)
if(BUILD_TESTING)
    add_custom_target(vmex_manager_ui_tests DEPENDS vmex_manager)
    add_test(NAME manager.wpf COMMAND "${VMEX_MANAGER_EXECUTABLE}" --self-test)
    set_tests_properties(manager.wpf PROPERTIES TIMEOUT 30)
endif()

set_target_properties(vmex PROPERTIES VS_DPI_AWARE "PerMonitor")
set_target_properties(vmex_cli PROPERTIES OUTPUT_NAME "vmex_cli")

add_dependencies(vmex vmex_launcher vmex_tap)
add_dependencies(vmex_cli vmex_launcher vmex_tap)

target_link_libraries(vmex PRIVATE vmex_core WindowsApp.lib)
target_link_libraries(vmex_cli PRIVATE vmex_core WindowsApp.lib)

vmex_add_version_resource(vmex "VolumeMixerExtender background host" "vmex" "vmex.exe" VFT_APP "${CMAKE_SOURCE_DIR}/ico.ico")
vmex_add_version_resource(vmex_cli "VolumeMixerExtender command line interface" "vmex_cli" "vmex_cli.exe" VFT_APP)

set(VMEX_PACKAGE_NAME "${VMEX_PRODUCT_NAME}_${VMEX_VERSION}")
set(VMEX_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/output" CACHE PATH "Release package output directory")

add_custom_target(package ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DVMEX_PACKAGE_DIR=${VMEX_PACKAGE_DIR}"
        "-DVMEX_PACKAGE_NAME=${VMEX_PACKAGE_NAME}"
        "-DVMEX_EXECUTABLE=$<TARGET_FILE:vmex>"
        "-DVMEX_CLI_EXECUTABLE=$<TARGET_FILE:vmex_cli>"
        "-DVMEX_CORE_DLL=$<TARGET_FILE:vmex_core>"
        "-DVMEX_MANAGER_EXECUTABLE=${VMEX_MANAGER_EXECUTABLE}"
        "-DVMEX_TAP_DLL=$<TARGET_FILE:vmex_tap>"
        "-DVMEX_LAUNCHER_DLL=$<TARGET_FILE:vmex_launcher>"
        "-DVMEX_DIAGNOSTICS_DLL=${VMEX_DIAGNOSTICS_DLL}"
        "-DVMEX_CONFIG=$<CONFIG>"
        -P "${CMAKE_SOURCE_DIR}/cmake/Package.cmake"
    DEPENDS vmex vmex_cli vmex_manager vmex_launcher vmex_tap
    VERBATIM)
