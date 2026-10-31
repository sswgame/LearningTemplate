# ==============================================================================
# @file Source/Engine/CMakeLists.txt
# @brief Engine 라이브러리 — 소스 목록 · 타깃(ABI 스탬프 · PCH · 내보내기) · 서드파티 링크 · RHI · 시스템 라이브러리 · 유니티 · 리플렉션 코드젠 · Jolt 오브젝트
# @details
# 폴더(Common/Reflection/...)는 소스를 나눠 두기 위한 것이고, 모듈 사이의
# 순환 참조(Reflection <-> Graphics 등) 때문에 하나의 라이브러리로 링크한다
# (Dev 는 SHARED, Shipping 은 STATIC).
# 절의 순서가 링크 · 소스 순서다 — 9) Jolt 오브젝트는 Engine 설정을 그 시점에 옮겨 가므로 Engine 에 더하는 설정은 모두 그 앞에 둔다.
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 라이브러리 종류 · 소스 목록
# Dev: SHARED / Shipping: STATIC
# RHI MODULE 엔트리(백엔드 폴더의 Module/)와 백엔드 장치 .cpp 는 항상 Engine GLOB 에서 뺀다(장치 소스는 cmake/Engine/RHIBackends.cmake 가 백엔드 폴더로 안다).
# Dev 는 RHI_* MODULE 이, Shipping 은 아래 5) 가 고른 백엔드 하나를 같은 함수로 가져다 짓는다.
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD)
	set(coreLibType STATIC)
else()
	set(coreLibType SHARED)
endif()

set(coreExclude
	"Graphics/RHI/.+/Module/"   # 백엔드 모듈 엔트리(RHI_* MODULE 이 짓는다)
	"Physics/Jolt/[^/]+\\.cpp$"   # EngineJolt_objects 가 짓는다(아래 9))
)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

foreach(exPattern IN LISTS coreExclude)
	list(FILTER ENGINE_SOURCES EXCLUDE REGEX "${exPattern}")
endforeach()

# RHI 백엔드 장치 소스는 Engine 이 짓지 않는다 — Dev 는 RHI_* 모듈이, Shipping 은 아래 5) 가 고른 하나를 가져간다.
sw_getAllRHIBackendSources(listRHIBackendSource)
list(REMOVE_ITEM ENGINE_SOURCES ${listRHIBackendSource})

# 자동화 런타임을 뺀 구성(`SW_ENABLE_AUTOMATION=OFF`) — `Automation/Runner/`(실행기 · 세션 · 판정 · 보고)를 폴더째 뺀다. 창 제목 추적(RunTitle) · 사진 준비 판정(CaptureUtil) · 감시 값(Probe/ · *Probes) · 단계 등록(Scenario/)은
# 엔진 프레임이 읽거나 기능이 등록하는 것이라 남는다.
if(NOT SW_ENABLE_AUTOMATION)
	sw_excludeUnbuiltSources(ENGINE_SOURCES
		"/Source/Engine/Automation/Runner/.*\\.(cpp|h)$")
	message(STATUS "[Engine] SW_ENABLE_AUTOMATION=OFF: automation runtime is not built")
endif()

# 이 타깃이 짓지 않는 폴더(전용 서버의 `Renderer/` — 목록은 cmake/Engine/BuildLayout.cmake 의 `SW_UNBUILT_ENGINE_FOLDERS`).
# .cpp 만 뺀다 — 헤더의 값 타입(뷰 요청 · 델리게이트)은 `Engine.h` 를 거쳐 서버도 읽는다. 리플렉션 헤더도 아래 8) 에서 뺀다.
set(swUnbuiltFolderRegex "")
foreach(swUnbuiltFolder IN LISTS SW_UNBUILT_ENGINE_FOLDERS)
	sw_excludeUnbuiltSources(ENGINE_SOURCES "/Source/Engine/${swUnbuiltFolder}/.*\\.(c|cpp)$")
	if(swUnbuiltFolderRegex)
		string(APPEND swUnbuiltFolderRegex "|")
	endif()
	string(APPEND swUnbuiltFolderRegex "/Source/Engine/${swUnbuiltFolder}/")
	message(STATUS "[Engine] ${SW_TARGET_TYPE} target: Engine/${swUnbuiltFolder} is not built")
endforeach()

# 개발 전용 소스(기능 폴더의 단위 테스트 · 개발 명령)는 배포 구성이 짓지 않는다.
if(SW_SHIPPING_BUILD)
	sw_excludeUnbuiltSources(ENGINE_SOURCES ${SW_DEV_ONLY_SOURCE_PATTERNS})
endif()

# ------------------------------------------------------------------------------
# 1-1) 층 — 파일마다 층(Scripts/generate/rules/EngineLayers.toml → configure 가 EngineLayers.cmake 로)을 정하고, 층의 .cpp 는 아래 11) 의
#      층 부분(`EngineLayer<N>`)이 짓는다. Engine 타깃 자신은 헤더 · 리플렉션 산출물 · Core/Jolt 오브젝트만 든다.
# ------------------------------------------------------------------------------
include("${CMAKE_BINARY_DIR}/generated/sw/config/EngineLayers.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Scripts/generate/rules/EngineLayers.toml")
set(listEngineLayerSource "")
foreach(tier IN LISTS SW_ENGINE_TIER_LIST)
	# 이 구성이 짓는 것(ENGINE_SOURCES — 짓지 않는 폴더 · RHI 백엔드 · Jolt 를 뺀 것)만 남긴다.
	list(TRANSFORM SW_ENGINE_TIER_${tier}_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE listTierSource)
	set(listTierUnbuilt ${listTierSource})
	list(REMOVE_ITEM listTierUnbuilt ${ENGINE_SOURCES})
	if(listTierUnbuilt)
		list(REMOVE_ITEM listTierSource ${listTierUnbuilt})
	endif()
	set(swEngineTierSource_${tier} ${listTierSource})
	list(APPEND listEngineLayerSource ${listTierSource})
endforeach()
list(REMOVE_ITEM ENGINE_SOURCES ${listEngineLayerSource})

# ------------------------------------------------------------------------------
# 2) 압축 코덱(zlib · LZ4 · Zstd) — Engine 에만 붙인다. 찾기 · 진단 · 링크 항목은 `sw_findEngineCompressionLibraries`(cmake/Engine/ThirdPartyLibs.cmake).
# ------------------------------------------------------------------------------
sw_findEngineCompressionLibraries(listEngineCompressionLib)

# ------------------------------------------------------------------------------
# 3) Engine 타겟 — ABI 스탬프 · include 전파 · 정의 · PCH · Core OBJECT 재사용 · 내보내기
# Core 소스는 한 번만 컴파일. 동일 .obj를 Engine에 넣어
# Dev에서 App/Editor가 Engine.dll로부터 FileUtil/Logger를 dllimport
# ReflectionParser는 Core STATIC만 링크해 Engine.dll 순환을 피함
# ------------------------------------------------------------------------------
add_library(Engine ${coreLibType} ${ENGINE_SOURCES})

# 핫 리로드 ABI 스탬프 — Engine 이 자기 헤더 지문을 박는다(`Engine/Module/EngineABIStamp.cpp`). 모듈도 같은 값을 박는다.
sw_defineEngineABIStamp()
add_dependencies(Engine SwEngineABIStamp)
target_include_directories(Engine PRIVATE "${CMAKE_BINARY_DIR}/generated/engineabi")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Module/EngineABIStamp.cpp" PROPERTIES
	OBJECT_DEPENDS "${SW_ENGINE_ABI_STAMP_HEADER}"
)

# `Source/Engine` 자체는 include 경로가 아니다 — `#include "pch.h"` 만 진입 헤더로 푼다(소비하는 타깃 모두가 받는다).
sw_addPCHEntryDirectory(Engine PUBLIC engine "Engine/pch.h")
# 셰이더 바인딩 계약(binding_slots.hlsli)은 HLSL 과 C++ 가 같은 파일을 읽는다. ShaderBindingSlots.h 가 include 한다.
target_include_directories(Engine PUBLIC "${CMAKE_SOURCE_DIR}/Content/engine/shaders")
target_link_libraries(Engine
	PUBLIC
	sw_public_source_includes
	PRIVATE
	sw_global_options
	${listEngineCompressionLib}
)

target_compile_definitions(Engine PRIVATE "SW_LOG_TAG=\"Engine\"" SW_ENGINE_INTERNAL)
sw_configurePCH(Engine "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")

target_sources(Engine PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../sw.natvis"
)

# Core 의 OBJECT 들(높은 티어 `Core_objects` · 낮은 티어 `Core_low_objects`) — 목록은 Core 의 `SW_CORE_OBJECTS` 속성이다(Source/Core/CMakeLists.txt).
get_target_property(listCoreObjects Core SW_CORE_OBJECTS)
if(NOT listCoreObjects)
	message(FATAL_ERROR "[Engine] Core 의 SW_CORE_OBJECTS 속성이 비었다 — Source/Core 를 먼저 add_subdirectory 해야 한다")
endif()
foreach(coreObjects IN LISTS listCoreObjects)
	target_sources(Engine PRIVATE $<TARGET_OBJECTS:${coreObjects}>)
	add_dependencies(Engine ${coreObjects})
endforeach()

sw_configureDLLExports(Engine ${coreLibType} ENGINE)
sw_keepImportLibraryIfUnchanged(Engine)
set_target_properties(Engine PROPERTIES FOLDER "Source/Engine")

# ------------------------------------------------------------------------------
# 4) 서드파티 링크 — 서드파티 물리 · 애니메이션 · 프로파일러 · 내비 · 글꼴 라이브러리를 링크하는 곳은 이 파일 하나다.
# 헤더는 각 백엔드 폴더에서만 include 한다 — 다른 코드와 다른 타깃은 엔진 인터페이스만 본다.
# ThirdParty include/link 는 PRIVATE 로 둔다. Engine 밖으로 헤더가 노출되지 않게 해 C-ABI 격리를 유지한다.
# ------------------------------------------------------------------------------
# 애니메이션 압축(ACL · RTM, 헤더만). 헤더는 `Animation/Codec/ACL/` 에서만 include 한다 — 코덱 인터페이스(`AnimationCodec.h`) 뒤에 감춘다.
target_link_libraries(Engine PRIVATE acl)
# OGG Vorbis 디코드(stb_vorbis, 헤더만). `Audio/AudioVorbisDecode.cpp` 한 곳에서만 include 한다.
target_link_libraries(Engine PRIVATE stb)

if(TARGET sw_third_party_includes)
	target_link_libraries(Engine PRIVATE sw_third_party_includes)
endif()

# 물리 백엔드 — Jolt(3D) · Box2D(2D). 헤더는 Physics/Jolt · Physics/Box2D 에서만 include 한다.
# Jolt 임포트 타깃은 사용 요건으로 AVX2 같은 대상 기능(-mavx2 ...)과 JPH_* 정의를 싣는다. 그대로 링크하면 **Engine 의 모든 TU**
# 가 AVX2 로 컴파일된다. 링크만 받고($<LINK_ONLY:>), 정의 · 옵션은 Jolt 백엔드 소스에만 붙인다 — 그 소스는 Engine 이 아니라
# 자기 PCH 를 가진 OBJECT 라이브러리 `EngineJolt_objects` 가 짓는다(아래 9)).
# Box2D 는 BOX2D_DLL 정의 하나뿐이라 그대로 PRIVATE 로 링크한다. 둘 다 DLL 이고(x64-windows) Bin 에는 vcpkg applocal 이 옮긴다.
target_link_libraries(Engine PRIVATE $<LINK_ONLY:joltphysics> box2d)

# Tracy — 엔진 프로파일러의 두 번째 출력(Profiling). 헤더는 Profiling/Tracy 에서만 include 한다.
# Shipping 은 링크하지 않는다(SW_ENABLE_TRACY 를 무시). 클라이언트는 프로세스에 하나 — Engine 만 링크하고, RHI 백엔드 · 모듈은 엔진
# 인터페이스(IProfilerBackend)만 본다. GPU 구간은 엔진이 이미 모으는 타임스탬프를 넘기므로 백엔드 DLL 이 Tracy 를 알 필요가 없다.
# 임포트 타깃의 정의(TRACY_ENABLE · TRACY_IMPORTS …) · include 경로는 Tracy 구현 소스에만 붙인다(Jolt 와 같은 방식).
# 링크하지 않는다 — Windows 는 `-gv_tracy` 로 켤 때 TracyClient.dll 을 올려 C API 함수 표를 채운다(`TracyProfilerBackend::loadClient`). 리눅스는 링크한다.
if(SW_ENABLE_TRACY AND NOT SW_SHIPPING_BUILD AND TARGET Tracy::TracyClient)
	if(NOT WIN32)
		target_link_libraries(Engine PRIVATE $<LINK_ONLY:tracy>)
	endif()
	target_compile_definitions(Engine PRIVATE SW_PROFILER_TRACY)
	file(GLOB swTracyBackendSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Profiling/Tracy/*.cpp")
	# include 경로는 시스템 경로로 준다 — Tracy C API 의 `___tracy_*` 이름이 -Wreserved-identifier 경고를 낸다.
	get_target_property(swTracyIncludeDir Tracy::TracyClient INTERFACE_INCLUDE_DIRECTORIES)
	if(MSVC)
		set(swTracyIncludeOption "/external:I${swTracyIncludeDir}")
	else()
		set(swTracyIncludeOption "-isystem${swTracyIncludeDir}")
	endif()
	set_source_files_properties(${swTracyBackendSources} PROPERTIES
		COMPILE_DEFINITIONS "$<TARGET_PROPERTY:Tracy::TracyClient,INTERFACE_COMPILE_DEFINITIONS>"
		COMPILE_OPTIONS "${swTracyIncludeOption}"
		SKIP_PRECOMPILE_HEADERS ON
	)
	message(STATUS "[Engine] Tracy profiler client enabled (loaded on demand on Windows)")
endif()

# 내비메시 백엔드 — Recast & Detour(정적 라이브러리, 굽기 · 질의 · 군중). 헤더는 Navigation/Recast 에서만 include 한다(INavMesh · INavCrowd 뒤).
target_link_libraries(Engine PRIVATE recastnavigation)
# 글자 래스터화 — FreeType(x64-windows 는 freetype.dll, 리눅스는 정적). 헤더는 Text/FreeType 에서만 include 한다(Engine/Text/IFontRasterizer.h 뒤).
target_link_libraries(Engine PRIVATE freetype)
# 복합 문자 셰이핑 — HarfBuzz(ITextShaper 뒤) + SheenBidi(TextBidi 의 수준 뒤). 헤더는 Text/HarfBuzz · Text/SheenBidi 에서만 include 한다.
# 구현 소스는 늘 GLOB 에 들고 `SW_TEXT_COMPLEX_SHAPING` 이 없으면 빈 TU 다(Tracy 와 같은 방식). 켜면 두 패키지가 필수다(ThirdParty/harfbuzz · sheenbidi).
if(SW_ENABLE_COMPLEX_TEXT)
	target_link_libraries(Engine PRIVATE harfbuzz sheenbidi)
	# PUBLIC — 테스트가 이 빌드의 셰이퍼를 알고 기대값을 고른다(TestTextShaper).
	target_compile_definitions(Engine PUBLIC SW_TEXT_COMPLEX_SHAPING)
	message(STATUS "[Engine] Complex text shaping enabled (HarfBuzz + SheenBidi)")
endif()

# ------------------------------------------------------------------------------
# 5) RHI 링크 — Shipping 은 백엔드 하나를 Engine 에 정적 링크, Dev 는 RHI_* MODULE 을 런타임에 로드
# 전용 서버 배포본은 RHI 백엔드를 하나도 링크하지 않는다(기동 표의 RHI 단계가 서버에서 돌지 않는다) — 백엔드 소스는 모두 "이 구성이 짓지 않는다".
# Dev 서버는 RHI_* 모듈을 매니페스트(`_listTarget: ["Client"]`)가 끈다.
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD AND SW_TARGET_TYPE STREQUAL "Server")
	message(STATUS "[Engine] Dedicated server: no RHI backend linked")
	sw_declareUnbuiltSources(${listRHIBackendSource})
elseif(SW_SHIPPING_BUILD)
	# 이름 · 별칭은 쿠킹 표(`Config/Engine/CookContract.json` `rhi_backends`)가 풀고, 이 플랫폼에 없는 백엔드면 구성을 세운다(sw_resolveShippingRHIBackend).
	# 주의: 여기서 백엔드를 못 고른 채 지나가면 Engine 은 백엔드 없이 링크되고 실행에서야 "백엔드 없음" 으로 진다(실행하면 등록표가 "백엔드 없음 · 있는 것" 을 알린다).
	sw_resolveShippingRHIBackend("${SW_SHIPPING_RHI_BACKEND}" swShippingBackend)
	# EngineConfig 가 다른 백엔드를 고르면 알린다 — 배포본은 링크한 하나만 띄운다.
	# `_defaultRHI` 는 선택 키다 — 기본값과 다를 때만 적는다(정본은 쿠킹 표).
	sw_readOptionalJSONValue(swEngineConfigRHI "${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_ENGINE_CONFIG}" _window _defaultRHI)
	if(DEFINED swEngineConfigRHI AND NOT swEngineConfigRHI STREQUAL swShippingBackend)
		message(WARNING "${SW_FILE_RUNTIME_ENGINE_CONFIG} picks _defaultRHI='${swEngineConfigRHI}' but Shipping links only '${swShippingBackend}'")
	endif()

	message(STATUS "[Engine] Shipping monolithic RHI backend: ${swShippingBackend}")
	sw_getRHIBackendSources(${swShippingBackend} listShippingRHISource)
	sw_getRHIBackendEntrySource(${swShippingBackend} swShippingRHIEntry)
	target_sources(Engine PRIVATE ${listShippingRHISource} ${swShippingRHIEntry}) # 진입 파일이 백엔드를 등록표에 올린다(정적 등록자)
	target_link_libraries(Engine PUBLIC ${SW_RHI_BACKEND_${swShippingBackend}_GRAPHICS_LIBS})
	target_compile_definitions(Engine PUBLIC ${SW_RHI_BACKEND_${swShippingBackend}_SHIPPING_DEFINE})

	# 고르지 않은 백엔드는 이 구성에서 아무도 짓지 않는다 — 소스 검사 게이트가 알도록 적는다.
	set(listUnbuiltRHISource ${listRHIBackendSource})
	list(REMOVE_ITEM listUnbuiltRHISource ${listShippingRHISource})
	sw_declareUnbuiltSources(${listUnbuiltRHISource})
endif()

# ------------------------------------------------------------------------------
# 6) Windows 시스템 라이브러리 — Input(XInput) + 파일 대화상자 + 셰이더 컴파일러 유틸리티 + OS 난수(bcrypt, Core 의 SecureRandom). 지연 로드(/DELAYLOAD)는 쓰지 않는다:
#    없을 수 있는 DLL(Media Foundation — Windows N · XAudio2)은 쓰는 쪽이 LoadLibrary + 함수 표로 올리므로 import 라이브러리를 링크하지 않는다
#    (`XAudio2AudioBackend.cpp`). d3dcompiler_47 은 Windows 8.1 부터 System32 에 늘 있어 일반 import 다(DX 백엔드 모듈도 그렇다).
# ------------------------------------------------------------------------------
if(WIN32)
	target_link_libraries(Engine PRIVATE
		bcrypt.lib
		comdlg32.lib
		d3dcompiler.lib
		dbghelp.lib
		synchronization.lib
		mfuuid.lib
		ole32.lib
		oleaut32.lib
		shell32.lib
		uiautomationcore.lib
	)
endif()

# ------------------------------------------------------------------------------
# 7) Unity — 제외 목록이 **없다.** 그게 정상이고, 다시 만들지 말 것
#
# **유니티 제외 목록을 다시 만들지 말 것** — 목록은 TU 를 나눌 때마다 같이 고쳐야 하는 자리가 되고, 빠뜨려도 Ninja-* 프리셋은
# 유니티가 꺼져 있어 CI 에서만 터진다.
#
# "익명 네임스페이스가 합쳐져 이름이 부딪친다" 는 **이름 규칙으로 막혀 있다**. 익명 네임스페이스의 도우미는
# 클래스가 아니라 **TU 이름**을 딴다(`ShaderCookStampInternal` 등, docs/agents/Helpers.md). 이름 규칙이 그것을 정한다.
#
# 다시 넣고 싶어지면 먼저 `SW_ENABLE_UNITY_BUILD` 를 켠 빌드로 **무엇이 실제로 부딪치는지** 보고, 목록이 아니라 그 이름을 고칠 것.
#
# 매크로는 이름 규칙으로 못 막는다. X11 을 include 하는 .cpp(`Convex` · `None` … 매크로)는 리눅스에서 `sw_setUnityBuild` 가
# include 줄을 보고 저절로 묶음에서 뺀다 — 이것도 목록이 아니다(`cmake/Modules/Options/UnityBuild.cmake`).
# ------------------------------------------------------------------------------
sw_setUnityBuild(Engine BATCH_SIZE 12)

# 파괴 · 파쇄는 씨앗과 사건만 보내고 받는 쪽이 같은 계산을 한다 — 구성마다 같은 비트여야 한다(`sw_markDeterministicSources`,
# `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`). 화면용(컴포넌트 · 렌더 도우미)은 넣지 않는다.
sw_markDeterministicSources(Engine
	Destruction/DestructionDamage.cpp
	Destruction/DestructionProfile.cpp
	Destruction/DestructionState.cpp
	Destruction/Fracture/FractureAsset.cpp
	Destruction/Fracture/FractureGraph.cpp
	Destruction/Fracture/PolygonFracture.cpp
	Destruction/Fracture/PolygonTriangulation.cpp
)
# 물리(Jolt · Box2D 감싸개, 강체 · 천 · 래그돌 컴포넌트)도 같다 — 시뮬레이션에 넣는 값이 구성마다 끝 비트가 달라지면 천 · 래그돌이 갈린다
# (cloth 쇼케이스가 Debug 와 Release 에서 2 % 달랐다). Jolt 자체는 결정적 빌드다(`ThirdParty/joltphysics/vcpkg-port`). 폴더째라 새 파일도 든다.
file(GLOB_RECURSE swDeterministicPhysicsSources RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Physics/*.cpp")
list(FILTER swDeterministicPhysicsSources EXCLUDE REGEX "/Tests/")
sw_markDeterministicSources(Engine ${swDeterministicPhysicsSources})

# ------------------------------------------------------------------------------
# 8) 리플렉션 코드젠 — ReflectionParser 필요 (루트가 Tools 이후 이 파일을 호출)
# ------------------------------------------------------------------------------
if(swUnbuiltFolderRegex)
	# 짓지 않는 폴더의 리플렉션 타입(렌더 패스 · 파이프라인 에셋)은 코드젠도 하지 않는다 — 그 구현이 빠졌다.
	sw_addReflectionStep(Engine
		MODULE Engine
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		EXCLUDE_REGEX "${swUnbuiltFolderRegex}"
	)
else()
	sw_addReflectionStep(Engine
		MODULE Engine
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
	)
endif()
sw_addReflectBuiltinsGen(Engine)

# ------------------------------------------------------------------------------
# 9) Jolt 백엔드 소스 — 자기 PCH 를 가진 OBJECT 라이브러리(목적 파일은 Core_objects 처럼 Engine 에 들어간다)
#
# Jolt 백엔드 소스는 Jolt 의 대상 기능(-mavx2 ...)으로 컴파일되므로 Engine PCH 를 쓸 수 없다(대상 기능이 다른 PCH 는 clang 이 거부한다).
# PCH 없이 지으면 TU 마다 엔진 `pch.h` 를 다시 파싱한다 — 그 파싱이 TU 시간의 절반이다. 그래서 이 소스만 따로 묶어 `pch.h` + Jolt
# 래퍼 헤더로 PCH 를 하나 만든다. 컴파일 설정은 Engine 의 것을 그대로 옮기고 Jolt 의 정의 ·
# 옵션만 더한다. 그래서 이 블록은 Engine 설정이 모두 끝난 뒤(파일 끝)에 둔다 — 이 뒤에 Engine 에 더한 설정은 Jolt 소스에 가지 않는다.
# 유니티 묶음에는 넣지 않는다. 코드젠 산출물(`FlagOps.gen.h` 등)을 읽으므로 Engine 이 기다리는 대상을 같이 기다린다.
# ------------------------------------------------------------------------------
file(GLOB swJoltBackendSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Physics/Jolt/*.cpp")
add_library(EngineJolt_objects OBJECT ${swJoltBackendSources})
set_target_properties(EngineJolt_objects PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	FOLDER "Source/Engine"
)
# Engine 에 직접 적은 값과 링크 목록을 그대로 옮긴다 — 링크 목록이 인터페이스 라이브러리의 include(SYSTEM 표시 포함) · 정의 · 옵션을 다시 실어 온다.
# ($<TARGET_PROPERTY:Engine,INCLUDE_DIRECTORIES> 로 한 번에 읽으면 서드파티 경로의 SYSTEM 표시가 빠져 그 헤더의 경고가 나온다.)
foreach(swProperty IN ITEMS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES COMPILE_OPTIONS LINK_LIBRARIES)
	get_target_property(swEngineValue Engine ${swProperty})
	if(swEngineValue)
		set_property(TARGET EngineJolt_objects APPEND PROPERTY ${swProperty} ${swEngineValue})
	endif()
endforeach()
target_compile_definitions(EngineJolt_objects PRIVATE $<TARGET_PROPERTY:joltphysics,INTERFACE_COMPILE_DEFINITIONS>)
target_compile_options(EngineJolt_objects PRIVATE $<TARGET_PROPERTY:joltphysics,INTERFACE_COMPILE_OPTIONS>)
if(SW_ENABLE_PCH)
	target_precompile_headers(EngineJolt_objects PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/pch.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Physics/Jolt/JoltJobSystem.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Physics/Jolt/JoltPhysicsScene.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Physics/Jolt/JoltUtil.h"
	)
endif()
get_target_property(swEngineDependency Engine MANUALLY_ADDED_DEPENDENCIES)
if(swEngineDependency)
	add_dependencies(EngineJolt_objects ${swEngineDependency})
endif()
target_sources(Engine PRIVATE $<TARGET_OBJECTS:EngineJolt_objects>)
add_dependencies(Engine EngineJolt_objects)

# ------------------------------------------------------------------------------
# 11) 층 부분 — 층마다 OBJECT 라이브러리 `EngineLayer<N>`(오브젝트는 Engine 이미지 안, `sw_addModulePart`). 부분 N 은 N 보다 위 층의 헤더마다 같은 경로의
#     `#error` 껍데기를 자기 include 자리 맨 앞에 둔다 — 아래 층이 위 층 헤더를 include 하면(전이 include 도) 컴파일이 멈춘다. 에디터 층과 같은 방식이다.
#     Engine 설정이 모두 끝난 뒤(Jolt 블록과 같은 자리)에 둔다 — 부분은 Engine 의 정의 · 옵션 · include · 링크 목록을 구성 때 복사한다.
# ------------------------------------------------------------------------------
function(sw_addEngineLayerStubs PART TIER)
	set(stubRoot "${CMAKE_BINARY_DIR}/generated/sw/EngineLayerStubs/tier${TIER}")
	set(listNewStub "")
	foreach(upperTier IN LISTS SW_ENGINE_TIER_LIST)
		if(NOT upperTier GREATER TIER)
			continue()
		endif()
		set(stubContent "#error \"Engine layer ${upperTier} header cannot be included from layer ${TIER} code (Scripts/generate/rules/EngineLayers.toml). A lower layer must not see an upper layer: move what you need down, or invert the dependency.\"
")
		foreach(header IN LISTS SW_ENGINE_TIER_${upperTier}_HEADERS)
			set(stubPath "${stubRoot}/${header}")
			set(oldContent "")
			if(EXISTS "${stubPath}")
				file(READ "${stubPath}" oldContent)
			endif()
			if(NOT oldContent STREQUAL stubContent)
				file(WRITE "${stubPath}" "${stubContent}")
			endif()
			list(APPEND listNewStub "${stubPath}")
		endforeach()
	endforeach()
	set(manifestPath "${stubRoot}.manifest")
	if(EXISTS "${manifestPath}")
		file(STRINGS "${manifestPath}" listOldStub)
		if(listNewStub)
			list(REMOVE_ITEM listOldStub ${listNewStub})
		endif()
		foreach(oldStub IN LISTS listOldStub)
			file(REMOVE "${oldStub}")
		endforeach()
	endif()
	string(REPLACE ";" "
" manifestContent "${listNewStub}")
	file(WRITE "${manifestPath}" "${manifestContent}
")
	target_include_directories(${PART} BEFORE PRIVATE "${stubRoot}")
endfunction()

# 링크 목록은 부분을 만들기 전에 한 번 읽는다 — `sw_addModulePart` 가 부분을 Engine 에 링크하므로 뒤에 읽으면 앞 부분이 섞인다.
get_target_property(swEngineLinkLibraries Engine LINK_LIBRARIES)
get_target_property(swEngineDependency Engine MANUALLY_ADDED_DEPENDENCIES)
foreach(tier IN LISTS SW_ENGINE_TIER_LIST)
	if(NOT swEngineTierSource_${tier})
		continue()   # 이 구성이 짓지 않는 층(전용 서버의 렌더러)
	endif()
	sw_addModulePart(EngineLayer${tier}
		OWNER Engine
		SOURCES ${swEngineTierSource_${tier}}
		LINK ${swEngineLinkLibraries}
	)
	sw_addEngineLayerStubs(EngineLayer${tier} ${tier})
	if(swEngineDependency)
		add_dependencies(EngineLayer${tier} ${swEngineDependency})
	endif()
endforeach()

# ------------------------------------------------------------------------------
# 10) RHI 백엔드 MODULE 플러그인 (DX는 Windows 전용, GL/Vulkan은 전 플랫폼) — Dev 만 짓는다. 런타임 로드라 링크하지 않는다.
# 백엔드 폴더 하나가 장치 소스와 모듈 엔트리(`<백엔드>/Module/` — CMakeLists · ModuleEntry · 매니페스트)를 함께 든다.
# Shipping 은 모듈 엔트리를 짓지 않는다 — 소스 검사 게이트에 적는다.
# ------------------------------------------------------------------------------
foreach(backendName IN LISTS SW_RHI_BACKEND_NAMES)
	set(swBackendModuleDir "Graphics/RHI/${SW_RHI_BACKEND_${backendName}_SOURCE_FOLDER}/Module")
	if(SW_SHIPPING_BUILD)
		sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}/${swBackendModuleDir}")
	else()
		add_subdirectory(${swBackendModuleDir})
	endif()
endforeach()
