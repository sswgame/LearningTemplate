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
# RHI MODULE 엔트리(백엔드 폴더의 Module/)와 백엔드 장치 .cpp 는 항상 Engine GLOB 에서 뺀다(장치 소스는 cmake/Modules/Build/RHIBackends.cmake 가 백엔드 폴더로 안다).
# Dev 는 RHI_* MODULE 이, Shipping 은 아래 5) 가 고른 백엔드 하나를 같은 함수로 가져다 짓는다.
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD)
	set(core_lib_type STATIC)
else()
	set(core_lib_type SHARED)
endif()

set(core_exclude
	"Graphics/RHI/.+/Module/"   # 백엔드 모듈 엔트리(RHI_* MODULE 이 짓는다)
	"Physics/Jolt/[^/]+\\.cpp$"   # EngineJolt_objects 가 짓는다(아래 9))
)

sw_glob_source_tree(ENGINE_SOURCES RECURSE "*.cpp" "*.c" "*.h" "*.hpp")

foreach(ex_pattern IN LISTS core_exclude)
	list(FILTER ENGINE_SOURCES EXCLUDE REGEX "${ex_pattern}")
endforeach()

# 플랫폼 폴더(`Windows/` · `Linux/` · `Android/` · `Web/`)는 그 플랫폼에서만 짓는다(Core · DevTools 와 같은 규칙) — 파일 안 플랫폼 `#if` 로 빈 TU 를
# 모든 플랫폼에서 컴파일하지 않는다(언리얼 UBT 가 `Windows/` · `Linux/` 폴더를 플랫폼마다 고르는 것과 같다). 전용 서버의 차이도 폴더가 맡는다 —
# 서버가 짓지 않는 폴더(`SW_EXCLUDED_ENGINE_FOLDERS` — 창 · 입력의 `Linux/` 포함)와 서버만 짓는 `Headless/`(아래).
sw_filter_platform_sources(ENGINE_SOURCES)

# RHI 백엔드 장치 소스는 Engine 이 짓지 않는다 — Dev 는 RHI_* 모듈이, Shipping 은 아래 5) 가 고른 하나를 가져간다.
sw_get_all_rhi_backend_sources(list_rhi_backend_source)
list(REMOVE_ITEM ENGINE_SOURCES ${list_rhi_backend_source})

# 자동화 실행기(실행기 · 세션 · 판정 · 보고 · 시나리오 녹화)는 엔진에 없다 — 개발 도구 `DevTools/Client/Automation` 이 짓는다(배포 · 전용 서버는 짓지 않는다).
# 엔진에 남는 것은 런타임 코드가 선언하는 얇은 창구다: 감시 값(`Watch/`), 단계 · 차단 조건 · 단계 초기화 등록과 시나리오 형식(`Scenario/`), 실행기 창구 `IAutomationRuntime`.
# 등록 매크로는 `SW_SHIPPING` 에서 빈 선언이 된다(`AutomationStepRegistry.h` · `AutomationWatch.h` · `AutomationStageReset.h`).

# 이 타깃이 짓지 않는 폴더(전용 서버의 `Renderer/` — 목록은 cmake/Modules/Build/BuildLayout.cmake 의 `SW_EXCLUDED_ENGINE_FOLDERS`).
# 그 폴더의 헤더는 타깃 게이트 `serverbuild` 가 막는다(서버는 `IEngineRenderDriver` 만 본다). 리플렉션 헤더도 아래 8) 에서 뺀다.
set(excluded_folder_regex "")
foreach(excluded_folder IN LISTS SW_EXCLUDED_ENGINE_FOLDERS)
	sw_remove_excluded_sources(ENGINE_SOURCES "/Source/Engine/${excluded_folder}/.*\\.(c|cpp)$")
	if(excluded_folder_regex)
		string(APPEND excluded_folder_regex "|")
	endif()
	string(APPEND excluded_folder_regex "/Source/Engine/${excluded_folder}/")
	message(STATUS "[Engine] ${SW_TARGET_TYPE} target: Engine/${excluded_folder} is not built")
endforeach()

# 오디오를 짓는 구성(클라이언트 · 에디터)은 플랫폼 장치 백엔드(`Audio/<플랫폼>/*AudioBackend.cpp`)를 정확히 하나 담는다 — 없으면 기동에서 `IAudioService::create` 가
# 치명 오류로 섰다(감사 P0-11). 구성에서 멈춰 빌드 오류로 옮긴다. 언리얼 AudioMixer 는 플랫폼마다 백엔드 모듈이 있고 없는 플랫폼은 패키징 대상이 아니다.
if(NOT "Audio" IN_LIST SW_EXCLUDED_ENGINE_FOLDERS)
	set(list_audio_backend_source ${ENGINE_SOURCES})
	list(FILTER list_audio_backend_source INCLUDE REGEX "/Source/Engine/Audio/[^/]+/[^/]+AudioBackend\\.cpp$")
	list(LENGTH list_audio_backend_source audio_backend_count)
	if(NOT audio_backend_count EQUAL 1)
		message(FATAL_ERROR "[Engine] ${CMAKE_SYSTEM_NAME} ${SW_TARGET_TYPE} target builds ${audio_backend_count} audio device backends (need exactly one): ${list_audio_backend_source}\n"
			"Add Source/Engine/Audio/<Platform>/<Name>AudioBackend.cpp for this platform (Windows XAudio2, Linux ALSA, Android AAudio, Web AudioWorklet exist).")
	endif()
endif()

# 창 없는 서버의 창 · 입력 · 클립보드(`Window/Headless` · `Input/Headless`)는 서버만 짓는다 — 클라이언트는 플랫폼 폴더(`Window/Linux` …)가 같은 함수를 정의한다.
# 구성으로 파일 안을 가르지 않고(그런 매크로는 없다 — `BuildInfo.h`) 폴더째 고른다(언리얼 `UE_SERVER` 대신 모듈 · 폴더를 빼는 쪽과 같다).
if(NOT SW_TARGET_TYPE STREQUAL "Server")
	sw_remove_excluded_sources(ENGINE_SOURCES "/Source/Engine/(Window|Input)/Headless/.*\\.(c|cpp)$")
endif()
# 서버 전용 소스(`Server` — 운영 HTTP 끝점 · 관리 채널 · 서버 서비스 레지스트리, `Config/Server` — 서버 비밀)는 클라이언트 구성이 짓지 않는다(`SW_SERVER_ONLY_SOURCE_PATTERNS`, 헤더는 게이트 `clientbuild`).
if(SW_TARGET_TYPE STREQUAL "Client")
	sw_remove_excluded_sources(ENGINE_SOURCES ${SW_SERVER_ONLY_SOURCE_PATTERNS})
endif()

# 개발 전용 소스(기능 폴더의 단위 테스트 · 개발 명령)는 배포 구성이 짓지 않는다.
if(SW_SHIPPING_BUILD)
	sw_remove_excluded_sources(ENGINE_SOURCES ${SW_DEV_ONLY_SOURCE_PATTERNS})
endif()

# 개발 · 배포 차이가 값뿐인 곳은 `build::kShipping`(Core/Foundation/BuildInfo.h)을 읽는다 — 규칙은 docs/agents/BuildConfigSwitch.md.

# ------------------------------------------------------------------------------
# 1-1) 층 — 파일마다 층(Config/Rules/Layers/EngineLayers.toml → configure 가 EngineLayers.cmake 로)을 정하고, 층의 .cpp 는 아래 11) 의
#      층 부분(`EngineLayer<N>`)이 짓는다. Engine 타깃 자신은 헤더 · 리플렉션 산출물 · Core/Jolt 오브젝트만 든다.
# ------------------------------------------------------------------------------
include("${CMAKE_BINARY_DIR}/generated/sw/config/EngineLayers.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Config/Rules/Layers/EngineLayers.toml")
set(list_engine_layer_source "")
foreach(tier IN LISTS SW_ENGINE_TIER_LIST)
	# 이 구성이 짓는 것(ENGINE_SOURCES — 짓지 않는 폴더 · RHI 백엔드 · Jolt 를 뺀 것)만 남긴다.
	list(TRANSFORM SW_ENGINE_TIER_${tier}_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE list_tier_source)
	set(list_tier_excluded ${list_tier_source})
	list(REMOVE_ITEM list_tier_excluded ${ENGINE_SOURCES})
	if(list_tier_excluded)
		list(REMOVE_ITEM list_tier_source ${list_tier_excluded})
	endif()
	set(engine_tier_source_${tier} ${list_tier_source})
	list(APPEND list_engine_layer_source ${list_tier_source})
endforeach()
list(REMOVE_ITEM ENGINE_SOURCES ${list_engine_layer_source})

# ------------------------------------------------------------------------------
# 2) 압축 코덱(zlib · LZ4 · Zstd) — Engine 에만 붙인다. 찾기 · 진단 · 링크 항목은 `sw_find_engine_compression_libraries`(cmake/Modules/Build/ThirdPartyLibs.cmake).
# ------------------------------------------------------------------------------
sw_find_engine_compression_libraries(list_engine_compression_lib)

# ------------------------------------------------------------------------------
# 3) Engine 타겟 — ABI 스탬프 · include 전파 · 정의 · PCH · Core OBJECT 재사용 · 내보내기
# Core 소스는 한 번만 컴파일. 동일 .obj를 Engine에 넣어
# Dev에서 App/Editor가 Engine.dll로부터 FileUtil/Logger를 dllimport
# ReflectionParser는 Core STATIC만 링크해 Engine.dll 순환을 피함
# ------------------------------------------------------------------------------
add_library(Engine ${core_lib_type} ${ENGINE_SOURCES})
sw_apply_target_layer_guard(Engine engine) # 타깃 경계 — Engine 은 GameFramework · Editor · 호스트를 보지 못한다. 층 부분 · Jolt 오브젝트는 이 include 자리를 복사한다
sw_require_template_definitions(Engine) # `.inl` 누락을 링크가 아니라 컴파일에서 — 층 부분 · Jolt 오브젝트도 이 옵션을 복사한다

# 핫 리로드 ABI 스탬프 — Engine 이 자기 헤더 지문을 박는다(`Engine/Module/EngineABIStamp.cpp`). 모듈도 같은 값을 박는다.
sw_define_engine_abi_stamp()
add_dependencies(Engine SwEngineABIStamp)
target_include_directories(Engine PRIVATE "${CMAKE_BINARY_DIR}/generated/engineabi")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Module/EngineABIStamp.cpp" PROPERTIES
	OBJECT_DEPENDS "${SW_ENGINE_ABI_STAMP_HEADER}"
)

# 쿠커 코드 지문 — 쿠킹 재사용 판단의 입력을 소스 내용에서 산출한다(`Engine/Content/CookCodeFingerprint.h`). 엔진 소스 어디가 바뀌어도 돌지만 결과가 같으면 파일을 다시 쓰지 않는다.
sw_glob_source_tree(cook_code_inputs RECURSE
	"${CMAKE_SOURCE_DIR}/Source/Engine/*.h"
	"${CMAKE_SOURCE_DIR}/Source/Engine/*.hpp"
	"${CMAKE_SOURCE_DIR}/Source/Engine/*.cpp"
	"${CMAKE_SOURCE_DIR}/Source/Engine/*.inl"
	"${CMAKE_SOURCE_DIR}/Source/Engine/*.xxx"
)
set(cook_code_header "${CMAKE_BINARY_DIR}/generated/engineabi/CookCodeFingerprint.gen.h")
# 쿠커 도구 판은 쿠킹하는 쪽의 vcpkg 기록이다 — 교차 빌드는 호스트 빌드의 것(HostTools.cmake), 아니면 이 빌드의 것.
if(CMAKE_CROSSCOMPILING)
	set(cook_vcpkg_status "${SW_HOST_VCPKG_STATUS}")
else()
	set(cook_vcpkg_status "${VCPKG_INSTALLED_DIR}/vcpkg/status")
endif()
add_custom_command(
	OUTPUT "${cook_code_header}"
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/cook_code_fingerprint.py"
		--root "${CMAKE_SOURCE_DIR}" --inputs "${CMAKE_SOURCE_DIR}/Config/Dev/CookCodeInputs.json"
		--vcpkg-status "${cook_vcpkg_status}" --out "${cook_code_header}"
		--cache "${CMAKE_BINARY_DIR}/generated/engineabi/CookCodeFingerprint.cache.json" # 파일 지문 캐시 — 바뀐 소스만 다시 읽는다
	DEPENDS ${cook_code_inputs} "${CMAKE_SOURCE_DIR}/Config/Dev/CookCodeInputs.json" "${cook_vcpkg_status}"
		"${CMAKE_SOURCE_DIR}/Scripts/generate/cook_code_fingerprint.py"
	COMMENT "Fingerprinting cooker code"
	VERBATIM
)
add_custom_target(SwCookCodeFingerprint DEPENDS "${cook_code_header}")
set_target_properties(SwCookCodeFingerprint PROPERTIES FOLDER "CMakePredefinedTargets")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Content/CookCodeFingerprint.cpp" PROPERTIES OBJECT_DEPENDS "${cook_code_header}")

# `Source/Engine` 자체는 include 경로가 아니다 — `#include "pch.h"` 만 진입 헤더로 푼다(소비하는 타깃 모두가 받는다).
sw_add_pch_entry_directory(Engine PUBLIC engine "Engine/pch.h")
# 셰이더 바인딩 계약(binding_slots.hlsli)은 HLSL 과 C++ 가 같은 파일을 읽는다. ShaderBindingSlots.h 가 include 한다.
target_include_directories(Engine PUBLIC "${CMAKE_SOURCE_DIR}/Content/engine/shaders")
target_link_libraries(Engine
	PUBLIC
	sw_public_source_includes
	PRIVATE
	sw_global_options
	${list_engine_compression_lib}
)

target_compile_definitions(Engine PRIVATE "SW_LOG_TAG=\"Engine\"" "SW_REGISTRAR_MODULE=\"Engine\"" SW_ENGINE_INTERNAL)
sw_configure_pch(Engine "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")

target_sources(Engine PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../sw.natvis"
)

# Core 의 OBJECT 들(티어 부분 `Core_tier<N>_objects`) — 목록은 Core 의 `SW_CORE_OBJECTS` 속성이다(Source/Core/CMakeLists.txt).
get_target_property(list_core_objects Core SW_CORE_OBJECTS)
if(NOT list_core_objects)
	message(FATAL_ERROR "[Engine] Core 의 SW_CORE_OBJECTS 속성이 비었다 — Source/Core 를 먼저 add_subdirectory 해야 한다")
endif()
foreach(core_objects IN LISTS list_core_objects)
	target_sources(Engine PRIVATE $<TARGET_OBJECTS:${core_objects}>)
	add_dependencies(Engine ${core_objects})
endforeach()

sw_configure_dll_exports(Engine ${core_lib_type} ENGINE)
sw_keep_import_library_if_unchanged(Engine)
set_target_properties(Engine PROPERTIES FOLDER "Source/Engine")

# ------------------------------------------------------------------------------
# 4) 서드파티 링크 — 서드파티 물리 · 애니메이션 · 프로파일러 · 내비 · 글꼴 라이브러리를 링크하는 곳은 이 파일 하나다.
# 헤더는 각 백엔드 폴더에서만 include 한다 — 다른 코드와 다른 타깃은 엔진 인터페이스만 본다.
# ThirdParty include/link 는 PRIVATE 로 둔다. Engine 밖으로 헤더가 노출되지 않게 해 C-ABI 격리를 유지한다.
# ------------------------------------------------------------------------------
# 애니메이션 압축(ACL · RTM, 헤더만). 헤더는 `Animation/Codec/ACL/` 에서만 include 한다 — 코덱 인터페이스(`AnimationCodec.h`) 뒤에 감춘다.
target_link_libraries(Engine PRIVATE acl)
# OGG Vorbis 디코드(stb_vorbis, 헤더만). `Audio/AudioVorbisDecoder.cpp` 한 곳에서만 include 한다.
target_link_libraries(Engine PRIVATE stb)
# 메시 단순화(meshoptimizer). 실행 중에 조립한 캐릭터 메시의 LOD 를 만든다 — `Character/Fit/CharacterLODSettings.cpp` 한 곳에서만 include 한다(에디터 임포터도 같은 라이브러리).
target_link_libraries(Engine PRIVATE meshoptimizer)

if(TARGET sw_third_party_includes)
	target_link_libraries(Engine PRIVATE sw_third_party_includes)
endif()

# 물리 백엔드 — Jolt(3D) · Box2D(2D). 헤더는 Physics/Jolt · Physics/Box2D 에서만 include 한다.
# Jolt 임포트 타깃은 사용 요건으로 AVX2 같은 대상 기능(-mavx2 ...)과 JPH_* 정의를 싣는다. 그대로 링크하면 **Engine 의 모든 TU**
# 가 AVX2 로 컴파일된다. 링크만 받고($<LINK_ONLY:>), 정의 · 옵션은 Jolt 백엔드 소스에만 붙인다 — 그 소스는 Engine 이 아니라
# 자기 PCH 를 가진 OBJECT 라이브러리 `EngineJolt_objects` 가 짓는다(아래 9)).
# Box2D 는 BOX2D_DLL 정의 하나뿐이라 그대로 PRIVATE 로 링크한다. 둘 다 DLL 이고(x64-windows) Bin 에는 vcpkg applocal 이 옮긴다.
target_link_libraries(Engine PRIVATE $<LINK_ONLY:joltphysics> box2d)

# Tracy 는 개발 도구(DevTools.dll)가 링크한다 — 엔진은 출력 인터페이스(`IProfilerBackend`)만 안다(Source/DevTools/CMakeLists.txt).

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
	sw_declare_excluded_sources(${list_rhi_backend_source})
elseif(SW_SHIPPING_BUILD)
	# 이름 · 별칭은 쿠킹 표(`Config/Engine/CookContract.json` `rhi_backends`)가 풀고, 이 플랫폼에 없는 백엔드면 구성을 세운다(sw_resolve_shipping_rhi_backend).
	# 주의: 여기서 백엔드를 못 고른 채 지나가면 Engine 은 백엔드 없이 링크되고 실행에서야 "백엔드 없음" 으로 진다(실행하면 등록표가 "백엔드 없음 · 있는 것" 을 알린다).
	sw_resolve_shipping_rhi_backend("${SW_SHIPPING_RHI_BACKEND}" shipping_backend)
	# EngineConfig 가 다른 백엔드를 고르면 알린다 — 배포본은 링크한 하나만 띄운다.
	# `_defaultRHI` 는 선택 키다 — 기본값과 다를 때만 적는다(정본은 쿠킹 표).
	sw_read_optional_json_value(engine_config_rhi "${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_ENGINE_CONFIG}" _window _defaultRHI)
	if(DEFINED engine_config_rhi AND NOT engine_config_rhi STREQUAL shipping_backend)
		message(WARNING "${SW_FILE_RUNTIME_ENGINE_CONFIG} picks _defaultRHI='${engine_config_rhi}' but Shipping links only '${shipping_backend}'")
	endif()

	message(STATUS "[Engine] Shipping monolithic RHI backend: ${shipping_backend}")
	sw_get_rhi_backend_sources(${shipping_backend} list_shipping_rhi_source)
	sw_get_rhi_backend_entry_source(${shipping_backend} shipping_rhi_entry)
	target_sources(Engine PRIVATE ${list_shipping_rhi_source} ${shipping_rhi_entry}) # 진입 파일이 백엔드를 등록표에 올린다(정적 등록자)
	target_link_libraries(Engine PUBLIC ${SW_RHI_BACKEND_${shipping_backend}_GRAPHICS_LIBS})
	target_compile_definitions(Engine PUBLIC ${SW_RHI_BACKEND_${shipping_backend}_SHIPPING_DEFINE})

	# 고르지 않은 백엔드는 이 구성에서 아무도 짓지 않는다 — 소스 검사 게이트가 알도록 적는다.
	set(list_excluded_rhi_source ${list_rhi_backend_source})
	list(REMOVE_ITEM list_excluded_rhi_source ${list_shipping_rhi_source})
	sw_declare_excluded_sources(${list_excluded_rhi_source})
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
# 매크로는 이름 규칙으로 못 막는다. X11 을 include 하는 .cpp(`Convex` · `None` … 매크로)는 리눅스에서 `sw_set_unity_build` 가
# include 줄을 보고 저절로 묶음에서 뺀다 — 이것도 목록이 아니다(`cmake/Toolchain/Options/UnityBuild.cmake`).
# ------------------------------------------------------------------------------
sw_set_unity_build(Engine)

# 파괴 · 파쇄는 씨앗과 사건만 보내고 받는 쪽이 같은 계산을 한다 — 구성마다 같은 비트여야 한다(`sw_mark_deterministic_sources`,
# `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`). 화면용(컴포넌트 · 렌더 도우미)은 넣지 않는다.
sw_mark_deterministic_sources(Engine
	Destruction/DestructionDamage.cpp
	Destruction/DestructionProfile.cpp
	Destruction/DestructionState.cpp
	Destruction/Fracture/FractureAsset.cpp
	Destruction/Fracture/FractureGraph.cpp
	Destruction/Fracture/PolygonFracture.cpp
)
# 물리(Jolt · Box2D 감싸개, 강체 · 천 · 래그돌 컴포넌트)도 같다 — 시뮬레이션에 넣는 값이 구성마다 끝 비트가 달라지면 천 · 래그돌이 갈린다
# (cloth 쇼케이스가 Debug 와 Release 에서 2 % 달랐다). Jolt 자체는 결정적 빌드다(`ThirdParty/joltphysics/vcpkg-port`). 폴더째라 새 파일도 든다.
sw_glob_source_tree(deterministic_physics_sources RECURSE RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/Physics/*.cpp")
list(FILTER deterministic_physics_sources EXCLUDE REGEX "/Tests/")
# 부력 부피(Environment/Water/BuoyancyVolumeComponent — 물 기능이 자기 컴포넌트를 들게 Physics 에서 옮겼다)도 물리에 넣는 값이라 같은 표시를 건다.
sw_mark_deterministic_sources(Engine ${deterministic_physics_sources} Environment/Water/BuoyancyVolumeComponent.cpp)

# ------------------------------------------------------------------------------
# 8) 리플렉션 코드젠 — ReflectionParser 필요 (루트가 Tools 이후 이 파일을 호출)
# ------------------------------------------------------------------------------
# 짓지 않는 폴더의 리플렉션 타입(렌더 패스 · 파이프라인 에셋)은 코드젠도 하지 않는다 — 그 구현이 빠졌다.
sw_add_reflection_step(Engine
	MODULE Engine
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
	EXCLUDE_REGEX "${excluded_folder_regex}"
)
sw_add_reflect_builtins_gen(Engine)

# ------------------------------------------------------------------------------
# 9) Jolt 백엔드 소스 — 자기 PCH 를 가진 OBJECT 라이브러리(목적 파일은 Core 티어 부분처럼 Engine 에 들어간다)
#
# Jolt 백엔드 소스는 Jolt 의 대상 기능(-mavx2 ...)으로 컴파일되므로 Engine PCH 를 쓸 수 없다(대상 기능이 다른 PCH 는 clang 이 거부한다).
# PCH 없이 지으면 TU 마다 엔진 `pch.h` 를 다시 파싱한다 — 그 파싱이 TU 시간의 절반이다. 그래서 이 소스만 따로 묶어 `pch.h` + Jolt
# 래퍼 헤더로 PCH 를 하나 만든다. 컴파일 설정은 Engine 의 것을 그대로 옮기고 Jolt 의 정의 ·
# 옵션만 더한다. 그래서 이 블록은 Engine 설정이 모두 끝난 뒤(파일 끝)에 둔다 — 이 뒤에 Engine 에 더한 설정은 Jolt 소스에 가지 않는다.
# 유니티 묶음에는 넣지 않는다. 코드젠 산출물(`FlagOps.gen.h` 등)을 읽으므로 Engine 이 기다리는 대상을 같이 기다린다.
# ------------------------------------------------------------------------------
sw_glob_source_tree(jolt_backend_sources "${CMAKE_CURRENT_SOURCE_DIR}/Physics/Jolt/*.cpp")
add_library(EngineJolt_objects OBJECT ${jolt_backend_sources})
set_target_properties(EngineJolt_objects PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	FOLDER "Source/Engine"
)
# Engine 에 직접 적은 값과 링크 목록을 그대로 옮긴다 — 링크 목록이 인터페이스 라이브러리의 include(SYSTEM 표시 포함) · 정의 · 옵션을 다시 실어 온다.
# ($<TARGET_PROPERTY:Engine,INCLUDE_DIRECTORIES> 로 한 번에 읽으면 서드파티 경로의 SYSTEM 표시가 빠져 그 헤더의 경고가 나온다.)
foreach(property IN ITEMS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES COMPILE_OPTIONS LINK_LIBRARIES)
	get_target_property(engine_value Engine ${property})
	if(engine_value)
		set_property(TARGET EngineJolt_objects APPEND PROPERTY ${property} ${engine_value})
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
get_target_property(engine_dependency Engine MANUALLY_ADDED_DEPENDENCIES)
if(engine_dependency)
	add_dependencies(EngineJolt_objects ${engine_dependency})
endif()
target_sources(Engine PRIVATE $<TARGET_OBJECTS:EngineJolt_objects>)
add_dependencies(Engine EngineJolt_objects)

# ------------------------------------------------------------------------------
# 10) 층 부분 — 층마다 OBJECT 라이브러리 `EngineLayer<N>`(오브젝트는 Engine 이미지 안, `sw_add_module_part`). 부분 N 은 N 보다 위 층의 헤더마다 같은 경로의
#     `#error` 껍데기를 자기 include 자리 맨 앞에 둔다 — 아래 층이 위 층 헤더를 include 하면(전이 include 도) 컴파일이 멈춘다. 에디터 층과 같은 방식이다.
#     Engine 설정이 모두 끝난 뒤(Jolt 블록과 같은 자리)에 둔다 — 부분은 Engine 의 정의 · 옵션 · include · 링크 목록을 구성 때 복사한다.
# ------------------------------------------------------------------------------
# 링크 목록은 부분을 만들기 전에 한 번 읽는다 — `sw_add_module_part` 가 부분을 Engine 에 링크하므로 뒤에 읽으면 앞 부분이 섞인다.
get_target_property(engine_link_libraries Engine LINK_LIBRARIES)
get_target_property(engine_dependency Engine MANUALLY_ADDED_DEPENDENCIES)
foreach(tier IN LISTS SW_ENGINE_TIER_LIST)
	if(NOT engine_tier_source_${tier})
		continue()   # 이 구성이 짓지 않는 층(전용 서버의 렌더러)
	endif()
	sw_add_module_part(EngineLayer${tier}
		OWNER Engine
		SOURCES ${engine_tier_source_${tier}}
		LINK ${engine_link_libraries}
	)
	sw_add_layer_stubs(EngineLayer${tier} ${tier} SW_ENGINE_TIER Engine)
	target_include_directories(EngineLayer${tier} PRIVATE "${CMAKE_BINARY_DIR}/generated/engineabi")
	add_dependencies(EngineLayer${tier} SwCookCodeFingerprint)
	if(engine_dependency)
		add_dependencies(EngineLayer${tier} ${engine_dependency})
	endif()
endforeach()

# ------------------------------------------------------------------------------
# 11) RHI 백엔드 MODULE 플러그인 (DX는 Windows 전용, GL/Vulkan은 전 플랫폼) — Dev 만 짓는다. 런타임 로드라 링크하지 않는다.
# 백엔드 폴더 하나가 장치 소스와 모듈 엔트리(`<백엔드>/Module/` — CMakeLists · ModuleEntry · 매니페스트)를 함께 든다.
# Shipping 은 모듈 엔트리를 짓지 않는다 — 소스 검사 게이트에 적는다.
# ------------------------------------------------------------------------------
foreach(backend_name IN LISTS SW_RHI_BACKEND_NAMES)
	set(backend_module_dir "Graphics/RHI/${SW_RHI_BACKEND_${backend_name}_SOURCE_FOLDER}/Module")
	if(SW_SHIPPING_BUILD)
		sw_declare_excluded_directory("${CMAKE_CURRENT_SOURCE_DIR}/${backend_module_dir}")
	else()
		add_subdirectory(${backend_module_dir})
	endif()
endforeach()
