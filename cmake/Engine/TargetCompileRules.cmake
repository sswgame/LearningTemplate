# ==============================================================================
# @file cmake/Engine/TargetCompileRules.cmake
# @brief 타깃 하나의 컴파일 규칙 — 내보내기 매크로 짝 · PCH · 결정성 TU(부동소수점 축약 끄기)
# ==============================================================================

# ------------------------------------------------------------------------------
# 내보내기 매크로 — 종류(KIND)마다 짝이 정해져 있다. STATIC(배포)에는 아무것도 붙이지 않는다.
#   ENGINE : SHARED 면 SW_EXPORTS(PRIVATE) · SW_IMPORTS(INTERFACE)        — Engine.dll
#   GF     : SHARED 면 SW_GF_EXPORTS(PRIVATE) · SW_GF_IMPORTS(INTERFACE)  — GameFramework.dll · 키트
#   MODULE : MODULE 이면 SW_MODULE_EXPORTS(PRIVATE)                       — C-ABI 진입점을 내보내는 플러그인(RHI · 게임 · 에디터)
# ------------------------------------------------------------------------------
function(sw_configureDllExports TARGET_NAME LIB_TYPE KIND)
	if(NOT KIND MATCHES "^(ENGINE|GF|MODULE|MODULE_SHARED|EDITOR)$")
		message(FATAL_ERROR "sw_configureDllExports(${TARGET_NAME}): KIND 는 ENGINE | GF | MODULE | MODULE_SHARED | EDITOR 이다 (받은 값: ${KIND})")
	endif()

	if(KIND STREQUAL "ENGINE" AND LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS INTERFACE SW_IMPORTS)
	elseif(KIND STREQUAL "GF" AND LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_GF_EXPORTS INTERFACE SW_GF_IMPORTS)
	elseif(KIND STREQUAL "MODULE" AND LIB_TYPE STREQUAL "MODULE")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
	elseif(KIND STREQUAL "MODULE_SHARED" AND LIB_TYPE STREQUAL "SHARED")
		# 다른 모듈을 링크하는 SHARED 모듈(에디터 확장) — 자기 C-ABI 진입점만 내보낸다.
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
	elseif(KIND STREQUAL "EDITOR" AND LIB_TYPE STREQUAL "SHARED")
		# EditorModule: 호스트가 부르는 C-ABI 진입점(SW_MODULE_API)과, 확장 모듈이 링크하는 에디터 API(SW_EDITOR_API)를 함께 내보낸다.
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS SW_EDITOR_EXPORTS INTERFACE SW_EDITOR_IMPORTS)
	endif()
endfunction()

# SW_ENABLE_PCH 가 켜져 있을 때만 PCH 를 건다.
function(sw_configurePch targetName headerPath)
	if(SW_ENABLE_PCH)
		target_precompile_headers(${targetName} PRIVATE "${headerPath}")
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_addSharedPchOwner — 같은 정의를 가진 타깃들이 PCH 하나를 나눠 쓰게 하는 중립 소유자(쓰는 쪽은 `sw_useSharedPch`).
#   clang 은 PCH 와 명령줄의 `-D` 가 하나라도 다르면 거부한다(`definition of macro 'SW_LOG_TAG' differs`). 소유자는 타깃 고유 정의가 없고
#   `LINK` 만 받아 공통 정의(`SW_IMPORTS` 등)를 물려받는다 — 특정 킷이 소유하면 그 킷의 정의가 PCH 에 구워져 나머지로 샌다.
# ------------------------------------------------------------------------------
function(sw_addSharedPchOwner OWNER_NAME HEADER_PATH)
	cmake_parse_arguments(ARG "" "" "LINK" ${ARGN})
	if(NOT SW_ENABLE_PCH OR TARGET ${OWNER_NAME})
		return()
	endif()
	set(ownerSource "${CMAKE_BINARY_DIR}/generated/sw/pch/${OWNER_NAME}.cpp")
	file(GENERATE OUTPUT "${ownerSource}" CONTENT "// ${OWNER_NAME}: shared PCH owner (no code)\n")
	add_library(${OWNER_NAME} OBJECT "${ownerSource}")
	target_link_libraries(${OWNER_NAME} PRIVATE ${ARG_LINK})
	set_target_properties(${OWNER_NAME} PROPERTIES FOLDER "Build/Pch" EXCLUDE_FROM_ALL ON)
	target_precompile_headers(${OWNER_NAME} PRIVATE "${HEADER_PATH}")
endfunction()

# ------------------------------------------------------------------------------
# sw_useSharedPch — 타깃이 `sw_addSharedPchOwner` 의 PCH 를 쓴다(clang-cl 만 — 그 밖은 PCH 없이 짓는다).
#   CMake 의 `REUSE_PRECOMPILE_HEADERS_FROM` 을 쓰지 않는 까닭 둘:
#   - 속성만 세우면(`set_target_properties`) CMake 4.4 는 조용히 무시한다 — `target_precompile_headers( REUSE_FROM )` 이어야 붙는다.
#   - CMake 는 PCH 플래그(`/Yu` · `/FI<cmake_pch.hxx>`)를 명령줄 맨 끝에 둔다. clang-cl 은 PCH 헤더보다 **앞에** 강제 포함한 파일을 건너뛰므로
#     타깃 고유 정의 헤더(`sw_setTargetDefsHeader`)가 사라진다. 그래서 PCH 플래그를 타깃 옵션 맨 앞(`BEFORE`)에 직접 넣어 정의 헤더 ·
#     의존 타깃의 `FlagOps.gen.h` 가 PCH **뒤에** 읽히게 한다.
#   PCH 안 헤더는 `/showIncludes` 에 나오지 않으므로 소유자의 PCH 오브젝트를 모든 C++ 소스의 `OBJECT_DEPENDS` 로 건다 —
#   소스가 다 붙은 뒤(리플렉션 단계 뒤) 부른다. `SKIP_PRECOMPILE_HEADERS` 소스는 `/Y-` 로 PCH 를 끈다.
# ------------------------------------------------------------------------------
function(sw_useSharedPch TARGET_NAME OWNER_NAME)
	if(NOT SW_ENABLE_PCH OR NOT MSVC OR NOT TARGET ${OWNER_NAME})
		return()
	endif()
	get_target_property(ownerBinaryDir ${OWNER_NAME} BINARY_DIR)
	set(pchDir "${ownerBinaryDir}/CMakeFiles/${OWNER_NAME}.dir")
	set(pchHeader "${pchDir}/cmake_pch.hxx")
	target_compile_options(${TARGET_NAME} BEFORE PRIVATE "/Yu${pchHeader}" "/Fp${pchDir}/cmake_pch.cxx.pch" "/FI${pchHeader}")
	add_dependencies(${TARGET_NAME} ${OWNER_NAME})
	get_target_property(listSource ${TARGET_NAME} SOURCES)
	foreach(src IN LISTS listSource)
		if(NOT src MATCHES "\\.(cpp|cxx|cc)$")
			continue()
		endif()
		get_source_file_property(bSkipPch "${src}" TARGET_DIRECTORY ${TARGET_NAME} SKIP_PRECOMPILE_HEADERS)
		if(bSkipPch)
			set_property(SOURCE "${src}" TARGET_DIRECTORY ${TARGET_NAME} APPEND PROPERTY COMPILE_OPTIONS "/Y-")
		else()
			set_property(SOURCE "${src}" TARGET_DIRECTORY ${TARGET_NAME} APPEND PROPERTY OBJECT_DEPENDS "${pchDir}/cmake_pch.cxx.obj")
		endif()
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# sw_setTargetDefsHeader — 타깃 고유 정의(`SW_LOG_TAG` · `SW_GF_EXPORTS`)를 `-D` 대신 PCH 뒤에 강제 포함하는 헤더로 준다.
#   PCH 를 나눠 쓰려면 명령줄 정의가 같아야 하므로 타깃마다 다른 값은 여기로 옮긴다. `Logger.h` 가 PCH 안에서 기본 `SW_LOG_TAG` 를 정의해
#   두므로 `#undef` 로 덮는다. 인자: LOG_TAG, GF_EXPORTS(켜면 `SW_GF_EXPORTS` 정의).
#   PCH 안의 헤더가 읽는 정의는 여기 둘 수 없다(PCH 를 만들 때 이미 정해졌다) — `SW_GF_EXPORTS` 를 읽는 `GameFrameworkExports.h` 는 PCH 밖이다.
# ------------------------------------------------------------------------------
function(sw_setTargetDefsHeader TARGET_NAME)
	cmake_parse_arguments(ARG "GF_EXPORTS" "LOG_TAG" "" ${ARGN})
	set(content "// ${TARGET_NAME}: per-target definitions (generated)\n#pragma once\n#undef SW_LOG_TAG\n#define SW_LOG_TAG \"${ARG_LOG_TAG}\"\n")
	if(ARG_GF_EXPORTS)
		string(APPEND content "#define SW_GF_EXPORTS\n")
	endif()
	set(header "${CMAKE_BINARY_DIR}/generated/sw/targetdefs/${TARGET_NAME}.h")
	file(GENERATE OUTPUT "${header}" CONTENT "${content}")
	if(MSVC)
		target_compile_options(${TARGET_NAME} PRIVATE "/FI${header}")
	else()
		target_compile_options(${TARGET_NAME} PRIVATE -include "${header}")
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_markDeterministicSources —같은 입력이면 구성 · 컴파일러 · 플랫폼을 넘어 같은 비트를 내야 하는 시뮬레이션 TU(파괴 · 파쇄 · 그 복제)에
#   부동소수점 축약(FMA 합치기)을 끈다. Release 의 /arch:AVX2 · -mavx2 는 `a * b + c` 를 FMA 한 번으로 합쳐 반올림이 Debug(/Od · AVX2 없음)와
#   달라진다 — 씨앗과 사건만 보내고 받는 쪽이 같은 계산을 하는 파괴 네트워킹은 Debug 서버 ↔ Shipping 클라이언트에서 다른 그림이 된다.
#   엔진 전체에는 걸지 않는다(렌더 · 애니메이션 수학은 FMA 가 이득이고 비트 결정성이 필요 없다). 언리얼도 결정성이 필요한 물리 경로만 /fp:precise 로 짓는다.
#   clang-cl 의 기본 /fp:precise 는 축약을 허락하므로(-ffp-contract=on) 축약만 따로 끈다.
#   유니티 묶음에서 뺀다 — 묶음 파일은 묶음의 옵션 하나로 컴파일되므로 파일별 옵션이 묶음 전체에 퍼지거나 빠진다.
#   PCH 는 그대로 쓴다(축약 옵션은 PCH 호환 검사 대상이 아니다).
# ------------------------------------------------------------------------------
function(sw_markDeterministicSources TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_markDeterministicSources: no target ${TARGET_NAME}")
	endif()
	if(MSVC)
		set(contractOff "/clang:-ffp-contract=off")
	else()
		set(contractOff "-ffp-contract=off")
	endif()
	foreach(src IN LISTS ARGN)
		if(NOT IS_ABSOLUTE "${src}")
			set(src "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
		endif()
		# 없는 경로를 조용히 넘기면 파일을 옮겼을 때 결정성이 소리 없이 꺼진다.
		if(NOT EXISTS "${src}")
			message(FATAL_ERROR "sw_markDeterministicSources(${TARGET_NAME}): no such file -> ${src}")
		endif()
		set_source_files_properties("${src}" TARGET_DIRECTORY ${TARGET_NAME} PROPERTIES
			COMPILE_OPTIONS "${contractOff}"
			SKIP_UNITY_BUILD_INCLUSION ON)
	endforeach()
endfunction()
