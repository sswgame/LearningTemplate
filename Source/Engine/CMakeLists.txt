# ==============================================================================
# @file Source/Engine/CMakeLists.txt
# @brief 코어 모듈(RHI, Reflection, Object, Utility, Window 등)을 빌드하는 타깃 스크립트
# @details
# 폴더(Common/Reflection/...)는 소스를 나눠 두기 위한 것이고, 모듈 사이의
# 순환 참조(Reflection <-> Graphics 등) 때문에 하나의 라이브러리로 링크한다
# (Dev 는 SHARED, Shipping 은 STATIC).
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 라이브러리 타입 · RHI 소스 목록
# Dev: SHARED / Shipping: STATIC
# RHI MODULE 엔트리(Modules/)와 백엔드 장치 .cpp 는 항상 Engine GLOB 에서 뺀다(장치 소스는 cmake/Engine/RhiBackends.cmake 가 백엔드 폴더로 안다).
# Dev 는 RHI_* MODULE 이, Shipping 은 아래 3) 이 고른 백엔드 하나를 같은 함수로 가져다 짓는다.
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD)
	set(coreLibType STATIC)
else()
	set(coreLibType SHARED)
endif()

# DelayLoadNotifyHook 이 Engine 에서 빠지는 것은 **의도한 것**이다. 이 훅은 지연 로드된 DLL 을
# 이미 로드된 엔진 모듈 핸들로 돌려주는 장치라, 그런 DLL 을 지연 로드하는 쪽(kit · SWGame)이
# sw_addDelayloadHook 으로 자기 바이너리에 넣어야 의미가 있다. 훅 변수는 모듈마다 따로다.
# Engine 이 지연 로드하는 것은 d3dcompiler_47 · mfplat · mfreadwrite · xaudio2_9 로 전부 시스템
# DLL 이고, 그것들은 훅이 돌려줄 핸들이 없다(Bin 에도 없다). 훅을 넣으면 정상 로드마다 훅이
# "못 찾았다" 는 ERROR 를 남기게 된다.
set(coreExclude
	"Graphics/RHI/Modules/"
	"Module/DelayLoadNotifyHook.cpp"
	"Physics/Jolt/[^/]+\\.cpp$"   # EngineJolt_objects 가 짓는다(아래 7))
)

# ------------------------------------------------------------------------------
# 2) Engine 타겟 — include 전파, Core_objects 재사용
# Core 소스는 한 번만 컴파일. 동일 .obj를 Engine에 넣어
# Dev에서 App/Editor가 Engine.dll로부터 FileUtil/Logger를 dllimport
# ReflectionParser는 Core STATIC만 링크해 Engine.dll 순환을 피함
# ------------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

foreach(exPattern IN LISTS coreExclude)
	list(FILTER ENGINE_SOURCES EXCLUDE REGEX "${exPattern}")
endforeach()

# RHI 백엔드 장치 소스는 Engine 이 짓지 않는다 — Dev 는 RHI_* 모듈이, Shipping 은 아래 3) 이 고른 하나를 가져간다.
sw_getAllRhiBackendSources(listRhiBackendSource)
list(REMOVE_ITEM ENGINE_SOURCES ${listRhiBackendSource})

# 셰이더 라이브 리로드는 개발 도구다. Shipping 은 쿠킹된 셰이더만 읽으므로 감시할 것이 없다.
# 실행되지 않는 것만으로는 부족하고, 바이너리에서 아예 빠져야 한다. 빼는 것은 소스 검사 게이트에도 적힌다(`sw_excludeUnbuiltSources`).
# (위 `coreExclude` 는 다른 타겟이 짓는 것 — RHI 모듈 · 지연 로드 훅 — 이라 적지 않는다.)
if(SW_SHIPPING_BUILD)
	sw_excludeUnbuiltSources(ENGINE_SOURCES "Graphics/Shader/Compile/ShaderRecompiler\\.(cpp|h)$")
endif()

# ResourcePackReader 가 uncompress 를 직접 부른다.
find_package(ZLIB REQUIRED)

# LZ4 · Zstd 코덱(Engine/Compression). zlib 과 같은 이유로 **Engine 에만** 붙인다.
# Core 는 압축 라이브러리에 의존하지 않게 두고, ReflectionParser 가 끌고 가지 않게 한다.
find_package(lz4 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

# zstd 는 vcpkg 구성(정적/공유)에 따라 노출하는 타겟 이름이 다르다. 하나만 적으면 다른 구성에서
# configure 가 조용히 지나가고 링크에서 터진다. zlib 에서 얻은 교훈과 같다.
if(TARGET zstd::libzstd)
	set(swZstdTarget zstd::libzstd)
elseif(TARGET zstd::libzstd_shared)
	set(swZstdTarget zstd::libzstd_shared)
elseif(TARGET zstd::libzstd_static)
	set(swZstdTarget zstd::libzstd_static)
else()
	message(FATAL_ERROR "zstd 임포트 타겟을 찾지 못했습니다 (zstd::libzstd / _shared / _static).")
endif()
message(STATUS "[Engine] lz4=lz4::lz4 zstd=${swZstdTarget}")

# 무엇으로 풀렸는지 configure 로그에 남긴다. Linux 에서 `libEngine.so: undefined reference to uncompress` 같은 링크
# 오류가 나면 링크 구문만 보고는 ZLIB::ZLIB 이 실제로 무엇을 가리키는지(정적/공유, debug/release, vcpkg/시스템) 알 수 없다.
get_target_property(swZlibType ZLIB::ZLIB TYPE)
get_target_property(swZlibImportedConfig ZLIB::ZLIB IMPORTED_CONFIGURATIONS)
get_target_property(swZlibLocationDebug ZLIB::ZLIB IMPORTED_LOCATION_DEBUG)
get_target_property(swZlibLocationRelease ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
get_target_property(swZlibLocationNoConfig ZLIB::ZLIB IMPORTED_LOCATION)
message(STATUS "[Engine] ZLIB::ZLIB type=${swZlibType} configs=${swZlibImportedConfig}")
message(STATUS "[Engine] ZLIB::ZLIB loc(debug)=${swZlibLocationDebug} loc(release)=${swZlibLocationRelease} loc=${swZlibLocationNoConfig}")
message(STATUS "[Engine] ZLIB_LIBRARIES=${ZLIB_LIBRARIES} ZLIB_INCLUDE_DIRS=${ZLIB_INCLUDE_DIRS}")

# 임포트 타겟이 아무 라이브러리도 가리키지 않으면 링크는 조용히 통과하고(공유 라이브러리는 미해결 심볼을
# 허용한다) 그걸 링크하는 실행 파일에서야 터진다. 여기서 먼저 멈춘다.
if(NOT swZlibLocationDebug AND NOT swZlibLocationRelease AND NOT swZlibLocationNoConfig AND NOT ZLIB_LIBRARIES)
	message(FATAL_ERROR "ZLIB::ZLIB 이 어떤 라이브러리도 가리키지 않습니다 — 이대로 링크하면 libEngine 이 uncompress 를 미해결로 남깁니다.")
endif()

add_library(Engine ${coreLibType} ${ENGINE_SOURCES})

# 핫 리로드 ABI 도장 — Engine 이 자기 헤더 지문을 박는다(`Engine/Module/EngineAbiStamp.cpp`). 모듈도 같은 값을 박는다.
sw_defineEngineAbiStamp()
add_dependencies(Engine SwEngineAbiStamp)
target_include_directories(Engine PRIVATE "${CMAKE_BINARY_DIR}/generated/engineabi")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Module/EngineAbiStamp.cpp" PROPERTIES
	OBJECT_DEPENDS "${SW_ENGINE_ABI_STAMP_HEADER}"
)

target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# 셰이더 바인딩 계약(bindingslots.hlsli)은 HLSL 과 C++ 가 같은 파일을 읽는다. ShaderBindingSlots.h 가 include 한다.
target_include_directories(Engine PUBLIC "${CMAKE_SOURCE_DIR}/Resource/engine/shaders")
target_link_libraries(Engine
	PUBLIC
	sw_public_source_includes
	PRIVATE
	sw_global_options

	# 리소스 팩 압축 해제(ResourcePackReader). Core 는 압축 라이브러리에 종속되지 않게
	# 두고(ICompressionCodec 주석 참고), 실제로 쓰는 Engine 에만 붙인다.
	#
	# raw 이름 `zlib` 을 넘기면 안 된다. 그건 링커에 그대로 `-lzlib` 로 전달되는데,
	# Windows vcpkg 는 zlib.lib 라 우연히 맞지만 Linux 는 libz.so 라 못 찾는다. 공유
	# 라이브러리는 미해결 심볼을 허용해서 libEngine.so 링크는 통과하고, 그걸 링크하는
	# App 에서야 `undefined reference to uncompress` 로 터진다. 임포트 타겟을 쓴다.
	ZLIB::ZLIB
	# 임포트 타겟 **하나에만** 기대지 않는다. Linux CI 에서 `libEngine.so: undefined reference to uncompress`
	# 가 세 번 났는데, 매번 링크 줄에 zlib 이 아예 붙지 않은 결과였다(임포트 타겟이 왜 비었는지는 이
	# 머신에서 재현하지 못했다). find_package 가 채우는 ZLIB_LIBRARIES 는
	# `optimized;<경로>;debug;<경로>` 형태의 **확정된 경로 목록**이라 타겟 속성 해석에 좌우되지 않는다.
	# 둘 다 걸어 어느 한쪽이 비어도 심볼이 남지 않게 한다(중복 지정은 링커가 무시한다).
	${ZLIB_LIBRARIES}

	# LZ4 · Zstd — CompressionCodecRegistry 에 EngineLoop 이 등록하는 코덱들이 쓴다.
	lz4::lz4
	${swZstdTarget}
)

target_compile_definitions(Engine PRIVATE "SW_LOG_TAG=\"Engine\"" SW_ENGINE_INTERNAL)
sw_configurePch(Engine "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")

target_sources(Engine PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../sw.natvis"
)

target_sources(Engine PRIVATE $<TARGET_OBJECTS:Core_objects>)
add_dependencies(Engine Core_objects)

# 애니메이션 압축(ACL · RTM, 헤더만). 헤더는 `Animation/Codec/Acl/` 에서만 include 한다 — 코덱 인터페이스(`AnimCodec.h`) 뒤에 감춘다.
target_link_libraries(Engine PRIVATE acl)

# ThirdParty include/link 는 PRIVATE 로 둔다. Engine 밖으로 헤더가 노출되지 않게 해 C-ABI 격리를 유지한다.
if(TARGET sw_third_party_includes)
	target_link_libraries(Engine PRIVATE sw_third_party_includes)
endif()

# ------------------------------------------------------------------------------
# 2-1) 물리 백엔드 — Jolt(3D) · Box2D(2D). 헤더는 Physics/Jolt · Physics/Box2D 에서만 include 한다(CheckThirdPartyIsolation.py).
# 서드파티 물리 · 애니메이션 라이브러리를 링크하는 곳은 이 파일 하나다 — 다른 타깃은 엔진 인터페이스(IPhysicsScene)만 본다.
#
# Jolt 임포트 타깃은 사용 요건으로 AVX2 같은 대상 기능(-mavx2 ...)과 JPH_* 정의를 싣는다. 그대로 링크하면 **Engine 의 모든 TU**
# 가 AVX2 로 컴파일된다. 링크만 받고($<LINK_ONLY:>), 정의 · 옵션은 Jolt 백엔드 소스에만 붙인다 — 그 소스는 Engine 이 아니라
# 자기 PCH 를 가진 OBJECT 라이브러리 `EngineJolt_objects` 가 짓는다(아래 7)).
# Box2D 는 BOX2D_DLL 정의 하나뿐이라 그대로 PRIVATE 로 링크한다. 둘 다 DLL 이고(x64-windows) Bin 에는 vcpkg applocal 이 옮긴다.
# ------------------------------------------------------------------------------
target_link_libraries(Engine PRIVATE $<LINK_ONLY:joltphysics> box2d)

# ------------------------------------------------------------------------------
# 2-2) Tracy — 엔진 프로파일러의 두 번째 출력(Profiling). 헤더는 Profiling/Tracy 에서만 include 한다(CheckThirdPartyIsolation.py).
# Shipping 은 링크하지 않는다(SW_ENABLE_TRACY 를 무시). 클라이언트는 프로세스에 하나 — Engine 만 링크하고, RHI 백엔드 · 모듈은 엔진
# 인터페이스(IProfilerBackend)만 본다. GPU 구간은 엔진이 이미 모으는 타임스탬프를 넘기므로 백엔드 DLL 이 Tracy 를 알 필요가 없다.
# 임포트 타깃의 정의(TRACY_ENABLE · TRACY_IMPORTS …) · include 경로는 Tracy 구현 소스에만 붙인다(Jolt 와 같은 방식).
# Windows 는 DLL 을 지연 로드한다 — `-gv_tracy` 로 켜기 전에는 올라오지 않는다(C API 함수만 쓰므로 지연 로드가 된다).
# ------------------------------------------------------------------------------
if(SW_ENABLE_TRACY AND NOT SW_SHIPPING_BUILD AND TARGET Tracy::TracyClient)
	target_link_libraries(Engine PRIVATE $<LINK_ONLY:tracy>)
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
	if(WIN32)
		sw_addDelayloadSystemDlls(Engine DLLS TracyClient.dll)
	endif()
	message(STATUS "[Engine] Tracy profiler client linked (delay-loaded on Windows)")
endif()
# 2-2b) RenderDoc in-app API — 헤더 하나(include 경로만, 링크할 것 없음). Renderer/Capture/RenderDocCapture.cpp 가 Dev 에서만 쓴다.
target_link_libraries(Engine PRIVATE renderdoc)
# 2-3) 내비메시 백엔드 — Recast & Detour(정적 라이브러리, 굽기 · 질의 · 군중). 헤더는 Navigation/Recast 에서만 include 한다
# (CheckThirdPartyIsolation.py) — 다른 코드는 엔진 인터페이스(INavMesh · INavCrowd)만 본다.
# ------------------------------------------------------------------------------
target_link_libraries(Engine PRIVATE recastnavigation)

# ------------------------------------------------------------------------------
# 2-5) 글자 래스터화 — FreeType(x64-windows 는 freetype.dll, 리눅스는 정적). 헤더는 Text/FreeType 에서만 include 한다(CheckThirdPartyIsolation.py)
# — 다른 코드는 엔진 인터페이스(Engine/Text/IFontRasterizer.h)만 본다.
# ------------------------------------------------------------------------------
target_link_libraries(Engine PRIVATE freetype)

# ------------------------------------------------------------------------------
# 3) RHI 링크 — Shipping 은 백엔드 하나를 Engine 에 정적 링크, Dev 는 RHI_* MODULE 을 런타임에 로드
# 전용 서버 배포본은 RHI 백엔드를 하나도 링크하지 않는다(기동 표의 RHI 단계가 서버에서 돌지 않는다) — 백엔드 소스는 모두 "이 구성이 짓지 않는다".
# Dev 서버는 RHI_* 모듈을 매니페스트(`_listTarget: ["Client"]`)가 끈다.
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD AND SW_TARGET_TYPE STREQUAL "Server")
	message(STATUS "[Engine] Dedicated server: no RHI backend linked")
	sw_declareUnbuiltSources(${listRhiBackendSource})
elseif(SW_SHIPPING_BUILD)
	# 이름 · 별칭은 쿠킹 표(`Config/Engine/CookContract.json` `rhi_backends`)가 풀고, 이 플랫폼에 없는 백엔드면 구성을 세운다(sw_resolveShippingRhiBackend).
	# 주의: 여기서 백엔드를 못 고른 채 지나가면 Engine 은 백엔드 없이 링크되고 실행에서야 "백엔드 없음" 으로 진다(RHIBackendRegistry.cpp 의 #error 가 다음 방어선).
	sw_resolveShippingRhiBackend("${SW_SHIPPING_RHI_BACKEND}" swShippingBackend)
	# EngineConfig 가 다른 백엔드를 고르면 알린다 — 배포본은 링크한 하나만 띄운다.
	file(READ "${CMAKE_SOURCE_DIR}/${SW_FILE_RUNTIME_ENGINE_CONFIG}" swEngineConfigText)
	string(JSON swEngineConfigRhi ERROR_VARIABLE swEngineConfigRhiError GET "${swEngineConfigText}" _window _defaultRHI)
	if(NOT swEngineConfigRhiError AND NOT swEngineConfigRhi STREQUAL swShippingBackend)
		message(WARNING "${SW_FILE_RUNTIME_ENGINE_CONFIG} picks _defaultRHI='${swEngineConfigRhi}' but Shipping links only '${swShippingBackend}'")
	endif()

	message(STATUS "[Engine] Shipping monolithic RHI backend: ${swShippingBackend}")
	sw_getRhiBackendSources(${swShippingBackend} listShippingRhiSource)
	target_sources(Engine PRIVATE ${listShippingRhiSource})
	target_link_libraries(Engine PUBLIC ${SW_RHI_BACKEND_${swShippingBackend}_GRAPHICS_LIBS})
	target_compile_definitions(Engine PUBLIC ${SW_RHI_BACKEND_${swShippingBackend}_SHIPPING_DEFINE})

	# 고르지 않은 백엔드는 이 구성에서 아무도 짓지 않는다 — 소스 검사 게이트가 알도록 적는다.
	set(listUnbuiltRhiSource ${listRhiBackendSource})
	list(REMOVE_ITEM listUnbuiltRhiSource ${listShippingRhiSource})
	sw_declareUnbuiltSources(${listUnbuiltRhiSource})
endif()

# ------------------------------------------------------------------------------
# 4) export / IPO / delay-load 훅 경로 / 로그 태그 / Windows 시스템 라이브러리
# SW_DELAYLOAD_HOOK_SOURCE는 sw_addDelayloadHook(ModuleTargets.cmake)이 참조
# ------------------------------------------------------------------------------
sw_configureDllExports(Engine ${coreLibType} ENGINE)
set_target_properties(Engine PROPERTIES FOLDER "Source/Engine")

set_property(TARGET Engine PROPERTY SW_DELAYLOAD_HOOK_SOURCE
	"${CMAKE_CURRENT_SOURCE_DIR}/Module/DelayLoadNotifyHook.cpp")

# Windows: Input(XInput) + Audio(XAudio2) + 파일 대화상자 + 셰이더 컴파일러 유틸리티
if(WIN32)
	target_link_libraries(Engine PRIVATE
		comdlg32.lib
		d3dcompiler.lib
		dbghelp.lib
		synchronization.lib
		mfplat.lib
		mfreadwrite.lib
		mfuuid.lib
		ole32.lib
		shell32.lib
	)
	# 전용 서버 빌드에는 XAudio2 구현이 없다(`IAudioSystem.cpp` · `XAudio2System.cpp` 의 SW_WITH_CLIENT_CODE) — 링크 · 지연 로드 항목도 뺀다.
	if(SW_TARGET_TYPE STREQUAL "Server")
		sw_addDelayloadSystemDlls(Engine DLLS d3dcompiler_47.dll mfplat.dll mfreadwrite.dll)
	else()
		target_link_libraries(Engine PRIVATE xaudio2.lib)
		sw_addDelayloadSystemDlls(Engine DLLS d3dcompiler_47.dll mfplat.dll mfreadwrite.dll xaudio2_9.dll)
	endif()
endif()

# ------------------------------------------------------------------------------
# 5) Unity — 제외 목록이 **없다.** 그게 정상이고, 다시 만들지 말 것
#
# **유니티 제외 목록을 다시 만들지 말 것** — 목록은 TU 를 나눌 때마다 같이 고쳐야 하는 자리가 되고, 빠뜨려도 Ninja-* 프리셋은
# 유니티가 꺼져 있어 CI 에서만 터진다.
#
# "익명 네임스페이스가 합쳐져 이름이 부딪친다" 는 **이름 규칙으로 막혀 있다**. 익명 네임스페이스의 도우미는
# 클래스가 아니라 **TU 이름**을 딴다(`ShaderCookStampInternal` 등, AGENTS.md). `CheckCodeConventions` 의
# `Naming/DuplicateInternalHelper` 가 그것을 검사한다. 목록이 아니라 린트가 지킨다.
#
# 다시 넣고 싶어지면 먼저 `cmake --build --preset CI-Debug` 로 **무엇이 실제로 부딪치는지** 보고, 목록이 아니라 그 이름을 고칠 것.
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
	Destruction/FractureAsset.cpp
	Destruction/FractureGraph.cpp
	Destruction/MeshFracture.cpp
	Destruction/PolygonFracture.cpp
	Destruction/PolygonTriangulation.cpp
)

# ------------------------------------------------------------------------------
# 6) ENUM() 코드젠 — ReflectionParser 필요 (루트가 Tools 이후 이 파일을 호출)
# RHI_* MODULE은 런타임 로드이므로 빌드 순서만 묶음 (링크하지 않음)
# ------------------------------------------------------------------------------
sw_addReflectionStep(Engine
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
sw_addReflectBuiltinsGen(Engine)

# ------------------------------------------------------------------------------
# 7) Jolt 백엔드 소스 — 자기 PCH 를 가진 OBJECT 라이브러리(목적 파일은 Core_objects 처럼 Engine 에 들어간다)
#
# Jolt 백엔드 소스는 Jolt 의 대상 기능(-mavx2 ...)으로 컴파일되므로 Engine PCH 를 쓸 수 없다(대상 기능이 다른 PCH 는 clang 이 거부한다).
# PCH 없이 지으면 TU 마다 엔진 `pch.h` 를 다시 파싱한다 — 그 파싱이 TU 시간의 절반이다. 그래서 이 소스만 따로 묶어 `pch.h` + Jolt
# 래퍼 헤더로 PCH 를 하나 만든다(측정은 docs/08_Verification.md "빌드 속도"). 컴파일 설정은 Engine 의 것을 그대로 옮기고 Jolt 의 정의 ·
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

# RHI 백엔드 MODULE 플러그인 (DX는 Windows 전용, GL/Vulkan은 전 플랫폼) — Dev 만 짓는다.
# Shipping 은 모듈 엔트리(Modules/<백엔드>/ModuleEntry.cpp)를 짓지 않는다 — 소스 검사 게이트에 적는다.
if(SW_SHIPPING_BUILD)
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RHI/Modules")
else()
	add_subdirectory(Graphics/RHI/Modules)
endif()
