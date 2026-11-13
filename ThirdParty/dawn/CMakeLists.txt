# ==============================================================================
# @file ThirdParty/dawn/CMakeLists.txt
# @brief Dawn(WebGPU 구현) · Tint(SPIR-V → WGSL) — vcpkg `dawn` 포트(BSD-3-Clause). WebGPU 백엔드의 그래픽 라이브러리(`sw_graphics_webgpu_libs`,
#        CookContract.json 의 graphics_libs)와 셰이더 쿠커가 실행 파일 옆에서 찾는 `tint` 를 낸다.
# @note  - 데스크톱(Windows · Linux): `dawn::webgpu_dawn`(네이티브 Dawn — D3D12 · Vulkan 위)으로 RHI_WebGPU 모듈을 돌려 본다.
#        - 웹(Emscripten): 같은 포트가 같은 판의 emdawnwebgpu 포트 파일을 깔고 `dawn::webgpu_dawn` 이 `--use-port=<그 파일>` 을 싣는다 —
#          emsdk 에 든 원격 포트 대신 데스크톱과 같은 Dawn 판의 webgpu.h 를 쓴다(판이 둘로 갈리지 않게).
#        - Android 는 Vulkan 하나라(CookContract.json platform_rhi_backends) WebGPU 를 짓지 않는다. 전용 서버는 RHI 가 없다.
#        판은 vcpkg 기준선(vcpkg.json builtin-baseline)의 `dawn` 이다 — 언리얼 · 유니티는 WebGPU 를 자체 분기로 들고 다니지만(기억) 여기는 포트 하나로 맞춘다.
# ==============================================================================

# ------------------------------------------------------------------------------
# sw_copy_tint_tool — 셰이더 쿠커(ShaderCompiler::convertSpirvToWgsl)가 실행 파일 옆에서 여는 `tint` 를 런타임 복사 큐에 넣는다.
#   포트의 `tint-tools` 기능이 `tools/dawn/` 에 깐다(vcpkg_copy_tools). 웹 빌드는 쿠커가 아니라 쿠킹한 꾸러미를 읽으므로 넣지 않는다.
#   함수는 아래 거르기보다 먼저 정의한다 — 서버 · Android 구성도 같은 이름을 부른다(아무 일도 하지 않는다).
#   배포 패키지에는 들지 않는다 — DXC 처럼 쿠킹 전용 도구이고, 스테이징은 실행 파일 하나와 공유 라이브러리만 베낀다(Scripts/package/pack.py select_staged_entries).
# ------------------------------------------------------------------------------
function(sw_copy_tint_tool TARGET_NAME)
	if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR SW_TARGET_TYPE STREQUAL "Server" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
		return()
	endif()
	set(tintName "tint${CMAKE_EXECUTABLE_SUFFIX}")
	set(tintPath "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/dawn/${tintName}")
	if(NOT EXISTS "${tintPath}")
		message(FATAL_ERROR "[dawn] ${tintPath} not found - vcpkg.json must request dawn[tint-tools] for ${VCPKG_TARGET_TRIPLET} (the WGSL_WebGPU shader target needs it)")
	endif()
	sw_queue_runtime_copy(${TARGET_NAME} "${tintPath}")
endfunction()

if(SW_TARGET_TYPE STREQUAL "Server" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
	return()
endif()

# 못 찾으면 구성을 세운다(REQUIRED) — 빈 타깃으로 대신하면 RHI_WebGPU 가 링크에서야 깨진다.
find_package(Dawn CONFIG REQUIRED GLOBAL)

if(NOT TARGET sw_graphics_webgpu_libs)
	add_library(sw_graphics_webgpu_libs INTERFACE)
	target_link_libraries(sw_graphics_webgpu_libs INTERFACE dawn::webgpu_dawn)
endif()
