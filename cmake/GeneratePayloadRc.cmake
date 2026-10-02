if(NOT DEFINED VMEX_RC_OUTPUT OR NOT DEFINED VMEX_PAYLOAD_LAUNCHER OR NOT DEFINED VMEX_PAYLOAD_TAP)
    message(FATAL_ERROR "GeneratePayloadRc: VMEX_RC_OUTPUT, VMEX_PAYLOAD_LAUNCHER and VMEX_PAYLOAD_TAP are required")
endif()

file(TO_CMAKE_PATH "${VMEX_PAYLOAD_LAUNCHER}" vmex_launcher_path)
file(TO_CMAKE_PATH "${VMEX_PAYLOAD_TAP}" vmex_tap_path)

set(vmex_rc "#include <windows.h>\n")
string(APPEND vmex_rc "#include \"PayloadResources.rc.h\"\n\n")
string(APPEND vmex_rc "VMEX_PAYLOAD_ID_LAUNCHER RCDATA \"${vmex_launcher_path}\"\n")
string(APPEND vmex_rc "VMEX_PAYLOAD_ID_TAP RCDATA \"${vmex_tap_path}\"\n")

get_filename_component(vmex_rc_dir "${VMEX_RC_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${vmex_rc_dir}")
file(WRITE "${VMEX_RC_OUTPUT}" "${vmex_rc}")
