# 긴 함수 정리 기준

> 상태: 구현중 · 선행: 없음 · 영역: project · 진행: 해당없음

100 줄이 넘는 함수를 언제 나누고 언제 그대로 두는지 정하고, 남은 함수의 판정을 적는 문서다. 나누기 자체는 코드 총량을 줄이지 않으므로 줄 수는 후보를 고르는 기준일 뿐이고, 나누는 이유는 아래 구조 문제여야 한다. 대부분의 영역은 이미 나눴고(빌드 미확인) 남은 것은 3절의 "나눔" 후보 열한 개뿐이다. 목록은 `py -3 Scripts/lint/report/RunFunctionSize.py --filter <경로>` 로 그때 잰다(줄 수를 손으로 적으면 코드가 바뀔 때 어긋난다). 게이트는 두지 않는다. 줄 수 상한을 게이트로 두면 이유 없는 기계적 분할이 늘어 총량이 오른다.

## 1. 나누는 기준

순서대로 보고 앞 기준에서 해결되면 뒤로 가지 않는다.

1. **중복부터 없앤다.** `RunDuplicateCode.py` 로 긴 함수 안의 반복 블록을 찾아 공용 함수나 표로 합친다. 총량이 줄어드는 유일한 경우다.
2. **다시 읽기와 두 번 검사를 없앤다.** 검사가 읽은 값을 실행이 다시 읽으면 한 번 읽은 타입 있는 값을 넘긴다(자동화 실행기가 대표 예, [DataDriven](DataDriven.md) 의 "자동화 단계는 한 번 읽는다").
3. **단계 경계에서 나눈다.** 파싱 · 검사 · 적용처럼 단계가 나뉘고 앞 단계의 지역 변수가 다음으로 안 넘어가면 단계마다 함수로 나눈다. 넘길 값이 많으면 단계 결과 구조체를 둔다.
4. **분기 사다리는 표로 바꾼다.** 중첩 깊이 4 이상이거나 같은 모양의 오류 반환이 반복되면 데이터 표나 `switch` 하나로 바꾼다.

그대로 두는 경우(이유를 함수 헤더 주석에 한 줄):
- 순서가 곧 의미인 선형 목록: 초기화 순서, 등록 목록, 프레임 단계 본문, 백엔드 API 호출 순서. 나누면 순서를 여러 곳에서 읽어야 한다.
- 경우마다 짧은 `switch` 디스패처.
- 생성 코드, X-macro 표, 테스트 본문, 셰이더 코드.

## 2. 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| 끝난 영역 | `Editor/Panels` · `Editor/Common` · `Editor/Viewport` · `Core/Network` · `Core/File` · `GameFramework/Kits` · `GameFramework/Base` · `Engine/Input` · `Destruction` · `Character` · `Audio` · `Localization` · `Physics` · `Games/Empty` · `Engine/Graphics` · `Renderer`, 직렬화 · 애니메이션 · 씬 · 텍스트 · 네비 · 에디터 임포터 · 엔진 갱신 등 약 100 개 | 코드 끝남, 빌드 미확인 |
| 남은 영역 | 3절의 "나눔" 후보. 영역 하나가 단위 하나이고 그 폴더를 다른 작업이 건드리는 중이면 미룬다 | 남음 |

- `drawAdaptiveGrid` 는 테마 토큰 작업(editorux) 때 함께 나눈다.
- 핫 패스 함수를 나눌 때만 Release 전후를 잰다.
- 나눈 함수는 이 문서에서 지우고, 유지로 정한 함수는 함수 헤더 주석에 이유를 적는다. 사용자 결정은 없다.

## 3. 목록 (본문 100 줄 초과 71 개, 2026-10-25)

### 나눔 후보

"예비" 는 본문을 다 읽지 않고 단계 주석 · 케이스 모양으로 정한 것이라 맡은 작업자가 읽고 확정한다. 기준 1(중복)은 어느 함수든 먼저 본다.

| 파일 | 함수 | 판정 |
|---|---|---|
| `Source/Engine/Input/Map/InputMapSerialization.cpp` | `InputMap::loadDefinition` | 기준 3(예비): 장치 · 액션 · 바인딩 · 맥락 읽기 |
| `Source/Engine/Profiling/ProfilerTraceExport.cpp` | `ProfilerTraceExport::makeTraceJSON` | 기준 3(예비): 머리말 · 스레드 이름 · 이벤트 · 카운터 쓰기 |
| `Source/Engine/Renderer/Scene/GPUMeshMorphPool.cpp` | `GPUMeshMorphPool::update` | 기준 3(예비): 요청 모으기 · 슬롯 배정 · 업로드 |
| `Source/Engine/Renderer/Vfx/VfxGPUSimulation.cpp` | `VfxGPUSimulation::prepareFrame` | 기준 3(예비): 파티클 배치 · 파티클 라이트 · 메시 방출기 · 지난 재생 놓기 |
| `Source/Engine/Renderer/Light/GPULightBuffer.cpp` | `GPULightBuffer::update` | 기준 3: 프레임 상한 나누기 · 업로드 목록 · 버퍼 확보 · 프로브 SH 올리기 |
| `Source/Editor/Panels/Catalog/CatalogTableEditor.cpp` | `CatalogTableEditor::draw` | 기준 3(예비): 그룹마다 표를 그리는 `for` 본문을 그룹 하나 그리기로 |
| `Source/Editor/Panels/Inspector/InspectorPropertyManager.cpp` | `drawValueWidget` | 기준 4(예비): 값 타입별 분기를 위젯 함수 · 오버로드로 |
| `Source/Editor/Panels/Inspector/EditorPropertyGrid.cpp` | `EditorPropertyGrid::drawTypeMethods` | 기준 3(예비): 메서드 하나 · 인자 입력란 · 호출 버튼 |
| `Source/Engine/Graphics/Texture/TextureCache.cpp` | `TextureCache::flushRegionUpdates` | 기준 3(예비): 요청 하나 내기 · `Busy` / `Rejected` 규칙 · 미룬 것 합치기 |
| `Source/Engine/Renderer/Canvas/CanvasRenderer.cpp` | `CanvasRenderer::uploadAtlasPage` | 기준 3(예비): 첫 전체 업로드 · 등록 · 구간 업로드 · 실패 규칙 |
| `Source/Engine/Destruction/FractureComponentBase.cpp` | `FractureComponentBase::updateGroups` | 기준 3(예비): 잠든 덩어리 정리 · 예산 초과 제거 순서 |
| `Source/Engine/Graphics/Shader/Compile/ShaderCookStamp.cpp` | `verifyDomain` | 기준 3(예비): 우선순위 폴더 · 헤더 스탬프 · 키 누락 판정 |
| `Source/Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.cpp` | `VulkanRHISwapChain::createSurface` | 기준 4(예비): 창 시스템 길(Win32 · Xlib · Xcb · Wayland)마다 표면 만들기 함수 |

### 유지

판정은 모두 "유지"이고 이유 몇 가지로 갈린다.

- **프레임 · 기동 순서가 곧 의미**: `App::run` · `App::initialize` · `Engine::initialize`(호스트 범위 → 엔진 → 헤드리스 → 자동화 → 설정 감시 순) · `EngineSubsystemSet::addStartupSubsystems`(순서와 범위가 의미) · `RenderThread::executeFrameBody`(단계별 측정 범위 주석과 함께 읽혀야 함) · `InputManager::beginFrame` · `GPUSceneBuilder::populateMeshesFromScene`(단계는 이미 나뉘고 남은 것은 단계를 고르는 결정 순서) · `ShaderCompiler.cpp` 의 `compileDxc`(인자 글의 수명이 이 함수 안).
- **백엔드 API 호출 순서가 곧 의미**:
  - DX11: `D3D11RHIResourceFactory::createPipelineState` · `createTexture2D`, `D3D11RHIDevice::initializeInternal`.
  - DX12: `D3D12RHIDevice::initializeInternal` · `createGlobalResources` · `endFrame`(업로드 리스트를 프레임 리스트 앞에 끼우는 규칙이 한곳에), `D3D12RHIResourceFactory::createTexture2D` · `submitTextureUpload`, `D3D12RHICommandContext::beginRenderPass`.
  - Vulkan: `VulkanRHIDevice::initializeInternal` · `createInstance` · `createLogicalDevice` · `createDescriptorResources` · `beginFrame`, `VulkanRHIResourceFactory` 의 `createTexture2D` · `updateStructuredBufferRegions` · `submitTextureUpload` · `submitTextureReadback` · `createPipelineState`, `VulkanRHISwapChain::create`, `VulkanRHICommandContext::beginRenderPass`.
  - OpenGL: `OpenGLRHIDevice::initializeInternal`, `OpenGLRHICommandContext::beginRenderPass`, `OpenGLRHIResourceFactory::submitTextureUpload`(전송 계약 1 · 2 · 4), `GlxPlatformContext::initialize` · `WglPlatformContext::initialize`.
  - 해제 목록(생성의 역순): `OpenGLRHIDevice::shutdownInternal` · `VulkanRHIDevice::shutdownInternal`.
- **짧은 `switch` 디스패처**(긴 케이스는 이미 뺐다): `FrameRenderer::executePass` · `InputManager::processNativeEvent`(Win32 · X11) · `InputManager::dispatchRawEvent` · `InputMap::evaluateBindingDown` · `AudioEngine::applyCommand` · `drawTypedProperty`(`MaterialPanel`) · `MeshModifiersPanel::drawParam` · `FightingMatch::updateFighter` · `RespEphemeralStore::sendOperation` · `CameraModeInternal::placeByMode`(`OrthoTopDown` 케이스가 가장 길다).
  - `BehaviorTreeRunner::tickNode`: 12 종류 `switch` 의 케이스가 평균 9 줄이고 합성 노드는 이미 뺐다. 더 가르면 같은 모양의 함수만 늘어난다.
  - `MemoryEphemeralDatabase::executeValue`: 연산 7 개 `switch`(케이스 10 ~ 24 줄)이고 연산 규칙(RESP 와 같은 동작)을 한곳에서 읽는 값이 있다. `Increment` 가 더 길어지면 그 케이스를 뺀다.
- **표 · 등록 목록**(경우마다 한 줄): `makeInfo`(`RenderPassTypeInfo`) · `buildReservedBindings`(`ShaderBindingValidator`) · `InputKeyTable::mapWin32VirtualKey` · `mapX11KeySym` · `mapScanCodeToKey`.
- **한 알고리즘**: `SceneVoxelGrid::traceRay` — 슬랩 판정 · 시작 칸 · 칸 걷기가 지역 변수를 모두 공유하고 103 줄이라 경계다. 구조체를 둘 만큼의 이득이 없다.

헤더 주석에 유지 이유를 아직 못 적은 함수: `SceneVoxelGrid::traceRay` · `addStartupSubsystems` · `App::initialize` · `InputManagerX11 processNativeEvent` · `OpenGLRHIResourceFactory::submitTextureUpload` · `D3D12RHIDevice::endFrame` · `D3D12RHIResourceFactory::submitTextureUpload` · `VulkanRHIDevice::createInstance`.
