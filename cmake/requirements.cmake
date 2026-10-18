nexrt_add_host_tool(requirements_tool NexRT.Requirements tools/NexRT.Requirements/NexRT.Requirements.csproj)
add_custom_target(requirements_check ALL
  COMMAND ${requirements_tool_COMMAND} check ${CMAKE_SOURCE_DIR}
  DEPENDS requirements_tool VERBATIM)
if(NOT RTOS_BUILD_FIRMWARE)
  add_test(NAME requirements_tracker COMMAND ${requirements_tool_COMMAND} check ${CMAKE_SOURCE_DIR})
  add_test(NAME requirements_negative COMMAND ${requirements_tool_COMMAND} self-test ${CMAKE_SOURCE_DIR})
endif()
add_dependencies(docs requirements_check)
