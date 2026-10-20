# Explicit prerequisite targets: report missing tools without installing them.
# Configuration itself needs CMake/Ninja, a compiler and .NET; the packaged
# standalone doctor diagnoses these before a new workspace can configure.
set(nxr_dependency_profiles host)
if(RTOS_BUILD_FIRMWARE)
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "cortex-m3")
    list(APPEND nxr_dependency_profiles arm)
  else()
    list(APPEND nxr_dependency_profiles riscv)
  endif()
endif()
if(RTOS_SIMULATION)
  list(APPEND nxr_dependency_profiles rtl)
endif()
set(nxr_dependency_options)
foreach(pair IN ITEMS "arm-prefix|ARM_TOOLCHAIN_PREFIX" "riscv-prefix|RISCV_TOOLCHAIN_PREFIX"
    "qemu|NXR_QEMU_ARM_EXECUTABLE" "sphinx|SPHINX_EXECUTABLE"
    "cppcheck|NXR_CPPCHECK_EXECUTABLE" "clang-tidy|NXR_CLANG_TIDY_EXECUTABLE"
    "riscue|RISCUE_SOURCE_DIR" "sharp|SHARPTESTBENCH_SOURCE_DIR")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 option)
  list(GET fields 1 variable)
  if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "" AND NOT "${${variable}}" MATCHES "-NOTFOUND$")
    list(APPEND nxr_dependency_options "--${option}" "${${variable}}")
  endif()
endforeach()
list(JOIN nxr_dependency_profiles "," nxr_dependency_selection)
add_custom_target(check_dependencies
  COMMAND ${handoff_tool_COMMAND} doctor "${CMAKE_SOURCE_DIR}" "${nxr_dependency_selection}" ${nxr_dependency_options}
  DEPENDS handoff_tool USES_TERMINAL VERBATIM)
add_custom_target(check_all_dependencies
  COMMAND ${handoff_tool_COMMAND} doctor "${CMAKE_SOURCE_DIR}" all ${nxr_dependency_options}
  DEPENDS handoff_tool USES_TERMINAL VERBATIM)
if(NOT RTOS_BUILD_FIRMWARE)
  add_test(NAME handoff_roundtrip COMMAND ${handoff_tool_COMMAND} self-test)
  set_tests_properties(handoff_roundtrip PROPERTIES TIMEOUT 60)
endif()
