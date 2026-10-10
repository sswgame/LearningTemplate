# ==============================================================================
# @file cmake/Engine/EngineAbiStamp.cmake
# @brief 핫 리로드 ABI 도장 — Core · Engine 헤더 지문을 Engine 과 Dev 모듈이 같이 박는다
# ==============================================================================

# ------------------------------------------------------------------------------
# 엔진 ABI 도장 — Core · Engine 헤더 내용의 지문 (핫 리로드가 올리기 전에 대조한다)
#
# 핫 리로드는 모듈만 갈아 끼우고 Engine 은 그대로다. Engine 헤더를 고친 빌드에서 `Engine.dll` 은 실행 중이라
# 다시 링크되지 못해도(잠김) 모듈은 **새 헤더로** 써질 수 있고, 그 모듈을 올리면 구조체 배치 · vtable 이 어긋나
# 조용히 망가진다. 그래서 Engine 과 모듈이 같은 지문을 박고 `LiveReloadManager` 가 섀도 복사본을 올리기 **전에**
# 파일 바이트에서 찾아 대조한다(`ModuleImagePatch::findEngineAbiStamp`). 헤더 내용이 같으면 스크립트가 파일을
# 다시 쓰지 않으므로(Ninja restat) 뒤따르는 재빌드가 없다.
# ------------------------------------------------------------------------------
set(SW_ENGINE_ABI_STAMP_HEADER "${CMAKE_BINARY_DIR}/generated/engineabi/EngineAbiStamp.gen.h")

function(sw_defineEngineAbiStamp)
	if(TARGET SwEngineAbiStamp)
		return()
	endif()
	file(GLOB_RECURSE swEngineAbiHeaders CONFIGURE_DEPENDS
		"${CMAKE_SOURCE_DIR}/Source/Core/*.h"
		"${CMAKE_SOURCE_DIR}/Source/Core/*.hpp"
		"${CMAKE_SOURCE_DIR}/Source/Core/*.inl"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.hpp"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.inl"
		"${CMAKE_SOURCE_DIR}/Source/Core/*.xxx"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.xxx"
		"${CMAKE_SOURCE_DIR}/Source/RuntimeAPI/*.h"
		"${CMAKE_SOURCE_DIR}/Source/RuntimeAPI/*.xxx"
		# GameFramework 는 Kits 까지 잡히지만 상관없다 — 스크립트가 Kits 를 빼고 해시하고, 결과가 같으면 파일을 다시 쓰지 않는다.
		"${CMAKE_SOURCE_DIR}/Source/GameFramework/*.h"
		"${CMAKE_SOURCE_DIR}/Source/GameFramework/*.xxx"
	)
	add_custom_command(
		OUTPUT "${SW_ENGINE_ABI_STAMP_HEADER}"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/GenerateEngineAbiStamp.py"
			--root "${CMAKE_SOURCE_DIR}" --out "${SW_ENGINE_ABI_STAMP_HEADER}"
		DEPENDS ${swEngineAbiHeaders} "${CMAKE_SOURCE_DIR}/Scripts/generate/GenerateEngineAbiStamp.py"
		COMMENT "Fingerprinting Core/Engine headers for the hot reload ABI stamp"
		VERBATIM
	)
	add_custom_target(SwEngineAbiStamp DEPENDS "${SW_ENGINE_ABI_STAMP_HEADER}")
	set_target_properties(SwEngineAbiStamp PROPERTIES FOLDER "CMakePredefinedTargets")
endfunction()

# 모듈이 자기가 빌드된 엔진 헤더의 지문을 박는다(Dev 의 공유 라이브러리만). 상수 하나가 두 가지 일을 한다.
#   - **도장** — 핫 리로드는 심볼이 아니라 파일 바이트에서 표식 문자열을 찾는다(모듈 코드가 돌기 전에).
#     내보내는 이유는 링커가 참조 없는 자료를 지우지 못하게 하려는 것이다.
#   - **결속 표식(리눅스)** — `LiveReloadManager::verifyModuleBindings` 는 "이 모듈이 의존을 어느 이미지에 묶었나" 를
#     묻는다. Windows 는 import 표에서 읽지만 리눅스는 그 정보를 밖에 내주지 않는다. 그래서 이름에 타겟 이름을 붙여
#     모듈마다 고유하게 두고, `dlsym( 모듈, "sw_moduleEngineAbiStamp_<의존>" )` 이 돌려주는 주소로 가린다 — dlsym 은
#     그 모듈의 검색 범위(자기 + 자기 의존)에서 찾으므로 그 주소가 곧 모듈이 실제로 묶인 의존 이미지의 것이다.
function(sw_addModuleEngineStamp TARGET_NAME)
	if(SW_SHIPPING_BUILD OR NOT TARGET ${TARGET_NAME})
		return()
	endif()

	get_target_property(swTargetType ${TARGET_NAME} TYPE)
	if(NOT swTargetType STREQUAL "SHARED_LIBRARY" AND NOT swTargetType STREQUAL "MODULE_LIBRARY")
		return()
	endif()

	sw_defineEngineAbiStamp()
	set(swStampSource "${CMAKE_BINARY_DIR}/generated/moduleidentity/${TARGET_NAME}EngineStamp.cpp")
	file(CONFIGURE OUTPUT "${swStampSource}" CONTENT
"// 생성 파일 - sw_addModuleEngineStamp (cmake/Engine/EngineAbiStamp.cmake). 고치지 마십시오.
// 이 모듈이 빌드된 Core · Engine 헤더의 지문입니다. 핫 리로드가 올리기 전에 돌고 있는 엔진의 것과 대조하고,
// 리눅스에서는 이 상수의 주소로 의존 모듈이 어느 이미지에 묶였는지 가립니다.
#include \"@SW_ENGINE_ABI_STAMP_HEADER@\"

#if defined( SW_PLATFORM_WINDOWS ) // 플랫폼은 CMake 의 SW_PLATFORM_* 로 묻는다 (Core/Common/TargetMacroCheck.h)
    #define SW_MODULE_ENGINE_STAMP_EXPORT __declspec( dllexport )
#else
    #define SW_MODULE_ENGINE_STAMP_EXPORT __attribute__( ( visibility( \"default\" ) ) )
#endif

extern \"C\" SW_MODULE_ENGINE_STAMP_EXPORT const char sw_moduleEngineAbiStamp_@TARGET_NAME@[];
extern \"C\" SW_MODULE_ENGINE_STAMP_EXPORT const char sw_moduleEngineAbiStamp_@TARGET_NAME@[] = SW_ENGINE_ABI_STAMP;
" @ONLY)

	target_sources(${TARGET_NAME} PRIVATE "${swStampSource}")
	set_source_files_properties("${swStampSource}" PROPERTIES
		SKIP_PRECOMPILE_HEADERS ON
		SKIP_UNITY_BUILD_INCLUSION ON
		OBJECT_DEPENDS "${SW_ENGINE_ABI_STAMP_HEADER}"
	)
	add_dependencies(${TARGET_NAME} SwEngineAbiStamp)
endfunction()
