# 다중 월드 · 에디터 툴 창 구현 계획 (D3-7)

> 상태: 구현중 · 선행: 없음 · 영역: editor · 옛번호: D3-7 · 진행: 5/6

옛 `D3-7` 의 설계 근거입니다. 다른 PC 에서 병합할 때 판단 기준으로 씁니다. 단계 1~4 와 단계 5 의 머티리얼 창 · 프리팹 창이 코드로 들어갔습니다(각 단계의 "구현됨" 참고).
파일 · 함수 이름은 2026-10-14 `wt/editor` 기준으로 읽은 것입니다.

## 1. 현재 구조

### 1.1 프레임 경로 — 활성 씬 하나

- `Source/Engine/Engine.h/.cpp` 의 `Engine::update( deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate&, bool bTickScene )` 가 한 프레임을 돕니다.
  씬을 고르는 곳은 모두 `_pSceneManager->getActiveScene()` 입니다(`makeFrameContext`, "Scene update" 블록, `SceneDebugDraw::update`, 패킷 단계).
- 씬 틱은 `SceneManager::update( deltaTime )` 하나입니다. `bTickScene` 이 false 면(에디터 정지) 건너뛰고 `_sceneDeltaSeconds` 는 0 입니다.
- 패킷은 `RenderPacketBuilder::beginPacket` → `populateScene( packet, views, sceneViewCameraProvider, pActiveScene, … )` → `populateCanvas` → 렌더 스레드 제출 순서입니다.
  `populateScene` 은 씬 포인터를 **하나** 받습니다.

### 1.2 GPUScene — 빌더 하나, 스냅샷 하나, GPU 측 하나

| 단계 | 타입 · 소유자 | 개수 |
|---|---|---|
| GT 빌드 | `GPUSceneBuilder` — `RenderPacketBuilder::_gpuSceneBuilder`(unique_ptr) | 1 |
| 전달 | `GPUSceneSnapshot` — `RenderFramePacket::_gpuScene` | 패킷당 1 |
| RT 업로드 · 컬링 | `GPUScene` — `FrameRenderer::_gpuScene`(`getGPUScene()`) | 1 |

컬링 산출물(간접 인자 · 가시 인스턴스 목록)은 `GPUScene::getCullView` 로 뷰마다 나뉘지만, 인스턴스 · 머티리얼 · 모프 풀은 GPUScene 하나에 모입니다.

### 1.3 뷰 — 이미 여러 개지만 모두 같은 씬

- `RenderViewRequest`(`Renderer/Frame/View/RenderView.h`)는 "값만 가지며 씬을 가리키지 않는" 추가 뷰입니다. 출력은 `RenderViewOutputKind`(렌더 텍스처 · `HostTarget` · 화면 사각형)이고 `_viewID` 로 뷰별 상태를 찾습니다.
- 호스트(에디터)가 넘기는 것은 `HostViewTargets` 의 `_game` · `_scene` **두 슬롯 고정**입니다.
- `RenderViewCollector` · `RenderViewScheduler` 가 카메라에서 추가 뷰를 모으고 갱신 주기 · 예산으로 `_bRender` 를 정합니다.

### 1.4 씬 · 오브젝트 · 물리

- `SceneManager`(`Engine/Scene/SceneManager.h`)는 `_listLoadedScene`(vector<unique_ptr<Scene>>)을 가지고 있으며, 그중 `_pActiveScene` 하나가 틱 · 렌더 대상입니다.
- `Scene` 은 `GameObjectManager` 를 소유합니다(`getObjectManager()`).
- 물리는 **이미 씬별**입니다. `ScenePhysics` 는 씬 서비스(`SW_SCENE_SERVICE`)라 `GameObjectManager` 마다 하나 생기고, `getScene3D()` 가 처음 부를 때 `PhysicsSystem::createScene3D()` 로 자기 `IPhysicsScene3D` 를 만듭니다. 고정 스텝은 `FixedStepAccumulator` 이고 스텝 크기는 전역 `EngineConfig::getActive()._fixedDeltaTime` 입니다.
- 시간 배율은 전역 `GameTimeScale`(`Engine/Config/GameTimeScale.h`) 하나입니다.
- 카메라 선택은 `GameObjectManager::getCameraRegistry()` 로 씬별입니다.

### 1.5 에디터

- `EditorRenderViews`(`Editor/Common/Workspace/EditorRenderViews.h`)는 `EditorViewKind` 로 고른 고정 배열(`_arrViewTarget`)의 오프스크린 타깃을 관리합니다(`ensureViewTargetSize` · `markViewDrawn` · `wasViewDrawn`). 게임 뷰 · 씬 뷰만 있습니다.
- 프리팹 격리(단계 5 이전) — `EditorPrefabIsolationCommands::enterPrefabIsolation( prefabPath )` · `exitPrefabIsolation( bSaveToPrefab )`. 활성 씬의 다른 오브젝트를 숨기고 스택은 워크스페이스의 `PrefabIsolationFrame` 이 가지고 있었습니다. 활성 씬을 빌리므로 씬이 더티면 막았습니다. 단계 5 에서 프리팹 창으로 옮겼습니다.
- 머티리얼 미리보기 — `Panels/Material/MaterialPreviewShading.h` 가 구의 한 점 색을 **CPU 에서** 셈합니다(렌더러를 쓰지 않음). 실제 렌더링 결과는 메인 뷰포트의 선택 오브젝트에 머티리얼을 입혀 봅니다.
- 선택은 `EditorSelection`(EditorSubsystem 하나), Undo 는 `EditorTransaction` → `CommandStack` 하나로 에디터 전체에 하나입니다.
- 애셋 문서 패널의 기반은 `EditorDocumentPanel`(포커스 경로 → 로드, 더티 확인 팝업)입니다. 카메라는 `EditorCamera`, 기즈모는 `Viewport/EditorViewportGizmo` 입니다.

### 1.6 요약 — 무엇이 하나뿐인가

| 대상 | 지금 | 목표 |
|---|---|---|
| 씬 틱 | 활성 씬 하나 | 월드마다 |
| 물리 월드 | 씬별(이미 됨) | 그대로, 월드 틱 정책을 따름 |
| 시간 배율 | 전역 `GameTimeScale` | 월드마다 |
| GPUSceneBuilder · 스냅샷 · GPUScene | 하나 | 그리는 월드마다 |
| 호스트 뷰 | `_game` · `_scene` 두 슬롯 | 가변 목록 |
| 선택 · Undo | 에디터 전체 하나 | 창(문서)마다 |

## 2. 목표 구조

### 2.1 미리보기 월드

- 새 타입 `World`(가칭, `Engine/Scene/World.h`)를 두지 않고 **`Scene` 을 월드 단위로 씁니다.** `Scene` 이 이미 오브젝트 매니저 · 물리 · 카메라를 씬별로 가지고 있기 때문입니다.
- `SceneManager` 에 "활성 씬" 과 별도로 **미리보기 씬 목록**을 둡니다. 미리보기 씬은 저장 · 전환 · 영속 오브젝트(`markPersistent`)에 끼지 않습니다.
- 씬마다 `SceneTickPolicy`(가칭): `{ bTick, timeScale, bPhysics, fixedStepOverride }`. 프리팹 창은 틱 안 함 · 물리 안 함, 애니메이션 창은 틱만, 래그돌 창은 물리까지.
- 조명 환경: 미리보기 씬은 기본 환경 프리셋(방향광 하나 · 스카이 · 바닥)을 씬 안의 오브젝트로 스폰합니다. 별도 "조명 환경" 타입은 만들지 않습니다.
- 시간: 전역 `GameTimeScale` 은 게임 월드 전용으로 두고, 미리보기 씬은 정책의 `timeScale` 을 곱한 값을 `SceneManager::update` 에서 넘깁니다.

### 2.2 월드별 렌더

- `RenderViewRequest` 와 호스트 뷰에 **씬 키**(`_sceneID`, uint32)를 더합니다. 뷰는 "어느 씬 · 어느 카메라 · 어느 렌더 타깃" 을 고릅니다.
- `HostViewTargets` 의 두 슬롯은 남기고(게임 · 씬 뷰), 그 옆에 `vector<HostSceneView>`(씬 포인터 · 카메라 행렬 · 타깃)를 더합니다. 안 보이는 창은 목록에 넣지 않으므로 그리지 않습니다.
- `RenderFramePacket` 은 스냅샷을 씬마다 하나씩 싣습니다(`vector<GPUSceneSnapshot>` 또는 주 씬 + 보조 목록). `GPUSceneBuilder` 도 씬마다 하나(이전 프레임 차이 추적 상태가 씬별이기 때문).
- RT 측 `GPUScene` 도 씬마다 하나를 `FrameRenderer` 가 키로 관리합니다. 미리보기 씬은 오브젝트 수가 적으므로 작은 초기 용량으로 만듭니다. 머티리얼 레지스트리(`GPUMaterialRegistry`) · 메시 정점 풀(`GPUMeshVertexPool`)은 **공유**(bindless 인덱스가 전역이라 나누면 메모리만 늡니다).
- 렌더 그래프는 씬 뷰마다 한 번 돕니다. 그림자 · 후처리는 미리보기 뷰에서 설정(`RenderViewSettings`)으로 줄일 수 있게 합니다.

### 2.3 툴 창 틀

- `EditorPreviewPanel`(가칭, `Editor/Common/GUI/`)이 `EditorDocumentPanel` 을 상속합니다. 가지고 있는 것: 미리보기 씬 핸들, 자기 렌더 타깃(`EditorRenderViews` 를 `EditorViewKind` 고정 배열에서 id 키 목록으로 바꿈), `EditorCamera` 인스턴스, 기즈모 상태, `EditorSelection` 인스턴스, `CommandStack` 인스턴스.
- 포커스된 창의 선택 · Undo 스택이 Ctrl+Z · 인스펙터 대상이 됩니다. 문서 계약(더티 · 저장 · 닫기 확인)은 `EditorDocumentPanel` 의 것을 씁니다.
- 키트 · 게임이 창을 등록하는 길은 1-4 의 C 확장 지점(패널 등록) 위에 둡니다 — 새 등록 방식을 만들지 않습니다.

## 3. 상용 엔진과 비교

아래는 기억에 기반한 정리이며 소스를 다시 확인하지 않았습니다.

| 항목 | 언리얼 | 유니티 | 이 계획 |
|---|---|---|---|
| 미리보기 월드 | `FPreviewScene` 이 자기 `UWorld`(EWorldType::EditorPreview) · 조명 · 스카이를 가지고 있음 | `PreviewRenderUtility` 가 숨은 씬 · 카메라 · 라이트를 만듦 | `Scene` 을 미리보기 씬으로 씀 |
| 렌더 | 월드마다 `FScene` | 숨은 씬을 카메라로 `RenderTexture` 에 | 씬마다 `GPUScene` |
| 툴 창 | `FAssetEditorToolkit` + `SEditorViewport` · 탭 | `EditorWindow` · `Editor.CreatePreviewSceneScene` | `EditorPreviewPanel` |
| 프리팹 격리 | 블루프린트 에디터의 뷰포트(별도 월드) | Prefab Stage — 별도 씬, 저장 시 반영 | 3단계에서 별도 씬으로 바꿈 |
| Undo | 전역 `GEditor->Trans`(트랜잭션 버퍼 하나), 애셋 단위 컨텍스트 | 전역 Undo, 씬 단위 그룹 | 창마다 `CommandStack` |

Undo 를 창마다 두는 것은 두 엔진과 다릅니다. 두 엔진 모두 전역 스택 하나에 대상 이름을 붙입니다. 창마다 두면 "창을 닫으면 그 창의 기록이 사라진다" 는 규칙이 단순하고, 미리보기 씬이 사라진 뒤 남은 명령이 죽은 오브젝트를 가리키는 일이 없어 이쪽을 고릅니다(되돌리기 어려운 결정 4).

## 4. 단계 분할

각 단계는 끝나면 `cmake --build --preset Ninja-Debug --target all AllTests` 가 경고 0 으로 빌드되고 기존 테스트가 통과하는 크기입니다.

### 단계 1 — 씬 틱 정책과 미리보기 씬 목록 (엔진, 렌더 변경 없음)
- 파일: `Engine/Scene/SceneManager.h/.cpp`(미리보기 목록 · `createPreviewScene` · `destroyPreviewScene` · 틱), `Engine/Scene/Scene.h`(틱 정책 필드), `Engine/Engine.cpp`("Scene update" 블록).
- 위험: 씬 서비스(`ScenePhysics` · `SceneAudio` · `SceneNavigation`)가 전역 단일 씬을 가정한 곳. 오디오는 미리보기 씬에서 끕니다.
- 확인: 새 단위 테스트 — 미리보기 씬이 정책대로 틱 · 물리 스텝을 하고 활성 씬 전환에 영향받지 않음. 씬 전환 테스트 기존 결과 유지.
- **구현됨(2026-10-15)**: `SceneManager` 의 미리보기 씬 목록(`createPreviewScene` · `destroyPreviewScene` · 키 `getPreviewSceneKey` · `findPreviewScene` · `tickPreviewScenes`),
  정책 `ScenePreviewPolicy { _bTick · _timeScale · _bPhysics · _bAudio · _bPlay }`, 툴이 겨누는 초점 씬(`focusScene` · `getFocusedScene`, 내려지면 활성 씬으로 복귀).
  정책의 물리는 `GameObjectManager::setPhysicsStepEnabled`, 오디오는 `Scene::setAudioUpdateEnabled` 로 닿는다. `EngineLoop` 는 `bTickScene` 과 무관하게 `tickPreviewScenes` 를 부른다.
  월드별 서브시스템 범위는 씬마다 `GameObjectManager` 가 갖는 `SubsystemScope` 라 따로 만들 것이 없고, 씬을 내리면 함께 내려간다(테스트 `PreviewScenesLiveApartFromLoadedScenes`).
  `fixedStepOverride` 는 `FixedStepAccumulator` 가 전역 설정을 읽어 넣지 않았다. 조명 환경 프리셋은 `EditorPreviewEnvironment`(방향광 · 바닥, 단계 4)로 들어갔고 스카이는 엔진에 스카이 컴포넌트가 없어 빠졌다.
  미리보기 씬은 디바이스가 있으면 만들 때 `Scene::initialize( pRHIDevice )` 를 부른다.

### 단계 2 — 씬별 GPUScene (렌더러)
- 파일: `Renderer/Frame/Packet/RenderFramePacket.h`, `RenderPacketBuilder.h/.cpp`(빌더를 씬 키 맵으로), `FrameRenderer.h` · `FrameRendererViews.cpp` · `FrameRendererDraw.cpp`(`_gpuScene` → 씬 키 맵), `Renderer/Scene/GPUScene.*`(공유 풀을 외부 참조로), `Renderer/Frame/View/RenderView.h`(`_sceneID`).
- 위험: 가장 큽니다. `CheckRenderOwnership.py` 의 소유 규칙, 컬링 슬롯 수를 업로드 전에 정해야 하는 제약(`FrameRenderer` 463행 주석), bindless 인덱스 해제 지연. 네 백엔드 모두 영향.
- 확인: RenderPass GPU 테스트 · 이미지 비교 테스트가 그대로(주 씬만 있을 때 동일 이미지), 새 테스트 — 두 씬을 두 타깃에 그려 서로 섞이지 않음. `Scripts/dev/BackendSmoke.py` 로 네 백엔드. Shipping 도 확인.
- **구현됨(2026-10-15, 모양을 바꿈)**: `FrameRenderer` 안의 `_gpuScene` 을 씬 키 맵으로 바꾸지 않고, **미리보기 씬마다 `FrameRenderer` 인스턴스 하나**를 주 렌더러가 소유한다
  (`FrameRenderer::executeScenePreviewPacket` · `retireScenePreviews`, `FrameRendererScenePreview.cpp`). GT 는 씬마다 `GPUSceneBuilder` 하나(`RenderPacketBuilder::populateScenePreviews`),
  패킷은 `RenderFramePacket::_listScenePreview`(미리보기 패킷 목록, 저장소 재사용). 렌더 스레드는 주 패킷 뒤에 미리보기 타깃을 열고 → 그 씬의 렌더러로 그리고 → 셰이더 읽기로 돌린다(`RenderThread::executeFrameBody`).
  까닭: 주 렌더러의 핫 패스(컬링 슬롯 · 패스 상수 · 트랜지언트 · `_pActiveView`)를 건드리지 않아 주 씬 이미지가 바뀔 여지가 없다. 대가로 머티리얼 · 정점 풀 · PSO 캐시가 렌더러마다 따로다(메모리 · 첫 프레임 PSO 생성 비용 — 결정 2 와 어긋남, 결정 기록 5-11).
  미리보기 렌더러는 프로파일러 · 타임스탬프 · 렌더 타깃 공개를 하지 않는다(`_bScenePreview`).

### 단계 3 — 호스트 뷰 목록과 `EditorRenderViews` 일반화
- 파일: `RenderView.h`(`HostSceneView` 목록), `Engine.h/.cpp`(`tick` 인자), `Editor/Common/Workspace/EditorRenderViews.h/.cpp`(id 키 타깃), 에디터 메인 루프의 `tick` 호출부.
- 위험: 에디터 없는 실행(백버퍼 경로)과 서버. 두 슬롯 경로를 그대로 두어 줄입니다.
- 확인: 에디터 스모크(`-gv_editorPanelDump -gv_profileFrames=60`), 게임 단독 실행 이미지 동일.
- **구현됨(2026-10-15)**: `HostViewTargets` 에 고정 배열 `_arrSceneView[kMaxHostSceneView]` · `_sceneViewCount`(호스트가 프레임마다 새로 채우므로 힙을 피했다), 값 타입 `HostSceneView`(씬 키 · 뷰-투영 · 눈 · 설정 · 타깃).
  C ABI 는 `EditorAPI::getSceneViews`(모듈 ABI v6). `EditorRenderViews` 는 두 고정 슬롯을 두고 그 옆에 id 키 미리보기 타깃 목록(`allocatePreviewTarget` · `ensurePreviewTargetSize` · `submitPreviewView` · `collectSceneViews`)을 더했다.

### 단계 4 — `EditorPreviewPanel` 틀과 창별 선택 · Undo
- 파일: `Editor/Common/GUI/EditorPreviewPanel.h/.cpp`(새), `EditorSelection.*` · `EditorTransaction.*`(전역 인스턴스 → 포커스 창의 인스턴스를 고르는 함수), `EditorCamera`, `Viewport/EditorViewportGizmo`(대상 씬을 인자로).
- 위험: 전역 선택을 직접 부르는 패널이 많습니다. 먼저 "현재 문서의 선택" 접근자 하나로 모은 뒤 바꿉니다.
- 확인: 에디터 시나리오 테스트(선택 · Undo 기존 시나리오), 새 시나리오 — 미리보기 창에서 Undo 해도 메인 씬 기록이 그대로.
- **구현됨(2026-10-15, 일부)**: `EditorPreviewViewport`(미리보기 씬 · 조명 환경 · 오빗 카메라 · 타깃)와 `EditorPreviewPanel`(문서 패널 + 뷰포트 + 창의 `CommandStack` · 선택 목록).
  창이 포커스를 얻으면 `SceneManager::focusScene` 과 `editor::setFocusedCommandStack` 을 걸고, Ctrl+Z · Ctrl+Y 는 `editor::getFocusedCommandStack` 을 되돌린다.
  `ObjectUndoUtil` 의 명령은 기록할 때의 씬 키를 기억해(`SceneManager::getPreviewSceneKeyOf`) 그 씬에서 되돌린다(테스트 `SceneTest.PreviewObjectUndoStaysInItsScene`).
  **창 범위 선택(2026-10-18)**: `EditorSelection` 은 타입으로 찾지 못한다(`kAccessorOnly` → `kIsAccessorOnlySubsystem` 이 `findSubsystem` · `EditorContext::getSubsystem` 을 컴파일에서 막는다).
  호출부는 `editor::getMainSceneSelection`(메인 월드를 보여 주는 창 · 탐침) · `getFocusedSelection`(인스펙터 · 전역 명령) · `findSelectionOf`(오브젝트를 받아 고치는 명령) 중 하나로 어느 선택인지 적는다.
  오브젝트 기록(`EditorTransaction::recordModify` · `recordObjectLifetime`)은 **오브젝트가 사는 씬**이 스택을 고른다 — 미리보기 씬이면 그 창의 스택, 메인 씬은 dirty 가 아니다.
  묶음(`beginTransaction`)은 전역 · 창 스택 모두에 걸고 빈 묶음은 스택이 버린다. 씬 뷰 · 계층이 포커스를 얻으면 겨누는 선택이 메인으로 돌아온다.
  인스펙터의 미리보기 갈래는 구조 편집(컴포넌트 추가 · 제거)도 내고, 미리보기 뷰포트의 클릭은 창 선택에 놓는다(`EditorPreviewPickUtil`). 테스트 `EditorSelectionScopeTest.*`.

### 단계 5 — 첫 창 두 개: 머티리얼 · 프리팹
- 파일: `Panels/Material/MaterialPanel.*`(구 미리보기를 미리보기 씬으로, `MaterialPreviewShading` 은 썸네일 폴백으로 남김), `EditorPrefabIsolationCommands.*`(활성 씬 대신 미리보기 씬에 프리팹을 스폰, 더티 막기 제거).
- 위험: 프리팹 저장 경로가 바뀝니다 — 중첩 격리 스택(`PrefabIsolationFrame`)의 의미를 유지해야 합니다.
- 확인: 프리팹 격리 기존 테스트, 머티리얼 미리보기 이미지 비교(끈 판과 달라야 함도 확인).
- **구현됨(2026-10-15, 머티리얼만)**: `MaterialPanel` 이 `EditorPreviewPanel` 을 상속해 미리보기 씬에 구(`PreviewSubject`)를 놓고 문서의 머티리얼(캐시 사본)을 건다. CPU 구(`MaterialPreviewShading`)는 탐침 · 폴백으로 옆에 남겼다.
  시나리오 `editor/previewscene.scenario.xml` + `-gv_editorPreviewScreenshot`.
- **구현됨(프리팹 창)**: `Panels/Prefab/PrefabEditorPanel`(`EditorPreviewPanel` 파생, id `prefab_window`, 제목 "Prefab Window")이 프리팹을 **자기 미리보기 씬**에 스폰해 편집한다. 메인 씬의 `PrefabPanel`(오버라이드 도구)과 따로 둔다.
  - 격리 스택은 전역 워크스페이스가 아니라 **창이 든다**(`Common/Commands/Scene/EditorPrefabIsolation`, ImGui 없음). 맨 아래 단계 = 문서 루트, 그 위 = 중첩 프리팹. 중첩 단계는 문서 루트 아래의 다른 오브젝트만 숨기고
    조명 · 바닥(문서 루트 밖)은 건드리지 않는다. 스택이 창의 씬과 함께 살고 죽으므로 메인 씬 세대가 바뀌어도(`EditorSceneGenerationSync`) 영향이 없다. `EditorPrefabIsolationCommands` 와 워크스페이스의 격리 상태, 더티 막기(`requiresCleanSceneForPrefabIsolation`)는 없앴다.
  - 놓인 프리팹 표는 이미 씬마다였고(`Scene::setEntityPrefabPath`), 고친 것은 읽고 쓰는 쪽이다 — `EditorWorkspace::setPlacedPrefabPath` · `getPlacedPrefabPath` 가 오브젝트 id 대신 `GameObject` 를 받아 **오브젝트가 사는 씬**(`editor::findSceneOf`)의 표를 쓴다.
    id 만 받던 옛 형태는 활성 씬으로 읽어 미리보기 씬의 인스턴스를 놓쳤다.
  - 저장은 지금 단계의 루트를 그 단계의 프리팹 파일에 쓴다(`EditorInspectorCommands::applyToPrefab`). 창의 편집은 창의 Undo 스택 · 선택에 남는다. 열기는 `PrefabEditorPanel::openWindow`(문서 포커스)이고 중첩은 `requestNestedIsolation`.
  - 테스트 `EditorPrefabIsolationTest.*` · `EditorWorkspaceTest.PrefabPathOfAPreviewSceneObjectLivesInThePreviewScene`(Debug 전체 빌드 경고 0, `Editor*` 스위트 EditorTest 300 · EditorUITest 6 통과). **확인 못 함**: 창을 실제로 연 화면(GPU)과 에디터 시나리오 — 아래 6절.
  - 알려진 한계: 없음(콘텐츠 브라우저 열기와 X 닫기 확인은 6절 1번에서 해소했다). 화면 확인(GPU)만 남았다.

### 단계 6 이후 — 나머지 창
메시/모델(LOD 비교) · 애니메이션 · 카메라 프리셋 · 래그돌 · 이펙트 · 장르 도구 · 캐릭터 편집 창([EditorCharacterEditor.md](EditorCharacterEditor.md)). 창마다 단위 하나.

## 5. 되돌리기 어려운 결정

1. **월드 = `Scene`** — 별도 `World` 타입을 두지 않습니다. 나중에 한 월드에 여러 씬(레벨 스트리밍)을 넣으려면 이 결정을 다시 봐야 합니다.
2. **GPUScene 을 씬마다, 머티리얼 · 정점 풀은 공유** — 풀을 나누면 bindless 인덱스 공간이 갈라져 셰이더 계약이 바뀝니다.
3. **패킷 형식에 씬 목록** — `RenderFramePacket` 은 GT/RT 계약이라 형식을 바꾸면 캡처 · 리플레이 도구도 따라 바뀝니다.
4. **창마다 Undo 스택** — 전역 스택으로 돌아가려면 명령에 대상 씬을 붙이는 작업이 다시 필요합니다.
5. **`HostViewTargets` 두 슬롯을 남기고 목록을 더함** — 게임 · 씬 뷰는 특별 취급이 남습니다. 모두 목록으로 합치면 깔끔하지만 에디터 없는 실행 경로까지 바뀝니다.

## 6. 남은 것

(번호는 옛 `D3-7` 항목의 (1)~(6) 을 그대로 둔다. (2)는 단계 2 로 들어갔다.)

단계 1~4 와 머티리얼 창은 들어갔습니다(각 단계의 "구현됨"). 규모가 XL 이라 에디터 보강 묶음에서 뺐고 설계부터 따로 짰습니다(2026-10-14 사용자 결정).

1. **프리팹 창의 남은 것** — 본체는 단계 5 의 "구현됨". 콘텐츠 브라우저의 프리팹 열기는 새 창으로 돌렸고(종류 테이블의 프리팹 패널 제목 = "Prefab Window", 오버라이드 도구 `PrefabPanel` 은 "Prefab Overrides" 로 따로 둠),
   X 로 닫을 때의 미저장 확인(Save / Don't Save / Cancel, 저장 실패면 열어 둠)도 들어갔다. 둘 다 GPU 없이 테스트(`EditorPanelDocumentTest` 의 닫기 상태 전이 · `EditorAssetType` 제목 단언)했다.
   **닫기 확인은 프리팹 창 것도 문서 패널 것도 아니라 모든 패널 공통이고, 닫는 길은 하나다**(2026-10-20). 확인은 `IEditorPanel` 에 있다. `setOpen( false )` 가 `requestClose` 라서 미저장 편집이 있으면 열어 둔 채 Save / Don't Save / Cancel 을 기다리고,
   X 는 `draw` 가 창을 연 직후 `absorbCloseButton` 으로 같은 길에 넣는다(접힌 창 · 숨은 탭의 X 도 — 모달은 창 밖에서 그린다). 그래서 문서 패널이 아닌 패널 중 편집 상태가 있는 것(Data Table · Project Settings · Catalog · Input Map · UI Preview)과
   패널 메뉴 토글 · 이름 붙인 레이아웃 불러오기 · 최대화 · 프리팹 창의 Close · `panel.close` 가 모두 같은 확인을 받는다. 닫는 길마다 확인을 손으로 부르는 코드는 없다.
   편집이 패널 밖에 살아 닫아도 잃는 것이 없는 패널만 `asksBeforeClose` 로 빠진다 — 지금은 전역 변수 창 하나(값이 관리자에 바로 적용되어 있어, 확인을 두면 저장하거나 값을 초기화하지 않고는 창을 숨길 수 없다). 편집 상태가 없는 패널은 dirty 가 없어 달라지지 않는다.
   에디터 종료는 원래 `EditorSceneFileCommands::tryBeginQuit` 가 열린 · 닫힌 패널의 dirty 를 모두 세어 종료 확인(Save All / Don't Save / Cancel)을 거치므로 그대로다. `panel.resetOptional` 과 모듈 언로드(`releasePanelsWithin`)는 패널을 닫는 것이 아니라 인스턴스를 버리는 것이라(문서에 적힌 대로 편집을 버린다) 묻지 않는다 — 시나리오 체인의 초기화가 쓴다.
   "Don't Save" 는 `revertDocument` 로 편집을 버린다 — 문서 패널의 기본은 문서를 다시 읽기(`reloadDocument`), 프리팹 창은 디스크 프리팹으로 문서 루트를 다시 세움(재정의). 미리보기 씬의 편집은 창 Undo 스택이 늘고 줄 때 기반(`EditorPreviewPanel::syncDocumentDirty`, 이제 `IEditorPanel` 의 훅)이 dirty 로 표시하므로 파생이 따로 알리지 않는다.
   `EditorDocumentPanel::drawContent` 가 `final` 이고 `drawDocumentContent` 를 부르는 구조는 남겼다(닫기 확인을 지키려던 것이 아니라 파생 이름을 되돌리는 변경을 피하려고).
   문서 패널의 상태 · 읽기 · 되돌리기는 ImGui 없이 `EditorDocumentPanelCore.cpp` 로 나눠 `EditorTest` 가 가짜 파생을 링크한다 — `EditorPanelDocumentTest.DiscardReloadsTheDocumentByDefault`(되돌리기 기본) 와 닫기 상태 전이 테스트(`CleanPanelClosesAtOnce` · `DirtyPanelAsksOnEveryClosePath` · `CloseChoiceDecidesWhatHappensToTheEdits` · `FailedSaveKeepsThePanelOpen` · `LivePanelClosesWithoutAsking`)가 있다.
   확인하지 않은 것: 문서 패널 중 프리팹 창 밖(머티리얼 창 등)의 닫기 확인 화면 — 기반이 하나이고 Input Map 과 프리팹 창이 같은 화면을 냈다는 것으로 갈음했다. 최대화 · 레이아웃 불러오기에 편집 중인 패널이 걸렸을 때의 모달 여러 개 동시 표시는 화면으로 보지 않았다(모달 ID 는 패널마다 따로다).
   확인함(2026-10-20, DX12, `editor.prefabwindow`): 콘텐츠 브라우저 더블클릭이 "Prefab Window" 탭(가운데 도크)을 열고, 편집 뒤 X 는 Save / Don't Save / Cancel 모달을 띄우며 Cancel 은 열어 두고 Don't Save 는 닫고 다시 열면 깨끗하고 그때의 X 는 묻지 않는다.
   확인함(2026-10-20, DX12, `editor.panelclose`): 문서 패널이 아닌 Input Map Editor 에서 발화 규칙을 고치면 제목에 ● 와 "Unsaved changes" 가 뜨고, 메뉴 토글 경로(`panel.close`)는 창을 닫지 않고 "This document has unsaved changes. Close anyway?" 모달을 띄우며 Cancel 은 열어 두고 편집(HoldThreshold)도 남는다.
   X 도 같은 모달을 띄우고 Don't Save 는 창을 닫으며, 다시 열면 바인딩이 Pressed 로 돌아가 있고 제목 표시도 없다(캡처 4장을 눈으로 봄). 남은 것: 다른 세 백엔드 화면.
   고침(2026-10-20): 제목을 나눈 뒤 기본 배치가 종류 테이블 제목("Prefab Window")만 붙여 열린 채 시작하는 "Prefab Overrides" 가 떠 있는 큰 창으로 씬 뷰를 덮었다. 패널이 영역을 밝히게(`getDefaultDockSlot`) 바꿔 가운데 탭으로 돌렸다(결정 기록 5-11).
3. **스카이** — 엔진에 `SkyComponent`(천정 · 지평선 · 땅 그러데이션, `SkyGradient::evaluateRadiance`)가 들어갔고(2026-10-21) 패킷(`RenderFramePacket::_sky`)과 패스 상수(`g_SkyZenith` · `g_SkyHorizon` · `g_SkyGround`)까지 실린다.
   조명 환경 프리셋이 하늘을 스폰한다(`EditorPreviewEnvironment::_bSky`). **남은 단위: 스카이 패스** — `RenderPassType::Sky`(깊이 1 의 풀스크린 삼각형을 LessEqual 로 그려 빈 곳만 칠함, 깊이 쓰기 없음, 하늘이 없거나 표면 값 보기면 건너뜀),
   `sky.hlsl`, `binding.hlsli` 의 PassCB 세 칸, 엔진 파이프라인 다섯의 Sky 패스(불투명 · 외곽선 뒤, 반투명 앞), 검증(뎁스 · 컬러 출력 필수). 코드는 작업자 워크트리 `previewwin` 에 커밋하지 않고 두었다 —
   셰이더 소스가 바뀌면 커밋 훅(`CheckShaderStamps`)이 네 백엔드 바이너리 재베이크(`App.exe --cook-shaders`)를 요구하는데 빌드 없이 구현만 하는 지시라 굽지 못했다. 빌드한 쪽이 구워 함께 커밋한다.
   남은 확인: 머티리얼 창 미리보기의 배경이 그러데이션인지(네 백엔드, `previewswap` 의 PPM), 하늘이 없는 게임 씬의 그림이 그대로인지(골든 이미지), 디퍼드(`-gv_deferred=1`)에서도 빈 곳만 칠하는지.
   하늘을 환경광으로 쓰는 것(반구 앰비언트 · SH)은 [IndirectLighting](IndirectLighting.md) P2 · P3 의 일이다.
4. **풀 공유** — PSO 캐시는 주 렌더러의 것을 함께 쓴다(`FrameRenderer::_pPSOCache`, 2026-10-21). 미리보기 렌더러는 엔진 PSO 를 만들지 않고 캐시에 없는 것만 만들며, 해제는 소유자만 한다.
   창을 닫으면 렌더러를 내리므로 예전에는 다시 열 때마다 엔진 PSO 를 모두 다시 만들었다(첫 프레임 히치, 창 수만큼 중복). 셰이더 핫 리로드는 주 렌더러가 캐시를 다시 채운 뒤 미리보기 렌더러가 자기 몫을 다시 만든다.
   정점 풀(`GPUMeshVertexPool`) · 머티리얼 그룹 버퍼는 씬마다 그대로다 — 배치 표가 씬마다라 인덱스 공간이 씬을 건너지 않고(셰이더 계약이 같다), 텍스처의 bindless 인덱스는 이미 디바이스 전역이다.
   남은 것: 창 여럿을 띄운 메모리와 창을 다시 여는 첫 프레임 시간을 재서(공유 전 커밋과 비교) 정점 풀까지 나눌 값이 있는지 정한다.
5. **미리보기 렌더러 내리기의 GPU 안전** — 구조는 `FrameRendererScenePreview.cpp` 머리 주석(해제 큐가 GPU 완료 뒤로 미룬다, PSO 는 주 렌더러 캐시에 남는다). 시나리오 `editor/previewreopen`(**아직 돌리지 않았다**)이
   머티리얼 창과 프리팹 창을 각각 연 채 8 번 닫고 다시 연다. 렌더러 수는 탐침 `Editor.ScenePreviewRenderers`(렌더 스레드가 공개하는 `FrameRenderer::getScenePreviewRendererCount`)로 본다 —
   닫을 때마다 줄고, 다시 열면 닫기 전 수로 돌아오며, 끝에서 시작 전 수로 돌아온다. 시작 줄 수(`ExpectLog atLeast` 머티리얼 9 · 전체 18)도 함께 센다.
   GPU 쪽 오류는 로그 줄로 본다. DX12 디버그 레이어와 Vulkan 검증 레이어는 Debug 빌드에서만 켜지고 오류가 `[Warning]` · `[Error]` 줄로 나와 로그 게이트가 시나리오를 진다. 그래서 검증 레이어 확인은 `build/Ninja-Debug` 로 돌린다.
   - 실행(한 줄): `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewreopen -scenario=engine/automation/editor/previewreopen.scenario.xml -dx12` (`-vk` · `-dx11` · `-gl` 로 네 백엔드, 상태 폴더는 빈 곳).
   - 한계: 닫은 프레임에 바로 여는 경합과 창 여러 개를 동시에 닫는 것은 넣지 않았다.
6. **미리보기 씬의 백엔드 교체**. 교체를 거는 길은 메뉴 · 콘솔 · 시나리오 단계 `SwapBackend` 이고(명령줄 `-gv_rhiSwapAtFrame` 은 2026-10-22 에 지웠다 — 시점은 시나리오 단계가 고른다),
   시나리오 `editor/previewswap`(2026-10-20 작성, **아직 돌리지 않았다**)은 같은 길을 `SwapBackend` 로 머티리얼 창이 열려 그려지는 프레임에 요청한다(`other` 로 건너고 `start` 로 돌아와 창이 열린 교체를 두 번 거친다).
   교체마다 `RHI.SwapFailedCount` 0 · `Module.BindingsOk` 를 보고, 교체 뒤 같은 문서를 다시 열어(에디터 인스턴스가 다시 서므로 창이 닫혀 있어도 되고 그대로여도 된다) 프레임 수가 다시 오르는지와 새 디바이스 위에 새 렌더러가 서는지(시작 줄 수 2 · 3)를 본다.
   교체 뒤 살아 있는 렌더러 수(`Editor.ScenePreviewRenderers`)가 교체 앞과 같은지도 본다 — 옛 디바이스의 렌더러가 남거나 창 하나에 둘이 서면 다르다.
   **자동 단언으로 못 한 것**: 미리보기 타깃의 그림. 평균 색의 문턱값을 GPU 로 돌려 보기 전에는 맞출 수 없어, 시나리오가 `gv_editorPreviewScreenshot` 을 교체 앞 · 뒤 · 복귀 뒤에 걸어 `Saved/Automation/previewswap_<before|after1|after2>.ppm` 으로 저장한다 — PNG 로 바꿔 눈으로 본다(빨간 구 + 바닥).
   - 실행(한 줄): `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewswap -scenario=engine/automation/editor/previewswap.scenario.xml -dx12` (`-vk` · `-gl` 로 시작해도 `other` 는 DX12 로 건넌다).
   - 모르는 것: 교체로 에디터 인스턴스가 다시 서므로 머티리얼 창의 열린 문서가 남는지는 확인하지 않았다. 그래서 시나리오가 교체 뒤 문서를 무조건 다시 연다 — 창이 열린 채 교체하는 것은 교체 앞에서 이미 성립한다.
   - 남은 것: 캐릭터 편집 창이 `EditorPreviewPanel` 을 이어받기([EditorCharacterEditor](EditorCharacterEditor.md), 헤더 계약을 따른다).
   고침(2026-10-20, 첫 측정): 두 시나리오가 DX11 에서만 졌다 — Quick Open 의 파일 목록을 일 스레드가 벽시계로 모으는데(DX12 약 450 프레임, DX11 약 650 프레임) `WaitUntil` 예산은 600 프레임이라 프레임이 빠른 DX11 이 먼저 바닥났다(`materialpreview` 도 같은 이유로 DX11 에서 졌다).
   `QuickLauncherPopup` 이 목록이 오는 동안 자동화 시계를 잡게(`SW_AUTOMATION_CLOCK_HOLDER`) 해, Quick Open 을 쓰는 모든 시나리오가 프레임 속도와 상관없이 같은 프레임에 연다.
   포커스를 쥔 채 닫으면 `applyWindowFocus( false )` 가 안 불려 초점 씬 · Undo 스택이 남는 결함도 실제였다(닫힌 창은 그려지지 않는다) — 닫는 길을 `IEditorPanel::closeNow` 하나로 모아 `onPanelClosed` 를 부르게 했고, `previewreopen` 이 `Editor.ToolFocusIsMain` 으로 본다. 위 수정은 **아직 빌드 · 실행하지 않았다**.
   두 시나리오는 `engine/automation/editor/` 에 있어 `AppScenarioTest.EditorScenariosPassOn<백엔드>` 의 체인이 자동으로 줍는다. 처음 돌릴 때는 위 한 줄로 단독 실행해 본 뒤 체인에 맡긴다(교체 · 렌더러 내리기가 터지면 체인 프로세스가 죽는다).
