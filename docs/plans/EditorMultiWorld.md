# 다중 월드 · 에디터 툴 창 구현 계획 (D3-7)

백로그 `D3-7` 의 설계 근거입니다. 다른 PC 에서 병합할 때 판단 기준으로 씁니다. 단계 1~4 와 단계 5 의 머티리얼 창이 코드로 들어갔습니다(2026-10-15, 각 단계의 "구현됨" 참고).
파일 · 함수 이름은 2026-10-14 `wt/editor` 기준으로 읽은 것입니다.

## 1. 현재 구조

### 1.1 프레임 경로 — 활성 씬 하나

- `Source/Engine/Engine.h/.cpp` 의 `Engine::tick( deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate&, bool bTickScene )` 가 한 프레임을 돕니다.
  씬을 고르는 곳은 모두 `_pSceneManager->getActiveScene()` 입니다(`makeFrameContext`, "Scene update" 블록, `SceneDebugDraw::update`, 패킷 단계).
- 씬 틱은 `SceneManager::tick( deltaTime )` 하나입니다. `bTickScene` 이 false 면(에디터 정지) 건너뛰고 `_sceneDeltaSeconds` 는 0 입니다.
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
- 시간 배율은 전역 `GameTimeScale`(`Engine/Utility/GameTimeScale.h`) 하나입니다.
- 카메라 선택은 `GameObjectManager::getCameraRegistry()` 로 씬별입니다.

### 1.5 에디터

- `EditorRenderViews`(`Editor/Common/Workspace/EditorRenderViews.h`)는 `EditorViewKind` 로 고른 고정 배열(`_arrViewTarget`)의 오프스크린 타깃을 관리합니다(`ensureViewTargetSize` · `markViewDrawn` · `wasViewDrawn`). 게임 뷰 · 씬 뷰만 있습니다.
- 프리팹 격리 — `EditorPrefabIsolationCommands::enterPrefabIsolation( prefabPath )` · `exitPrefabIsolation( bSaveToPrefab )`. 활성 씬의 다른 오브젝트를 숨기고 스택은 워크스페이스의 `PrefabIsolationFrame` 이 가지고 있습니다. 활성 씬을 빌리므로 씬이 더티면 막습니다.
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
- 시간: 전역 `GameTimeScale` 은 게임 월드 전용으로 두고, 미리보기 씬은 정책의 `timeScale` 을 곱한 값을 `SceneManager::tick` 에서 넘깁니다.

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
  전역 `EditorSelection` · 인스펙터 · 기즈모를 창의 것으로 바꾸는 일은 남았다(백로그 `D3-7`).

### 단계 5 — 첫 창 두 개: 머티리얼 · 프리팹
- 파일: `Panels/Material/MaterialPanel.*`(구 미리보기를 미리보기 씬으로, `MaterialPreviewShading` 은 썸네일 폴백으로 남김), `EditorPrefabIsolationCommands.*`(활성 씬 대신 미리보기 씬에 프리팹을 스폰, 더티 막기 제거).
- 위험: 프리팹 저장 경로가 바뀝니다 — 중첩 격리 스택(`PrefabIsolationFrame`)의 의미를 유지해야 합니다.
- 확인: 프리팹 격리 기존 테스트, 머티리얼 미리보기 이미지 비교(끈 판과 달라야 함도 확인).
- **구현됨(2026-10-15, 머티리얼만)**: `MaterialPanel` 이 `EditorPreviewPanel` 을 상속해 미리보기 씬에 구(`PreviewSubject`)를 놓고 문서의 머티리얼(캐시 사본)을 건다. CPU 구(`MaterialPreviewShading`)는 탐침 · 폴백으로 옆에 남겼다.
  시나리오 `editor/previewscene.scenario.xml` + `-gv_editorPreviewScreenshot`. 프리팹 창은 남았다(백로그 `D3-7`).

### 단계 6 이후 — 나머지 창
메시/모델(LOD 비교) · 애니메이션 · 카메라 프리셋 · 래그돌 · 이펙트 · 장르 도구 · 캐릭터 편집 창([EditorCharacterEditor.md](EditorCharacterEditor.md)). 창마다 단위 하나.

## 5. 되돌리기 어려운 결정

1. **월드 = `Scene`** — 별도 `World` 타입을 두지 않습니다. 나중에 한 월드에 여러 씬(레벨 스트리밍)을 넣으려면 이 결정을 다시 봐야 합니다.
2. **GPUScene 을 씬마다, 머티리얼 · 정점 풀은 공유** — 풀을 나누면 bindless 인덱스 공간이 갈라져 셰이더 계약이 바뀝니다.
3. **패킷 형식에 씬 목록** — `RenderFramePacket` 은 GT/RT 계약이라 형식을 바꾸면 캡처 · 리플레이 도구도 따라 바뀝니다.
4. **창마다 Undo 스택** — 전역 스택으로 돌아가려면 명령에 대상 씬을 붙이는 작업이 다시 필요합니다.
5. **`HostViewTargets` 두 슬롯을 남기고 목록을 더함** — 게임 · 씬 뷰는 특별 취급이 남습니다. 모두 목록으로 합치면 깔끔하지만 에디터 없는 실행 경로까지 바뀝니다.
