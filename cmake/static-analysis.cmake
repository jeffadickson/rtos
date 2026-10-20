# Optional developer targets: never add analysis to ALL or firmware dependencies.
find_program(NXR_CPPCHECK_EXECUTABLE NAMES cppcheck)
find_program(NXR_CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy-18)

# Use only C++ translation units actually configured in this build. Headers are
# reached through their includes; startup/context assembly is outside these tools.
get_property(nxr_analysis_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(nxr_analysis_sources)
foreach(nxr_target IN LISTS nxr_analysis_targets)
  get_target_property(nxr_target_sources ${nxr_target} SOURCES)
  foreach(nxr_source IN LISTS nxr_target_sources)
    if(nxr_source MATCHES "\\.(cpp|cc|cxx)$")
      get_filename_component(nxr_source "${nxr_source}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
      list(APPEND nxr_analysis_sources "${nxr_source}")
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES nxr_analysis_sources)

# Ninja/Make generators supply the database. Explicit targets fail if unavailable
# rather than silently claiming success with incomplete include/feature settings.
if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  if(NXR_CPPCHECK_EXECUTABLE)
    set(nxr_cppcheck_target_args)
    if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(riscv32|cortex-m3)$")
      list(APPEND nxr_cppcheck_target_args --platform=unix32)
    endif()
    add_custom_target(cppcheck
      COMMAND ${NXR_CPPCHECK_EXECUTABLE} --version
      COMMAND ${NXR_CPPCHECK_EXECUTABLE}
        --project=${CMAKE_BINARY_DIR}/compile_commands.json
        --enable=warning,performance,portability --error-exitcode=1
        --std=c++17 --inline-suppr --template=gcc
        --suppressions-list=${CMAKE_SOURCE_DIR}/config/cppcheck-suppressions.txt
        ${nxr_cppcheck_target_args}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} USES_TERMINAL VERBATIM)
  endif()
  if(NXR_CLANG_TIDY_EXECUTABLE)
    set(nxr_tidy_target_args)
    if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv32")
      list(APPEND nxr_tidy_target_args --extra-arg=--target=riscv32-unknown-elf)
    elseif(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR STREQUAL "cortex-m3")
      list(APPEND nxr_tidy_target_args --extra-arg=--target=arm-none-eabi)
    endif()
    add_custom_target(clang_tidy
      COMMAND ${NXR_CLANG_TIDY_EXECUTABLE} --version
      COMMAND ${NXR_CLANG_TIDY_EXECUTABLE}
        -p=${CMAKE_BINARY_DIR} --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
        ${nxr_tidy_target_args} ${nxr_analysis_sources}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} USES_TERMINAL VERBATIM)
  endif()
endif()
foreach(nxr_analyzer IN ITEMS cppcheck clang_tidy)
  if(NOT TARGET ${nxr_analyzer})
    add_custom_target(${nxr_analyzer}
      COMMAND ${CMAKE_COMMAND} -E echo
        "${nxr_analyzer} requires its executable and a Ninja/Makefiles compile_commands.json. Install the tool or set NXR_CPPCHECK_EXECUTABLE/NXR_CLANG_TIDY_EXECUTABLE, then reconfigure."
      COMMAND ${CMAKE_COMMAND} -E false VERBATIM)
  endif()
endforeach()
add_custom_target(cpp_analysis DEPENDS cppcheck clang_tidy)

find_program(NXR_CLANG_EXECUTABLE NAMES clang++)
if(NXR_CLANG_EXECUTABLE)
  add_custom_target(static_analysis
    COMMAND ${NXR_CLANG_EXECUTABLE} --target=riscv32-unknown-elf -march=rv32im_zicsr -mabi=ilp32
      -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_SOURCE_DIR}/src -I${CMAKE_SOURCE_DIR}/ports/riscue
      -DRTOS_MAX_THREADS=${RTOS_MAX_THREADS} -DRTOS_TICK_HZ=${RTOS_TICK_HZ}
      -DRTOS_IDLE_STACK_BYTES=${RTOS_IDLE_STACK_BYTES} -DRTOS_ROUND_ROBIN=$<BOOL:${RTOS_ROUND_ROBIN}>
      -DRTOS_METRICS=$<BOOL:${RTOS_METRICS}> -DRTOS_STACK_GUARD=$<BOOL:${RTOS_STACK_GUARD}>
      -DRTOS_MTIME_ADDRESS=${RTOS_MTIME_ADDRESS} -DRTOS_MTIMECMP_ADDRESS=${RTOS_MTIMECMP_ADDRESS}
      --analyze -Xanalyzer -analyzer-output=text
      ${CMAKE_SOURCE_DIR}/src/kernel.cpp ${CMAKE_SOURCE_DIR}/src/ethernet.cpp ${CMAKE_SOURCE_DIR}/src/memory.cpp
    VERBATIM)
else()
  add_custom_target(static_analysis COMMAND ${CMAKE_COMMAND} -E echo "Install Clang with RV32 support for static analysis."
    COMMAND ${CMAKE_COMMAND} -E false VERBATIM)
endif()

if(NXR_CLANG_EXECUTABLE)
  add_custom_target(sync_static_analysis
    COMMAND ${NXR_CLANG_EXECUTABLE} --target=riscv32-unknown-elf -march=rv32im_zicsr -mabi=ilp32
      -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -DRTOS_SYNC=1 -DRTOS_LOGGING=1
      -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_SOURCE_DIR}/src -I${CMAKE_SOURCE_DIR}/ports/riscue
      --analyze -Xanalyzer -analyzer-output=text
      ${CMAKE_SOURCE_DIR}/src/kernel.cpp ${CMAKE_SOURCE_DIR}/src/sync.cpp
      ${CMAKE_SOURCE_DIR}/src/mutex.cpp ${CMAKE_SOURCE_DIR}/src/log.cpp
    VERBATIM)
endif()
