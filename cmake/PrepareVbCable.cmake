set(vmex_cable_hash "b950e39f01af1d04ea623c8f6d8eb9b6ea5c477c637295fabf20631c85116bfb")
set(vmex_cable_archive "${VMEX_CABLE_CACHE}/VBCABLE_Driver_Pack45.zip")
set(vmex_cable_files
    vbMmeCable64_win10.inf
    vbaudio_cable64_win10.cat
    vbaudio_cable64_win10.sys
    vbaudio_cable64arm_win10.sys
    VBCABLE_ControlPanel.exe
    VBCABLE_Setup_x64.exe
    pin_in.ico
    pin_out.ico
    readme.txt)
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
file(REMOVE_RECURSE "${VMEX_CABLE_DESTINATION}")
file(MAKE_DIRECTORY "${VMEX_CABLE_DESTINATION}")
file(ARCHIVE_EXTRACT INPUT "${vmex_cable_archive}" DESTINATION "${VMEX_CABLE_DESTINATION}"
    PATTERNS ${vmex_cable_files})
message(STATUS "VB-CABLE: Windows 11 x64 files prepared from the original Pack45 package")
