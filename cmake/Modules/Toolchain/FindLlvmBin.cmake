# @brief LLVM/clang-cl bin 디렉터리를 절대 경로로 탐색합니다.
# vcpkg detect_compiler 중에도 동작해야 하므로 CMAKE_SOURCE_DIR에 의존하지 않습니다.

if(NOT COMMAND sw_find_llvm_bin)
    function(sw_find_llvm_bin OUT_VAR)
        set(_llvm_bin "")

        # 1) 환경 변수
        foreach(_env_name LLVM_ROOT LLVM_PATH LLVM_DIR LLVM_HOME)
            if(DEFINED ENV{${_env_name}} AND EXISTS "$ENV{${_env_name}}/bin/clang-cl.exe")
                set(_llvm_bin "$ENV{${_env_name}}/bin")
                break()
            endif()
        endforeach()

        # 2) Config/engine_config.json (프로젝트 루트 후보를 list-file 기준으로 탐색)
        if(NOT _llvm_bin)
            get_filename_component(_toolchain_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" ABSOLUTE)
            set(_candidate_roots
                "${_toolchain_dir}/../../.."          # cmake/Modules/Toolchain -> repo root
                "${_toolchain_dir}/../../../.."
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}"
            )
            foreach(_candidate_root IN LISTS _candidate_roots)
                get_filename_component(_abs_root "${_candidate_root}" ABSOLUTE)
                set(_cfg_json "${_abs_root}/Config/engine_config.json")
                if(EXISTS "${_cfg_json}")
                    file(READ "${_cfg_json}" _cfg_content)
                    string(JSON _json_llvm ERROR_VARIABLE _json_err GET "${_cfg_content}" "llvm_path")
                    if(NOT _json_err AND _json_llvm AND EXISTS "${_json_llvm}/bin/clang-cl.exe")
                        set(_llvm_bin "${_json_llvm}/bin")
                        break()
                    endif()
                endif()
            endforeach()
        endif()

        # 3) PATH 상의 clang-cl (하드코딩 설치 경로는 Scripts/search_paths 담당)
        if(NOT _llvm_bin)
            find_program(_clang_cl_exe NAMES clang-cl clang-cl.exe)
            if(_clang_cl_exe)
                get_filename_component(_llvm_bin "${_clang_cl_exe}" DIRECTORY)
            endif()
        endif()

        set(${OUT_VAR} "${_llvm_bin}" PARENT_SCOPE)
    endfunction()
endif()
