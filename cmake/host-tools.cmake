# These executables run on the BUILD HOST, never on the firmware architecture.
find_program(DOTNET_EXECUTABLE NAMES dotnet REQUIRED)
# Native AOT publishers are memory-intensive. Keep fresh-machine Ninja builds
# from launching every SDK/linker concurrently while C++ work stays parallel.
set(RTOS_HOST_TOOL_JOBS 1 CACHE STRING "Concurrent C# host-tool publishers (Ninja)")
if(NOT RTOS_HOST_TOOL_JOBS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "RTOS_HOST_TOOL_JOBS must be a positive integer")
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS nexrt_host_tools=${RTOS_HOST_TOOL_JOBS})
option(RTOS_NATIVE_TOOLS "Publish and execute C# host tools as Native AOT binaries" ON)
string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" NXR_HOST_PROCESSOR)
if(NXR_HOST_PROCESSOR MATCHES "^(x86_64|amd64)$")
  set(NXR_HOST_ARCH x64)
elseif(NXR_HOST_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(NXR_HOST_ARCH arm64)
else()
  set(NXR_HOST_ARCH unsupported)
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  if(EXISTS /etc/alpine-release)
    set(NXR_HOST_OS linux-musl)
  else()
    set(NXR_HOST_OS linux)
  endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(NXR_HOST_OS win)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(NXR_HOST_OS osx)
else()
  set(NXR_HOST_OS unsupported)
endif()
if(RTOS_NATIVE_TOOLS AND (NXR_HOST_ARCH STREQUAL "unsupported" OR NXR_HOST_OS STREQUAL "unsupported"))
  message(FATAL_ERROR "Native AOT host is unsupported; explicitly select -DRTOS_NATIVE_TOOLS=OFF for managed tooling")
endif()
set(NXR_HOST_RID "${NXR_HOST_OS}-${NXR_HOST_ARCH}")
set(RTOS_PREBUILT_NATIVE_TOOLS "" CACHE PATH "Validated build-host native tool bundle")
function(nexrt_add_host_tool target assembly project)
  get_filename_component(project_directory "${CMAKE_SOURCE_DIR}/${project}" DIRECTORY)
  file(GLOB tool_sources CONFIGURE_DEPENDS "${project_directory}/*.cs")
  if(EXISTS "${CMAKE_SOURCE_DIR}/Directory.Build.props")
    list(APPEND tool_sources "${CMAKE_SOURCE_DIR}/Directory.Build.props")
  endif()
  if(assembly STREQUAL "NexRT.ArmSim" OR assembly STREQUAL "NexRT.Sim" OR assembly STREQUAL "Rtos.Tools" OR assembly STREQUAL "NexRT.Evidence" OR assembly STREQUAL "NexRT.Handoff")
    list(APPEND tool_sources "${CMAKE_SOURCE_DIR}/tools/HostProcess.cs")
  endif()
  set(fingerprint_input "${NXR_HOST_RID}")
  foreach(input IN LISTS tool_sources)
    file(SHA256 "${input}" digest)
    string(APPEND fingerprint_input "${input}:${digest}\n")
  endforeach()
  file(SHA256 "${CMAKE_SOURCE_DIR}/${project}" digest)
  string(APPEND fingerprint_input "${project}:${digest}")
  string(SHA256 fingerprint "${fingerprint_input}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${tool_sources} "${CMAKE_SOURCE_DIR}/${project}")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tool-fingerprints")
  # Preserve mtime for unchanged content; a changed fingerprint must republish
  # even when an earlier build finished after the source modification timestamp.
  set(fingerprint_file "${CMAKE_BINARY_DIR}/tool-fingerprints/${target}.txt")
  file(CONFIGURE OUTPUT "${fingerprint_file}" CONTENT "${fingerprint}" @ONLY)
  if(RTOS_NATIVE_TOOLS AND RTOS_PREBUILT_NATIVE_TOOLS)
    get_filename_component(bundle "${RTOS_PREBUILT_NATIVE_TOOLS}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(CMAKE_HOST_WIN32)
      set(suffix .exe)
    else()
      set(suffix "")
    endif()
    set(output "${bundle}/${assembly}/${assembly}${suffix}")
    if(NOT EXISTS "${output}" OR NOT EXISTS "${output}.sources")
      message(FATAL_ERROR "Build native host tools first; missing ${output} or source fingerprint")
    endif()
    file(READ "${output}.sources" actual_fingerprint)
    if(NOT actual_fingerprint STREQUAL fingerprint)
      message(FATAL_ERROR "Stale or wrong-host native tool: ${assembly}; rebuild host tools")
    endif()
    add_custom_target(${target} ALL DEPENDS "${output}" "${output}.sources")
    set(${target}_COMMAND "${output}" PARENT_SCOPE)
    set(${target}_OUTPUT "${output}" PARENT_SCOPE)
    return()
  endif()
  if(RTOS_NATIVE_TOOLS)
    set(output_directory "${CMAKE_BINARY_DIR}/native-tools/${assembly}")
    if(CMAKE_HOST_WIN32)
      set(suffix .exe)
    else()
      set(suffix "")
    endif()
    set(output "${output_directory}/${assembly}${suffix}")
    add_custom_command(OUTPUT "${output}" "${output_directory}/${assembly}.xml" "${output}.sources"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${output_directory}"
      COMMAND ${DOTNET_EXECUTABLE} publish "${CMAKE_SOURCE_DIR}/${project}"
        --configuration Release --runtime ${NXR_HOST_RID} --self-contained true --output "${output_directory}" --nologo
        --property:PublishAot=true --property:IlcTreatWarningsAsErrors=true --property:ILLinkTreatWarningsAsErrors=true
        --property:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/native-obj/${target}/
        --property:BaseOutputPath=${CMAKE_BINARY_DIR}/host-tool-bin/${target}/
        --property:DocumentationFile=${output_directory}/${assembly}.xml
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/tool-fingerprints/${target}.txt" "${output}.sources"
      COMMAND ${CMAKE_COMMAND} -E touch "${output}" "${output_directory}/${assembly}.xml"
      DEPENDS "${CMAKE_SOURCE_DIR}/${project}" ${tool_sources} "${fingerprint_file}"
      JOB_POOL nexrt_host_tools VERBATIM)
    set(command "${output}")
    message(STATUS "${assembly}: Native AOT host executable (${NXR_HOST_RID})")
  else()
    set(output_directory "${CMAKE_BINARY_DIR}/managed-tools/${assembly}")
    set(output "${output_directory}/${assembly}.dll")
    add_custom_command(OUTPUT "${output}" "${output_directory}/${assembly}.xml" "${output}.sources"
      COMMAND ${DOTNET_EXECUTABLE} build "${CMAKE_SOURCE_DIR}/${project}" --configuration Release
        --output "${output_directory}" --nologo --property:PublishAot=false
        --property:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/managed-obj/${target}/
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/tool-fingerprints/${target}.txt" "${output}.sources"
      COMMAND ${CMAKE_COMMAND} -E touch "${output}" "${output_directory}/${assembly}.xml"
      DEPENDS "${CMAKE_SOURCE_DIR}/${project}" ${tool_sources} "${fingerprint_file}"
      JOB_POOL nexrt_host_tools VERBATIM)
    set(command "${DOTNET_EXECUTABLE};${output}")
  endif()
  add_custom_target(${target} ALL DEPENDS "${output}" "${output_directory}/${assembly}.xml" "${output}.sources")
  set(${target}_COMMAND "${command}" PARENT_SCOPE)
  set(${target}_OUTPUT "${output}" PARENT_SCOPE)
endfunction()
