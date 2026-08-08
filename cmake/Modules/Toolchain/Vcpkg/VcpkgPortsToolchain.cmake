# vcpkg 포트 빌드 전용 툴체인 파일
# clang-cl + LLVM 툴체인을 절대 경로로 지정합니다.

include("${CMAKE_CURRENT_LIST_DIR}/../FindLlvmBin.cmake")
sw_find_llvm_bin(_llvm_bin)

if(NOT _llvm_bin)
    message(FATAL_ERROR
        "[VcpkgPortsToolchain] clang-cl.exe를 찾을 수 없습니다.\n"
        "  LLVM을 설치하거나 LLVM_DIR/LLVM_ROOT를 설정한 뒤\n"
        "  python Scripts/setup/SetupEnvironment.py 를 실행하세요."
    )
endif()

set(CMAKE_C_COMPILER   "${_llvm_bin}/clang-cl.exe" CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER "${_llvm_bin}/clang-cl.exe" CACHE STRING "" FORCE)
set(CMAKE_LINKER       "${_llvm_bin}/lld-link.exe" CACHE STRING "" FORCE)
if(EXISTS "${_llvm_bin}/llvm-rc.exe")
    set(CMAKE_RC_COMPILER "${_llvm_bin}/llvm-rc.exe" CACHE STRING "" FORCE)
endif()

# spirv-reflect 등 포트 빌드 시 clang-cl의 엄격한 Werror로 인한 빌드 실패 방지
if(NOT COMMAND _target_compile_options)
    function(target_compile_options target)
        set(args "${ARGN}")
        string(REPLACE "-Werror" "" args "${args}")
        _target_compile_options(${target} ${args})
    endfunction()
endif()

if(NOT COMMAND _add_compile_options)
    function(add_compile_options)
        set(args "${ARGN}")
        string(REPLACE "-Werror" "" args "${args}")
        _add_compile_options(${args})
    endfunction()
endif()
