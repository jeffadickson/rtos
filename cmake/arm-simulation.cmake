option(RTOS_ARM_SIMULATION "Build native C# QEMU ARM stress runner" OFF)
if(RTOS_ARM_SIMULATION)
  nexrt_add_host_tool(arm_sim_tool NexRT.ArmSim tools/NexRT.ArmSim/NexRT.ArmSim.csproj)
  if(TARGET native_tools)
    add_dependencies(native_tools arm_sim_tool)
  endif()
  if(NOT RTOS_BUILD_FIRMWARE AND RTOS_NATIVE_TOOLS)
    add_test(NAME arm_runner_process COMMAND ${arm_sim_tool_COMMAND} --self-test)
    set_tests_properties(arm_runner_process PROPERTIES TIMEOUT 30)
  endif()
  if(RTOS_TARGET_ID STREQUAL "qemu-mps2-m3" AND TARGET demo)
    find_program(NXR_QEMU_ARM_EXECUTABLE NAMES qemu-system-arm REQUIRED)
    add_custom_target(arm_stress
      COMMAND ${arm_sim_tool_COMMAND} ${NXR_QEMU_ARM_EXECUTABLE}
        $<TARGET_FILE:demo> ${CMAKE_BINARY_DIR}/runs
      DEPENDS demo arm_sim_tool USES_TERMINAL VERBATIM)
    if(RTOS_CAMEO_EMBED)
      add_custom_target(arm_cameo_embed
        COMMAND ${arm_sim_tool_COMMAND} --cameo-embed ${NXR_QEMU_ARM_EXECUTABLE}
          $<TARGET_FILE:demo> ${CMAKE_SOURCE_DIR}/tests/scripts/mps2_live_probe.hex
        DEPENDS demo arm_sim_tool USES_TERMINAL VERBATIM)
    endif()
  endif()
endif()
