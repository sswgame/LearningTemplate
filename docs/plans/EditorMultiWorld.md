# 다중 월드 · 에디터 툴 창 구현 계획 (D3-7)

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: D3-7 · 진행: 6/6

**남은 확인(통합에서 확인):** 매니페스트 id `preview-windows-scenarios`, 매니페스트 id `preview-window-reopen-cost`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

옛 `D3-7` 의 설계 근거입니다. 단계 1~6 이 코드로 들어갔습니다.

## 1. 현재 구조

(이전 상태) 씬 틱 · GPUScene · 호스트 뷰(`_game` · `_scene` 두 슬롯) · 에디터 선택 · Undo 가 모두 활성 씬 하나 또는 에디터 전체 하나였고 물리와 카메라 선택만 이미 씬별이었습니다. 아래 단계로 월드마다(틱 · 렌더) · 창마다(선택 · Undo)로 바뀌었습니다.

## 2. 목표 구조

- 새 `World` 타입 없이 **`Scene` 을 월드 단위로 씁니다**(오브젝트 매니저 · 물리 · 카메라가 이미 씬별). `SceneManager` 에 활성 씬과 별도로 **미리보기 씬 목록**을 두며 저장 · 전환 · 영속 오브젝트에 끼지 않습니다. 씬마다 틱 정책(틱 · 시간 배율 · 물리 · 오디오)을 두고, 조명 환경은 기본 프리셋을 씬 안 오브젝트로 스폰합니다. 전역 `GameTimeScale` 은 게임 월드 전용입니다.
- 렌더: 호스트 뷰에 씬 키를 더하고 두 고정 슬롯 옆에 `HostSceneView` 목록을 둡니다(안 보이는 창은 안 그림). 씬마다 `GPUSceneBuilder` · 스냅샷(이전 프레임 차이 추적이 씬별) · `GPUScene` 입니다. 렌더 그래프는 씬 뷰마다 한 번 돌고 그림자 · 후처리는 `RenderViewSettings` 로 줄입니다.
- 툴 창 틀: `EditorPreviewPanel`(`EditorDocumentPanel` 파생)이 미리보기 씬 · 렌더 타깃 · 카메라 · 기즈모 · `EditorSelection` · `CommandStack` 을 가지고, 포커스된 창의 선택 · Undo 가 Ctrl+Z · 인스펙터 대상입니다. 창 등록은 기존 패널 등록 확장 지점 위에 둡니다.

## 3. 상용 엔진과 비교

언리얼 `FPreviewScene`(자기 `UWorld`), 유니티 `PreviewRenderUtility`(숨은 씬)와 같은 방향이고 프리팹 격리는 유니티 Prefab Stage 와 같습니다. Undo 를 창마다 두는 것은 두 엔진(전역 스택 하나)과 다릅니다 — 창을 닫으면 그 기록이 사라지는 규칙이 단순하고 죽은 오브젝트를 가리키는 명령이 없어서입니다(결정 4). 기억 기반 비교입니다.

## 4. 단계 분할

### 단계 1 — 씬 틱 정책과 미리보기 씬 목록 (엔진, 렌더 변경 없음)

**구현됨.** `SceneManager` 의 `createPreviewScene` · `destroyPreviewScene` · `findPreviewScene` · `tickPreviewScenes`, 정책 `ScenePreviewPolicy { _bTick · _timeScale · _bPhysics · _bAudio · _bPlay }`, 초점 씬 `focusScene`(내려지면 활성 씬으로 복귀). `EngineLoop` 는 `bTickScene` 과 무관하게 `tickPreviewScenes` 를 부릅니다. 서브시스템 범위는 씬마다 `GameObjectManager` 의 `SubsystemScope` 라 따로 만들지 않았습니다.

### 단계 2 — 씬별 GPUScene (렌더러)

**구현됨(모양을 바꿈).** `FrameRenderer` 안 `_gpuScene` 을 맵으로 바꾸지 않고 **미리보기 씬마다 `FrameRenderer` 인스턴스 하나**를 주 렌더러가 소유합니다(`executeScenePreviewPacket` · `retireScenePreviews`, `FrameRendererScenePreview.cpp`). GT 는 씬마다 `GPUSceneBuilder`(`populateScenePreviews`), 패킷은 `RenderFramePacket::_listScenePreview`, 렌더 스레드는 주 패킷 뒤에 미리보기 타깃을 그립니다. 까닭: 주 렌더러 핫 패스를 안 건드려 주 씬 이미지가 바뀔 여지가 없습니다. 대가로 머티리얼 · 정점 풀이 렌더러마다 따로입니다(결정 2 와 어긋남, 결정 기록 5-11). 미리보기 렌더러는 프로파일러 · 타임스탬프를 하지 않습니다(`_bScenePreview`).

### 단계 3 — 호스트 뷰 목록과 `EditorRenderViews` 일반화

**구현됨.** `HostViewTargets::_arrSceneView[kMaxHostSceneView]`(프레임마다 채워 힙을 피함), C ABI `EditorAPI::getSceneViews`(모듈 ABI v6), `EditorRenderViews` 의 id 키 미리보기 타깃 목록.

### 단계 4 — `EditorPreviewPanel` 틀과 창별 선택 · Undo

**구현됨.** `EditorPreviewViewport` + `EditorPreviewPanel`. 포커스를 얻으면 `focusScene` 과 `editor::setFocusedCommandStack` 을 걸고 `ObjectUndoUtil` 명령은 기록 때의 씬 키로 그 씬에서 되돌립니다. `EditorSelection` 은 타입으로 못 찾고(`kAccessorOnly`) `getMainSceneSelection` · `getFocusedSelection` · `findSelectionOf` 중 하나로 어느 선택인지 적습니다. 오브젝트 기록은 **오브젝트가 사는 씬**이 스택을 고릅니다(미리보기면 그 창, 메인 씬은 dirty 아님). 테스트 `EditorSelectionScopeTest.*`, `SceneTest.PreviewObjectUndoStaysInItsScene`.

### 단계 5 — 첫 창 두 개: 머티리얼 · 프리팹

**구현됨.** `MaterialPanel` 이 `EditorPreviewPanel` 을 상속해 구(`PreviewSubject`)에 문서 머티리얼을 겁니다(CPU 구 `MaterialPreviewShading` 은 폴백). `PrefabEditorPanel`(id `prefab_window`, "Prefab Window")이 프리팹을 자기 미리보기 씬에 스폰해 편집하고 메인 씬의 `PrefabPanel`("Prefab Overrides")과 따로입니다. 격리 스택은 **창이 듭니다**(`EditorPrefabIsolation`, ImGui 없음; 중첩 단계는 문서 루트 밖 조명 · 바닥을 건드리지 않음). 놓인 프리팹 표는 `GameObject` 를 받아 **오브젝트가 사는 씬**의 표를 씁니다(`editor::findSceneOf`). 저장은 지금 단계 루트를 그 단계 프리팹 파일에 씁니다(`applyToPrefab`). 테스트 `EditorPrefabIsolationTest.*`.

### 단계 6 — 틀 위에 올라간 창과 나머지 창의 자리

이펙트(`VfxPreviewPanel`) · 캐릭터 편집([결정 기록 5-11](../decisions/5-11_Editor.md)) · 페어드 애니메이션 창이 틀 위에 올랐습니다. 메시/모델 · 애니메이션 클립 · 카메라 프리셋 · 래그돌 · 장르 도구 창은 [Candidates-editor](Candidates-editor.md) `PreviewWindowsRemaining` 후보입니다.

## 5. 되돌리기 어려운 결정

1. **월드 = `Scene`** — 한 월드에 여러 씬(레벨 스트리밍)을 넣으려면 다시 봐야 한다.
2. **GPUScene 을 씬마다, 머티리얼 · 정점 풀은 공유**(단계 2 에서 렌더러째 따로로 바뀜) — 풀을 나누면 bindless 인덱스 공간이 갈라진다.
3. **패킷 형식에 씬 목록** — `RenderFramePacket` 은 GT/RT 계약이라 캡처 · 리플레이 도구도 따라 바뀐다.
4. **창마다 Undo 스택** — 전역으로 돌아가려면 명령에 대상 씬을 붙이는 작업이 다시 필요하다.
5. **`HostViewTargets` 두 슬롯을 남기고 목록을 더함** — 모두 목록으로 합치면 에디터 없는 실행 경로까지 바뀐다.

## 6. 남은 것

(번호는 옛 `D3-7` 항목 (1)~(6) 을 그대로 둔다. (2)는 단계 2 로 들어갔다.)

1. **프리팹 창의 남은 것** — 콘텐츠 브라우저의 프리팹 열기는 새 창으로 돌렸고 **닫기 확인은 모든 패널 공통이며 닫는 길은 하나**입니다: `IEditorPanel::setOpen( false )` 가 `requestClose` 라 미저장 편집이 있으면 Save / Don't Save / Cancel 을 기다리고, X 는 `absorbCloseButton` 으로 같은 길에 들어갑니다. 편집이 패널 밖에 살아 잃는 것이 없는 패널(전역 변수 창)만 `asksBeforeClose` 로 빠집니다. "Don't Save" 는 `revertDocument`(문서 패널은 `reloadDocument`, 프리팹 창은 디스크로 루트 재구성)이고 `EditorPreviewPanel::syncDocumentDirty` 가 창 Undo 스택 변화를 dirty 로 표시합니다. 문서 패널 상태 · 되돌리기는 ImGui 없이 `EditorDocumentPanelCore.cpp` 에 있어 `EditorPanelDocumentTest.*` 가 봅니다. 확인함(DX12, `editor.prefabwindow` · `editor.panelclose`). **확인하지 않은 것**: 다른 세 백엔드 화면, 프리팹 창 밖 문서 패널의 닫기 화면, 편집 중 패널이 여럿 걸린 최대화 · 레이아웃 불러오기의 모달 동시 표시.
3. **스카이** — `SkyComponent` + 패킷 `RenderFramePacket::_sky` + 스카이 패스(`RenderPassType::Sky`, `sky.hlsl`)가 들어갔고 조명 프리셋이 하늘을 스폰합니다(`EditorPreviewEnvironment::_bSky`). 시나리오 `editor/previewsky` 가 네 백엔드에서 통과하고 디퍼드는 vk · dx11 · gl 이 통과했습니다. **디퍼드 DX12 는 시나리오가 집니다**: `[RHI] [D3D12] [Error] 온라인 디스크립터 블록이 바닥났습니다 (64×512)` — 미리보기와 상관없는 디퍼드 DX12 에디터 전체 문제인지 확인하지 못해 렌더러 담당에게 넘깁니다. 하늘이 없는 게임 씬이 그대로인지는 골든으로 봅니다(안 봄). 하늘을 환경광으로 쓰는 것은 [결정 기록 5-5](../decisions/5-5_GraphicsRHI.md) P2 · P3.
   - 실행: `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewsky "-scenario=engine/automation/editor/previewsky.scenario.xml" -dx12` (`-vk` · `-dx11` · `-gl`, 각각 `-gv_deferred=1` 도). PowerShell 에서 `-scenario=` 는 따옴표로 묶는다.
4. **풀 공유** — PSO 캐시는 주 렌더러의 것을 함께 씁니다(`FrameRenderer::_pPSOCache`). 미리보기 렌더러는 캐시에 없는 것만 만들고 해제는 소유자만 합니다(예전엔 창을 다시 열 때마다 엔진 PSO 를 다 다시 만듦). 정점 풀 · 머티리얼 그룹 버퍼는 씬마다 그대로입니다. 남은 것(`preview-window-reopen-cost`): 창 여럿을 띄운 메모리와 창을 다시 여는 첫 프레임 시간을 재서(공유 전 커밋과 비교) 정점 풀까지 나눌 값이 있는지 정합니다.
5. **미리보기 렌더러 내리기의 GPU 안전** — 구조는 `FrameRendererScenePreview.cpp` 머리 주석(해제 큐가 GPU 완료 뒤로 미룸). 시나리오 `editor/previewreopen` 이 네 백엔드에서 통과했습니다(DX12 디버그 레이어 · Vulkan 검증 레이어 오류 0, 탐침 `Editor.ScenePreviewRenderers` 가 닫을 때 줄고 열면 돌아옴). 검증 레이어는 Debug 에서만 켜지므로 `build/Ninja-Debug` 로 돌립니다. 한계: 닫은 프레임에 바로 여는 경합과 창 여러 개를 동시에 닫는 것은 넣지 않았습니다.
   - 실행: `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewreopen -scenario=engine/automation/editor/previewreopen.scenario.xml -dx12` (`-vk` · `-dx11` · `-gl`, 상태 폴더는 빈 곳).
6. **미리보기 씬의 백엔드 교체** — 시나리오 `editor/previewswap` 이 머티리얼 창이 열려 그려지는 프레임에 `SwapBackend` 로 `other` 로 건너고 `start` 로 돌아오는 교체를 두 번 거치며 `RHI.SwapFailedCount` 0 · `Module.BindingsOk` · 교체 앞 · 뒤 `Editor.ScenePreviewRenderers` 같음을 봅니다(네 백엔드 통과, 교체 앞 · 뒤 · 복귀 뒤 타깃 `Saved/Automation/previewswap_<before|after1|after2>.ppm` 을 눈으로 봄). **자동 단언으로 못 한 것**: 타깃의 그림(평균 색 문턱값을 GPU 로 보기 전에는 못 맞춤). 모르는 것: 교체로 에디터 인스턴스가 다시 서므로 머티리얼 창의 열린 문서가 남는지(시나리오가 교체 뒤 문서를 무조건 다시 엶). 남은 것: 캐릭터 편집 창이 `EditorPreviewPanel` 을 이어받기([결정 기록 5-11](../decisions/5-11_Editor.md)).
   - 실행: `App.exe -EnableEditor -gv_editorStateDir=Saved/Automation/EditorState/previewswap -scenario=engine/automation/editor/previewswap.scenario.xml -dx12` (`-vk` · `-gl` 로 시작해도 `other` 는 DX12).
   - 두 시나리오(reopen · swap)는 `AppScenarioTest.EditorScenariosPassOn<백엔드>` 체인이 줍습니다. 처음에는 위 한 줄로 단독 실행한 뒤 체인에 맡깁니다(교체 · 렌더러 내리기가 터지면 체인 프로세스가 죽음). Quick Open 파일 목록이 벽시계라 `QuickLauncherPopup` 이 자동화 시계를 잡습니다(`SW_AUTOMATION_CLOCK_HOLDER`).
