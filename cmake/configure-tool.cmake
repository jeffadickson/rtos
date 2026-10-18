nexrt_add_host_tool(configure_tool NexRT.Configure tools/NexRT.Configure/NexRT.Configure.csproj)
if(NOT RTOS_BUILD_FIRMWARE)
  add_test(NAME configuration COMMAND ${configure_tool_COMMAND} self-test ${CMAKE_SOURCE_DIR})
endif()
