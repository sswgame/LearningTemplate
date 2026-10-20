# 다음 세션 인계

이 문서는 **지금 상태**만 둡니다. 새 세션은 이 문서를 먼저 읽고, 나머지는 아래에서 일에 맞는 것만 찾습니다. 한 사실은 한 곳에만 씁니다.

| 알고 싶은 것 | 어디 |
|---|---|
| 남은 일 한 건 한 건 | 후보 문서 `Candidates-<영역>.md` — `py -3 -m Scripts plans --area <영역>`(목록), `--file <이름>`(머리 줄과 절 목록) |
| 설계와 구현 단위 | 계획 문서 — `py -3 -m Scripts plans`(목록) |
| 앞으로 할 일의 순서 | [로드맵](Roadmap.md) |
| 결정과 근거 | [결정 기록](../09_Decisions.md) |
| 작업 방식 · 검증 | [작업 방식](../11_Workflow.md), [검증과 측정](../08_Verification.md) |
| 에이전트에게 맡길 때 · 토큰 | [에이전트와 토큰](../12_Agents.md), [작업자 규칙](WorkerRules.md) |
| 환경 · 도구 함정 | [함정](../14_Pitfalls.md) — 해당하는 일을 할 때만 |

한 일은 `git log` 가 정본입니다.

## 1. 지금 상태 (2026-10-20, 오후 세션 끝)

- **push 완료**: main `59ec7f76d`. 이번 세션 병합분 — `panelgap`(비문서 패널 · 모든 닫는 길을 `IEditorPanel::requestClose` 한 입구로 · 시나리오 `editor.panelclose`), `vkfix`(Vulkan `vkDestroyBuffer` 사용 중 해제: 근본 원인은 ImGui 백엔드의 정점 · 인덱스 버퍼 링이 3 칸인데 캡처 프레임이 메인 뷰포트를 두 번 그려 쓰는 칸을 지운 것 — `kImGuiDrawBufferRingSize`(인플라이트 × 2)로 Vulkan · DX12 모두 고침, 옛 커밋에서도 4/4 실패해 이번 변경의 회귀가 아니었다. 고친 뒤 Vulkan · DX12 시나리오 89 개 PASS), 그리고 아래 이전 병합분 — IncrementalCook 을 끝으로, GpuVerification V2 일부(리눅스 소프트웨어 렌더러 요구 백엔드에서 `gl` 제외), 문서 패널 닫기 확인을 `EditorDocumentPanel` 기반으로(`drawContent` final, `drawDocumentContent` 필수, `revertDocument` 기본), `TextAddress` 로 객체 주소 · 값 주소 형식 분리(`formatPropertyText` · `applyPropertyText` · `formatValueText` · `parseTextValue`), 결정 기록 5-12 에 "셰이더 바이너리 계속 커밋", "백엔드 등급제 안 함".
- DX12 플로팅 창을 6 버퍼로 연 화면, DX11 · GL 의 vkfix 뒤 재확인은 하지 않았다. 시나리오 실행이 `animgraphlayers.animgraph.json` 의 줄바꿈만 바꿔 놓는다(내용 변경 없음, 원인 조사 필요).
- **hostgpu 전체는 못 돌림**(Debug 25 분 시한에 8 항목 중 5 개만 끝남: EngineTest Shard1 · 2, AppTest Shard1 통과). 남은 AppTest Shard4 · 5, QaGoldenImages 는 Shipping 으로 시한 없이 돌릴 것. 확인한 DX12 캡처에서 모달이 캡처 오른쪽 아래 가장자리에 붙어 "Close anyway?" 가 잘려 보이는 점(UX), GL 은 가운데에 뜨는 점이 다르다. 모달 위치를 백엔드와 무관하게 창 가운데로 맞출 것.
- `structplan` 은 병합 · push 했다(`37d4c29b7`).
- **다른 세션의 `wt/edcand`**(패널 열기 요청을 패널 형식으로 받는 변경, 커밋 3 개, 미커밋 수정 2 개)가 병합 전이다. `panelgap` 과 에디터 패널 코드에서 부딪힐 수 있다.
- **GpuVerification V2 남은 일**: 정책 수정이 든 main 의 CI 가 리눅스에서 통과하는지 세 번 읽은 뒤 `continue-on-error` 해제와 `softwareRenderSeconds` 수정(단계 시간이 한 자리 초라 120 초는 크다).
- **결함 두 개의 조사 결과(코드는 아직 안 고침)**: (6) 앱이 스스로 종료하지 않는 것은 설계다 — `-gv_screenshot` 은 종료를 요청하지 않고 메인 루프는 `isQuitRequested()` 나 표준 입력 quit 에서만 빠진다(`App.cpp:436`). 해법은 사진을 다 찍은 뒤 `Engine::requestQuit` 를 부르는 옵션이다(`RenderThread.cpp:561`, `Engine.cpp:506`). 쓰기 실패는 0 이 아닌 코드로. `-gv_screenshotEditor`(`EditorUICapture.cpp:143`)도 같은 모양. (3) `-scenario=` 리소스 ID 는 읽는 경로가 이미 받는 구조이고, `startAutomationSession` 이 모듈을 올리기 전에 돌아 도메인 루트가 아직 없는 것이 원인 후보다(미확인, `App.cpp:152`).
- **TypedComponents 앱 측정 넷**(독점 Release)은 아직. 6/6 이지만 "구현대기"로 남은 이유.
- **TextAddress 후속**: `valueToText` · `serializeValueBinary` · `deserializeValueBinary` · `parseTextValueCoerced` · `copyPropertyValue` · `applyPropertyBinary` 가 아직 `void*` 를 받는다.

### 1.1 리팩토링 후보(읽기 전용 감사 셋의 결과, 코드는 안 고침)

작업자 묶음은 서로 파일이 겹치지 않게 나눈다. 위험이 낮은 것부터, [동작] 이 붙은 것은 테스트 · 스크린샷과 함께.
- **먼저(기계적, 낮음)**: Kits 카탈로그 18 개의 `readInto` · `readSetting` · `loadRoot` 반복을 `GameDataXML` 한 곳으로(A1~A3), 해시 사본(`ScheduleSystem*` · `SceneStateHash` · `GimmickStateBundle` · `GimmickCircuit` · `FixedLayout`)을 `HashUtil` 로(FNV-1a 인지 확인), `computeYaw` 사본 4 곳, 죽은 인자 `cbIndex` · `clearColor` · `indexStride`, 호출처 없는 선언 약 35 개와 접근자, float↔비트 변환 약 10 곳을 `BitUtil` 로, 유니온 파인드 2 벌, `applyObjectEdit` 사본, 자체 `contains` 래퍼 5 개, 자동화 단계의 속성 검사 도우미 중복.
- **그다음(중간)**: 배치 목록 3 벌 복사를 1 벌 + 구간 뷰로(`GPUSceneSnapshot.h`, 성능 이득이 큼), 캐시 클래스 10 개를 `SharedAssetCache` 템플릿으로, `captureSnapshot` 의 직렬화 두 번 돌기 · `makeGameObjectXMLContext` 반복 등록, `parseFloat3/4` · `readFloat2` 의 조용한 기본값을 오류로(폴백 금지 원칙, 호출 약 21 곳), `ServerApp` 설정 읽기 실패의 기본값 폴백.
- **마지막(GPU · 벤치 · 셰이더 회귀 필요)**: `FrameRendererPassExecute` 462 줄, `GPUSceneBuilder::populateMeshesFromScene` 383 줄, `GPUMeshVertexPool` ↔ `MorphPool` 공용화, `FrameRenderer::execute(Scene&)` 이중 경로 합치기, 2D · 3D 물리 컴포넌트 쌍 템플릿화, `reflectSpirv` 416 줄.
- **하지 않는다**: `VfxCPUSimulation` 의 `lerp` 를 `MathUtil::lerp` 로 바꾸기(부동소수 결과가 달라지고 GPU 시뮬레이션과 수열이 어긋난다). `StateArchiveUtil` 쓰기 · 읽기 두 벌 구조는 큰 구조 변경이라 건별로 하지 않는다.

### 1.2 구조 단순화(상용 엔진 비교)

결정: 셰이더 바이너리 커밋 유지, 백엔드 등급제 안 함(결정 기록 5-12). 하기로 한 것은 `StructureSimplification`(계획 문서, `structplan` 이 작성 중)의 순서 — 5(설정 `T{}` 폴백 제거 · 게임 설정 정본 하나) → 2(팩 쓰기 · 스탬프를 C++ 로) → 3(에셋 종류를 등록 줄 하나로, "에셋 로더 등록제" 보류를 다시 연다) → 4(텍스트 형식을 역할마다 하나로, `KeyValueFile` 제거). 키트 51 개(게임이 이름으로 부르는 것은 9 개)를 견본으로 둘지 기본 빌드에서 뺄지는 사용자가 아직 정하지 않았다.

- **vcpkg**: 워크트리는 메인의 `Tools/vcpkg` · `build/vcpkg_installed` · `build/vcpkg_cache` 를 정션으로 공유한다(사용자 지시). 워크트리를 지울 때는 `worktree-remove`. 빈 폴더 `D:/w/audiolayout` · `D:/w/vfx` 는 다른 프로세스가 쥐고 있어 남았다.
- GPU 는 이 PC 에서 된다(RTX 3070 Ti, DX12). 못 한 것: TypedComponents 앱 측정 넷(8.4), 다른 세 백엔드 화면, Vfx V2 · V3, 패널 메뉴 토글 등 X 가 아닌 닫는 길(`panelgap` 이 다룸).
- NetSettingsRemaining 은 설정 데이터 형식을 새로 정해야 해서 DataDriven 과 함께 다룬다.
- PairedAnimation 남은 것: P3 본체(복제), 시나리오 테스트(활 · 클립 에셋 없음), M3 에디터.
- 야간 `Load (Linux | Release, nightly)` 근본 원인은 모른다. 근본 원인 후보(미해결): `DiskPathVsResourceId`.

## 2. 첫 일

1. 사용자 지침(2026-10-20): 되묻지 않고, 상용 엔진과 우리 엔진이 발전할 방향을 기준으로 구조적 해결을 한다. 각 작업이 끝날 때마다 push 한다(`full-build` 증거 게이트를 지킨다).
2. `panelgap` · `structplan` 의 보고를 확인해 병합하고, 병합본에서 `full-build` 후 push.
3. 리팩토링 1.1 의 "먼저" 묶음을 파일이 겹치지 않게 작업자 둘(동시 빌드 최대 2)에게 맡긴다. 그다음 `StructureSimplification` 의 첫 단위(정책 5).
4. TypedComponents 앱 측정 넷(독점, Release), 그다음 Roadmap 의 `[병렬 가능]` 묶음: Vfx V2 · V3(`gpu-verifier` 와), 카메라 디렉터, 애니메이션(AnimationGraphLayers, PairedAnimation P3), SerializerSilentSkips, AsyncResourceLoading A2(설계 검토 뒤).

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
