  if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Firmware requires cmake/riscue-toolchain.cmake")
  endif()
  add_library(rtos STATIC src/kernel.cpp src/memory.cpp ports/riscue/context.S)
  target_link_libraries(rtos PUBLIC rtos_options)
  if(RTOS_SYNC)
    target_sources(rtos PRIVATE src/sync.cpp src/mutex.cpp)
  endif()
  if(RTOS_LOGGING)
    target_sources(rtos PRIVATE src/log.cpp)
  endif()
  target_include_directories(rtos PRIVATE ports/riscue)
  add_library(nexrt::kernel ALIAS rtos)
  target_compile_options(rtos PRIVATE -Os -fstack-usage -ffunction-sections -fdata-sections)
  set(RTOS_APPLICATION examples/blink.cpp CACHE FILEPATH "Application source")
  add_executable(demo ${RTOS_APPLICATION} ports/riscue/start.S)
  target_compile_options(demo PRIVATE -fstack-usage)
  target_link_libraries(demo PRIVATE rtos gcc)
  set_target_properties(demo PROPERTIES SUFFIX .elf)
  target_link_options(demo PRIVATE -nostdlib -T${CMAKE_SOURCE_DIR}/ports/riscue/link.ld
    -Wl,--gc-sections,--no-relax,-Map,${CMAKE_BINARY_DIR}/demo.map)
  set_property(TARGET demo APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/ports/riscue/link.ld)
  set_property(TARGET demo APPEND PROPERTY LINK_DEPENDS ${rtos_tools_OUTPUT})
  add_dependencies(demo rtos_tools)
  add_custom_command(TARGET demo POST_BUILD
    COMMAND ${rtos_tools_COMMAND} audit
      --nm ${CMAKE_NM} --elf $<TARGET_FILE:demo>
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:demo> VERBATIM)
  add_custom_target(static_contract_check
    COMMAND ${rtos_tools_COMMAND} static-contract
      --cxx ${CMAKE_CXX_COMPILER} --nm ${CMAKE_NM}
      --source-root ${CMAKE_SOURCE_DIR} --build-dir ${CMAKE_BINARY_DIR}/static-contract
    DEPENDS rtos_tools VERBATIM)

if(RTOS_WEB)
    add_executable(web_link_probe examples/web_probe.cpp ports/riscue/start.S)
    target_include_directories(web_link_probe PRIVATE generated)
    target_compile_options(web_link_probe PRIVATE -Os)
    target_link_libraries(web_link_probe PRIVATE rtos_web rtos gcc)
    target_link_options(web_link_probe PRIVATE -nostdlib -T${CMAKE_SOURCE_DIR}/ports/riscue/link.ld -Wl,--gc-sections,--no-relax)
    set_target_properties(web_link_probe PROPERTIES SUFFIX .elf)
    add_dependencies(web_link_probe rtos_tools)
    add_custom_command(TARGET web_link_probe POST_BUILD COMMAND ${rtos_tools_COMMAND} audit --nm ${CMAKE_NM} --elf $<TARGET_FILE:web_link_probe> VERBATIM)
endif()
