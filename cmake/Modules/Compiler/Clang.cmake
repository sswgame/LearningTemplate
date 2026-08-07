# Clang / AppleClang compiler flags.

if(
    NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
    AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
)
    return()
endif()

find_program(SW_COMPILER_LAUNCHER NAMES sccache ccache HINTS "$ENV{PATH}")
if(SW_COMPILER_LAUNCHER)
    message(STATUS "[Clang.cmake] Compiler Cache launcher detected: ${SW_COMPILER_LAUNCHER}")
    set(CMAKE_C_COMPILER_LAUNCHER "${SW_COMPILER_LAUNCHER}" PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SW_COMPILER_LAUNCHER}" PARENT_SCOPE)
endif()

add_library(sw_compiler_clang INTERFACE)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(CMAKE_INCLUDE_SYSTEM_FLAG_C "/imsvc ")
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/imsvc ")
else()
    set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-isystem ")
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-isystem ")
endif()

target_compile_options(sw_compiler_clang INTERFACE
    $<$<NOT:$<BOOL:${MSVC}>>:-finput-charset=UTF-8>
    $<$<NOT:$<BOOL:${MSVC}>>:-fexec-charset=UTF-8>
    $<$<CXX_COMPILER_ID:Clang>:$<$<BOOL:${MSVC}>:/utf-8>>
    $<$<BOOL:${MSVC}>:/bigobj>
    $<$<BOOL:${MSVC}>:/external:I>
    $<$<BOOL:${MSVC}>:${CMAKE_SOURCE_DIR}/ThirdParty>
    $<$<BOOL:${MSVC}>:/external:W0>
    -Wall
    -Wextra
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-pre-c++17-compat
    -Wno-unsafe-buffer-usage
    -Wno-global-constructors
    -Wno-exit-time-destructors
    -Wno-padded
    -Wno-switch-default
    -Wno-covered-switch-default
    -Wno-format-nonliteral
    -Wno-nonportable-include-path
    -Wno-cast-function-type-strict
    -Wno-invalid-offsetof
    -Wno-unused-command-line-argument
    $<$<BOOL:${MSVC}>:-Qunused-arguments>
    $<$<BOOL:${MSVC}>:-Wno-language-extension-token>
)

target_compile_options(sw_compiler_clang INTERFACE
    $<$<CONFIG:Debug>:-g>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Debug>:/Z7>>
    $<$<NOT:$<BOOL:${MSVC}>>:$<$<CONFIG:Debug>:-O0>>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Debug>:/Od>>
)

target_compile_options(sw_compiler_clang INTERFACE
    $<$<NOT:$<BOOL:${MSVC}>>:$<$<CONFIG:Release>:-O3>>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Release>:/O2>>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Release>:/arch:AVX2>>
    $<$<NOT:$<BOOL:${MSVC}>>:$<$<CONFIG:Release>:-mavx2>>
)

include("${CMAKE_CURRENT_LIST_DIR}/../Toolchain/FindLlvmBin.cmake")
sw_find_llvm_bin(_llvm_bin)
find_program(SW_LLD_LINK_EXE NAMES lld-link lld HINTS "${_llvm_bin}")
if(SW_LLD_LINK_EXE)
    message(STATUS "[Clang.cmake] LLD Fast Linker detected: ${SW_LLD_LINK_EXE}")
    target_link_options(sw_compiler_clang INTERFACE "-fuse-ld=lld")
endif()

target_link_options(sw_compiler_clang INTERFACE
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Debug>:/INCREMENTAL>>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Debug>:/DEBUG:FASTLINK>>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Release>:/INCREMENTAL:NO>>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Release>:/OPT:REF>>
    $<$<BOOL:${MSVC}>:$<$<CONFIG:Release>:/OPT:ICF>>
)

target_compile_definitions(sw_compiler_clang INTERFACE SW_COMPILER_CLANG)

list(APPEND sw_flag_libraries sw_compiler_clang)
