# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 호스트 · 런타임의 기반(Common, Log, FileUtil, String 등)을 담는 STATIC 타깃
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 소스 — 폴더의 .cpp 전부. 플랫폼 폴더(Windows/ · Linux/ · Posix/)는 그 플랫폼에서만(`sw_filterPlatformSources` — 고르지 않은 것은
#    소스 검사 게이트에 "이 구성이 짓지 않는다" 로 적힌다). 목록을 손으로 들지 않는다 — 빠지면 ReflectionParser(Core 를 링크한다) 링크에서야 깨지던 자리다.
# ------------------------------------------------------------------------------
sw_globSourceTree(listCoreSource RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
sw_filterPlatformSources(listCoreSource)
# 단위 테스트(틀 `Testing/` 과 기능 폴더의 `Tests/`)는 배포 구성이 짓지 않는다 — 배포본에 테스트 코드가 없다(구성마다 다른 케이스 수가 아니라 0).
if(SW_SHIPPING_BUILD)
	sw_removeExcludedSources(listCoreSource "/Source/Core/Testing/" ${SW_DEV_ONLY_SOURCE_PATTERNS})
endif()

# ------------------------------------------------------------------------------
# 2) 티어 표 — 폴더 목록을 CMake 에 또 적지 않고 `Config/Rules/Layers/CoreLayers.toml` 의 `[tier]`(폴더 = 숫자)와 `[subtier]`("폴더/하위" = 숫자)를
#    그대로 읽는다. 파일의 티어는 그 폴더 경로의 가장 긴 표 키가 정한다(`Log/Sink/X.cpp` 는 `Log/Sink`). 표에 없는 폴더는 구성이 선다.
# ------------------------------------------------------------------------------
set(swCoreTierTable "${CMAKE_SOURCE_DIR}/Config/Rules/Layers/CoreLayers.toml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${swCoreTierTable}")
file(STRINGS "${swCoreTierTable}" listCoreTierLine REGEX "^\"?[A-Za-z0-9_/]+\"?[ \t]*=[ \t]*[0-9]+")
foreach(tierLine IN LISTS listCoreTierLine)
	string(REGEX MATCH "^\"?([A-Za-z0-9_/]+)\"?[ \t]*=[ \t]*([0-9]+)" ignored "${tierLine}")
	set(swCoreTier_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
if(NOT DEFINED swCoreTier_Log)
	message(FATAL_ERROR "[Core] ${swCoreTierTable} 의 [tier] 에서 Log 의 티어를 읽지 못했다 — 로그가 있는 티어를 가를 수 없다")
endif()

# sw_getCoreTier: `Source/Core` 기준 파일 경로의 티어(가장 긴 표 키). 바로 아래 파일이거나 표에 없는 폴더면 빈 값이다.
function(sw_getCoreTier RELATIVE_PATH OUT_VAR)
	get_filename_component(folder "${RELATIVE_PATH}" DIRECTORY)
	set(result "")
	while(folder)
		if(DEFINED swCoreTier_${folder})
			set(result ${swCoreTier_${folder}})
			break()
		endif()
		get_filename_component(folder "${folder}" DIRECTORY)
	endwhile()
	set(${OUT_VAR} "${result}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 3) 소스를 티어로 가른다 — 티어마다 OBJECT 부분 `Core_tier<N>_objects` 하나(Engine 층 부분 `EngineLayer<N>` 과 같은 틀).
#    단위 테스트(`<폴더>/Tests/`)와 `Source/Core` 바로 아래 파일은 테스트 틀(Testing)을 쓰므로 맨 위 티어 부분이 짓는다.
# ------------------------------------------------------------------------------
set(listCoreTier "")
set(listCoreTopSource "")
foreach(source IN LISTS listCoreSource)
	file(RELATIVE_PATH relativeSource "${CMAKE_CURRENT_SOURCE_DIR}" "${source}")
	if(relativeSource MATCHES "/Tests/" OR NOT relativeSource MATCHES "/")
		list(APPEND listCoreTopSource "${source}")
		continue()
	endif()
	sw_getCoreTier("${relativeSource}" sourceTier)
	if(sourceTier STREQUAL "")
		message(FATAL_ERROR "[Core] ${relativeSource} 의 폴더가 티어 표(Config/Rules/Layers/CoreLayers.toml 의 [tier])에 없다 — 티어를 정해 넣는다")
	endif()
	list(APPEND swCoreTierSource_${sourceTier} "${source}")
	list(APPEND listCoreTier ${sourceTier})
endforeach()
list(REMOVE_DUPLICATES listCoreTier)
list(SORT listCoreTier COMPARE NATURAL)
list(GET listCoreTier -1 swCoreTopTier)
list(APPEND swCoreTierSource_${swCoreTopTier} ${listCoreTopSource})

# 헤더마다 티어 — 껍데기(아래 4)와 티어별 pch(아래 5)가 읽는다. 바로 아래 헤더(pch · CoreMinimal 진입 헤더)는 티어가 없다.
sw_globSourceTree(listCoreHeader RECURSE RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.inl")
set(listCoreTieredHeader "")
foreach(header IN LISTS listCoreHeader)
	if(NOT header MATCHES "/")
		continue()
	endif()
	sw_getCoreTier("${header}" headerTier)
	if(headerTier STREQUAL "")
		message(FATAL_ERROR "[Core] ${header} 의 폴더가 티어 표(Config/Rules/Layers/CoreLayers.toml 의 [tier])에 없다 — 티어를 정해 넣는다")
	endif()
	set(swCoreHeaderTier_${header} ${headerTier})
	list(APPEND listCoreTieredHeader "${header}")
endforeach()

# ------------------------------------------------------------------------------
# 4) 위 티어 껍데기 — 부분 N 은 N 보다 위 티어의 헤더마다 같은 경로의 `#error` 파일을 include 자리 맨 앞에 둔다. 티어 N 의 .cpp 가 위 티어 헤더를
#    include 하면(헤더를 거친 전이 include 도) 컴파일이 멈춘다. 예전에는 Log 아래 · 위 두 부분만 짓고 나머지 티어는 린트(`CoreTiers.py`)가 직접 include 를 봤다.
#    헤더는 그 헤더를 include 하는 같은 · 아래 티어 .cpp 와 부분의 pch 를 컴파일할 때 걸린다(Engine 층 부분과 같은 틈 — 아무도 include 하지 않는 헤더는 보지 않는다).
# ------------------------------------------------------------------------------
function(sw_writeCoreTierStubs PART TIER)
	set(stubRoot "${CMAKE_BINARY_DIR}/generated/sw/CoreLayerStubs/tier${TIER}")
	set(listStub "")
	foreach(header IN LISTS listCoreTieredHeader)
		set(headerTier ${swCoreHeaderTier_${header}})
		if(NOT headerTier GREATER TIER)
			continue()
		endif()
		set(stubContent "#error \"Core tier ${headerTier} header cannot be included from tier ${TIER} Core code (Config/Rules/Layers/CoreLayers.toml). A lower tier must not see an upper one: move what you need down, or report through SW_LOW_LEVEL_REPORT.\"
")
		set(stubPath "${stubRoot}/Core/${header}")
		set(oldStub "")
		if(EXISTS "${stubPath}")
			file(READ "${stubPath}" oldStub)
		endif()
		if(NOT oldStub STREQUAL stubContent)
			file(WRITE "${stubPath}" "${stubContent}")
		endif()
		list(APPEND listStub "${stubPath}")
	endforeach()
	# 헤더가 사라지거나 티어가 바뀌면 그 껍데기도 지운다(남으면 옮겨 온 아래 티어 헤더를 가린다).
	set(manifestPath "${stubRoot}.manifest")
	if(EXISTS "${manifestPath}")
		file(STRINGS "${manifestPath}" listOldStub)
		if(listStub)
			list(REMOVE_ITEM listOldStub ${listStub})
		endif()
		foreach(oldStubPath IN LISTS listOldStub)
			file(REMOVE "${oldStubPath}")
		endforeach()
	endif()
	string(REPLACE ";" "
" manifestContent "${listStub}")
	file(WRITE "${manifestPath}" "${manifestContent}
")
	target_include_directories(${PART} BEFORE PRIVATE "${stubRoot}")
endfunction()

# ------------------------------------------------------------------------------
# 5) 티어 부분 — 한 번만 컴파일한다. ReflectionParser 는 Core STATIC 을 링크하고, Engine 은 같은 OBJECT 들을 넣어 Dev 에서 Engine.dll 로 export 한다
#    (`Core` 의 `SW_CORE_OBJECTS` 속성이 목록이다). SW_EXPORTS: App · Editor(SW_IMPORTS)가 dllimport 할 수 있게 한다.
#    pch: 부분마다 `CoreMinimal.h` 의 include 가운데 그 티어 이하만 담은 생성 헤더를 쓴다 — 소스의 `#include "pch.h"` 는 그대로 두고 생성 폴더만 다르다.
#    위 티어 선언(티어 3 코드의 `formatString`, Log 아래의 `SW_LOG_*` …)을 pch 가 몰래 넣어 주지 않는다. Engine 층 부분처럼 pch 는 부분 수만큼 짓는다.
# ------------------------------------------------------------------------------
# 잎 잠금 단언(잠금을 쥔 채 로그를 쓰면 Debug 에서 멈춘다) — 위반 자리를 다 고친 뒤 켠다(NEXT 의 lockfmt 후속). 꺼져 있으면 단언 대신 아무것도 하지 않는다.
option(SW_ENFORCE_LEAF_LOCK_LOG "Debug: assert when a log is written while a sw::mutex is held (leaf lock rule)" OFF)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/CoreMinimal.h" listCoreMinimalLine REGEX "^#include \"Core/[^\"]+\"")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/CoreMinimal.h")
set(listCoreObjects "")
foreach(tier IN LISTS listCoreTier)
	set(partName Core_tier${tier}_objects)
	set(pchEntryDir "${CMAKE_BINARY_DIR}/generated/sw/pchentry/coretier${tier}")
	set(pchContent "// generated by Source/Core/CMakeLists.txt — CoreMinimal.h 의 include 가운데 티어 ${tier} 이하\n#pragma once\n")
	foreach(includeLine IN LISTS listCoreMinimalLine)
		string(REGEX MATCH "^#include \"Core/([^\"]+)\"" ignored "${includeLine}")
		set(includedTier ${swCoreHeaderTier_${CMAKE_MATCH_1}})
		if(includedTier STREQUAL "")
			message(FATAL_ERROR "[Core] CoreMinimal.h 의 ${CMAKE_MATCH_1} 티어를 모른다 — 티어 표를 고친다")
		endif()
		if(NOT includedTier GREATER tier)
			string(APPEND pchContent "#include \"Core/${CMAKE_MATCH_1}\"\n")
		endif()
	endforeach()
	file(CONFIGURE OUTPUT "${pchEntryDir}/pch.h" CONTENT "${pchContent}" @ONLY)

	add_library(${partName} OBJECT ${swCoreTierSource_${tier}})
	set_target_properties(${partName} PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		FOLDER "Source/Core"
	)
	target_include_directories(${partName} PRIVATE "${pchEntryDir}") # `Source/Core` 자체는 include 경로가 아니다 — `#include "pch.h"` 만 이 생성 헤더로 푼다
	sw_applyTargetLayerGuard(${partName} core) # 타깃 경계 — Core 는 Engine 과 그 위를 보지 못한다
	sw_writeCoreTierStubs(${partName} ${tier})
	target_link_libraries(${partName} PUBLIC sw_public_source_includes)
	target_link_libraries(${partName} PUBLIC sw_third_party_includes)
	if(NOT SW_SHIPPING_BUILD)
		target_compile_definitions(${partName} PRIVATE SW_EXPORTS)
	endif()
	if(WIN32)
		target_link_libraries(${partName} PUBLIC bcrypt comdlg32 ole32 shell32 dbghelp synchronization ws2_32)
	endif()
	# `SW_LOG_TAG` · 등록자의 모듈 이름(`SW_REGISTRAR_MODULE` — Core 오브젝트가 들어가는 이미지 이름, Engine 과 같다)은 로그가 있는 티어만 정의한다.
	# 부분마다 pch 를 따로 지으므로(clang 은 pch 와 `-D` 가 다르면 거부) 정의가 달라도 된다.
	if(NOT tier LESS swCoreTier_Log)
		target_compile_definitions(${partName} PRIVATE "SW_LOG_TAG=\"Engine\"" "SW_REGISTRAR_MODULE=\"Engine\"")
		if(SW_ENFORCE_LEAF_LOCK_LOG)
			target_compile_definitions(${partName} PRIVATE SW_ENFORCE_LEAF_LOCK_LOG)
		endif()
	endif()
	sw_configurePCH(${partName} "${pchEntryDir}/pch.h")
	sw_setUnityBuild(${partName})
	target_link_libraries(${partName} PRIVATE sw_global_options)
	list(APPEND listCoreObjects ${partName})
endforeach()

# 벡터 거리 · 길이(`float3::getDistance` 등)는 파괴 · 파쇄가 쓰는 줄 밖 함수다 — 구성마다 같은 비트여야 한다(`sw_markDeterministicSources`).
# 여기서 축약을 끄는 비용은 이 함수들의 FMA 하나씩이다(인라인이 아니라 호출 비용이 이미 더 크다). 결정적 삼각함수(`DeterministicMathUtil`)는 롤백 시뮬레이션이
# libm 대신 부르는 줄 밖 함수라 같은 표시를 건다. 파쇄 다각형 삼각 분할(`Spatial/PolygonTriangulation`)도 파괴 사건 해시가 구성마다 같아야 해 같은 표시를 건다.
# 광선 ↔ 도형 판정 · 방향 판정(`Math/Ray` · `Math/OrientationUtil`)은 결정적 시뮬레이션 키트(MechArena)가 부르는 줄 밖 실수 함수라 같은 표시를 건다.
foreach(swDeterministicSource IN ITEMS Math/VectorMath.cpp Math/DeterministicMathUtil.cpp Math/Ray.cpp Math/OrientationUtil.cpp Spatial/PolygonTriangulation.cpp)
	sw_getCoreTier("${swDeterministicSource}" deterministicTier)
	if(NOT "${CMAKE_CURRENT_SOURCE_DIR}/${swDeterministicSource}" IN_LIST swCoreTierSource_${deterministicTier})
		message(FATAL_ERROR "[Core] ${swDeterministicSource} 가 티어 ${deterministicTier} 부분 소스에 없다 — 결정성 표시(sw_markDeterministicSources)가 걸릴 타깃을 다시 정한다")
	endif()
	sw_markDeterministicSources(Core_tier${deterministicTier}_objects ${swDeterministicSource})
endforeach()

# ------------------------------------------------------------------------------
# 6) Core STATIC — 파서가 링크하는 껍데기. Engine 은 OBJECT 들을 직접 가져간다
# ------------------------------------------------------------------------------
set(listCoreObjectFile "")
foreach(coreObjects IN LISTS listCoreObjects)
	list(APPEND listCoreObjectFile $<TARGET_OBJECTS:${coreObjects}>)
endforeach()
add_library(Core STATIC ${listCoreObjectFile})
set_target_properties(Core PROPERTIES
	FOLDER "Source"
)
target_link_libraries(Core PUBLIC sw_public_source_includes)
target_link_libraries(Core PUBLIC sw_third_party_includes)

if(WIN32)
	target_link_libraries(Core PUBLIC bcrypt comdlg32 ole32 shell32 dbghelp synchronization ws2_32)
endif()

# Engine 이 $<TARGET_OBJECTS:...> 를 가져갈 수 있도록 의존성을 드러낸다
add_dependencies(Core ${listCoreObjects})
set_property(TARGET Core PROPERTY SW_CORE_OBJECTS ${listCoreObjects})

list(LENGTH listCoreObjects coreObjectCount)
message(STATUS "[Core] STATIC - foundation, ${coreObjectCount} tier parts (ReflectionParser links Core; Engine shares the parts' objects)")
