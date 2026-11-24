# Host-only verification. Firmware consumers do not depend on these model libraries.
function(rtos_memcheck target name)
  if(RTOS_HOST_VALGRIND)
    add_test(NAME memcheck_${name} COMMAND ${NXR_VALGRIND_EXECUTABLE}
      --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=all
      --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes
      $<TARGET_FILE:${target}> ${ARGN})
    set_tests_properties(memcheck_${name} PROPERTIES TIMEOUT 120 LABELS memcheck)
    if(NXR_VALGRIND_LIB)
      set_tests_properties(memcheck_${name} PROPERTIES ENVIRONMENT "VALGRIND_LIB=${NXR_VALGRIND_LIB}")
    endif()
  endif()
endfunction()
function(rtos_host_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  endif()
endfunction()

function(rtos_host_test target)
  cmake_parse_arguments(T "SYNC;LOGGING" "NAME" "SOURCES;LIBRARIES" ${ARGN})
  if(T_UNPARSED_ARGUMENTS OR NOT T_NAME OR NOT T_SOURCES)
    message(FATAL_ERROR "Invalid host test declaration: ${target}")
  endif()
  add_executable(${target} ${T_SOURCES})
  target_include_directories(${target} PRIVATE include src tests)
  if(T_SYNC OR T_LOGGING)
    target_compile_definitions(${target} PRIVATE RTOS_SYNC=1)
  endif()
  if(T_LOGGING)
    target_compile_definitions(${target} PRIVATE RTOS_LOGGING=1)
  endif()
  if(T_LIBRARIES)
    target_link_libraries(${target} PRIVATE ${T_LIBRARIES})
  endif()
  rtos_host_warnings(${target})
  add_test(NAME ${T_NAME} COMMAND ${target})
  set_tests_properties(${T_NAME} PROPERTIES TIMEOUT 120)
  rtos_memcheck(${target} ${T_NAME})
endfunction()

# Build the shared synchronization model once, instead of recompiling it per fixture.
add_library(rtos_sync_model STATIC src/sync.cpp src/mutex.cpp)
target_include_directories(rtos_sync_model PRIVATE include src)
target_compile_definitions(rtos_sync_model PRIVATE RTOS_SYNC=1)
rtos_host_warnings(rtos_sync_model)

if(DOTNET_EXECUTABLE)
  add_test(NAME tooling COMMAND ${rtos_tools_COMMAND} self-test)
  add_test(NAME evidence_tool COMMAND ${evidence_tool_COMMAND} self-test)
  set_tests_properties(evidence_tool PROPERTIES TIMEOUT 30)
  add_test(NAME flagship_tool COMMAND ${flagship_tool_COMMAND} self-test)
  set_tests_properties(flagship_tool PROPERTIES TIMEOUT 30)
endif()
rtos_host_test(memory_test NAME memory SOURCES tests/memory.cpp src/memory.cpp LIBRARIES rtos_options)
if(MSVC)
  target_compile_options(memory_test PRIVATE /Oi-)
else()
  target_compile_options(memory_test PRIVATE -fno-builtin)
endif()
rtos_host_test(scheduler_test NAME scheduler SOURCES tests/scheduler.cpp LIBRARIES rtos_options)
if(RTOS_SYNC)
  target_sources(scheduler_test PRIVATE src/sync.cpp)
endif()
rtos_host_test(hw_sched_model_test NAME hw_sched_model SOURCES tests/hw_sched_model.cpp LIBRARIES rtos_options)
if(RTOS_SYNC)
  target_sources(hw_sched_model_test PRIVATE src/sync.cpp)
endif()
if(RTOS_ETHERNET)
  rtos_host_test(ethernet_test NAME ethernet SOURCES tests/ethernet.cpp LIBRARIES rtos_ethernet)
endif()
if(RTOS_WEB)
  rtos_host_test(web_test NAME web SOURCES tests/web.cpp LIBRARIES rtos_web)
endif()
rtos_host_test(static_test NAME static_storage SOURCES tests/static.cpp LIBRARIES rtos_options)
rtos_host_test(sync_test NAME synchronization SYNC SOURCES tests/sync.cpp LIBRARIES rtos_sync_model)
rtos_host_test(queue_test NAME ipc_queue_atomic SYNC SOURCES tests/queue.cpp LIBRARIES rtos_sync_model)
rtos_host_test(mutex_test NAME mutex_inheritance SYNC SOURCES tests/mutex.cpp LIBRARIES rtos_sync_model)
rtos_host_test(log_test NAME queued_printf LOGGING SOURCES tests/log.cpp src/log.cpp LIBRARIES rtos_sync_model)
rtos_host_test(log_edges_test NAME queued_printf_edges LOGGING SOURCES tests/log_edges.cpp src/log.cpp LIBRARIES rtos_sync_model)
rtos_host_test(sync_edges_test NAME synchronization_edges SYNC SOURCES tests/sync_edges.cpp LIBRARIES rtos_sync_model)
rtos_host_test(sync_deadlines_test NAME synchronization_deadlines SYNC SOURCES tests/sync_deadlines.cpp LIBRARIES rtos_sync_model)
rtos_host_test(public_api_test NAME ipc_public_api SYNC SOURCES tests/public_api.cpp LIBRARIES rtos_sync_model)
rtos_host_test(stack_watermark_test NAME stack_watermark SOURCES tests/stack_watermark.cpp)
if(RTOS_DMA)
  rtos_host_test(dma_test NAME dma_ownership SYNC SOURCES tests/dma.cpp LIBRARIES rtos_dma rtos_sync_model)
endif()
rtos_host_test(ipc_random_test NAME ipc_random_seed_1 SYNC SOURCES tests/ipc_random.cpp LIBRARIES rtos_sync_model)
foreach(seed IN ITEMS 42 3735928559 4294967295)
  add_test(NAME ipc_random_seed_${seed} COMMAND ipc_random_test ${seed})
  set_tests_properties(ipc_random_seed_${seed} PROPERTIES TIMEOUT 120)
  rtos_memcheck(ipc_random_test ipc_random_seed_${seed} ${seed})
endforeach()
rtos_host_test(ipc_wait_order_test NAME ipc_wait_order SYNC SOURCES tests/ipc_wait_order.cpp LIBRARIES rtos_sync_model)
rtos_host_test(safe_concurrency_test NAME safe_concurrency SYNC SOURCES tests/safe_concurrency.cpp LIBRARIES rtos_sync_model)
rtos_host_test(rtos_gaps_test NAME rtos_gaps SYNC SOURCES tests/rtos_gaps.cpp LIBRARIES rtos_sync_model)
rtos_host_test(storage_test NAME storage SOURCES tests/storage.cpp src/storage.cpp LIBRARIES rtos_options)
rtos_host_test(flash_emulators_test NAME flash_emulators SOURCES tests/flash_emulators.cpp src/storage.cpp LIBRARIES rtos_options)
rtos_host_test(nand_lifecycle_sim_test NAME nand_lifecycle_sim SOURCES tests/nand_lifecycle_sim.cpp src/storage.cpp LIBRARIES rtos_options)

find_program(NXR_DAFNY_EXECUTABLE NAMES dafny HINTS "$ENV{HOME}/.dotnet/tools")
find_program(NXR_Z3_EXECUTABLE NAMES z3)
if(NXR_DAFNY_EXECUTABLE AND NXR_Z3_EXECUTABLE AND NOT RTOS_HOST_SANITIZERS AND NOT RTOS_HOST_VALGRIND)
  foreach(dfy_name IN ITEMS SchedulerProof LockFreedomProof DmaOwnershipProof TmrVoterProof StorageProof)
    add_test(NAME dafny_${dfy_name}
      COMMAND ${NXR_DAFNY_EXECUTABLE} verify --solver-path ${NXR_Z3_EXECUTABLE}
              "${CMAKE_SOURCE_DIR}/formal/${dfy_name}.dfy")
    set_tests_properties(dafny_${dfy_name} PROPERTIES TIMEOUT 120)
  endforeach()
endif()

find_program(NXR_VERILATOR_EXECUTABLE NAMES verilator)
if(NXR_VERILATOR_EXECUTABLE AND NOT RTOS_HOST_SANITIZERS AND NOT RTOS_HOST_VALGRIND)
  set(nxr_v_sched_dir "${CMAKE_BINARY_DIR}/verilator_hw_sched")
  set(nxr_v_sched_bin "${nxr_v_sched_dir}/Vnexrt_hw_sched")
  add_custom_command(
    OUTPUT "${nxr_v_sched_bin}"
    COMMAND ${NXR_VERILATOR_EXECUTABLE} -Wall --cc --exe --build -j 0
      --Mdir "${nxr_v_sched_dir}"
      -CFLAGS "-std=c++17 -O2 -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_SOURCE_DIR}/src -UNDEBUG"
      "${CMAKE_SOURCE_DIR}/rtl/nexrt_hw_sched.sv"
      "${CMAKE_SOURCE_DIR}/tests/hw_sched_verilator.cpp"
    DEPENDS "${CMAKE_SOURCE_DIR}/rtl/nexrt_hw_sched.sv"
            "${CMAKE_SOURCE_DIR}/tests/hw_sched_verilator.cpp"
            "${CMAKE_SOURCE_DIR}/include/rtos_hw_sched.hpp"
    VERBATIM)
  add_custom_target(hw_sched_verilator_sim ALL DEPENDS "${nxr_v_sched_bin}")
  add_test(NAME hw_sched_verilator COMMAND "${nxr_v_sched_bin}")
  set_tests_properties(hw_sched_verilator PROPERTIES TIMEOUT 120)

  set(nxr_v_rf_dir "${CMAKE_BINARY_DIR}/verilator_banked_rf")
  set(nxr_v_rf_bin "${nxr_v_rf_dir}/Vriscue_banked_rf")
  add_custom_command(
    OUTPUT "${nxr_v_rf_bin}"
    COMMAND ${NXR_VERILATOR_EXECUTABLE} -Wall --cc --exe --build -j 0
      --Mdir "${nxr_v_rf_dir}"
      -CFLAGS "-std=c++17 -O2 -UNDEBUG"
      "${CMAKE_SOURCE_DIR}/rtl/riscue_banked_rf.sv"
      "${CMAKE_SOURCE_DIR}/tests/banked_rf_verilator.cpp"
    DEPENDS "${CMAKE_SOURCE_DIR}/rtl/riscue_banked_rf.sv"
            "${CMAKE_SOURCE_DIR}/tests/banked_rf_verilator.cpp"
    VERBATIM)
  add_custom_target(banked_rf_verilator_sim ALL DEPENDS "${nxr_v_rf_bin}")
  add_test(NAME banked_rf_verilator COMMAND "${nxr_v_rf_bin}")
  set_tests_properties(banked_rf_verilator PROPERTIES TIMEOUT 120)

  if(DOTNET_EXECUTABLE)
    add_test(NAME flagship_evidence
      COMMAND ${flagship_tool_COMMAND} verify "${nxr_v_sched_bin}" "${nxr_v_rf_bin}"
              "${CMAKE_BINARY_DIR}/flagship-evidence.json")
    set_tests_properties(flagship_evidence PROPERTIES TIMEOUT 120)
  endif()

  if(NXR_DAFNY_EXECUTABLE AND NXR_Z3_EXECUTABLE AND DOTNET_EXECUTABLE)
    file(GLOB_RECURSE NXR_DAFNY_RUNTIME_DLLS
      "$ENV{HOME}/.dotnet/tools/.store/dafny/*/DafnyRuntime.dll")
    if(NXR_DAFNY_RUNTIME_DLLS)
      list(GET NXR_DAFNY_RUNTIME_DLLS 0 NXR_DAFNY_RUNTIME_DLL)
      set(nxr_dfy_cs_dir "${CMAKE_BINARY_DIR}/dafny_cs")
      set(nxr_dfy_cs_file "${nxr_dfy_cs_dir}/SchedulerProof.cs")
      set(nxr_formal_oracle_out "${CMAKE_BINARY_DIR}/formal-oracle")
      set(nxr_formal_oracle_dll "${nxr_formal_oracle_out}/NexRT.FormalOracle.dll")
      add_custom_command(
        OUTPUT "${nxr_dfy_cs_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${nxr_dfy_cs_dir}"
        COMMAND ${NXR_DAFNY_EXECUTABLE} translate cs
          --solver-path "${NXR_Z3_EXECUTABLE}"
          --output "${nxr_dfy_cs_dir}/SchedulerProof"
          "${CMAKE_SOURCE_DIR}/formal/SchedulerProof.dfy"
        DEPENDS "${CMAKE_SOURCE_DIR}/formal/SchedulerProof.dfy"
        VERBATIM)
      add_custom_command(
        OUTPUT "${nxr_formal_oracle_dll}"
        COMMAND ${DOTNET_EXECUTABLE} build
          "${CMAKE_SOURCE_DIR}/tools/NexRT.FormalOracle/NexRT.FormalOracle.csproj"
          --configuration Release --nologo
          --output "${nxr_formal_oracle_out}"
          "-p:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/formal-oracle-obj/"
          "-p:GeneratedDafnyCs=${nxr_dfy_cs_file}"
          "-p:DafnyRuntimeDll=${NXR_DAFNY_RUNTIME_DLL}"
        DEPENDS "${nxr_dfy_cs_file}"
                "${CMAKE_SOURCE_DIR}/tools/NexRT.FormalOracle/Program.cs"
                "${CMAKE_SOURCE_DIR}/tools/NexRT.FormalOracle/NexRT.FormalOracle.csproj"
                "${CMAKE_SOURCE_DIR}/tools/HostProcess.cs"
        VERBATIM)
      add_custom_target(formal_oracle_tool ALL DEPENDS "${nxr_formal_oracle_dll}")
      add_test(NAME dafny_rtl_lockstep
        COMMAND ${DOTNET_EXECUTABLE} "${nxr_formal_oracle_dll}" "${nxr_v_sched_bin}")
      set_tests_properties(dafny_rtl_lockstep PROPERTIES TIMEOUT 120)
    endif()
  endif()
endif()

# Deliberately invalid probes are never ordinary tests or firmware dependencies.
if(RTOS_HOST_SANITIZERS OR RTOS_HOST_VALGRIND)
  add_executable(memory_negative_probe tests/memory_negative.cpp)
  rtos_host_warnings(memory_negative_probe)
  if(RTOS_HOST_SANITIZERS)
    foreach(mode IN ITEMS asan ubsan)
      add_test(NAME negative_${mode} COMMAND ${evidence_tool_COMMAND} probe ${mode}
        $<TARGET_FILE:memory_negative_probe> ${CMAKE_BINARY_DIR}/negative-${mode}.log)
      set_tests_properties(negative_${mode} PROPERTIES TIMEOUT 40)
    endforeach()
  else()
    add_test(NAME negative_valgrind COMMAND ${evidence_tool_COMMAND} probe valgrind
      $<TARGET_FILE:memory_negative_probe> ${CMAKE_BINARY_DIR}/negative-valgrind.log
      ${NXR_VALGRIND_EXECUTABLE} "${NXR_VALGRIND_LIB}")
    set_tests_properties(negative_valgrind PROPERTIES TIMEOUT 40 LABELS memcheck)
  endif()
endif()
