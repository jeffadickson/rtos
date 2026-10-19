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
