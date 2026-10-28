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

게이트는 두지 않습니다. 줄 수 상한을 게이트로 두면 이유 없는 기계적 분할이 늘어나고, 그것은 총량을 늘립니다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| L3 이후 | 영역(폴더)마다 기준 1 ~ 4 를 적용합니다. 영역 하나가 단위 하나이고, 그 폴더를 다른 작업이 건드리는 중이면 미룹니다 | 영역마다 다름 | 그 영역의 다른 계획 단위(예: 자동화는 데이터 주도 D8) | 그 영역의 테스트, 동작 변경 없음 | 핫 패스 함수를 나눌 때만 Release 전후 측정 |

L3 의 단순한 중복 합치기는 `mech-worker` 에 맡길 수 있습니다.

끝난 영역(2026-10-18 ~ 25, 대부분 빌드 미확인): `Source/Editor/Panels` · `Editor/Common` · `Editor/Viewport` · `Core/Network` · `Core/File` · `GameFramework/Kits`(`Feature/Online` 포함) · `GameFramework/Base` · `Engine/Input` · `Destruction` · `Character` · `Audio` · `Localization` · `Physics` · `Games/Empty` · `Engine/Graphics` · `Renderer`, 그리고 `wt/plangfxq` 두 묶음(직렬화 · 애니메이션 · 씬 · 텍스트 · 네비 · 에디터 임포터 · 엔진 갱신 등 약 100 개). 단계 함수로 나눴고, 짧은 `switch` 디스패처 · 기동 순서 열은 유지로 판정해 함수 주석에 이유를 적었다. `drawAdaptiveGrid` 는 테마 토큰 작업(editorux) 때 함께 나눈다.
남은 긴 함수와 판정은 3절 표가 정본이다(영역별 이력의 "남은 N 개" 셈은 다시 뽑으면 어긋났다). 영역을 맡기 전에 3절 표에서 그 영역 행만 봅니다.

## 3. 목록

본문 100 줄을 넘는 함수는 지금 71 개이고, 아래 표가 전부다(2026-10-25, `RunFunctionSize.py` 를 `Source` 전체에서 다시 돌림). 이미 나뉜 영역은 이 표에 없다: `Source/Core/Network` 의 `NetRequestServer::handleFrame` · `NetConnection::writePacket` 도 지금은 60 줄 아래라 끝났다.

**표에는 줄 수를 적지 않습니다.** 손으로 적은 줄 수는 코드가 바뀌면 어긋나고, 그래서 이미 나뉜 함수를 작업자가 다시 맡았다. 줄 수 · 중첩은 `py -3 Scripts/lint/report/RunFunctionSize.py --filter <경로>` 로 그때 잰다.
표가 맡는 것은 사람이 정한 판정뿐이고, `py -3 -m Scripts plans --check` 가 표의 행(파일 + 함수 이름)과 코드의 100 줄 초과 함수가 같은지 본다. 새로 길어진 함수는 판정 행을 더하게 하고, 나뉜 함수의 행은 지우게 한다(틀리면 종료 1). 함수를 나누면 그 행을 지우고, 유지로 정하면 판정 칸에 이유를 적고 함수 헤더 주석에도 같은 이유를 적는다.

판정 칸은 `나눔`(기준 번호와 단계) 또는 `유지`(이유)로 시작한다. `나눔 (예비)` 는 본문을 다 읽지 않고 단계 주석 · 케이스 모양으로 정한 것이라, 맡은 작업자가 읽고 확정한다. 기준 1(중복)은 어느 함수든 먼저 본다.

| 파일 | 함수 | 판정 |
| `Source/Engine/Input/Map/InputMapSerialization.cpp` | `InputMap::loadDefinition` | 나눔 (예비) — 기준 3: 장치 · 액션 · 바인딩 · 맥락 읽기 단계 |
| `Source/Engine/Profiling/ProfilerTraceExport.cpp` | `ProfilerTraceExport::makeTraceJSON` | 나눔 (예비) — 기준 3: 머리말 · 스레드 이름 · 이벤트 · 카운터 쓰기 단계 |
| `Source/App/App.cpp` | `App::run` | 유지 — 프레임 단계 본문이라 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX11/D3D11RHIResourceFactoryPipeline.cpp` | `D3D11RHIResourceFactory::createPipelineState` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Renderer/Scene/GPUMeshMorphPool.cpp` | `GPUMeshMorphPool::update` | 나눔 (예비) — 기준 3: 요청 모으기 · 슬롯 배정 · 업로드 단계 |
|---|---|---|
| `Source/GameFramework/Base/Actor/AI/BehaviorTree.cpp` | `BehaviorTreeRunner::tickNode` | 유지 — 12 종류 `switch` 의 케이스가 평균 9 줄이고 합성 노드(`tickSequence` · `tickSelector`)는 이미 뺐다. 가장 긴 케이스(`Repeat` · `Cooldown`)도 13 줄 안쪽이라 더 가르면 같은 모양의 함수만 늘어난다. 함수 주석에 이유가 있다 |
| `Source/GameFramework/Base/Online/Cache/MemoryEphemeralStore.cpp` | `MemoryEphemeralDatabase::executeValue` | 유지 — 연산 7 개의 `switch` 이고 케이스가 10 ~ 24 줄이다(`Increment` 가 가장 길다). 연산 하나의 규칙(RESP 와 같은 동작)을 한곳에서 읽는 값이 있어 유지하되, `Increment` 가 더 길어지면 그 케이스를 뺀다. 함수 주석에 이유가 있다 |
| `Source/Engine/Renderer/Vfx/VfxGPUSimulation.cpp` | `VfxGPUSimulation::prepareFrame` | 나눔 (예비) — 기준 3: 파티클 배치 · 파티클 라이트 · 메시 방출기 · 지난 재생 놓기 |
| `Source/Engine/Renderer/Light/GPULightBuffer.cpp` | `GPULightBuffer::update` | 나눔 — 기준 3: 프레임 상한 나누기 · 업로드 목록 짜기 · 버퍼 확보 · 프로브 SH 올리기 |
| `Source/Editor/Panels/Catalog/CatalogTableEditor.cpp` | `CatalogTableEditor::draw` | 나눔 (예비) — 기준 3: 그룹마다 표를 그리는 `for` 본문(약 100 줄)을 그룹 하나 그리기로 뺀다 |
| `Source/Editor/Panels/Inspector/InspectorPropertyManager.cpp` | `drawValueWidget` | 나눔 (예비) — 기준 4: 값 타입별 분기(`return` 24 개)를 타입별 위젯 함수 또는 오버로드로 |
| `Source/Editor/Panels/Inspector/EditorPropertyGrid.cpp` | `EditorPropertyGrid::drawTypeMethods` | 나눔 (예비) — 기준 3: 메서드 하나 그리기 · 인자 입력란 · 호출 버튼 |
| `Source/Engine/Graphics/Texture/TextureCache.cpp` | `TextureCache::flushRegionUpdates` | 나눔 (예비) — 기준 3: 요청 하나 내기 · `Busy` / `Rejected` 규칙 · 미룬 것 합치기 |
| `Source/Engine/Renderer/Canvas/CanvasRenderer.cpp` | `CanvasRenderer::uploadAtlasPage` | 나눔 (예비) — 기준 3: 첫 전체 업로드 · 등록 · 구간 업로드 · 실패 규칙 |
| `Source/Engine/Destruction/FractureComponentBase.cpp` | `FractureComponentBase::updateGroups` | 나눔 (예비) — 기준 3: 잠든 덩어리 정리 · 예산 초과 제거 순서 정하기 |
| `Source/Engine/Graphics/Shader/Compile/ShaderCookStamp.cpp` | `verifyDomain` | 나눔 (예비) — 기준 3: 우선순위 폴더 판정 · 헤더 스탬프 판정 · 키 누락 판정 |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.cpp` | `VulkanRHISwapChain::createSurface` | 나눔 (예비) — 기준 4: 창 시스템 길(Win32 · Xlib · Xcb · Wayland)마다 표면 만들기 함수로 |
| `Source/Engine/Graphics/Lighting/SceneVoxelGrid.cpp` | `SceneVoxelGrid::traceRay` | 유지 — 슬랩 판정 · 시작 칸 · 칸 걷기가 지역 변수(`tNear` · 칸 · 축 · 경계 거리)를 모두 공유하는 한 알고리즘이고 103 줄이라 경계다. 단계 결과 구조체를 둘 만큼의 이득이 없다. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/EngineSubsystemSet.cpp` | `addStartupSubsystems` | 유지 — 기동 서브시스템을 순서대로 단계 객체에 등록하는 열이고, 순서와 범위(호스트 · 엔진 · Server 제외)가 곧 의미다. `Engine::initialize` 와 같은 이유. 헤더 주석에 이유를 적어야 한다 |
| `Source/App/App.cpp` | `App::initialize` | 유지 — 호스트 범위 → 엔진 → 헤드리스 작업 → 자동화 → 설정 감시 순의 시동 열이고 순서가 곧 의미다(`Engine::initialize` 와 같은 이유). 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Engine.cpp` | `Engine::initialize` | 유지 — 기동 순서가 곧 의미인 열(함수 주석에 이유) |
| `Source/Engine/Renderer/RenderThread.cpp` | `RenderThread::executeFrameBody` | 유지 — 프레임 단계 순서가 곧 의미이고 단계별 측정 범위 주석이 함께 읽혀야 한다 |
| `Source/Engine/Renderer/Frame/FrameRendererPassExecute.cpp` | `FrameRenderer::executePass` | 유지 — 패스별 `execute*Pass` 로 뺀 뒤 남은 짧은 `switch` 디스패처(함수 주석에 이유) |
| `Source/Engine/Renderer/Scene/GPUSceneBuilder.cpp` | `GPUSceneBuilder::populateMeshesFromScene` | 유지 — 단계는 이미 나뉘었고 남은 본문은 단계를 고르는 결정 순서(함수 주석에 이유) |
| `Source/Engine/Graphics/Shader/Compile/ShaderCompiler.cpp` | `compileDxc` | 유지 — 생성 · 소스 읽기 · 인자 · 컴파일 · 결과 읽기의 순서가 곧 의미이고 인자 글의 수명이 이 함수 안이다 |
| `Source/Engine/Renderer/Pipeline/RenderPassTypeInfo.cpp` | `makeInfo` | 유지 — 경우마다 표 한 줄(함수 주석에 이유) |
| `Source/Engine/Graphics/Shader/Binding/ShaderBindingValidator.cpp` | `buildReservedBindings` | 유지 — 줄마다 리소스 하나인 등록 목록(함수 주석에 이유) |
| `Source/Engine/Input/Windows/InputKeyTableWin32.cpp` | `InputKeyTable::mapWin32VirtualKey` | 유지 — 케이스마다 한 줄인 키 표(함수 주석에 이유) |
| `Source/Engine/Input/Linux/InputKeyTableX11.cpp` | `InputKeyTable::mapX11KeySym` | 유지 — 케이스마다 한 줄인 키 표(함수 주석에 이유) |
| `Source/Engine/Input/Map/InputKeyTable.cpp` | `InputKeyTable::mapScanCodeToKey` | 유지 — 케이스마다 한 줄인 키 표(함수 주석에 이유) |
| `Source/Engine/Input/Windows/InputManagerWin32.cpp` | `InputManager::processNativeEvent` | 유지 — 긴 케이스는 뺀 뒤 남은 메시지별 짧은 `switch`(함수 주석에 이유) |
| `Source/Engine/Input/Linux/InputManagerX11.cpp` | `InputManager::processNativeEvent` | 유지 — Win32 짝과 같은 이벤트 종류별 `switch` 디스패처. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Input/InputManager.cpp` | `InputManager::dispatchRawEvent` | 유지 — 이벤트 종류마다 장치 한 곳에 적용하는 짧은 `switch`(함수 주석에 이유) |
| `Source/Engine/Input/InputManager.cpp` | `InputManager::beginFrame` | 유지 — 프레임 단계 순서가 곧 의미인 선형 본문(함수 주석에 이유) |
| `Source/Engine/Input/Map/InputMapEvaluate.cpp` | `InputMap::evaluateBindingDown` | 유지 — 긴 케이스는 뺀 뒤 남은 종류별 짧은 `switch`(함수 주석에 이유) |
| `Source/Engine/Audio/AudioEngine.cpp` | `AudioEngine::applyCommand` | 유지 — 긴 케이스는 뺀 뒤 남은 명령별 짧은 `switch`(함수 주석에 이유) |
| `Source/Editor/Panels/Material/MaterialPanel.cpp` | `drawTypedProperty` | 유지 — 경우마다 짧은 `switch` 디스패처(함수 주석에 이유) |
| `Source/GameFramework/PlugIn/Editor/MeshModifiers/MeshModifiersPanel.cpp` | `MeshModifiersPanel::drawParam` | 유지 — 매개변수 종류마다 짧은 경우를 늘어놓은 `switch`(함수 주석에 이유) |
| `Source/GameFramework/Kits/Genre/Action/Fighting/FightingMatch.cpp` | `FightingMatch::updateFighter` | 유지 — 상태별 짧은 `switch` 디스패처(헤더에 이유) |
| `Source/GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespEphemeralStore.cpp` | `RespEphemeralStore::sendOperation` | 유지 — 연산별 짧은 `switch` 디스패처(헤더에 이유) |
| `Source/GameFramework/Base/Actor/Camera/CameraMode.cpp` | `CameraModeInternal::placeByMode` | 유지 — 카메라 모드별 짧은 분기 `switch`(함수 주석에 이유). `OrthoTopDown` 케이스가 가장 길다 |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHIDeviceInit.cpp` | `OpenGLRHIDevice::initializeInternal` | 유지 — 컨텍스트 · 확장 확인 · 기본 객체 생성의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHIDeviceInit.cpp` | `OpenGLRHIDevice::shutdownInternal` | 유지 — 생성의 역순으로 놓는 해제 목록(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHICommandContext.cpp` | `OpenGLRHICommandContext::beginRenderPass` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.cpp` | `OpenGLRHIResourceFactory::submitTextureUpload` | 유지 — 검사 · 링 복사 · 컨텍스트 빌려 올리기 · 펜스의 순서가 곧 의미(전송 계약 1 · 2 · 4). 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/GL/Platform/GlxPlatformContext.cpp` | `GlxPlatformContext::initialize` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/GL/Platform/WglPlatformContext.cpp` | `WglPlatformContext::initialize` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX11/D3D11RHIDeviceInit.cpp` | `D3D11RHIDevice::initializeInternal` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX11/D3D11RHIResourceFactory.cpp` | `D3D11RHIResourceFactory::createTexture2D` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIDeviceInit.cpp` | `D3D12RHIDevice::initializeInternal` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIDeviceDescriptor.cpp` | `D3D12RHIDevice::createGlobalResources` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIDeviceSubmission.cpp` | `D3D12RHIDevice::endFrame` | 유지 — 닫기 · 타임스탬프 해석 · 제출 · 펜스 · Present 의 프레임 끝 순서가 곧 의미이고 업로드 리스트를 프레임 리스트 앞에 끼우는 규칙이 한곳에 있어야 한다. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.cpp` | `D3D12RHIResourceFactory::createTexture2D` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.cpp` | `D3D12RHIResourceFactory::submitTextureUpload` | 유지 — 서브리소스 · 풋프린트 · 스테이징 복사 · 상태 전이의 순서가 곧 의미. 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/DX12/D3D12RHICommandContext.cpp` | `D3D12RHICommandContext::beginRenderPass` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.cpp` | `VulkanRHIDevice::initializeInternal` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.cpp` | `VulkanRHIDevice::shutdownInternal` | 유지 — 생성의 역순으로 놓는 해제 목록(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInit.cpp` | `VulkanRHIDevice::createInstance` | 유지 — 확장 · 레이어 확인 후 생성하는 백엔드 API 호출 순서가 곧 의미(창 시스템 길 분기는 짧다). 헤더 주석에 이유를 적어야 한다 |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInit.cpp` | `VulkanRHIDevice::createLogicalDevice` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceDescriptor.cpp` | `VulkanRHIDevice::createDescriptorResources` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceSubmission.cpp` | `VulkanRHIDevice::beginFrame` | 유지 — 프레임 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::createTexture2D` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::updateStructuredBufferRegions` | 유지 — 스테이징 확보 · 복사 · 장벽 · 제출의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::submitTextureUpload` | 유지 — 검사 · 링 복사 · 기록 · 제출의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.cpp` | `VulkanRHIResourceFactory::submitTextureReadback` | 유지 — 검사 · 링 할당 · 기록 · 제출의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactoryPipeline.cpp` | `VulkanRHIResourceFactory::createPipelineState` | 유지 — 서술을 단계 순서로 채운 뒤 만드는 선형 목록(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.cpp` | `VulkanRHISwapChain::create` | 유지 — 표면 능력 조회 · 형식 고르기 · 스왑체인 · 이미지 뷰 생성의 순서가 곧 의미(함수 주석에 이유) |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.cpp` | `VulkanRHICommandContext::beginRenderPass` | 유지 — 백엔드 API 호출 순서가 곧 의미(함수 주석에 이유) |

## 4. 사용자 결정

없습니다.
