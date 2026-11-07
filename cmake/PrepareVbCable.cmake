set(vmex_cable_hash "b950e39f01af1d04ea623c8f6d8eb9b6ea5c477c637295fabf20631c85116bfb")
set(vmex_cable_archive "${VMEX_CABLE_CACHE}/VBCABLE_Driver_Pack45.zip")
set(vmex_cable_files
    vbMmeCable64_win10.inf
    vbaudio_cable64_win10.cat
    vbaudio_cable64_win10.sys
    vbaudio_cable64arm_win10.sys)
file(MAKE_DIRECTORY "${VMEX_CABLE_CACHE}")
set(vmex_cable_cached "")
if(EXISTS "${vmex_cable_archive}")
    file(SHA256 "${vmex_cable_archive}" vmex_cable_cached)
endif()
if(NOT vmex_cable_cached STREQUAL vmex_cable_hash)
    file(DOWNLOAD "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip"
        "${vmex_cable_archive}" EXPECTED_HASH "SHA256=${vmex_cable_hash}" TLS_VERIFY ON STATUS vmex_cable_download)
    list(GET vmex_cable_download 0 vmex_cable_code)
    if(NOT vmex_cable_code EQUAL 0)
        message(FATAL_ERROR "VB-CABLE download failed: ${vmex_cable_download}")
    endif()
endif()
file(MAKE_DIRECTORY "${VMEX_CABLE_DESTINATION}")
file(ARCHIVE_EXTRACT INPUT "${vmex_cable_archive}" DESTINATION "${VMEX_CABLE_DESTINATION}"
    PATTERNS ${vmex_cable_files})
set(vmex_cable_resource_id 201)
set(vmex_cable_resource "")
foreach(vmex_cable_file IN LISTS vmex_cable_files)
    string(APPEND vmex_cable_resource "${vmex_cable_resource_id} RCDATA \"${VMEX_CABLE_DESTINATION}/${vmex_cable_file}\"\n")
    math(EXPR vmex_cable_resource_id "${vmex_cable_resource_id} + 1")
endforeach()
file(WRITE "${VMEX_CABLE_DESTINATION}/DriverPayload.rc" "${vmex_cable_resource}")
message(STATUS "VB-CABLE: original Pack45 driver files prepared for embedding")
