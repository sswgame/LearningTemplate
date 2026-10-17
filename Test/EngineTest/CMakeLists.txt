# ==============================================================================
# @file Test/EngineTest/CMakeLists.txt
# @brief Engine 단위 테스트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) EngineTest 타겟 정의 및 런타임 의존성 복사
#
# `RUN_SERIAL` 은 **일부러 없다**. 다른 테스트와 함께 돌아도 되는 근거: `Bin` 에 쓰는 파일이 없고(임시 파일은 케이스 폴더
# `%TEMP%/sw_<pid>/…`), 로그는 이름이 실행마다 다르고, Vulkan 파이프라인 캐시는 원자적으로 쓴다. 직렬로 묶으면
# `ctest -L "nogpu|lint" -j 4` 의 끝을 이 둘(EngineTest_NoGPU · SmokeTest)이 혼자 돌아 전체가 거의 두 배로 늘어난다.
# GPU 를 잡는 `_HostOnly` 조각은 직렬이다(`sw_addTestExecutable`).
# ------------------------------------------------------------------------------
# 키트 목록을 적지 않는다 — 켜진 키트는 레지스트리가 답하고, 꺼진 키트의 헤더를 include 하는 테스트 파일은 빠진다
# (`sw_excludeSourcesOfInactiveKits`, 프로젝트 매니페스트가 키트를 끄면 그 키트의 테스트도 짓지 않는다).
sw_getDynamicModules(swEngineTestKit KINDS kit)
# 부하 테스트 봇(Tools/OnlineLoadBot)은 개발 · Game 타깃에서만 지어진다 — 없는 구성에서는 그 테스트를 뺀다.
set(swEngineTestTool "")
set(swEngineTestExclude "")
if(TARGET OnlineLoadBotCore)
	list(APPEND swEngineTestTool OnlineLoadBotCore)
else()
	list(APPEND swEngineTestExclude "/TestOnlineLoadBot(Load)?\\.cpp$")
endif()

# 에디터 확장(GF_EditorPlugIn_Admin)의 화면 없는 부분(제공자 · 세션)은 ImGui 를 몰라 EngineTest 가 직접 컴파일한다 — 확장 DLL 은 링크하지 않는다.
# 키트가 꺼지면 그 키트의 테스트 파일이 빠지므로(`sw_excludeSourcesOfInactiveKits`) 이 소스도 같이 뺀다.
set(swEngineTestExtensionSource "")
if(TARGET GF_Admin)
	set(swAdminEditorDirectory "${CMAKE_SOURCE_DIR}/Source/GameFramework/PlugIn/Editor/Admin")
	list(APPEND swEngineTestExtensionSource "${swAdminEditorDirectory}/AdminCommandProvider.cpp" "${swAdminEditorDirectory}/AdminToolSession.cpp")
endif()
# 기믹 회로 그래프 편집 모델(GF_EditorPlugIn_Base)도 같다 - 화면 없는 모델과 컴포넌트 다리만 직접 컴파일한다.
set(swGimmickGraphDirectory "${CMAKE_SOURCE_DIR}/Source/GameFramework/PlugIn/Editor/Base/Gimmick")
list(APPEND swEngineTestExtensionSource "${swGimmickGraphDirectory}/GimmickGraphModel.cpp" "${swGimmickGraphDirectory}/GimmickGraphBridge.cpp")
# 어빌리티 디버그 창의 행 모델도 같다.
list(APPEND swEngineTestExtensionSource "${CMAKE_SOURCE_DIR}/Source/GameFramework/PlugIn/Editor/Base/Ability/AbilityDebugModel.cpp")
# NPC 일정 디버그 창의 표 행 변환(GF_EditorPlugIn_Base)도 화면 없는 부분만 직접 컴파일한다.
list(APPEND swEngineTestExtensionSource "${CMAKE_SOURCE_DIR}/Source/GameFramework/PlugIn/Editor/Base/Schedule/ScheduleTimelineRows.cpp")
# 메시 모디파이어(GF_EditorPlugIn_MeshModifiers)의 순수 로직 — 스택 틀(Stack/)과 모디파이어(Modifiers/)를 폴더째 모은다.
# 모디파이어를 더해도 이 줄을 고치지 않는다(병렬로 더하는 작업이 이 목록에서 부딪히지 않게). 등록은 각 .cpp 의 SW_MESH_MODIFIER 가 한다.
file(GLOB swMeshModifierSource CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/Source/GameFramework/PlugIn/Editor/MeshModifiers/Stack/*.cpp"
	"${CMAKE_SOURCE_DIR}/Source/GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/*.cpp")
list(APPEND swEngineTestExtensionSource ${swMeshModifierSource})
# 캐릭터 편집(GF_EditorPlugIn_CharacterEditor)의 화면 없는 편집 모델(Model/)도 폴더째 모은다.
file(GLOB swCharacterEditorModelSource CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Source/GameFramework/PlugIn/Editor/CharacterEditor/Model/*.cpp")
list(APPEND swEngineTestExtensionSource ${swCharacterEditorModelSource})
# 텍스처 표본기가 이미지 디코드를 맡기는 Editor 의 ImageUtil(stb_image 를 감싼다)도 직접 컴파일한다 - EditorModule 은 링크하지 않는다.
# stb include 는 `stb` 타깃을 직접 링크해 얻는다(아래 LIBS) — 다른 타깃이 끌어오는 vcpkg include 폴더는 구성마다 있거나 없다.
list(APPEND swEngineTestExtensionSource "${CMAKE_SOURCE_DIR}/Source/Editor/Common/Asset/ImageUtil.cpp")
# 에디터 테마 토큰(editor/theme/*.tokens.json)의 순수 로더와 데이터 종류 등록 줄도 직접 컴파일한다 - 스키마 테스트가 그 파일을 실제 로더로 읽는다.
list(APPEND swEngineTestExtensionSource
	"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorThemeTokens.cpp"
	"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorThemeTokensDataKind.cpp")

# 테스트 소스는 이 폴더를 훑는다(꺼진 키트의 헤더를 include 하는 파일은 빠진다 — 프로젝트 매니페스트가 키트를 끄면 그 키트의 테스트도 짓지 않는다).
# 갈래(`_NoGPU` · `_HostOnly`) · 조각 · 시간 · 느린 케이스의 비용표는 Test/TestPolicy.json.
sw_addTestExecutable(EngineTest
	EXTRA_SOURCES
		${swEngineTestExtensionSource}
	EXCLUDE
		${swEngineTestExclude}
	LIBS
		Engine
		GameFramework
		${swEngineTestKit}
		${swEngineTestTool}
		stb
)

# RHI Vulkan Debug validation 경로와 동일 Bin 을 쓰므로 layer + mimalloc 배포.
#
# DXC 는 **배포 빌드에서도** 테스트 옆에 둔다. App 과 달리 테스트 바이너리는 Bin 이 아니라
# TestBin 으로 나가므로(sw_addTestExecutable) 배포물에 셰이더 컴파일러가 섞이지 않는다.
# 없으면 ShaderBindingValidatorTest.ReflectionNamesAreUniformAcrossBackends 가 DXIL 리플렉션을
# 못 얻어 "forwardlit_ps g_SwMaterials 원소 없음: dx12" 로 실패한다 — 셰이더가 아니라 도구가
# 없어서 나는 실패라 원인을 찾기 어렵다.
# 에디터 테마 토큰(EditorThemeDef)은 REFLECT 타입이라 StaticType() 이 코드젠에서 나온다. EditorModule 을 링크하지 않으므로 이 헤더만 따로 코드젠한다. 서버 버스 혼합 빌드 테스트의 본문 타입(ServerBusTestTypes.h)도 같다.
sw_addReflectionStep(EngineTest
	HEADERS
		"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Config/EditorThemeTokens.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/GameFramework/Online/ServerBusTestTypes.h"
	INCLUDES
		"${CMAKE_SOURCE_DIR}/Source"
		"${CMAKE_SOURCE_DIR}/Test"
)

sw_deployRuntimeDependencies(EngineTest ALWAYS_DXC)

# RHI MODULE은 런타임 로드 — Engine과 순환 없이 테스트 빌드 시 플러그인 재빌드
#
# 백엔드 이름을 여기 적지 않는다. `sw_addRhiBackendModule` 이 타겟을 만들면서 등록한 것을 묻는다
# (`cmake/Engine/ModuleRegistry.cmake` 의 동적 모듈 레지스트리) — 이름을 글자 그대로 적으면 백엔드가
# 늘 때 조용히 빠진다. Shipping 은 백엔드를 Engine 에 정적 링크해 모듈 타겟이 없으므로 목록이 빈다.
sw_addDynamicModuleDependencies(EngineTest KINDS rhi)

# ------------------------------------------------------------------------------
# 2) ctest — 정책(`hostSplit`)이 `EngineTest_NoGPU_Shard<k>` · `EngineTest_HostOnly_Shard<k>` 를 등록한다
#
# CI 러너가 못 돌리는 스위트(GPU · 디스플레이 · DXC)는 **자기 파일에서** `SW_TEST_REQUIRES_HOST( 스위트, "이유" )`
# 로 선언한다. `_NoGPU` 는 그 선언을 뺀 집합(`--host_suites=exclude`, 라벨 `nogpu` — CI 가 도는 것),
# `_HostOnly` 는 그 나머지(`--host_suites=only`, 라벨 `hostgpu`)다. GPU 가 있는 개발자는 배포 구성에서 이렇게 돈다:
#
#   ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure
#
# 스위트 이름을 이 파일에 적지 않는다 — 이름 목록은 새 GPU 테스트가 비켜 가 그대로 CI 로 들어간다. 리눅스는 X11
# 디스플레이가 없어 조용히 스킵되지만, Windows 는 DX11/DX12 가 Basic Render Driver(WARP)로 **초기화에 성공해서**
# 픽셀 검증이 실제로 돌고 진다. CI 는 `hostgpu` 를 돌리지 못하고 Shipping 에서만 지는 GPU 테스트도 있으니,
# 일을 마치기 전에 GPU 가 있는 기계에서 위 명령(Shipping)으로 `hostgpu` 를 돌릴 것.
# ------------------------------------------------------------------------------
