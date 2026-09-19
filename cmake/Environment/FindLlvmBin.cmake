# ==============================================================================
# @file cmake/Environment/FindLlvmBin.cmake
# @brief LLVM/clang-cl bin 디렉터리 탐색
# ==============================================================================

if(NOT COMMAND sw_findLlvmBin)
    # ------------------------------------------------------------------------------
    # 1) sw_findLlvmBin — clang-cl / clang 이 있는 bin 경로
    # 우선순위: env → PATH → toolchain_config(Environment/) → search_paths → Program Files
    # 시스템/기존 설치를 프로젝트 Tools보다 먼저 (최초 clone 시 Tools 없어도 OK)
    # ------------------------------------------------------------------------------
    function(sw_findLlvmBin OUT_VAR)
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Config/GenerateConfigConstants.cmake" OPTIONAL)
        set(llvmBin "")

        foreach(envName LLVM_ROOT LLVM_PATH LLVM_DIR LLVM_HOME)
            if(DEFINED ENV{${envName}} AND EXISTS "$ENV{${envName}}/bin/clang-cl.exe")
                set(llvmBin "$ENV{${envName}}/bin")
                break()
            elseif(DEFINED ENV{${envName}} AND EXISTS "$ENV{${envName}}/bin/clang")
                set(llvmBin "$ENV{${envName}}/bin")
                break()
            endif()
        endforeach()

        if(NOT llvmBin)
            find_program(clangClExe NAMES clang-cl clang-cl.exe clang)

            if(clangClExe)
                get_filename_component(llvmBin "${clangClExe}" DIRECTORY)
            endif()
        endif()

        # 이 파일이 사는 곳은 `<repo>/cmake/Environment` 다 — **저장소 루트는 두 단계 위**다.
        # 예전에는 세·네 단계 위만 후보로 두어 저장소 루트가 목록에 없었다(이름이 `toolchainDir` 인 것이
        # 흔적이다 — 경로는 툴체인 파일 위치 기준으로 적혔는데 값은 이 파일의 위치다). 평소에는
        # `CMAKE_SOURCE_DIR` 이 대신 맞춰 주지만 **vcpkg 포트 빌드에서는 그것이 vcpkg 의 scripts 폴더**라,
        # PATH/ENV 를 비운 채 도는 그 자리에서 저장소의 `Tools/LLVM` 을 영영 못 찾았다. 그 결과
        # `vcpkg_installed/vcpkg/compiler-file-hash-cache.json` 이 한 번 지워지면(다른 트리플릿으로
        # 설치를 돌리면 그렇게 된다) 그 뒤로 configure 가 서지 않는다.
        get_filename_component(toolchainDir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" ABSOLUTE)
        set(candidateRoots
            "${toolchainDir}/.."
            "${toolchainDir}/../.."
            "${toolchainDir}/../../.."
            "${toolchainDir}/../../../.."
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}"
        )

        if(NOT llvmBin)
            foreach(candidateRoot IN LISTS candidateRoots)
                get_filename_component(absRoot "${candidateRoot}" ABSOLUTE)
                set(cfgJson "${absRoot}/${SW_DIR_CONFIG_ENV}/${SW_FILE_TOOLCHAIN_CONFIG}")

                if(EXISTS "${cfgJson}")
                    file(READ "${cfgJson}" cfgContent)
                    string(JSON jsonLlvm ERROR_VARIABLE jsonErr GET "${cfgContent}" "${SW_KEY_LLVM_PATH}")

                    if(NOT jsonErr AND jsonLlvm)
                        if(EXISTS "${jsonLlvm}/bin/clang-cl.exe" OR EXISTS "${jsonLlvm}/bin/clang")
                            set(llvmBin "${jsonLlvm}/bin")
                            break()
                        endif()
                    endif()
                endif()
            endforeach()
        endif()

        if(NOT llvmBin)
            foreach(candidateRoot IN LISTS candidateRoots)
                get_filename_component(absRoot "${candidateRoot}" ABSOLUTE)
                set(llvmSubdir "")
                set(localJson "${absRoot}/${SW_DIR_CONFIG_ENV}/${SW_FILE_SEARCH_PATHS}")
                set(defaultsJson "${absRoot}/${SW_DIR_CONFIG_ENV}/${SW_FILE_SEARCH_PATHS_DEFAULTS}")

                if(EXISTS "${localJson}")
                    file(READ "${localJson}" localContent)
                    string(JSON llvmSubdir ERROR_VARIABLE jsonErr GET "${localContent}" "${SW_KEY_LLVM_TOOLS_SUBDIR}")

                    if(jsonErr)
                        set(llvmSubdir "")
                    endif()
                endif()

                if(llvmSubdir STREQUAL "" AND EXISTS "${defaultsJson}")
                    file(READ "${defaultsJson}" defaultsContent)
                    string(JSON llvmSubdir ERROR_VARIABLE jsonErr GET "${defaultsContent}" "${SW_KEY_LLVM_TOOLS_SUBDIR}")

                    if(jsonErr)
                        set(llvmSubdir "")
                    endif()
                endif()

                if(llvmSubdir STREQUAL "")
                    set(llvmSubdir "${SW_DIR_TOOLS_LLVM}")
                endif()

                set(kit "${absRoot}/${llvmSubdir}")

                if(EXISTS "${kit}/bin/clang-cl.exe" OR EXISTS "${kit}/bin/clang")
                    set(llvmBin "${kit}/bin")
                    break()
                endif()
            endforeach()
        endif()

        # vcpkg detect_compiler는 PATH/ENV를 비운 채 이 툴체인만 로드한다.
        if(NOT llvmBin)
            foreach(stdLlvm IN ITEMS
                "C:/Program Files/LLVM"
                "C:/Program Files (x86)/LLVM"
            )
                if(EXISTS "${stdLlvm}/bin/clang-cl.exe")
                    set(llvmBin "${stdLlvm}/bin")
                    break()
                elseif(EXISTS "${stdLlvm}/bin/clang")
                    set(llvmBin "${stdLlvm}/bin")
                    break()
                endif()
            endforeach()
        endif()

        set(${OUT_VAR} "${llvmBin}" PARENT_SCOPE)
    endfunction()
endif()
