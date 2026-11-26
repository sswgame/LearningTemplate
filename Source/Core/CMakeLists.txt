# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 호스트 · 런타임의 기반(Common, Log, FileUtil, String 등)을 담는 STATIC 타깃
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 소스 — 폴더의 .cpp 전부. 플랫폼 폴더(Windows/ · Linux/ · Posix/)는 그 플랫폼에서만(`sw_filter_platform_sources` — 고르지 않은 것은
#    소스 검사 게이트에 "이 구성이 짓지 않는다" 로 적힌다). 목록을 손으로 들지 않는다 — 빠지면 ReflectionParser(Core 를 링크한다) 링크에서야 깨지던 자리다.
# ------------------------------------------------------------------------------
sw_glob_source_tree(list_core_source RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
sw_filter_platform_sources(list_core_source)
# 힙 백엔드 — `SW_MEMORY_BACKEND`(DerivedOptions.cmake 가 푼 값)가 고른 `Memory/MemoryBackend<이름>.cpp` 하나만 짓고 나머지는 "이 구성이 짓지 않는다" 로 적는다.
# 링크 시점 선택이라 할당마다 가상 호출이 없다(언리얼은 `FMalloc*` 가상 호출 — 실행 중에 바꿀 일이 없어 링크로 고정한다).
if(SW_MEMORY_BACKEND STREQUAL "crt")
	set(memory_backend_file "CRT")
elseif(SW_MEMORY_BACKEND STREQUAL "mimalloc")
	set(memory_backend_file "Mimalloc")
else()
	message(FATAL_ERROR "[Core] SW_MEMORY_BACKEND='${SW_MEMORY_BACKEND}' has no MemoryBackend implementation file")
endif()
set(list_memory_backend_source ${list_core_source})
list(FILTER list_memory_backend_source INCLUDE REGEX "/Source/Core/Memory/MemoryBackend[A-Za-z]+\\.cpp$")
if(NOT "${CMAKE_CURRENT_SOURCE_DIR}/Memory/MemoryBackend${memory_backend_file}.cpp" IN_LIST list_memory_backend_source)
	message(FATAL_ERROR "[Core] Memory/MemoryBackend${memory_backend_file}.cpp is missing — the heap backend '${SW_MEMORY_BACKEND}' has no implementation")
endif()
list(REMOVE_ITEM list_memory_backend_source "${CMAKE_CURRENT_SOURCE_DIR}/Memory/MemoryBackend${memory_backend_file}.cpp")
foreach(unused_backend_source IN LISTS list_memory_backend_source)
	get_filename_component(unused_backend_name "${unused_backend_source}" NAME_WE)
	sw_remove_excluded_sources(list_core_source "/Source/Core/Memory/${unused_backend_name}\\.cpp$")
endforeach()
# 단위 테스트(틀 `TestFramework/` 과 기능 폴더의 `Tests/`)는 배포 구성이 짓지 않는다 — 배포본에 테스트 코드가 없다(구성마다 다른 케이스 수가 아니라 0).
if(SW_SHIPPING_BUILD)
	sw_remove_excluded_sources(list_core_source "/Source/Core/TestFramework/" ${SW_DEV_ONLY_SOURCE_PATTERNS})
endif()

# ------------------------------------------------------------------------------
# 2) 층 표 — 폴더 목록을 CMake 에 또 적지 않고 `Config/Rules/Layers/CoreLayers.toml` 의 `[layer]`(폴더 = 숫자)와 `[sublayer]`("폴더/하위" = 숫자)를
#    그대로 읽는다. 파일의 층은 그 폴더 경로의 가장 긴 표 키가 정한다(`Log/Backend/X.cpp` 는 `Log/Backend`). 표에 없는 폴더는 구성이 선다.
# ------------------------------------------------------------------------------
set(core_layer_table "${CMAKE_SOURCE_DIR}/Config/Rules/Layers/CoreLayers.toml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${core_layer_table}")
file(STRINGS "${core_layer_table}" list_core_layer_line ENCODING UTF-8 REGEX "^\"?[A-Za-z0-9_/]+\"?[ \t]*=[ \t]*[0-9]+")
foreach(layer_line IN LISTS list_core_layer_line)
	string(REGEX MATCH "^\"?([A-Za-z0-9_/]+)\"?[ \t]*=[ \t]*([0-9]+)" ignored "${layer_line}")
	set(core_layer_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
if(NOT DEFINED core_layer_Network)
	message(FATAL_ERROR "[Core] ${core_layer_table} 의 [layer] 에서 Network 의 층을 읽지 못했다 — 소켓 링크(ws2_32)를 걸 부분을 가를 수 없다")
endif()
if(NOT DEFINED core_layer_Log)
	message(FATAL_ERROR "[Core] ${core_layer_table} 의 [layer] 에서 Log 의 층을 읽지 못했다 — 로그가 있는 층을 가를 수 없다")
endif()

# sw_get_core_layer: `Source/Core` 기준 파일 경로의 층(가장 긴 표 키). 바로 아래 파일이거나 표에 없는 폴더면 빈 값이다.
function(sw_get_core_layer RELATIVE_PATH OUT_VAR)
	get_filename_component(folder "${RELATIVE_PATH}" DIRECTORY)
	set(result "")
	while(folder)
		if(DEFINED core_layer_${folder})
			set(result ${core_layer_${folder}})
			break()
		endif()
		get_filename_component(folder "${folder}" DIRECTORY)
	endwhile()
	set(${OUT_VAR} "${result}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 3) 소스를 층으로 가른다 — 층마다 OBJECT 부분 `Core_layer<N>_objects` 하나(Engine 층 부분 `EngineLayer<N>` 과 같은 틀).
#    단위 테스트(`<폴더>/Tests/`)와 `Source/Core` 바로 아래 파일은 테스트 틀(TestFramework)을 쓰므로 맨 위 층 부분이 짓는다.
# ------------------------------------------------------------------------------
set(list_core_layer "")
set(list_core_top_source "")
foreach(source IN LISTS list_core_source)
	file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_SOURCE_DIR}" "${source}")
	if(relative_source MATCHES "/Tests/" OR NOT relative_source MATCHES "/")
		list(APPEND list_core_top_source "${source}")
		continue()
	endif()
	sw_get_core_layer("${relative_source}" source_layer)
	if(source_layer STREQUAL "")
		message(FATAL_ERROR "[Core] ${relative_source} 의 폴더가 층 표(Config/Rules/Layers/CoreLayers.toml 의 [layer])에 없다 — 층을 정해 넣는다")
	endif()
	list(APPEND core_layer_source_${source_layer} "${source}")
	list(APPEND list_core_layer ${source_layer})
endforeach()
list(REMOVE_DUPLICATES list_core_layer)
list(SORT list_core_layer COMPARE NATURAL)
list(GET list_core_layer -1 core_top_layer)
list(APPEND core_layer_source_${core_top_layer} ${list_core_top_source})

# 헤더마다 층 — 가드(아래 4)와 층별 pch(아래 5)가 읽는다. 바로 아래 헤더(pch · CoreMinimal 진입 헤더)는 층이 없다.
sw_glob_source_tree(list_core_header RECURSE RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.inl")
set(list_core_layer_header "")
foreach(header IN LISTS list_core_header)
	if(NOT header MATCHES "/")
		continue()
	endif()
	sw_get_core_layer("${header}" header_layer)
	if(header_layer STREQUAL "")
		message(FATAL_ERROR "[Core] ${header} 의 폴더가 층 표(Config/Rules/Layers/CoreLayers.toml 의 [layer])에 없다 — 층을 정해 넣는다")
	endif()
	set(core_header_layer_${header} ${header_layer})
	list(APPEND list_core_layer_header "${header}")
endforeach()

# ------------------------------------------------------------------------------
# 4) 위 층 가드 — 부분 N 은 N 보다 위 층의 헤더마다 같은 경로의 `#error` 파일을 include 자리 맨 앞에 둔다. 층 N 의 .cpp 가 위 층 헤더를
#    include 하면(헤더를 거친 전이 include 도) 컴파일이 멈춘다. 쓰기 · 낡은 가드 지우기는 Engine · Editor 층과 같은 함수다(TargetCompileRules.cmake).
#    헤더는 그 헤더를 include 하는 같은 · 아래 층 .cpp 와 부분의 pch 를 컴파일할 때 걸린다(Engine 층 부분과 같은 틈 — 아무도 include 하지 않는 헤더는 보지 않는다).
# ------------------------------------------------------------------------------
function(sw_write_core_layer_guards PART LAYER)
	set(guard_root "${CMAKE_BINARY_DIR}/generated/sw/CoreLayerGuards/layer${LAYER}")
	set(list_guard "")
	foreach(header IN LISTS list_core_layer_header)
		set(header_layer ${core_header_layer_${header}})
		if(NOT header_layer GREATER LAYER)
			continue()
		endif()
		sw_write_layer_guard(list_guard "${guard_root}/Core/${header}" "#error \"Core layer ${header_layer} header cannot be included from layer ${LAYER} Core code (Config/Rules/Layers/CoreLayers.toml). A lower layer must not see an upper one: move what you need down, or report through SW_LOW_LEVEL_REPORT.\"
")
	endforeach()
	sw_apply_layer_guards(${PART} "${guard_root}" ${list_guard})
endfunction()

# ------------------------------------------------------------------------------
# 5) 층 부분 — 한 번만 컴파일한다. ReflectionParser 는 Core STATIC 을 링크하고, Engine 은 같은 OBJECT 들을 넣어 Dev 에서 Engine.dll 로 export 한다
#    (`Core` 의 `SW_CORE_OBJECTS` 속성이 목록이다). SW_EXPORTS: App · Editor(SW_IMPORTS)가 dllimport 할 수 있게 한다.
#    pch: 부분마다 `CoreMinimal.h` 의 include 가운데 그 층 이하만 담은 생성 헤더를 쓴다 — 소스의 `#include "pch.h"` 는 그대로 두고 생성 폴더만 다르다.
#    위 층 선언(층 3 코드의 `formatString`, Log 아래의 `SW_LOG_*` …)을 pch 가 몰래 넣어 주지 않는다. Engine 층 부분처럼 pch 는 부분 수만큼 짓는다.
# ------------------------------------------------------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/CoreMinimal.h" list_core_minimal_line ENCODING UTF-8 REGEX "^#include \"Core/[^\"]+\"")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/CoreMinimal.h")
set(list_core_objects "")
foreach(layer IN LISTS list_core_layer)
	set(part_name Core_layer${layer}_objects)
	set(pch_entry_dir "${CMAKE_BINARY_DIR}/generated/sw/pchentry/corelayer${layer}")
	set(pch_content "// generated by Source/Core/CMakeLists.txt — CoreMinimal.h 의 include 가운데 층 ${layer} 이하\n#pragma once\n")
	foreach(include_line IN LISTS list_core_minimal_line)
		string(REGEX MATCH "^#include \"Core/([^\"]+)\"" ignored "${include_line}")
		set(included_layer ${core_header_layer_${CMAKE_MATCH_1}})
		if(included_layer STREQUAL "")
			message(FATAL_ERROR "[Core] CoreMinimal.h 의 ${CMAKE_MATCH_1} 층을 모른다 — 층 표를 고친다")
		endif()
		if(NOT included_layer GREATER layer)
			string(APPEND pch_content "#include \"Core/${CMAKE_MATCH_1}\"\n")
		endif()
	endforeach()
	file(CONFIGURE OUTPUT "${pch_entry_dir}/pch.h" CONTENT "${pch_content}" @ONLY)

	add_library(${part_name} OBJECT ${core_layer_source_${layer}})
	set_target_properties(${part_name} PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		FOLDER "Source/Core"
	)
	target_include_directories(${part_name} PRIVATE "${pch_entry_dir}") # `Source/Core` 자체는 include 경로가 아니다 — `#include "pch.h"` 만 이 생성 헤더로 푼다
	sw_apply_target_layer_guard(${part_name} core) # 타깃 경계 — Core 는 Engine 과 그 위를 보지 못한다
	sw_write_core_layer_guards(${part_name} ${layer})
	target_link_libraries(${part_name} PUBLIC sw_public_source_includes)
	target_link_libraries(${part_name} PUBLIC sw_third_party_includes)
	if(NOT SW_STATIC_MODULES) # Engine 이 공유 라이브러리일 때만 내보낸다
		target_compile_definitions(${part_name} PRIVATE SW_EXPORTS)
	endif()
	if(WIN32)
		target_link_libraries(${part_name} PUBLIC bcrypt comdlg32 ole32 shell32 dbghelp synchronization)
		# 소켓은 맨 위 층 Network 만 쓴다 — 다른 부분이 Winsock 을 물려받지 않는다(Core STATIC · Engine 은 아래에서 그대로 링크한다).
		if(layer EQUAL core_layer_Network)
			target_link_libraries(${part_name} PUBLIC ws2_32)
		endif()
	endif()
	# `SW_LOG_TAG` · 등록자의 모듈 이름(`SW_REGISTRAR_MODULE` — Core 오브젝트가 들어가는 이미지 이름, Engine 과 같다)은 로그가 있는 층만 정의한다.
	# 부분마다 pch 를 따로 지으므로(clang 은 pch 와 `-D` 가 다르면 거부) 정의가 달라도 된다.
	if(NOT layer LESS core_layer_Log)
		target_compile_definitions(${part_name} PRIVATE "SW_LOG_TAG=\"Engine\"" "SW_REGISTRAR_MODULE=\"Engine\"")
	endif()
	sw_configure_pch(${part_name} "${pch_entry_dir}/pch.h")
	sw_set_unity_build(${part_name})
	target_link_libraries(${part_name} PRIVATE sw_global_options)
	list(APPEND list_core_objects ${part_name})
endforeach()

# 벡터 기본 연산(`float3::dot` · `getDistance` 등)은 `VectorMath.h` 끝의 축약을 끈 pragma 구간에서 헤더 인라인이다 — 부르는 TU 의 옵션과 상관없이 같은 비트다.
# 여기 남는 Math .cpp 는 행렬 · 쿼터니언 · libm 을 부르는 줄 밖 함수다(구성마다 같은 비트여야 한다 — `sw_mark_deterministic_sources`). 결정적 삼각함수(`DeterministicMathUtil`)는 롤백 시뮬레이션이
# libm 대신 부르는 줄 밖 함수라 같은 표시를 건다. 파쇄 다각형 삼각 분할(`Spatial/PolygonTriangulation`)도 파괴 사건 해시가 구성마다 같아야 해 같은 표시를 건다.
# 광선 ↔ 도형 판정 · 방향 판정(`Math/Ray` · `Math/OrientationUtil`)은 결정적 시뮬레이션 키트(MechArena)가 부르는 줄 밖 실수 함수라 같은 표시를 건다.
# Math 폴더는 통째로 건다(감사 C-14) — 손으로 적은 목록은 결정성 물리가 부르는 `MatrixMath`(쿼터니언 곱 · 회전) · `ValueNoise` 를 빠뜨렸다.
# 새 Math 파일도 저절로 든다(Engine Physics 폴더와 같은 틀, 유니티 Burst `FloatMode.Deterministic` 이 수학 묶음 전체에 거는 것과 같은 자리).
# 축약만 끈다: `MatrixMath` 의 slerp · 오일러 변환은 여전히 libm 이다 — 같은 플랫폼 안에서는 CRT 가 같아 구성 간 비트가 같고(결정 D7-8),
# 롤백 시뮬레이션은 삼각함수를 `DeterministicMathUtil` 로 부른다. 표시 비용(FMA 합치기 없음 · 유니티 묶음에서 빠짐)은 재지 않았다.
sw_glob_source_tree(list_deterministic_math_source RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/Math/*.cpp")
foreach(deterministic_source IN LISTS list_deterministic_math_source ITEMS Spatial/PolygonTriangulation.cpp)
	sw_get_core_layer("${deterministic_source}" deterministic_layer)
	if(NOT "${CMAKE_CURRENT_SOURCE_DIR}/${deterministic_source}" IN_LIST core_layer_source_${deterministic_layer})
		message(FATAL_ERROR "[Core] ${deterministic_source} 가 층 ${deterministic_layer} 부분 소스에 없다 — 결정성 표시(sw_mark_deterministic_sources)가 걸릴 타깃을 다시 정한다")
	endif()
	sw_mark_deterministic_sources(Core_layer${deterministic_layer}_objects ${deterministic_source})
endforeach()

# ------------------------------------------------------------------------------
# 6) Core STATIC — 파서가 링크하는 정적 라이브러리(층 부분의 오브젝트를 모은다). Engine 은 OBJECT 들을 직접 가져간다
# ------------------------------------------------------------------------------
set(list_core_object_file "")
foreach(core_objects IN LISTS list_core_objects)
	list(APPEND list_core_object_file $<TARGET_OBJECTS:${core_objects}>)
endforeach()
add_library(Core STATIC ${list_core_object_file})
set_target_properties(Core PROPERTIES
	FOLDER "Source"
)
target_link_libraries(Core PUBLIC sw_public_source_includes)
target_link_libraries(Core PUBLIC sw_third_party_includes)

if(WIN32)
	target_link_libraries(Core PUBLIC bcrypt comdlg32 ole32 shell32 dbghelp synchronization ws2_32)
endif()

# Engine 이 $<TARGET_OBJECTS:...> 를 가져갈 수 있도록 의존성을 드러낸다
add_dependencies(Core ${list_core_objects})
set_property(TARGET Core PROPERTY SW_CORE_OBJECTS ${list_core_objects})

# 힙 백엔드 라이브러리 — 백엔드 구현 파일이 든 층 부분만 헤더를 보고, Core 오브젝트를 담는 쪽(Core STATIC · Engine)이 링크한다.
# Engine 은 오브젝트를 `$<TARGET_OBJECTS>` 로 가져가 부분의 링크 의존을 물려받지 못하므로 `SW_CORE_LINK_LIBRARIES` 속성으로 넘긴다(Source/Engine/CMakeLists.txt).
set(list_core_link_library "")
if(SW_MEMORY_BACKEND STREQUAL "mimalloc")
	list(APPEND list_core_link_library sw_mimalloc)
endif()
if(list_core_link_library)
	sw_get_core_layer("Memory/MemoryBackend${memory_backend_file}.cpp" memory_backend_layer)
	target_link_libraries(Core_layer${memory_backend_layer}_objects PRIVATE ${list_core_link_library})
	target_link_libraries(Core PRIVATE ${list_core_link_library})
endif()
set_property(TARGET Core PROPERTY SW_CORE_LINK_LIBRARIES ${list_core_link_library})

list(LENGTH list_core_objects core_object_count)
message(STATUS "[Core] STATIC - foundation, ${core_object_count} layer parts (ReflectionParser links Core; Engine shares the parts' objects)")
