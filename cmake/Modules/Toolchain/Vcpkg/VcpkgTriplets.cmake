# ==============================================================================
# @file cmake/Modules/Toolchain/Vcpkg/VcpkgTriplets.cmake
# @brief vcpkg 오버레이 triplet · 포트 툴체인을 구성 단계가 빌드 폴더에 생성한다 — 포트 ABI 해시가 "포트 결과를 정하는 값" 만 보게 한다
#
# vcpkg 는 포트의 ABI 해시에 triplet 파일과 chainload 툴체인 파일의 **바이트**를 넣고, 그 파일이 include 한 파일은 보지 않는다.
# 저장소의 .cmake 를 그대로 triplet 으로 쓰면 두 방향으로 틀린다(2026-11-08 — 줄끝을 LF 로 바꾼 커밋 하나에 포트 95 개를 모두 다시 지었다):
#   - 너무 거칠다: 주석 · 이름 · 줄끝만 바뀌어도 모든 포트를 다시 짓는다(바이너리 캐시 275 벌을 하나도 쓰지 못했다).
#   - 빠뜨린다: 툴체인이 include 한 탐색 코드(FindLlvmBin · WindowsToolSearch)가 다른 도구를 골라도 해시가 그대로라 옛 바이너리를 쓴다.
# 그래서 도구 탐색은 여기(본 구성)서 끝내고, 생성 파일에는 풀린 값만 적는다 — 줄끝 LF · 주석 없음 · include 없음. 내용이 같으면 다시 쓰지 않는다.
# 컴파일 규칙 파일(CMAKE_USER_MAKE_RULES_OVERRIDE)은 `VCPKG_HASH_ADDITIONAL_FILES` 로 해시에 넣는다. 컴파일러 자체는 vcpkg 가 실행 파일 SHA1 로 따로 센다.
# 파일 사이 참조는 `${CMAKE_CURRENT_LIST_DIR}` 그대로 적어 빌드 폴더 위치가 해시에 섞이지 않게 한다(워크트리 · 프리셋마다 같은 해시).
# 언리얼 UBT 가 툴체인 버전 · 플래그로 빌드 키를 만드는 것과 같은 자리다. 이 파일을 고쳐도 생성 결과가 같으면 포트를 다시 짓지 않는다.
# ==============================================================================
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../../../Environment/FindLlvmBin.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../Environment/WindowsToolSearch.cmake")

# ------------------------------------------------------------------------------
# 1) sw_vcpkg_write_if_changed — 내용이 다를 때만 쓴다(줄 목록을 LF 로 잇는다)
# ------------------------------------------------------------------------------
function(sw_vcpkg_write_if_changed FILE_PATH)
	list(JOIN ARGN "\n" swContent)
	string(APPEND swContent "\n")
	if(EXISTS "${FILE_PATH}")
		file(READ "${FILE_PATH}" swOld)
		if(swOld STREQUAL swContent)
			return()
		endif()
	endif()
	file(WRITE "${FILE_PATH}" "${swContent}")
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_vcpkg_resolve_windows_port_tools — 포트 빌드 도구(고정 LLVM 키트 + Windows SDK mt.exe). 없으면 구성을 멈춘다(폴백 없음)
# ------------------------------------------------------------------------------
function(sw_vcpkg_resolve_windows_port_tools OUT_PREFIX)
	sw_find_llvm_bin(swLlvmBin)
	foreach(swTool IN ITEMS clang-cl lld-link llvm-lib llvm-rc)
		if(NOT EXISTS "${swLlvmBin}/${swTool}.exe")
			message(FATAL_ERROR "[VcpkgTriplets] ${swLlvmBin}/${swTool}.exe is missing — the pinned LLVM kit builds the ports (py -3 Scripts/setup/llvm.py --install)")
		endif()
	endforeach()

	get_filename_component(swRepoRoot "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../.." ABSOLUTE)
	set(swConfigFile "${swRepoRoot}/${SW_DIR_CONFIG_ENV}/${SW_FILE_TOOLCHAIN_CONFIG}")
	sw_require_json_value(swSdkDir "${swConfigFile}" "${SW_KEY_WINDOWS_SDK_DIR}")
	sw_require_json_value(swSdkVersion "${swConfigFile}" "${SW_KEY_WINDOWS_SDK_VERSION}")
	sw_find_windows_sdk_mt("${swSdkDir}" "${swSdkVersion}" swMt)
	if(NOT swMt)
		message(FATAL_ERROR "[VcpkgTriplets] mt.exe not found in Windows SDK ${swSdkVersion} (${swSdkDir}) — port links need the manifest tool")
	endif()

	set(${OUT_PREFIX}_LLVM_BIN "${swLlvmBin}" PARENT_SCOPE)
	set(${OUT_PREFIX}_MT "${swMt}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 3) sw_vcpkg_write_overlay_triplets — 오버레이 triplet 폴더를 만든다. OUT_DIR 에 그 폴더 경로
# ------------------------------------------------------------------------------
function(sw_vcpkg_write_overlay_triplets OUT_DIR)
	set(swDir "${CMAKE_BINARY_DIR}/vcpkg_triplets")
	file(MAKE_DIRECTORY "${swDir}")

	# OpenGLRHI 는 core 4.6 컨텍스트를 쓰므로 glad 도 core 로 생성한다(GLAD_PROFILE).
	set(swLinuxCommon
		"set(VCPKG_CMAKE_SYSTEM_NAME Linux)"
		"set(VCPKG_TARGET_ARCHITECTURE x64)"
		"set(VCPKG_CRT_LINKAGE dynamic)"
		"set(VCPKG_LIBRARY_LINKAGE static)"
		"set(GLAD_PROFILE core)"
	)
	# 리눅스 포트 컴파일러는 프리셋 환경의 CC · CXX(clang)가 정한다 — 설치 스탬프가 그 값을 센다(Vcpkg.cmake 1 절).
	sw_vcpkg_write_if_changed("${swDir}/x64-linux.cmake" ${swLinuxCommon})
	# ThreadSanitizer 구성(SW_SANITIZER_KIND=thread): 원자 연산으로 스스로 동기화하는 포트만 계측한다(계측 없이 링크하면 그 동기화가 안 보여 거짓 경쟁이 난다).
	# joltphysics — 잡 의존 · 장벽 · 잡 풀 · 임시 할당자, 접촉 콜백이 그 잡 스레드에서 불린다. box2d — 엔진 스레드끼리 월드를 만지는 경쟁이 라이브러리 안 접근이다.
	# 나머지는 스레드를 만들지 않고, 셰이더 컴파일러 · 압축을 계측하면 TSan 감속이 테스트 시한에 닿는다. 새로 들인 포트가 원자 연산으로 동기화하면 목록에 더한다.
	sw_vcpkg_write_if_changed("${swDir}/x64-linux-tsan.cmake" ${swLinuxCommon}
		"if(PORT MATCHES \"^(joltphysics|box2d)$\")"
		"set(VCPKG_C_FLAGS \"-fsanitize=thread -fno-omit-frame-pointer\")"
		"set(VCPKG_CXX_FLAGS \"-fsanitize=thread -fno-omit-frame-pointer\")"
		"set(VCPKG_LINKER_FLAGS \"-fsanitize=thread\")"
		"endif()"
	)

	if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
		sw_vcpkg_resolve_windows_port_tools(swTools)
		# 포트를 clang-cl · lld-link · llvm-lib · llvm-rc 로 짓는다(본 프로젝트와 같은 키트 — ABI 를 맞춘다).
		sw_vcpkg_write_if_changed("${swDir}/x64-windows.cmake"
			"set(VCPKG_TARGET_ARCHITECTURE x64)"
			"set(VCPKG_CRT_LINKAGE dynamic)"
			"set(VCPKG_LIBRARY_LINKAGE dynamic)"
			"set(GLAD_PROFILE core)"
			"set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE \"\${CMAKE_CURRENT_LIST_DIR}/x64-windows.toolchain.cmake\")"
			"set(VCPKG_HASH_ADDITIONAL_FILES \"\${CMAKE_CURRENT_LIST_DIR}/x64-windows.rules.cmake\")"
		)
		sw_vcpkg_write_if_changed("${swDir}/x64-windows.toolchain.cmake"
			"set(CMAKE_C_COMPILER \"${swTools_LLVM_BIN}/clang-cl.exe\" CACHE STRING \"\" FORCE)"
			"set(CMAKE_CXX_COMPILER \"${swTools_LLVM_BIN}/clang-cl.exe\" CACHE STRING \"\" FORCE)"
			"set(CMAKE_LINKER \"${swTools_LLVM_BIN}/lld-link.exe\" CACHE STRING \"\" FORCE)"
			"set(CMAKE_RC_COMPILER \"${swTools_LLVM_BIN}/llvm-rc.exe\" CACHE STRING \"\" FORCE)"
			"set(CMAKE_AR \"${swTools_LLVM_BIN}/llvm-lib.exe\" CACHE FILEPATH \"\" FORCE)"
			"set(CMAKE_C_COMPILER_AR \"${swTools_LLVM_BIN}/llvm-lib.exe\" CACHE FILEPATH \"\" FORCE)"
			"set(CMAKE_CXX_COMPILER_AR \"${swTools_LLVM_BIN}/llvm-lib.exe\" CACHE FILEPATH \"\" FORCE)"
			"set(CMAKE_MT \"${swTools_MT}\" CACHE FILEPATH \"\" FORCE)"
			"set(CMAKE_USER_MAKE_RULES_OVERRIDE \"\${CMAKE_CURRENT_LIST_DIR}/x64-windows.rules.cmake\")"
		)
		# clang-cl 은 CMAKE_<LANG>_FLAGS 를 타깃 COMPILE_OPTIONS 앞에 두어 포트의 -Werror 가 -Wno-error 를 이긴다 — <FLAGS> 뒤(명령줄 끝)에 붙인다.
		# clang-cl 은 /EHsc 가 없으면 예외가 꺼진다(spirv-cross 가 throw 를 쓴다).
		sw_vcpkg_write_if_changed("${swDir}/x64-windows.rules.cmake"
			"foreach(swLang IN ITEMS C CXX)"
			"if(DEFINED CMAKE_\${swLang}_COMPILE_OBJECT AND NOT CMAKE_\${swLang}_COMPILE_OBJECT MATCHES \"-Wno-error\")"
			"string(REPLACE \"<FLAGS>\" \"<FLAGS> -Wno-error\" CMAKE_\${swLang}_COMPILE_OBJECT \"\${CMAKE_\${swLang}_COMPILE_OBJECT}\")"
			"endif()"
			"endforeach()"
			"if(DEFINED CMAKE_CXX_COMPILE_OBJECT AND NOT CMAKE_CXX_COMPILE_OBJECT MATCHES \"/EH\")"
			"string(REPLACE \"<FLAGS>\" \"<FLAGS> /EHsc\" CMAKE_CXX_COMPILE_OBJECT \"\${CMAKE_CXX_COMPILE_OBJECT}\")"
			"endif()"
		)
	endif()

	# 웹: 엔진은 pthread(공유 메모리)로 짓는다(Platform/Web.cmake) — 포트도 -pthread(atomics · bulk-memory)여야 링크된다. 기본 triplet 을 읽고 플래그만 더해
	# 도구 사슬 · 연결 방식은 vcpkg 기본을 따른다.
	sw_vcpkg_write_if_changed("${swDir}/wasm32-emscripten-pthread.cmake"
		"include(\"\${VCPKG_ROOT_DIR}/triplets/community/wasm32-emscripten.cmake\")"
		"set(VCPKG_C_FLAGS \"-pthread\")"
		"set(VCPKG_CXX_FLAGS \"-pthread\")"
	)

	set(${OUT_DIR} "${swDir}" PARENT_SCOPE)
endfunction()
