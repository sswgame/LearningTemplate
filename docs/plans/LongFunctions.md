# 긴 함수 정리 기준 (2026-10-18)

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: C-18 · 진행: 해당없음

100 줄이 넘는 함수를 언제 나누고 언제 그대로 두는지 정하고, 목록을 뽑는 도구와 작업 단위를 둡니다.
나누기 자체는 코드 총량을 줄이지 않습니다. 그래서 줄 수는 후보를 고르는 기준일 뿐이고, 나누는 이유는 아래 1절의 구조 문제여야 합니다.
목록은 `Scripts/lint/report/RunFunctionSize.py` 가 뽑습니다(보고 전용, clang-tidy `readability-function-size` 와 `Source/Engine/Text` 에서 함수 11개의 줄 수가 같음).

## 1. 나누는 기준

순서대로 봅니다. 앞 기준에서 해결되면 뒤로 가지 않습니다.

1. **중복부터 없앱니다.** `RunDuplicateCode.py` 로 긴 함수 안의 반복 블록을 찾고, 공용 함수나 테이블로 합칩니다. 총량이 줄어드는 유일한 경우입니다.
2. **다시 읽기와 두 번 검사를 없앱니다.** 검사 단계가 읽은 값을 실행 단계가 다시 읽는 함수는 한 번 읽은 타입 있는 값을 넘기게 바꿉니다. 자동화 실행기가 대표 예이고, 데이터 주도 계획 7.3 의 "자동화 단계는 한 번만 읽습니다" 가 이 작업입니다.
3. **단계 경계에서 나눕니다.** 파싱, 검사, 적용처럼 단계가 나뉘고, 앞 단계의 지역 변수가 다음 단계로 넘어가지 않는 함수는 단계마다 함수로 나눕니다. 넘겨야 할 값이 많으면 단계 결과 구조체를 둡니다.
4. **분기 사다리는 테이블로 바꿉니다.** 중첩 깊이가 4 이상이거나 같은 형태의 오류 반환이 반복되면, 경우를 데이터 테이블이나 `switch` 하나로 바꿉니다.

그대로 두는 경우는 다음과 같습니다. 이유를 함수 헤더 주석에 한 줄로 적습니다.
- 순서가 곧 의미인 선형 목록입니다. 초기화 순서, 등록 목록, 프레임 단계 본문이 해당합니다. 나누면 순서를 여러 곳에서 읽어야 합니다.
- 경우마다 짧은 `switch` 디스패처입니다.
- 생성 코드, X-macro 테이블, 테스트 본문, 셰이더 코드입니다.

게이트는 두지 않습니다. 줄 수 상한을 게이트로 두면 이유 없는 기계적 분할이 늘어나고, 그것은 총량을 늘립니다. 결정 기록에 한 줄로 남깁니다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| L3 이후 | 영역(폴더)마다 기준 1 ~ 4 를 적용합니다. 영역 하나가 단위 하나이고, 그 폴더를 다른 작업이 건드리는 중이면 미룹니다 | 영역마다 다름 | 그 영역의 다른 계획 단위(예: 자동화는 데이터 주도 D8) | 그 영역의 테스트, 동작 변경 없음 | 핫 패스 함수를 나눌 때만 Release 전후 측정 |

L3 의 단순한 중복 합치기는 `mech-worker` 에 맡길 수 있습니다. 끝난 영역: `Source/Editor/Viewport`(2026-10-18 — 페이스 버튼 표, 기즈모 설정 · 단일 조작, 바 손잡이를 단계 함수로. 격자 `drawAdaptiveGrid` 는 람다를 옮기면 숫자 색 리터럴이 새 줄로 잡혀, 테마 토큰 작업(editorux) 때 함께 나눈다). `Source/Editor/Panels`(2026-10-21 — 본문 100 줄 이상 29건을 의미 단위로 나눴다: 계층 행 · 슬롯 칸 · 카탈로그 XML 읽기 · 속성 표 행과 열거형 콤보 · 레이어 편집기 · 입력 맵 · 콘솔 · 단축키 · 스프라이트 클립 · 시퀀서 · 애니메이션 그래프 · 프로파일러 · 히스토리 · 전역 변수 · 패키징 · 타일맵 · 콘텐츠 브라우저 · 인스펙터 컴포넌트 메뉴 · 데이터 표. `MaterialPanel::drawTypedProperty` 는 경우마다 짧은 `switch` 디스패처라 유지하고 함수 주석에 이유를 적었다. 빌드와 에디터 시나리오 확인은 병합 뒤에 한다). 영역을 맡기 전에 3절 목록을 같은 명령으로 다시 뽑습니다.

## 3. 목록

`py -3 Scripts/lint/report/RunFunctionSize.py --group-depth 3 --top 3` 로 뽑았습니다(2026-10-18, `Source`, 본문 100 줄 초과 234개).
기준 번호는 줄 수 · 중첩 · `return` · 람다 줄만 보고 붙인 예비 판정입니다. L3 에서 그 영역을 맡은 작업자가 본문을 읽고 확정하며, 기준 1(중복)은 어느 함수든 먼저 봅니다.

| 영역 | 수 | 함수(본문 줄 · 중첩 · return · 람다 줄) | 기준 |
|---|---|---|---|
| `Source/GameFramework/Kits` | 35 | `RTSWorld::readState` (`Genre/Strategy/RealTimeStrategy/RTSWorldState.cpp:110`) 216 · 3 · 17 · 0 | 2 · 3 |
|  |  | `WesternCatalog::loadRoot` (`Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.cpp:122`) 205 · 3 · 1 · 0 | 2 · 3 |
|  |  | `FightingMatch::updateFighter` (`Genre/Action/Fighting/FightingMatch.cpp:218`) 172 · 4 · 2 · 0 | 3 |
| `Source/Engine/Graphics` | 34 | `ShaderReflectionUtil::reflectSpirv` (`Shader/Reflection/ShaderReflectionSpirv.cpp:124`) 416 · 6 · 3 · 46 | 4 |
|  |  | `ShaderCompiler::compileHlsl` (`Shader/Compile/ShaderCompiler.cpp:272`) 405 · 5 · 9 · 27 | 4 |
|  |  | `OpenGLRHIDevice::initializeInternal` (`RHI/GL/OpenGLRHIDeviceInit.cpp:48`) 229 · 3 · 7 · 0 | 유지(순서가 곧 의미) |
| `Source/Engine/Renderer` | 22 | `FrameRenderer::executePass` (`Frame/FrameRendererPassExecute.cpp:237`) 460 · 6 · 1 · 20 | 4 |
|  |  | `GPUSceneBuilder::populateFromScene` (`Scene/GPUSceneBuilder.cpp:382`) 385 · 8 · 5 · 0 | 4 |
|  |  | `RenderPipelineAsset::validate` (`Pipeline/RenderPipelineAsset.cpp:35`) 319 · 4 · 1 · 30 | 4 |
| `Source/GameFramework/Base` | 14 | `CameraModeInternal::evaluateView` (`Actor/Camera/CameraMode.cpp:171`) 213 · 4 · 4 · 0 | 3 |
|  |  | `AppearanceResolver::resolve` (`Gameplay/Appearance/AppearanceResolver.cpp:565`) 204 · 4 · 0 · 3 | 2 · 3 |
|  |  | `GimmickCircuit::populate` (`Gameplay/Gimmick/GimmickCircuit.cpp:93`) 189 · 4 · 3 · 0 | 2 · 3 |
| `Source/Engine/Input` | 13 | `InputMap::loadFromResource` (`Map/InputMapSerialization.cpp:436`) 340 · 5 · 5 · 252 | 3(람다를 단계 함수로) |
|  |  | `InputKeyTable::mapWin32VirtualKey` (`Windows/InputKeyTableWin32.cpp:188`) 302 · 2 · 75 · 0 | 유지(switch 디스패처) |
|  |  | `InputKeyTable::mapX11KeySym` (`Linux/InputKeyTableX11.cpp:13`) 252 · 1 · 105 · 0 | 유지(switch 디스패처) |
| `Source/Editor/Common` | 12 | `VrmMaterialImporter::readVrm0Material` (`Asset/VrmMaterialImporter.cpp:290`) 220 · 3 · 16 · 7 | 2 · 3 |
|  |  | `ImGuiVulkanRendererBackend::initialize` (`Backend/Render/ImGuiVulkanRendererBackend.cpp:83`) 123 · 3 · 7 · 38 | 3 |
|  |  | `LocalizationTools::gatherProject` (`Localization/LocalizationTools.cpp:411`) 116 · 3 · 3 · 0 | 3 |
| `Source/Engine/Destruction` | 9 | `simplifyCaps` (`MeshFracture.cpp:606`) 272 · 5 · 3 · 2 | 4 |
|  |  | `MeshFractureUtil::fracture` (`MeshFracture.cpp:1223`) 233 · 5 · 5 · 17 | 4 |
|  |  | `PolygonFractureUtil::fracture` (`PolygonFracture.cpp:180`) 199 · 3 · 4 · 0 | 3 |
| `Source/Engine/Object` | 5 | `SceneAudio::update` (`Component/Audio/SceneAudio.cpp:131`) 136 · 3 · 1 · 0 | 3 |
|  |  | `DirectionalLightComponent::computeShadowProjectionForView` (`Component/3D/DirectionalLightComponent.cpp:285`) 127 · 2 · 3 · 0 | 3 |
|  |  | `TickRegistry::applyStageDelta` (`GameObject/Tick/TickRegistry.cpp:609`) 119 · 5 · 3 · 4 | 4 |
| `Source/Core/Network` | 4 | `NetHost::handlePacket` (`Connection/NetHost.cpp:691`) 213 · 4 · 32 · 0 | 3 |
|  |  | `NetRequestServer::handleFrame` (`Message/NetRequest.cpp:353`) 133 · 4 · 5 · 0 | 3 |
|  |  | `NetConnection::writePacket` (`Connection/NetConnection.cpp:262`) 117 · 2 · 0 · 48 | 3 |
| `Source/Engine/Character` | 4 | `FitSolver::solve` (`Fit/FitSolver.cpp:254`) 148 · 4 · 3 · 0 | 3 |
|  |  | `PairedAnimationSession::start` (`PairedAnimation/PairedAnimationSession.cpp:160`) 122 · 3 · 13 · 0 | 3 |
|  |  | `PairedAnimationSession::update` (`PairedAnimation/PairedAnimationSession.cpp:428`) 103 · 3 · 7 · 0 | 3 |
| `Source/Engine` | 3 | `Engine::initialize` (`Engine.cpp:183`) 250 · 3 · 8 · 0 | 유지(순서가 곧 의미) |
|  |  | `Engine::update` (`Engine.cpp:606`) 160 · 3 · 2 · 0 | 유지(순서가 곧 의미) |
|  |  | `HeadlessSubsystem::initializeSubsystem` (`HeadlessSubsystem.cpp:42`) 155 · 3 · 8 · 0 | 유지(순서가 곧 의미) |
| `Source/Engine/Audio` | 3 | `AudioEngine::applyCommand` (`AudioEngine.cpp:624`) 212 · 4 · 0 · 0 | 3 |
|  |  | `AudioEngine::applyPostEvent` (`AudioEngineEvents.cpp:153`) 170 · 4 · 4 · 0 | 3 |
|  |  | `AudioMixerDesc::validate` (`AudioMixerDesc.cpp:36`) 123 · 4 · 1 · 0 | 4 |
| `Source/Engine/Localization` | 3 | `applyFields` (`CultureInfo.cpp:239`) 121 · 2 · 12 · 0 | 3 |
|  |  | `PortableObjectFile::parse` (`PortableObjectFile.cpp:133`) 119 · 3 · 2 · 0 | 2 · 3 |
|  |  | `CultureInfo::formatDateTime` (`CultureInfo.cpp:599`) 101 · 3 · 1 · 0 | 3 |
| `Source/Engine/Physics` | 3 | `JoltPhysicsScene::createJoint` (`Jolt/JoltPhysicsSceneQuery.cpp:136`) 120 · 3 · 3 · 0 | 3 |
|  |  | `Box2DPhysicsScene::attachShape` (`Box2D/Box2DPhysicsScene.cpp:210`) 109 · 4 · 5 · 0 | 3 |
|  |  | `Box2DPhysicsScene::createJoint` (`Box2D/Box2DPhysicsSceneQuery.cpp:199`) 102 · 3 · 5 · 0 | 3 |
| `Source/Games/Empty` | 3 | `BenchScene::spawn` (`BenchScene.cpp:388`) 181 · 3 · 5 · 0 | 3 |
|  |  | `BenchScene::spawnRigCharacters` (`BenchSceneRig.cpp:120`) 135 · 3 · 1 · 0 | 3 |
|  |  | `BenchScene::update` (`BenchScene.cpp:1167`) 105 · 3 · 4 · 0 | 3 |
| `Source/Core/File` | 2 | `WindowsFileWatcher::workerThreadMain` (`Windows/WindowsFileWatcher.cpp:113`) 120 · 5 · 2 · 0 | 4 |
|  |  | `LinuxFileWatcher::workerThreadMain` (`Linux/LinuxFileWatcher.cpp:151`) 116 · 5 · 3 · 0 | 4 |
| `Source/Core/Process` | 2 | `Process::launch` (`Posix/PosixProcess.cpp:91`) 120 · 2 · 5 · 0 | 3 |
|  |  | `Process::launch` (`Windows/WindowsProcess.cpp:59`) 118 · 2 · 5 · 0 | 3 |
| `Source/Core/Spatial` | 2 | `BVHTree3D::balance` (`BVHTree3D.cpp:320`) 114 · 2 · 4 · 0 | 3 |
|  |  | `BVHTree3D::insertLeaf` (`BVHTree3D.cpp:162`) 105 · 2 · 1 · 0 | 3 |
| `Source/Engine/Animation` | 2 | `PoseRetargeter::initialize` (`Retarget/PoseRetargeter.cpp:64`) 124 · 3 · 4 · 0 | 3 |
|  |  | `AnimationGraphAsset::parseLayers` (`Graph/AnimationGraphAsset.cpp:243`) 112 · 3 · 1 · 0 | 2 · 3 |
| `Source/Engine/Automation` | 2 | `appendFrameSteps` (`ScenarioRecorder.cpp:140`) 186 · 4 · 0 · 25 | 3 |
|  |  | `AutomationScenario::parse` (`AutomationScenario.cpp:145`) 178 · 4 · 11 · 2 | 2 · 3 |
| `Source/Engine/Module` | 2 | `ModuleCatalog::resolve` (`ModuleCatalog.cpp:317`) 141 · 3 · 6 · 0 | 2 · 3 |
|  |  | `ModuleCatalog::parseManifest` (`ModuleCatalog.cpp:105`) 136 · 2 · 16 · 0 | 2 · 3 |
| `Source/Engine/Profiling` | 2 | `ProfilerTraceExport::mergeTraceJSON` (`ProfilerTraceExport.cpp:597`) 159 · 3 · 5 · 0 | 3 |
|  |  | `ProfilerTraceExport::parseTraceJSON` (`ProfilerTraceExport.cpp:171`) 141 · 3 · 4 · 19 | 2 · 3 |
| `Source/Engine/Scene` | 2 | `Scene::instantiate` (`Scene.cpp:249`) 123 · 3 · 3 · 0 | 3 |
|  |  | `SceneCooker::cookAllScenes` (`SceneCooker.cpp:222`) 111 · 3 · 4 · 0 | 2 · 3 |
| `Source/Engine/Serialization` | 2 | `SerializeContext::getDefault` (`Base/SerializeContext.cpp:264`) 245 · 4 · 1 · 240 | 3(람다를 단계 함수로) |
|  |  | `deserializeTagged` (`Format/BinarySerializer.cpp:139`) 120 · 3 · 7 · 0 | 3 |
| `Source/Engine/UserSettings` | 2 | `UserSettingsSchema::loadRoot` (`UserSettingsSchema.cpp:312`) 221 · 6 · 3 · 0 | 4 |
|  |  | `UserSettingsSchema::validate` (`UserSettingsSchema.cpp:665`) 202 · 5 · 1 · 0 | 4 |
| `Source/GameFramework/PlugIn` | 2 | `MeshModifiersPanel::drawNumberListParam` (`Editor/MeshModifiers/MeshModifiersPanel.cpp:796`) 145 · 6 · 0 · 0 | 4 |
|  |  | `MeshModifiersPanel::drawParam` (`Editor/MeshModifiers/MeshModifiersPanel.cpp:565`) 116 · 3 · 0 · 0 | 3 |
| `Source/ModuleHost` | 2 | `ModuleCompiler::runBuildThread` (`ModuleCompiler.cpp:122`) 106 · 2 · 2 · 0 | 2 · 3 |
|  |  | `LiveReloadManager::reloadCascade` (`LiveReloadManager.cpp:846`) 105 · 3 · 7 · 0 | 2 · 3 |
| `Source/Core/Container` | 1 | `parseFormatSpec` (`formatString.h:395`) 129 · 3 · 3 · 0 | 2 · 3 |
| `Source/Core/LogSink` | 1 | `AsyncLogSink::writeLogInternal` (`AsyncLogSink.cpp:349`) 158 · 3 · 2 · 0 | 3 |
| `Source/Editor` | 1 | `ImGuiEditor::updateUI` (`ImGuiEditor.cpp:764`) 101 · 3 · 1 · 0 | 3 |
| `Source/Editor/Popups` | 1 | `QuickLauncherPopup::drawContent` (`QuickLauncherPopup.cpp:169`) 146 · 3 · 1 · 0 | 3 |
| `Source/Editor/SelfTest` | 1 | `TestRunnerPanel::drawUnitTestTab` (`TestRunnerPanel.cpp:284`) 112 · 4 · 0 · 0 | 3 |
| `Source/Engine/Navigation` | 1 | `RecastNavMesh::bakeTile` (`Recast/RecastNavMesh.cpp:237`) 231 · 3 · 15 · 0 | 2 · 3 |
| `Source/Engine/Resource` | 1 | `DDSLoader::loadFromMemory` (`Image/DDSLoader.cpp:134`) 174 · 3 · 7 · 0 | 2 · 3 |
| `Source/Engine/Text` | 1 | `TextLayoutEngine::layout` (`TextLayout.cpp:326`) 286 · 4 · 3 · 0 | 3 |
| `Source/Engine/TileMap` | 1 | `TileMapXMLData::loadFromXML` (`TileMapXML.cpp:89`) 177 · 5 · 7 · 0 | 4 |
| `Source/Engine/UI` | 1 | `UIStyleTransition::onStyleChanged` (`Animation/UIStyleTransition.cpp:428`) 104 · 3 · 3 · 0 | 3 |
| `Source/Engine/Window` | 1 | `Win32Window::wndProc` (`Windows/Win32Window.cpp:201`) 121 · 5 · 8 · 0 | 4 |
| `Source/Games/HarvestValley` | 1 | `FarmAutoFarmerAIComponent::think` (`FarmAutoFarmerAIComponent.cpp:135`) 177 · 5 · 4 · 0 | 4 |

## 4. 사용자 결정

없습니다.
