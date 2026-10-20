cmake_minimum_required(VERSION 3.22)
include(cmake/platform.cmake)
project(riscue_rtos VERSION 0.2.0 LANGUAGES CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/configuration.cmake)
include(cmake/host-tools.cmake)
nexrt_add_host_tool(rtos_tools Rtos.Tools tools/Rtos.Tools/Rtos.Tools.csproj)
add_library(rtos_options INTERFACE)
target_include_directories(rtos_options INTERFACE include src)
add_library(nexrt::options ALIAS rtos_options)
foreach(key MAX_THREADS TICK_HZ IDLE_STACK_BYTES MTIME_ADDRESS MTIMECMP_ADDRESS IPC_MAX_MESSAGE_BYTES MAX_HELD_MUTEXES)
  target_compile_definitions(rtos_options INTERFACE RTOS_${key}=${RTOS_${key}})
endforeach()
foreach(key ROUND_ROBIN METRICS STACK_GUARD STACK_WATERMARK SYNC LOGGING)
  target_compile_definitions(rtos_options INTERFACE RTOS_${key}=$<BOOL:${RTOS_${key}}>)
endforeach()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rtos_options INTERFACE -Wall -Wextra -Werror)
endif()
include(cmake/modules.cmake)
if(RTOS_BUILD_FIRMWARE)
  include(cmake/firmware.cmake)
else()
  enable_testing()
  include(cmake/host-tests.cmake)
endif()
find_program(SPHINX_EXECUTABLE NAMES sphinx-build
  HINTS ${CMAKE_SOURCE_DIR}/build/docs-venv/bin ${CMAKE_SOURCE_DIR}/build/docs-venv/Scripts)
if(SPHINX_EXECUTABLE)
  add_custom_target(docs
    COMMAND ${SPHINX_EXECUTABLE} -n -W --keep-going -b html
      ${CMAKE_SOURCE_DIR}/docs ${CMAKE_BINARY_DIR}/docs/html VERBATIM)
else()
  add_custom_target(docs COMMAND ${CMAKE_COMMAND} -E echo
    "Install docs/requirements.txt and reconfigure to locate sphinx-build."
    COMMAND ${CMAKE_COMMAND} -E false VERBATIM)
endif()

include(cmake/requirements.cmake)

include(cmake/static-analysis.cmake)

include(cmake/configure-tool.cmake)

if(RTOS_NATIVE_TOOLS)
  add_custom_target(native_tools DEPENDS rtos_tools requirements_tool configure_tool)
endif()

nexrt_add_host_tool(workbench_tool NexRT.Workbench tools/NexRT.Workbench/NexRT.Workbench.csproj)

if(RTOS_NATIVE_TOOLS)
  add_dependencies(native_tools workbench_tool)
endif()

option(RTOS_SIMULATION "Build native C# RISCue/SharpTestBench simulation tooling" OFF)
if(RTOS_SIMULATION)
  nexrt_add_host_tool(simulation_tool NexRT.Sim tools/NexRT.Sim/NexRT.Sim.csproj)
  if(RTOS_NATIVE_TOOLS)
    add_dependencies(native_tools simulation_tool)
  endif()
  set(RISCUE_SOURCE_DIR /mnt/c/dev/RISCue CACHE PATH "Existing RISCue checkout for simulation")
  set(SHARPTESTBENCH_SOURCE_DIR /mnt/c/dev/SharpTestBench CACHE PATH "Existing SharpTestBench checkout")
  set(RTOS_SIM_ELF ${CMAKE_SOURCE_DIR}/build/uart/demo.elf CACHE FILEPATH "Firmware for UART simulation")
  add_custom_target(sharp_demo
    COMMAND ${simulation_tool_COMMAND} ${CMAKE_SOURCE_DIR} ${RISCUE_SOURCE_DIR} ${SHARPTESTBENCH_SOURCE_DIR} ${RTOS_SIM_ELF}
    DEPENDS simulation_tool USES_TERMINAL VERBATIM)
endif()

include(cmake/arm-simulation.cmake)
