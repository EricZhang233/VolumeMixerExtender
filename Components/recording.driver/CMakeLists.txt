cmake_minimum_required(VERSION 3.25)
project(VmexRecordingDriver LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_LIBRARIES "")
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_CXX_FLAGS_DEBUG "/Od /Z7")
set(CMAKE_CXX_FLAGS_RELEASE "/O2")
set(CMAKE_EXE_LINKER_FLAGS "/NODEFAULTLIB /MANIFEST:NO /DRIVER /SUBSYSTEM:NATIVE /ENTRY:FxDriverEntry /INCREMENTAL:NO")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG /INCREMENTAL:NO")

foreach(path VMEX_SYSVAD_SOURCE VMEX_WDK_ROOT VMEX_SDK_ROOT)
    if(NOT IS_DIRECTORY "${${path}}")
        message(FATAL_ERROR "Missing ${path}; use build-driver.ps1")
    endif()
endforeach()
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../Version.h" version_header)
string(REGEX MATCH "VMEX_VERSION_STRING[ \t]+\"([0-9.]+)\"" _ "${version_header}")
set(VMEX_VERSION "${CMAKE_MATCH_1}")
string(REPLACE "." "," VMEX_VERSION_COMMA "${VMEX_VERSION}")
if(NOT VMEX_VERSION_COMMA MATCHES "^[0-9]+,[0-9]+,[0-9]+,[0-9]+$")
    string(APPEND VMEX_VERSION_COMMA ",0")
endif()
string(REPLACE "," "." VMEX_DRIVER_VERSION "${VMEX_VERSION_COMMA}")
configure_file(Version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Version.rc" @ONLY)
configure_file(vmex_recording.inf.in "${CMAKE_CURRENT_BINARY_DIR}/vmex_recording.inf" @ONLY)
set(sysvad "${VMEX_SYSVAD_SOURCE}/audio/sysvad")
set(common "${sysvad}/EndpointsCommon")

add_executable(vmex_recording
    Lease.cpp
    Endpoint.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/Version.rc"
    "${sysvad}/adapter.cpp"
    "${sysvad}/basetopo.cpp"
    "${sysvad}/common.cpp"
    "${sysvad}/hw.cpp"
    "${sysvad}/kshelper.cpp"
    "${sysvad}/savedata.cpp"
    "${sysvad}/tonegenerator.cpp"
    "${sysvad}/TabletAudioSample/hdmitopo.cpp"
    "${sysvad}/TabletAudioSample/micintopo.cpp"
    "${sysvad}/TabletAudioSample/spdiftopo.cpp"
    "${common}/AudioModuleHelper.cpp"
    "${common}/MiniportAudioEngineNode.cpp"
    "${common}/MiniportStreamAudioEngineNode.cpp"
    "${common}/mintopo.cpp"
    "${common}/minwavert.cpp"
    "${common}/minwavertstream.cpp"
    "${common}/NewDelete.cpp"
    "${common}/speakerhptopo.cpp"
    "${common}/speakertopo.cpp"
    "${common}/micarraytopo.cpp")
set_target_properties(vmex_recording PROPERTIES SUFFIX ".sys")
target_include_directories(vmex_recording PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../../Core"
    "${sysvad}" "${common}" "${sysvad}/TabletAudioSample"
    "${VMEX_WDK_ROOT}/c/Include/10.0.28000.0/km"
    "${VMEX_WDK_ROOT}/c/Include/wdf/kmdf/1.15"
    "${VMEX_SDK_ROOT}/c/Include/10.0.28000.0/shared"
    "${VMEX_SDK_ROOT}/c/Include/10.0.28000.0/um")
target_compile_definitions(vmex_recording PRIVATE _AMD64_ AMD64 _WIN64 UNICODE _UNICODE INITGUID=
    _USE_WAVERT_ _NEW_DELETE_OPERATORS_ NTSTRSAFE_LIB NTDDI_VERSION=0x0A00000C WINVER=0x0A00 _WIN32_WINNT=0x0A00
    WDF_MAJOR_VERSION=1 WDF_MINOR_VERSION=15 KMDF_VERSION_MAJOR=1 KMDF_VERSION_MINOR=15)
target_compile_options(vmex_recording PRIVATE /kernel /GR- /GS /Zl /W3 /utf-8 /EHs-c-
    "$<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_LIST_DIR}/Lease.h>")
target_link_directories(vmex_recording PRIVATE
    "${VMEX_WDK_ROOT}/c/Lib/10.0.28000.0/km/x64"
    "${VMEX_WDK_ROOT}/c/Lib/wdf/kmdf/x64/1.15")
target_link_libraries(vmex_recording PRIVATE ntoskrnl hal portcls stdunk libcntpr ntstrsafe ks wdmsec BufferOverflowFastFailK WdfDriverEntry WdfLdr)
