if(NOT DEFINED VMEX_RC_OUTPUT OR NOT DEFINED VMEX_PAYLOAD_LAUNCHER OR
   NOT DEFINED VMEX_PAYLOAD_TAP OR NOT DEFINED VMEX_PAYLOAD_DIAGNOSTICS OR
   NOT DEFINED VMEX_NOTIFICATION_SCRIPT OR NOT DEFINED VMEX_PAYLOAD_CORE)
    message(FATAL_ERROR "GeneratePayloadRc: all payload paths are required")
endif()

file(TO_CMAKE_PATH "${VMEX_PAYLOAD_LAUNCHER}" vmex_launcher_path)
file(TO_CMAKE_PATH "${VMEX_PAYLOAD_TAP}" vmex_tap_path)
file(TO_CMAKE_PATH "${VMEX_PAYLOAD_DIAGNOSTICS}" vmex_diagnostics_path)
file(TO_CMAKE_PATH "${VMEX_NOTIFICATION_SCRIPT}" vmex_notification_script_path)
file(TO_CMAKE_PATH "${VMEX_PAYLOAD_CORE}" vmex_core_path)

set(vmex_rc "#include <windows.h>\n")
string(APPEND vmex_rc "#include \"PayloadResources.rc.h\"\n\n")
string(APPEND vmex_rc "VMEX_PAYLOAD_ID_LAUNCHER RCDATA \"${vmex_launcher_path}\"\n")
string(APPEND vmex_rc "VMEX_PAYLOAD_ID_TAP RCDATA \"${vmex_tap_path}\"\n")
string(APPEND vmex_rc "VMEX_PAYLOAD_ID_DIAGNOSTICS RCDATA \"${vmex_diagnostics_path}\"\n")
string(APPEND vmex_rc "VMEX_PAYLOAD_ID_NOTIFICATION_TOOLKIT RCDATA \"${vmex_notification_script_path}\"\n")
string(APPEND vmex_rc "VMEX_PAYLOAD_ID_CORE RCDATA \"${vmex_core_path}\"\n")

get_filename_component(vmex_rc_dir "${VMEX_RC_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${vmex_rc_dir}")
file(WRITE "${VMEX_RC_OUTPUT}" "${vmex_rc}")
