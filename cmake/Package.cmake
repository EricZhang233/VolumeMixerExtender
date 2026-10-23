if(NOT DEFINED VMEX_PACKAGE_DIR OR NOT DEFINED VMEX_PACKAGE_NAME OR
   NOT DEFINED VMEX_EXECUTABLE OR NOT DEFINED VMEX_CLI_EXECUTABLE OR
   NOT DEFINED VMEX_CORE_DLL OR NOT DEFINED VMEX_MANAGER_EXECUTABLE OR
   NOT DEFINED VMEX_TAP_DLL OR NOT DEFINED VMEX_LAUNCHER_DLL OR
   NOT DEFINED VMEX_DIAGNOSTICS_DLL)
    message(FATAL_ERROR "Package: package directory, name, vmex, vmex_cli, vmex_core, manager and payloads are required")
endif()

if(DEFINED VMEX_CONFIG AND NOT VMEX_CONFIG STREQUAL "Release")
    return()
endif()

set(vmex_stage "${VMEX_PACKAGE_DIR}/${VMEX_PACKAGE_NAME}")

file(REMOVE_RECURSE "${vmex_stage}")
file(MAKE_DIRECTORY "${vmex_stage}")
file(COPY "${VMEX_EXECUTABLE}" DESTINATION "${vmex_stage}")
file(COPY "${VMEX_CLI_EXECUTABLE}" DESTINATION "${vmex_stage}")
file(COPY "${VMEX_CORE_DLL}" DESTINATION "${vmex_stage}")
file(COPY "${VMEX_MANAGER_EXECUTABLE}" DESTINATION "${vmex_stage}")
file(COPY "${VMEX_TAP_DLL}" DESTINATION "${vmex_stage}")
file(COPY "${VMEX_LAUNCHER_DLL}" DESTINATION "${vmex_stage}")
file(COPY "${VMEX_DIAGNOSTICS_DLL}" DESTINATION "${vmex_stage}")

file(REMOVE "${VMEX_PACKAGE_DIR}/${VMEX_PACKAGE_NAME}.zip")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${VMEX_PACKAGE_DIR}/${VMEX_PACKAGE_NAME}.zip"
        --format=zip vmex.exe vmex_cli.exe vmex_core.dll vmex_manager.exe
        vmex_tap.dll vmex_launcher.dll xamldiagnostics.dll
    WORKING_DIRECTORY "${vmex_stage}"
    RESULT_VARIABLE vmex_tar_result)

if(NOT vmex_tar_result EQUAL 0)
    message(FATAL_ERROR "Package: zip creation failed with ${vmex_tar_result}")
endif()

file(REMOVE_RECURSE "${vmex_stage}")

message(STATUS "Package: ${VMEX_PACKAGE_DIR}/${VMEX_PACKAGE_NAME}.zip")
