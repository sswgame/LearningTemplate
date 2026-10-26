# 다중 월드 · 에디터 툴 창 구현 계획 (D3-7)

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: D3-7 · 진행: 6/6

**남은 확인(통합에서 확인):** 매니페스트 id `preview-windows-scenarios`, 매니페스트 id `preview-window-reopen-cost`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

옛 `D3-7` 의 설계 근거입니다. 단계 1~6 이 코드로 들어갔습니다(각 단계의 "구현됨"). 파일 · 함수 이름은 2026-10-14 `wt/editor` 기준으로 읽은 것입니다.

## 1. 현재 구조

(단계 1~5 이전 상태. 목표와의 차이를 1.6 표로 남긴다.)

### 1.1 프레임 경로 — 활성 씬 하나

`Engine::update( deltaTime, views, …, bTickScene )` 가 한 프레임을 돌고, 씬은 모두 `_pSceneManager->getActiveScene()` 로 고릅니다. 씬 틱은 `SceneManager::update` 하나(`bTickScene` false 면 건너뜀). 패킷은 `beginPacket` → `populateScene`(씬 포인터 **하나**) → `populateCanvas` → 렌더 스레드 제출 순서입니다.

### 1.2 GPUScene — 빌더 하나, 스냅샷 하나, GPU 측 하나

`GPUSceneBuilder`(`RenderPacketBuilder` 소유) 1, `GPUSceneSnapshot`(패킷당) 1, `GPUScene`(`FrameRenderer` 소유) 1. 컬링 산출물은 뷰마다 나뉘지만 인스턴스 · 머티리얼 · 모프 풀은 GPUScene 하나에 모입니다.

### 1.3 뷰 — 이미 여러 개지만 모두 같은 씬

`RenderViewRequest` 는 씬을 가리키지 않는 값 타입 추가 뷰입니다. 호스트가 넘기는 `HostViewTargets` 는 `_game` · `_scene` 두 슬롯 고정이었습니다.

### 1.4 씬 · 오브젝트 · 물리

`SceneManager` 의 `_listLoadedScene` 중 `_pActiveScene` 하나만 틱 · 렌더 대상이었습니다. 물리(`ScenePhysics`, 씬 서비스) · 카메라 선택(`getCameraRegistry()`)은 **이미 씬별**이고, 시간 배율은 전역 `GameTimeScale` 하나입니다. 미리보기 정책의 고정 스텝 덮어쓰기는 `setFixedStepOverride`.

### 1.5 에디터

`EditorRenderViews` 는 `EditorViewKind` 고정 배열(게임 · 씬 뷰)이었고, 프리팹 격리는 활성 씬의 다른 오브젝트를 숨기는 방식(단계 5 에서 프리팹 창으로 대체)이었습니다. 머티리얼 미리보기는 `MaterialPreviewShading` 이 구를 CPU 에서 셈했습니다. 선택은 `EditorSelection` 하나, Undo 는 `CommandStack` 하나로 에디터 전체에 하나였고, 문서 패널 기반은 `EditorDocumentPanel` 입니다.

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

- 새 `World` 타입 없이 **`Scene` 을 월드 단위로 씁니다**(오브젝트 매니저 · 물리 · 카메라가 이미 씬별).
- `SceneManager` 에 활성 씬과 별도로 **미리보기 씬 목록**을 둡니다. 저장 · 전환 · 영속 오브젝트에 끼지 않습니다.
- 씬마다 틱 정책 `{ bTick, timeScale, bPhysics, fixedStepOverride }`. 프리팹 창은 틱 · 물리 없음, 애니메이션 창은 틱만, 래그돌 창은 물리까지.
- 조명 환경은 기본 프리셋(방향광 · 스카이 · 바닥)을 씬 안 오브젝트로 스폰합니다(별도 타입 없음). 전역 `GameTimeScale` 은 게임 월드 전용이고 미리보기는 정책의 `timeScale` 을 곱해 넘깁니다.

### 2.2 월드별 렌더

- `RenderViewRequest` 와 호스트 뷰에 씬 키(`_sceneID`)를 더합니다. `HostViewTargets` 두 슬롯은 남기고 `HostSceneView` 목록을 더합니다(안 보이는 창은 목록에 없으니 안 그림).
- 씬마다 스냅샷 · `GPUSceneBuilder`(이전 프레임 차이 추적이 씬별) · RT 측 `GPUScene`. 머티리얼 레지스트리 · 메시 정점 풀은 공유 계획이었습니다(bindless 인덱스가 전역) — 실제 구현은 단계 2 에서 달라졌습니다.
- 렌더 그래프는 씬 뷰마다 한 번 돌고 그림자 · 후처리는 `RenderViewSettings` 로 줄일 수 있게 합니다.

### 2.3 툴 창 틀

`EditorPreviewPanel`(`Editor/Common/GUI/`)이 `EditorDocumentPanel` 을 상속하고 미리보기 씬 핸들 · 자기 렌더 타깃(`EditorRenderViews` 를 id 키 목록으로) · `EditorCamera` · 기즈모 · `EditorSelection` · `CommandStack` 인스턴스를 가집니다. 포커스된 창의 선택 · Undo 가 Ctrl+Z · 인스펙터 대상입니다. 키트 · 게임이 창을 등록하는 길은 기존 패널 등록 확장 지점 위에 둡니다.

## 3. 상용 엔진과 비교

기억 기반 정리(소스 재확인 없음). 언리얼은 `FPreviewScene` 이 자기 `UWorld` · 조명 · 스카이를, 유니티는 `PreviewRenderUtility` 가 숨은 씬을 만든다 — 이 계획은 `Scene` 을 미리보기 씬으로 쓰고 씬마다 렌더한다. 툴 창은 언리얼 `FAssetEditorToolkit`, 유니티 `EditorWindow` 에 해당하고 프리팹 격리는 유니티 Prefab Stage(별도 씬, 저장 시 반영)와 같다.
Undo 를 창마다 두는 것은 두 엔진과 다르다(둘 다 전역 스택 하나에 대상 이름을 붙임). 창마다 두면 "창을 닫으면 그 기록이 사라진다" 규칙이 단순하고 죽은 오브젝트를 가리키는 명령이 없어 이쪽을 고른다(결정 4).

## 4. 단계 분할

### 단계 1 — 씬 틱 정책과 미리보기 씬 목록 (엔진, 렌더 변경 없음)

- 위험: 씬 서비스(`ScenePhysics` · `SceneAudio` · `SceneNavigation`)가 단일 씬을 가정한 곳. 오디오는 미리보기에서 끈다.
- **구현됨(2026-10-15)**: `SceneManager` 의 `createPreviewScene` · `destroyPreviewScene` · `getPreviewSceneKey` · `findPreviewScene` · `tickPreviewScenes`, 정책 `ScenePreviewPolicy { _bTick · _timeScale · _bPhysics · _bAudio · _bPlay }`, 초점 씬(`focusScene` · `getFocusedScene`, 내려지면 활성 씬으로 복귀). 물리 · 오디오는 `GameObjectManager::setPhysicsStepEnabled` · `Scene::setAudioUpdateEnabled`. `EngineLoop` 는 `bTickScene` 과 무관하게 `tickPreviewScenes` 를 부른다. 서브시스템 범위는 씬마다 `GameObjectManager` 의 `SubsystemScope` 라 따로 만들지 않았다. 조명 프리셋은 `EditorPreviewEnvironment`(단계 4). 테스트 `PreviewScenesLiveApartFromLoadedScenes`.

### 단계 2 — 씬별 GPUScene (렌더러)

- 위험: 가장 크다. `CheckRenderOwnership.py` 소유 규칙, 컬링 슬롯 수를 업로드 전에 정하는 제약, bindless 인덱스 해제 지연, 네 백엔드 영향.
- **구현됨(2026-10-15, 모양을 바꿈)**: `FrameRenderer` 의 `_gpuScene` 을 씬 키 맵으로 바꾸지 않고 **미리보기 씬마다 `FrameRenderer` 인스턴스 하나**를 주 렌더러가 소유한다(`executeScenePreviewPacket` · `retireScenePreviews`, `FrameRendererScenePreview.cpp`). GT 는 씬마다 `GPUSceneBuilder`(`populateScenePreviews`), 패킷은 `RenderFramePacket::_listScenePreview`. 렌더 스레드는 주 패킷 뒤에 미리보기 타깃을 열고 그 씬의 렌더러로 그린다(`RenderThread::executeFrameBody`).
  까닭: 주 렌더러 핫 패스를 건드리지 않아 주 씬 이미지가 바뀔 여지가 없다. 대가로 머티리얼 · 정점 풀이 렌더러마다 따로다(결정 2 와 어긋남, 결정 기록 5-11). 미리보기 렌더러는 프로파일러 · 타임스탬프 · 렌더 타깃 공개를 하지 않는다(`_bScenePreview`).

### 단계 3 — 호스트 뷰 목록과 `EditorRenderViews` 일반화

- **구현됨(2026-10-15)**: `HostViewTargets` 에 고정 배열 `_arrSceneView[kMaxHostSceneView]` · `_sceneViewCount`(프레임마다 새로 채워 힙을 피함), 값 타입 `HostSceneView`. C ABI `EditorAPI::getSceneViews`(모듈 ABI v6). `EditorRenderViews` 는 두 고정 슬롯 옆에 id 키 미리보기 타깃 목록(`allocatePreviewTarget` · `ensurePreviewTargetSize` · `submitPreviewView` · `collectSceneViews`)을 더했다.

### 단계 4 — `EditorPreviewPanel` 틀과 창별 선택 · Undo

- **구현됨(2026-10-15 ~ 18)**: `EditorPreviewViewport`(미리보기 씬 · 조명 환경 · 오빗 카메라 · 타깃)와 `EditorPreviewPanel`(문서 패널 + 뷰포트 + 창의 `CommandStack` · 선택). 포커스를 얻으면 `SceneManager::focusScene` 과 `editor::setFocusedCommandStack` 을 걸고 Ctrl+Z · Y 는 `getFocusedCommandStack` 을 되돌린다. `ObjectUndoUtil` 명령은 기록 때의 씬 키를 기억해 그 씬에서 되돌린다(`SceneTest.PreviewObjectUndoStaysInItsScene`).
  **창 범위 선택**: `EditorSelection` 은 타입으로 찾지 못한다(`kAccessorOnly`). 호출부는 `editor::getMainSceneSelection` · `getFocusedSelection` · `findSelectionOf` 중 하나로 어느 선택인지 적는다. 오브젝트 기록(`recordModify` · `recordObjectLifetime`)은 **오브젝트가 사는 씬**이 스택을 고른다(미리보기 씬이면 그 창의 스택, 메인 씬은 dirty 아님). 묶음(`beginTransaction`)은 전역 · 창 스택 모두에 걸고 빈 묶음은 버린다. 씬 뷰 · 계층이 포커스를 얻으면 선택이 메인으로 돌아온다. 테스트 `EditorSelectionScopeTest.*`.

### 단계 5 — 첫 창 두 개: 머티리얼 · 프리팹

- **구현됨(머티리얼)**: `MaterialPanel` 이 `EditorPreviewPanel` 을 상속해 미리보기 씬의 구(`PreviewSubject`)에 문서 머티리얼을 건다. CPU 구는 탐침 · 폴백으로 남겼다. 시나리오 `editor/previewscene.scenario.xml` + `-gv_editorPreviewScreenshot`.
- **구현됨(프리팹 창)**: `Panels/Prefab/PrefabEditorPanel`(id `prefab_window`, 제목 "Prefab Window")이 프리팹을 자기 미리보기 씬에 스폰해 편집한다. 메인 씬의 `PrefabPanel`(오버라이드 도구, "Prefab Overrides")과 따로.
  - 격리 스택은 **창이 든다**(`Common/Commands/Scene/EditorPrefabIsolation`, ImGui 없음). 맨 아래 단계 = 문서 루트, 위 = 중첩 프리팹. 중첩 단계는 문서 루트 아래의 다른 오브젝트만 숨기고 조명 · 바닥은 건드리지 않는다. `EditorPrefabIsolationCommands` · 워크스페이스의 격리 상태 · 더티 막기는 없앴다.
  - 놓인 프리팹 표는 이미 씬마다였고, `EditorWorkspace::setPlacedPrefabPath` · `getPlacedPrefabPath` 가 오브젝트 id 대신 `GameObject` 를 받아 **오브젝트가 사는 씬**(`editor::findSceneOf`)의 표를 쓴다.
  - 저장은 지금 단계의 루트를 그 단계 프리팹 파일에 쓴다(`EditorInspectorCommands::applyToPrefab`). 열기는 `PrefabEditorPanel::openWindow`, 중첩은 `requestNestedIsolation`.
  - 테스트 `EditorPrefabIsolationTest.*` · `EditorWorkspaceTest.PrefabPathOfAPreviewSceneObjectLivesInThePreviewScene`.

### 단계 6 — 틀 위에 올라간 창과 나머지 창의 자리

이펙트(`VfxPreviewPanel`) · 캐릭터 편집([EditorCharacterEditor.md](EditorCharacterEditor.md)) · 페어드 애니메이션 창이 틀 위에 올랐다. 메시/모델(LOD 비교) · 애니메이션 클립 · 카메라 프리셋 · 래그돌 · 장르 도구 창은 창마다 독립이라 [Candidates-editor](Candidates-editor.md) `PreviewWindowsRemaining` 후보로 옮겼다.

## 5. 되돌리기 어려운 결정

1. **월드 = `Scene`** — 한 월드에 여러 씬(레벨 스트리밍)을 넣으려면 다시 봐야 한다.
2. **GPUScene 을 씬마다, 머티리얼 · 정점 풀은 공유**(단계 2 에서 렌더러째 따로로 바뀜) — 풀을 나누면 bindless 인덱스 공간이 갈라진다.
3. **패킷 형식에 씬 목록** — `RenderFramePacket` 은 GT/RT 계약이라 캡처 · 리플레이 도구도 따라 바뀐다.
4. **창마다 Undo 스택** — 전역으로 돌아가려면 명령에 대상 씬을 붙이는 작업이 다시 필요하다.
5. **`HostViewTargets` 두 슬롯을 남기고 목록을 더함** — 모두 목록으로 합치면 깔끔하지만 에디터 없는 실행 경로까지 바뀐다.

## 6. 남은 것

(번호는 옛 `D3-7` 항목 (1)~(6) 을 그대로 둔다. (2)는 단계 2 로 들어갔다.)

1. **프리팹 창의 남은 것** — 콘텐츠 브라우저의 프리팹 열기는 새 창으로 돌렸고 X 로 닫을 때의 미저장 확인(Save / Don't Save / Cancel, 저장 실패면 열어 둠)이 들어갔다(GPU 없이 `EditorPanelDocumentTest` 로 테스트).
   **닫기 확인은 모든 패널 공통이고 닫는 길은 하나다**(2026-10-20): 확인은 `IEditorPanel` 에 있고 `setOpen( false )` 가 `requestClose` 라 미저장 편집이 있으면 열어 둔 채 확인을 기다린다. X 는 `draw` 가 창을 연 직후 `absorbCloseButton` 으로 같은 길에 넣는다. 패널 메뉴 토글 · 레이아웃 불러오기 · 최대화 · `panel.close` 가 모두 같은 확인을 받는다. 편집이 패널 밖에 살아 닫아도 잃는 것이 없는 패널만 `asksBeforeClose` 로 빠진다(전역 변수 창). 에디터 종료는 `EditorSceneFileCommands::tryBeginQuit` 의 종료 확인이 따로 있고, `panel.resetOptional` · 모듈 언로드(`releasePanelsWithin`)는 인스턴스를 버리는 것이라 묻지 않는다.
   "Don't Save" 는 `revertDocument`(문서 패널 기본 `reloadDocument`, 프리팹 창은 디스크 프리팹으로 루트를 다시 세움). 미리보기 씬 편집은 창 Undo 스택이 늘고 줄 때 `EditorPreviewPanel::syncDocumentDirty` 가 dirty 로 표시한다. 문서 패널의 상태 · 읽기 · 되돌리기는 ImGui 없이 `EditorDocumentPanelCore.cpp` 로 나눠 `EditorTest` 가 링크한다(`EditorPanelDocumentTest.*`).
   확인함(2026-10-20, DX12, `editor.prefabwindow` · `editor.panelclose`): 프리팹 창 더블클릭 열기 · X 모달 · Cancel/Don't Save 동작, Input Map Editor 의 ● 표시 · `panel.close` 모달. **확인하지 않은 것**: 다른 세 백엔드 화면, 머티리얼 창 등 프리팹 창 밖 문서 패널의 닫기 화면(기반이 하나라 갈음), 최대화 · 레이아웃 불러오기에 편집 중 패널이 여럿 걸릴 때의 모달 동시 표시.
   고침(2026-10-20): 제목을 나눈 뒤 "Prefab Overrides" 가 열린 채 시작해 씬 뷰를 덮던 것을 `getDefaultDockSlot` 으로 가운데 탭에 돌렸다(결정 기록 5-11).
3. **스카이** — `SkyComponent`(천정 · 지평선 · 땅 그러데이션)와 패킷(`RenderFramePacket::_sky`) · 패스 상수, 스카이 패스(`RenderPassType::Sky`, `sky.hlsl`)가 들어갔고 조명 환경 프리셋이 하늘을 스폰한다(`EditorPreviewEnvironment::_bSky`).
   확인함(2026-10-22, Debug): 시나리오 `editor/previewsky` 가 네 백엔드에서 통과하고 창을 연 타깃과 다시 연 타깃(`Saved/Automation/previewsky_<first|reopen>.ppm`)을 눈으로 봤다(위가 회청색 그러데이션, 구 · 바닥은 덮이지 않음). 디퍼드(`-gv_deferred=1`)는 vk · dx11 · gl 이 통과했다.
   **디퍼드 DX12 는 시나리오가 진다**: `[RHI] [D3D12] [Error] 온라인 디스크립터 블록이 바닥났습니다 (64×512)` 로그 한 줄. 미리보기 창과 상관없는 디퍼드 DX12 에디터 전체의 문제인지 확인하지 못했다 — 렌더러 담당에게 넘긴다. 하늘이 없는 게임 씬의 그림이 그대로인지는 골든 이미지로 본다(안 봄).
   - 실행: `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewsky "-scenario=engine/automation/editor/previewsky.scenario.xml" -dx12` (`-vk` · `-dx11` · `-gl`, 각각 `-gv_deferred=1` 도). PowerShell 에서 `-scenario=` 는 따옴표로 묶는다.
   하늘을 환경광으로 쓰는 것은 [IndirectLighting](IndirectLighting.md) P2 · P3 의 일이다.
4. **풀 공유** — PSO 캐시는 주 렌더러의 것을 함께 쓴다(`FrameRenderer::_pPSOCache`, 2026-10-21). 미리보기 렌더러는 캐시에 없는 것만 만들고 해제는 소유자만 한다(예전에는 창을 다시 열 때마다 엔진 PSO 를 모두 다시 만들었다). 셰이더 핫 리로드는 주 렌더러가 캐시를 채운 뒤 미리보기가 자기 몫을 다시 만든다. 정점 풀 · 머티리얼 그룹 버퍼는 씬마다 그대로다(인덱스 공간이 씬을 건너지 않고 텍스처 bindless 인덱스는 디바이스 전역).
   남은 것(`preview-window-reopen-cost`): 창 여럿을 띄운 메모리와 창을 다시 여는 첫 프레임 시간을 재서(공유 전 커밋과 비교) 정점 풀까지 나눌 값이 있는지 정한다.
5. **미리보기 렌더러 내리기의 GPU 안전** — 구조는 `FrameRendererScenePreview.cpp` 머리 주석(해제 큐가 GPU 완료 뒤로 미룸, PSO 는 주 캐시에 남음).
   **확인함(2026-10-22, Debug — DX12 디버그 레이어 · Vulkan 검증 레이어)**: 시나리오 `editor/previewreopen` 이 네 백엔드에서 통과(경고 · 오류 줄 없음). 머티리얼 창과 프리팹 창을 각각 연 채 8 번 닫고 다시 열며 탐침 `Editor.ScenePreviewRenderers` 가 닫을 때 줄고 다시 열면 돌아오고 끝에서 시작 수로 돌아온다. 검증 레이어는 Debug 에서만 켜지므로 `build/Ninja-Debug` 로 돌린다.
   - 실행: `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewreopen -scenario=engine/automation/editor/previewreopen.scenario.xml -dx12` (`-vk` · `-dx11` · `-gl`, 상태 폴더는 빈 곳).
   - 한계: 닫은 프레임에 바로 여는 경합과 창 여러 개를 동시에 닫는 것은 넣지 않았다.
6. **미리보기 씬의 백엔드 교체** — 교체는 메뉴 · 콘솔 · 시나리오 단계 `SwapBackend` 로 건다(`-gv_rhiSwapAtFrame` 은 2026-10-22 에 지움). 시나리오 `editor/previewswap` 이 머티리얼 창이 열려 그려지는 프레임에 `other` 로 건너고 `start` 로 돌아오는 교체를 두 번 거치며 `RHI.SwapFailedCount` 0 · `Module.BindingsOk` · 교체 앞 · 뒤 `Editor.ScenePreviewRenderers` 같음 · 교체 뒤 문서를 다시 열어 프레임 수가 오르는지를 본다.
   **확인함(2026-10-22, Debug)**: 네 백엔드로 시작해 통과, 교체 앞 · 뒤 · 복귀 뒤 타깃 셋(`Saved/Automation/previewswap_<before|after1|after2>.ppm`)을 눈으로 봤다(빨간 구 + 바닥 + 하늘). **자동 단언으로 못 한 것**: 미리보기 타깃의 그림(평균 색 문턱값을 GPU 로 보기 전에는 맞출 수 없음).
   - 실행: `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewswap -scenario=engine/automation/editor/previewswap.scenario.xml -dx12` (`-vk` · `-gl` 로 시작해도 `other` 는 DX12).
   - 모르는 것: 교체로 에디터 인스턴스가 다시 서므로 머티리얼 창의 열린 문서가 남는지는 확인하지 않았다(시나리오가 교체 뒤 문서를 무조건 다시 연다).
   - 남은 것: 캐릭터 편집 창이 `EditorPreviewPanel` 을 이어받기([EditorCharacterEditor](EditorCharacterEditor.md), 헤더 계약을 따른다).
   고침(2026-10-20): 두 시나리오가 DX11 에서만 졌다 — Quick Open 파일 목록을 일 스레드가 벽시계로 모으는데 프레임이 빠른 DX11 이 `WaitUntil` 예산(600 프레임)이 먼저 바닥났다. `QuickLauncherPopup` 이 목록이 오는 동안 자동화 시계를 잡게(`SW_AUTOMATION_CLOCK_HOLDER`) 해 해결. 포커스를 쥔 채 닫으면 초점 씬 · Undo 스택이 남는 결함도 실제여서 닫는 길을 `IEditorPanel::closeNow` 하나로 모았다(`previewreopen` 이 `Editor.ToolFocusIsMain` 으로 봄). 두 시나리오는 `AppScenarioTest.EditorScenariosPassOn<백엔드>` 체인이 줍는다 — 처음에는 위 한 줄로 단독 실행해 본 뒤 체인에 맡긴다(교체 · 렌더러 내리기가 터지면 체인 프로세스가 죽는다).
