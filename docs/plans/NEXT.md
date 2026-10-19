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

## 1. 지금 상태 (2026-10-20, 밤 세션 끝)

- **진행 중인 작업자는 없다.** 워크트리는 모두 지웠다(`git worktree list`). 빈 폴더 `D:/w/audiolayout` · `D:/w/vfx` 는 다른 프로세스가 쥐고 있어 남았다. 나중에 지운다.
- **이번 세션에 main 으로 병합함**: ReflectionFormats · GimmickCircuitState 를 끝으로 바꿈, `SkeletalAnimatorComponent::seekCurrentTime`(PairedAnimation P3 의 선행 훅), 프리팹 창 열기 경로 · X 닫을 때 미저장 확인, 에디터 시나리오 `editor.prefabwindow` · `editor.inspectorhandle`(DX12 PASS, 프리팹 창 화면과 인스펙터 선택기 화면 확인 끝), 핸들 선택기가 값을 못 쓰던 결함 수정, `formatValueText` 로 값 주소와 객체 주소 형식 분리, IncrementalCook K0 측정(6/6, 결과는 [검증과 측정](../08_Verification.md)). 마지막 push 는 `33c8734fa`, 그 뒤 K0 문서 병합 `bd126548d` 는 push 전이다. push 전에 `ci-jobs --runs` 로 직전 CI 를 확인한다.
- **CI**: main CI `39295170988` 의 실패 하나(`AssetCacheRegistryTest...`)는 고쳤다(`4548c65d2`). 고친 뒤 CI 가 통과하는지는 아직 못 봤다.
- **K0 결과 요약**: 이득은 셰이더 컴파일 캐시(252 → 5 s)와 팩 증분(12 → 1.7 s)에서 난다. 씬 · 프리팹 · 레지스트리 · 내비메시는 합쳐 1.4 s 라 몫이 작다. 쿠커에 단계별 타이머가 없어 측정이 파일 수정 시각 추정에 기댔다(후보: 쿠킹 요약 로그에 단계별 ms).
- **새로 찾은 후보(아직 문서에 안 옮김)**: (1) DX12 `AppScenarioTest.EditorScenariosPassOnDirectX12` 의 savedstate.1·2 · viewportoverlay.1·2 묶음이 main 에서도 실패한다(SceneViewDrawn · OverlayDock · OverlayVertical, 원인 모름). (2) `layout.reset` 직후 "Prefab Overrides" 도구가 도킹 대신 큰 떠 있는 창으로 뜬다. (3) `-scenario=` 에 리소스 ID 를 주면 not found 이고 절대 경로여야 돈다. (4) `formatPropertyText` · `applyPropertyText` 는 아직 `const void*` 라 진짜 프로퍼티에 값 주소를 넘기는 실수를 컴파일러가 못 막는다. (5) 모든 문서 패널이 X 로 닫을 때 미저장 확인이 없다(`EditorDocumentPanel` 로 끌어올리는 것이 구조적 해결). (6) 앱 실행 후 스스로 종료되지 않는 문제(`-gv_screenshot` 뒤 종료 코드 124).
- **vcpkg**: 워크트리는 메인의 `Tools/vcpkg` · `build/vcpkg_installed` · `build/vcpkg_cache` 를 정션으로 공유한다(사용자 지시). 워크트리를 지울 때는 `worktree-remove`.
- GPU 는 이 PC 에서 된다(RTX 3070 Ti, DX12). 못 한 것: TypedComponents 앱 측정 넷(8.4), 다른 세 백엔드 화면, Vfx V2 · V3, 패널 메뉴 토글 등 X 가 아닌 닫는 길.
- 사용자 결정 대기 없음. NetSettingsRemaining 은 설정 데이터 형식을 새로 정해야 해서 DataDriven 과 함께 다룬다.
- PairedAnimation 남은 것: P3 본체(복제), 시나리오 테스트(활 · 클립 에셋 없음), M3 에디터.
- 야간 `Load (Linux | Release, nightly)` 근본 원인은 모른다. 근본 원인 후보(미해결): `DiskPathVsResourceId`.

## 2. 첫 일

1. 사용자 지침(2026-10-20): 되묻지 않고, 상용 엔진과 우리 엔진이 발전할 방향을 기준으로 구조적 해결을 한다.
2. push 한 main 의 CI 를 `ci-jobs --summary` 로 확인하고 K0 문서 병합을 push 한다.
3. 위 새 후보 (1) 의 DX12 에디터 시나리오 실패 원인을 먼저 본다(main 에서 재현된다).
4. TypedComponents 앱 측정 넷(독점, Release), 그다음 Roadmap 의 `[병렬 가능]` 묶음: Vfx V2 · V3(`gpu-verifier` 와), 카메라 디렉터, 애니메이션(AnimationGraphLayers, PairedAnimation P3), SerializerSilentSkips, AsyncResourceLoading A2(설계 검토 뒤). 동시 빌드는 최대 2.
## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
