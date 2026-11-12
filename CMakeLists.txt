# =============================================================================
# Earth Siege 2 - static recompilation hosts (32-bit, MSVC x86)
#
#   py -3 tools/catalog.py && py -3 run_lift.py
#   build.cmd                # -> build/es2shell.exe (VSHELL.EXE), build/es2sim.exe (DBSIM.EXE)
#   build\es2shell.exe --play
# =============================================================================
cmake_minimum_required(VERSION 3.20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(earthsiege2 C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "the host must be 32-bit: use build.cmd (vcvarsall amd64_x86)")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "runtime/native32 is MSVC x86 inline assembly")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(PCRECOMP "${CMAKE_SOURCE_DIR}/../pcrecomp" CACHE PATH "pcrecomp checkout")
include(${PCRECOMP}/cmake/pcrecomp_gen.cmake)
set_source_files_properties(src/runtime/host.c PROPERTIES COMPILE_OPTIONS "/W3")

# One host per game executable; both are based at 0x00400000.
foreach(m shell sim)
  if(m STREQUAL "shell")
    set(exe "VSHELL.EXE")
  else()
    set(exe "DBSIM.EXE")
  endif()
  add_executable(es2${m}
    src/runtime/host.c
    ${PCRECOMP}/runtime/native32/native32.c
    ${PCRECOMP}/runtime/native32/modules32.c
    ${PCRECOMP}/runtime/recomp32/image_loader.c
    ${PCRECOMP}/runtime/recomp32/recomp_trace.c
  )
  pcrecomp_gen_sources(es2${m} DIR ${CMAKE_SOURCE_DIR}/src/recomp/gen/${m})
  target_include_directories(es2${m} PRIVATE
    src/runtime src/recomp/gen/${m} ${PCRECOMP}/runtime/native32 ${PCRECOMP}/runtime/recomp32)
  target_compile_definitions(es2${m} PRIVATE ES2_EXE="${exe}" RECOMP_RETADDR=0u _CRT_SECURE_NO_WARNINGS
                                             RECOMP_FLAT_MEMORY RECOMP_LOCAL_REGS)
  # Linked high so the guest's 0x00400000.. is free. /NXCOMPAT is load-bearing:
  # a native call into guest code faults, and that fault is the callback.
  target_link_options(es2${m} PRIVATE /BASE:0x60000000 /FIXED:NO /DYNAMICBASE:NO
                                      /NXCOMPAT /LARGEADDRESSAWARE /STACK:0x40000 /MANIFESTUAC:NO /MAP)
  target_link_libraries(es2${m} PRIVATE user32 gdi32 kernel32 winmm advapi32)
endforeach()
