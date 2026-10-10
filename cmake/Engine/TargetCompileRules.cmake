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
	if(NOT KIND MATCHES "^(ENGINE|GF|MODULE)$")
		message(FATAL_ERROR "sw_configureDllExports(${TARGET_NAME}): KIND 는 ENGINE | GF | MODULE 이다 (받은 값: ${KIND})")
	endif()

	if(KIND STREQUAL "ENGINE" AND LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS INTERFACE SW_IMPORTS)
	elseif(KIND STREQUAL "GF" AND LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_GF_EXPORTS INTERFACE SW_GF_IMPORTS)
	elseif(KIND STREQUAL "MODULE" AND LIB_TYPE STREQUAL "MODULE")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
	endif()
endfunction()

# SW_ENABLE_PCH 가 켜져 있을 때만 PCH 를 건다.
function(sw_configurePch targetName headerPath)
	if(SW_ENABLE_PCH)
		target_precompile_headers(${targetName} PRIVATE "${headerPath}")
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_markDeterministicSources — 같은 입력이면 구성 · 컴파일러 · 플랫폼을 넘어 같은 비트를 내야 하는 시뮬레이션 TU(파괴 · 파쇄 · 그 복제)에
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
