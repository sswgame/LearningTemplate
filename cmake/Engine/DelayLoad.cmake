# ==============================================================================
# @file cmake/Engine/DelayLoad.cmake
# @brief Windows 지연 로드(/DELAYLOAD)를 정하는 유일한 자리 — 모듈 그래프(훅 · 미리 묶기)와 시스템 DLL
# ==============================================================================

# ------------------------------------------------------------------------------
# Windows delay-load + 훅 소스 바인딩
# 지연 로드는 이 함수와 sw_addDelayloadSystemDlls 로만 정한다(CheckDelayLoadSites). lld 의 x64 지연 로드 썽크(__tailMerge_<dll>)는
# xmm0~3 을 [rsp] · [rsp+10h] · … 에 두고 __delayLoadHelper2 를 부르는데 [rsp..rsp+1Fh] 가 그 호출의 홈 공간이라, 첫 호출이 묶으면
# 그 호출의 첫 float 인자(xmm0)가 망가진다. 그래서 여기서 넣는 훅 TU 가 bindDelayLoadImports 를 내보내고, 모듈을 올리는 자리
# (LiveReloadManager · ModuleHost)와 엔진 기동이 그 코드가 돌기 전에 부른다(ModuleImageUtil::bindDelayLoadImports).
# ------------------------------------------------------------------------------
function(sw_addDelayloadHook TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "DLLS" ${ARGN})

	if(NOT WIN32)
		return()
	endif()

	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_addDelayloadHook: target '${TARGET_NAME}' does not exist")
	endif()

	# 훅 소스가 지어진다는 표시 — 아무도 부탁하지 않으면 `sw_writeUnbuiltSourceList` 가 짓지 않는 소스로 적는다.
	set_property(GLOBAL PROPERTY SW_DELAYLOAD_HOOK_REQUESTED TRUE)

	# 훅 소스의 자리는 한 곳 — Engine 의 SW_DELAYLOAD_HOOK_SOURCE(Source/Engine/CMakeLists.txt)다. Engine 이 없는 구성에서 훅을 부탁하는
	# 타깃은 없다. 속성이 비거나 그 파일이 없으면 설정 실수다 — 손으로 적은 폴백 경로로 대신하지 않는다(그러면 옮긴 것을 아무도 모른다).
	set(swHookSrc "")
	if(TARGET Engine)
		get_property(swHookSrc TARGET Engine PROPERTY SW_DELAYLOAD_HOOK_SOURCE)
	endif()
	if(NOT swHookSrc)
		message(FATAL_ERROR "[sw_addDelayloadHook] Engine has no SW_DELAYLOAD_HOOK_SOURCE (${TARGET_NAME})")
	endif()
	if(NOT EXISTS "${swHookSrc}")
		message(FATAL_ERROR "[sw_addDelayloadHook] SW_DELAYLOAD_HOOK_SOURCE points at a file that does not exist: ${swHookSrc}")
	endif()

	target_sources(${TARGET_NAME} PRIVATE "${swHookSrc}")
	target_link_libraries(${TARGET_NAME} PRIVATE delayimp)

	foreach(dll IN LISTS ARG_DLLS)
		target_link_options(${TARGET_NAME} PRIVATE "LINKER:/DELAYLOAD:${dll}")
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# Windows delay-load — 필요할 때만 올리는 시스템 DLL(훅 · 미리 묶기 없음)
# 미리 묶지 않으므로 첫 호출이 썽크를 지난다 — 이 DLL 에서 부르는 함수 가운데 **첫 인자가 float · double 인 것이 없어야** 한다(xmm0 이 망가진다).
# 지금 목록(D3DCompile · D3DReflect · MFStartup · MFCreate* · XAudio2Create · Tracy C API)은 첫 인자가 모두 포인터 · 정수다. 빠지면 기능이
# 꺼지는 선택 DLL(Windows N 의 Media Foundation 등)이라 기동에서 미리 올리지 않는다.
# ------------------------------------------------------------------------------
function(sw_addDelayloadSystemDlls TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "DLLS" ${ARGN})
	if(NOT WIN32)
		return()
	endif()

	target_link_libraries(${TARGET_NAME} PRIVATE delayimp)
	foreach(dll IN LISTS ARG_DLLS)
		target_link_options(${TARGET_NAME} PRIVATE "LINKER:/DELAYLOAD:${dll}")
	endforeach()
endfunction()
