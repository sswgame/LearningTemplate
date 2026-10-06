# ==============================================================================
# @file cmake/Engine/RhiBackends.cmake
# @brief RHI 백엔드 표의 CMake 쪽 — 정본은 Config/Engine/CookContract.json 의 rhi_backends(configure 가 generated/sw/config/CookContract.cmake 로 옮긴다)
# @note 백엔드의 장치 소스는 `Source/Engine/Graphics/RHI/<source_folder>/` 아래 .cpp 전부다 — 목록을 따로 적지 않는다.
#       RHI_* MODULE(Dev)과 Engine 정적 링크(Shipping)가 같은 함수로 묻는다.
# ==============================================================================
include_guard(GLOBAL)

# 주의: `cmake -P` 스크립트(시험)도 이 파일을 include 한다. 스크립트 모드는 cmake_minimum_required 가 없으면 정책이 전부 OLD 라
# `IN_LIST`(CMP0057)가 "Unknown arguments" 가 된다 — 조용히 다르게 돌지 않게 여기서 세운다.
if(NOT CMAKE_MINIMUM_REQUIRED_VERSION)
	message(FATAL_ERROR "[RHI] include RhiBackends.cmake after cmake_minimum_required() — script mode (cmake -P) otherwise runs it under OLD policies")
endif()

# 백엔드 NAME(RHIBackend 열거자 — DirectX12 …)의 장치 소스.
function(sw_getRhiBackendSources NAME OUT_VAR)
	file(GLOB_RECURSE listSource CONFIGURE_DEPENDS
		"${CMAKE_SOURCE_DIR}/Source/Engine/Graphics/RHI/${SW_RHI_BACKEND_${NAME}_SOURCE_FOLDER}/*.cpp")
	set(${OUT_VAR} ${listSource} PARENT_SCOPE)
endfunction()

# 모든 백엔드의 장치 소스(Engine GLOB 에서 뺀다 — 백엔드 소스는 모듈 · Shipping 갈래가 가져간다).
function(sw_getAllRhiBackendSources OUT_VAR)
	set(listAll "")
	foreach(name IN LISTS SW_RHI_BACKEND_NAMES)
		sw_getRhiBackendSources(${name} listSource)
		list(APPEND listAll ${listSource})
	endforeach()
	set(${OUT_VAR} ${listAll} PARENT_SCOPE)
endfunction()

# 쿠킹 표의 백엔드 이름(대소문자까지 그대로 — DirectX11 · DirectX12 · Vulkan · OpenGL)만 받는다. 명령줄 별칭(dx12 · vk …)은 받지 않는다 — 모르는 값이면 구성을 세운다.
function(sw_checkRhiBackendName TEXT)
	if(NOT TEXT IN_LIST SW_RHI_BACKEND_NAMES)
		string(REPLACE ";" ", " listName "${SW_RHI_BACKEND_NAMES}")
		message(FATAL_ERROR "[RHI] unknown backend '${TEXT}' for SW_SHIPPING_RHI_BACKEND — valid names: ${listName} "
			"(Config/Engine/CookContract.json rhi_backends). An existing build folder keeps the old cache value: "
			"re-configure with -DSW_SHIPPING_RHI_BACKEND=<name> or edit it in CMakeCache.txt")
	endif()
endfunction()

# 배포 빌드가 Engine 에 넣을 백엔드 — 이름을 확인하고, 이 플랫폼(sw_platform_name)에 그 백엔드가 있는지 본다. 없으면 구성을 세운다.
# 플랫폼은 그 백엔드 모듈의 매니페스트(_listPlatform)가 정본이다. 주의: 배포 구성에서는 모듈이 꺼져 있으므로(`_listConfiguration`) 활성 여부가
# 아니라 플랫폼 칸을 본다 — 이 확인이 없으면 그 플랫폼에 없는 백엔드를 고른 Engine 이 백엔드 없이 링크된다.
function(sw_resolveShippingRhiBackend TEXT OUT_VAR)
	sw_checkRhiBackendName("${TEXT}")
	set(backendName ${TEXT})
	set(moduleName ${SW_RHI_BACKEND_${backendName}_MODULE})
	get_property(listPlatform GLOBAL PROPERTY SW_MODULE_${moduleName}_PLATFORMS)
	if(NOT sw_platform_name IN_LIST listPlatform)
		message(FATAL_ERROR "[RHI] SW_SHIPPING_RHI_BACKEND=${TEXT}: ${backendName} is not available on ${sw_platform_name} "
			"(${moduleName}.module.json _listPlatform: ${listPlatform})")
	endif()
	set(${OUT_VAR} ${backendName} PARENT_SCOPE)
endfunction()
