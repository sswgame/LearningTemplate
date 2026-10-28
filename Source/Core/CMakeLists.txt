# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 호스트 · 런타임의 기반(Common, Log, FileUtil, String 등)을 담는 STATIC 타깃
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 소스 — 폴더의 .cpp 전부. 플랫폼 폴더(Windows/ · Linux/ · Posix/)는 그 플랫폼에서만(`sw_filterPlatformSources` — 고르지 않은 것은
#    소스 검사 게이트에 "이 구성이 짓지 않는다" 로 적힌다). 목록을 손으로 들지 않는다 — 빠지면 ReflectionParser(Core 를 링크한다) 링크에서야 깨지던 자리다.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE listCoreSource CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
sw_filterPlatformSources(listCoreSource)

# ------------------------------------------------------------------------------
# 2) 소스를 티어로 가른다 — 낮은 티어(티어 표에서 `Log` 보다 아래인 폴더)는 따로 짓는다.
#    낮은 티어의 `.cpp` 는 `Logger.h` 가 없는 전용 pch(`pchLow.h`)로 컴파일되어 pch 가 몰래 넣어 주던 위 티어 선언(`SW_LOG_*` · `StringBuilder` ·
#    `FileUtil` …)을 쓰면 컴파일이 실패한다. 폴더 목록을 CMake 에 또 적지 않고 `Scripts/generate/rules/CoreLayers.toml` 의 티어 표(`[tier]`)를 그대로 읽는다 —
#    표에 없는 폴더는 구성이 선다. 린트 규칙 4(`SW_LOG_*` 금지)는 이 구성이 못 막는 곳(낮은 티어 **헤더** — 위 티어 pch 로
#    컴파일되는 TU 가 포함한다 — 와 pch 를 끈 구성의 방어)과 빠른 피드백을 맡는다.
# ------------------------------------------------------------------------------
set(swCoreTierTable "${CMAKE_SOURCE_DIR}/Scripts/generate/rules/CoreLayers.toml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${swCoreTierTable}")
file(STRINGS "${swCoreTierTable}" listCoreTierLine REGEX "^[A-Za-z0-9_]+[ \t]*=[ \t]*[0-9]+")
foreach(tierLine IN LISTS listCoreTierLine)
	string(REGEX MATCH "^([A-Za-z0-9_]+)[ \t]*=[ \t]*([0-9]+)" ignored "${tierLine}")
	set(swCoreTier_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
if(NOT DEFINED swCoreTier_Log)
	message(FATAL_ERROR "[Core] ${swCoreTierTable} 의 [tier] 에서 Log 의 티어를 읽지 못했다 — 낮은 티어를 가를 수 없다")
endif()

set(listCoreLowSource "")
set(listCoreHighSource "")
foreach(source IN LISTS listCoreSource)
	file(RELATIVE_PATH relativeSource "${CMAKE_CURRENT_SOURCE_DIR}" "${source}")
	string(FIND "${relativeSource}" "/" slashPosition)
	if(slashPosition EQUAL -1)
		list(APPEND listCoreHighSource "${source}") # Source/Core 바로 아래 파일은 폴더가 아니라 높은 쪽이다
		continue()
	endif()
	string(SUBSTRING "${relativeSource}" 0 ${slashPosition} sourceFolder)
	if(NOT DEFINED swCoreTier_${sourceFolder})
		message(FATAL_ERROR "[Core] Core/${sourceFolder}/ 는 티어 표(Scripts/generate/rules/CoreLayers.toml 의 [tier])에 없는 폴더다 — 티어를 정해 넣는다")
	endif()
	if(swCoreTier_${sourceFolder} LESS swCoreTier_Log)
		list(APPEND listCoreLowSource "${source}")
	else()
		list(APPEND listCoreHighSource "${source}")
	endif()
endforeach()
if(NOT listCoreLowSource OR NOT listCoreHighSource)
	message(FATAL_ERROR "[Core] 티어로 가르니 한쪽이 비었다(낮은 ${listCoreLowSource} / 높은 ${listCoreHighSource}) — 티어 표를 읽는 쪽이 틀렸다")
endif()

# ------------------------------------------------------------------------------
# 3) Core OBJECT 둘 — 한 번만 컴파일한다(`Core_objects` 높은 티어 · `Core_low_objects` 낮은 티어)
#    ReflectionParser: Core STATIC 을 링크한다
#    Engine: 같은 OBJECT 들을 넣어 Dev 에서 Engine.dll 로 export 한다(`Core` 의 `SW_CORE_OBJECTS` 속성이 목록이다)
#    SW_EXPORTS: App · Editor(SW_IMPORTS)가 dllimport 할 수 있게 한다
#    pch 진입 헤더: 소스의 `#include "pch.h"` 는 그대로 두고 생성 폴더(NAME)만 달라 낮은 티어는 `Core/pchLow.h` 로 풀린다.
# ------------------------------------------------------------------------------
function(sw_addCoreObjects TARGET_NAME PCH_ENTRY_NAME PCH_FILE)
	add_library(${TARGET_NAME} OBJECT ${ARGN})
	set_target_properties(${TARGET_NAME} PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		FOLDER "Source"
	)

	# `Source/Core` 자체는 include 경로가 아니다 — `#include "pch.h"` 만 Core 진입 헤더로 푼다.
	sw_addPCHEntryDirectory(${TARGET_NAME} PRIVATE ${PCH_ENTRY_NAME} "Core/${PCH_FILE}")
	target_link_libraries(${TARGET_NAME} PUBLIC sw_public_source_includes)
	target_link_libraries(${TARGET_NAME} PUBLIC sw_third_party_includes)

	if(NOT SW_SHIPPING_BUILD)
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS)
	endif()

	if(WIN32)
		target_link_libraries(${TARGET_NAME} PUBLIC bcrypt comdlg32 ole32 shell32 dbghelp synchronization ws2_32)
	endif()

	sw_configurePCH(${TARGET_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/${PCH_FILE}")
	if(SW_UNITY_BATCH_CORE)
		sw_setUnityBuild(${TARGET_NAME} BATCH_SIZE ${SW_UNITY_BATCH_CORE})
	endif()
	target_link_libraries(${TARGET_NAME} PRIVATE sw_global_options)
endfunction()

sw_addCoreObjects(Core_objects core pch.h ${listCoreHighSource})
# `SW_LOG_TAG` 는 높은 티어만 정의한다 — 낮은 티어는 `Logger.h` 가 없어 쓸 데가 없다. 두 타깃은 pch 를 따로 지으므로(clang 은 pch 와 `-D` 가 다르면 거부) 정의가 달라도 된다.
target_compile_definitions(Core_objects PRIVATE "SW_LOG_TAG=\"Engine\"")
# 잎 잠금 단언(잠금을 쥔 채 로그를 쓰면 Debug 에서 멈춘다) — 위반 자리를 다 고친 뒤 켠다(NEXT 의 lockfmt 후속). 꺼져 있으면 단언 대신 아무것도 하지 않는다.
option(SW_ENFORCE_LEAF_LOCK_LOG "Debug: assert when a log is written while a sw::mutex is held (leaf lock rule)" OFF)
if(SW_ENFORCE_LEAF_LOCK_LOG)
	target_compile_definitions(Core_objects PRIVATE SW_ENFORCE_LEAF_LOCK_LOG)
endif()

sw_addCoreObjects(Core_low_objects corelow pchLow.h ${listCoreLowSource})
# 벡터 거리 · 길이(`float3::getDistance` 등)는 파괴 · 파쇄가 쓰는 줄 밖 함수다 — 구성마다 같은 비트여야 한다(`sw_markDeterministicSources`).
# 여기서 축약을 끄는 비용은 이 함수들의 FMA 하나씩이다(인라인이 아니라 호출 비용이 이미 더 크다). Math 는 낮은 티어라 낮은 쪽에 건다.
# 결정적 삼각함수(`DeterministicMathUtil`)는 롤백 시뮬레이션이 libm 대신 부르는 줄 밖 함수라 같은 표시를 건다.
foreach(swDeterministicSource IN ITEMS Math/VectorMath.cpp Math/DeterministicMathUtil.cpp)
	if(NOT "${CMAKE_CURRENT_SOURCE_DIR}/${swDeterministicSource}" IN_LIST listCoreLowSource)
		message(FATAL_ERROR "[Core] ${swDeterministicSource} 가 낮은 티어 소스에 없다 — 결정성 표시(sw_markDeterministicSources)가 걸릴 타깃을 다시 정한다")
	endif()
endforeach()
sw_markDeterministicSources(Core_low_objects Math/VectorMath.cpp Math/DeterministicMathUtil.cpp)

# ------------------------------------------------------------------------------
# 4) Core STATIC — 파서가 링크하는 껍데기. Engine 은 OBJECT 들을 직접 가져간다
# ------------------------------------------------------------------------------
add_library(Core STATIC $<TARGET_OBJECTS:Core_objects> $<TARGET_OBJECTS:Core_low_objects>)
set_target_properties(Core PROPERTIES
	FOLDER "Source"
)
target_link_libraries(Core PUBLIC sw_public_source_includes)
target_link_libraries(Core PUBLIC sw_third_party_includes)

if(WIN32)
	target_link_libraries(Core PUBLIC bcrypt comdlg32 ole32 shell32 dbghelp synchronization ws2_32)
endif()

# Engine 이 $<TARGET_OBJECTS:...> 를 가져갈 수 있도록 의존성을 드러낸다
add_dependencies(Core Core_objects Core_low_objects)
set_property(TARGET Core PROPERTY SW_CORE_OBJECTS Core_objects Core_low_objects)

message(STATUS "[Core] STATIC - foundation (ReflectionParser links Core; Engine shares Core_objects + Core_low_objects)")
