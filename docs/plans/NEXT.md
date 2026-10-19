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

## 1. 지금 상태 (2026-10-20)

- **진행 중인 작업자 하나(`worktree-make` 워크트리, 병합 안 함)**: `D:/w/audiolayout`(`wt/audiolayout`, AudioChannelLayout U1). 보고가 오면 확인하고 `git merge --no-ff` 로 병합한다. README 진행 표 충돌은 `status-board --write README.md` 로 다시 만든다.
- **이번 세션에 main 으로 병합함(push 안 함)**: GpuVerification V2 절반(`isBackendUnderTest`, Linux `gl` 원인은 모름, `continue-on-error` 그대로) · RealElapsedTime · EditorMultiWorld 프리팹 창(화면 미확인) · Vfx V0 · V1(V2 렌더 경로와 V3 는 GPU · 셰이더 굽기가 필요해 못 했다) · 캐릭터가 움직이는 바닥의 속도를 받음(3D · 2D, 후속은 `MovingGroundFollowUps`) · GimmickCircuitState 2-d 통과(5/5) · AsyncResourceLoading A1 조사 · 문서 정리(측정 조건 형식 등) · `PairedAnimationDef` 캐시가 늘려 CI 를 깨던 테스트 수정. `worktree-agent-*` 옛 브랜치 넷은 워크트리를 지웠고 브랜치만 남았다(내용은 모두 옮겼으니 `git branch -D` 해도 된다).
- **CI**: main CI 실행 `39295170988` 의 실패는 모두 `AssetCacheRegistryTest.BuiltInCachesAreReachableThroughTheRegistry` 하나였고 고쳤다(`4548c65d2`). push 전에 `ci-jobs --runs` 로 직전 CI 를 확인한다.
- **vcpkg**: 워크트리는 메인의 `Tools/vcpkg` · `build/vcpkg_installed` · `build/vcpkg_cache` 를 정션으로 공유하고 다시 설치하지 않는다(사용자 지시, 분리하지 않는다). 후보 `WorktreeVcpkgInstalledDir` 는 지웠다. 워크트리를 지울 때는 정션을 먼저 `rmdir` 로 끊는다. `git worktree remove` 는 긴 경로에서 실패할 수 있다.
- **새 후보**: `UiTooltipAndCursor`(Candidates-ui), `ReflectionMaxCountConstant`(Candidates-engine). 런타임 UI 는 호버 경로 · `:hover` 스타일은 있고 툴팁 · 커서 모양 · 월드 위젯 입력(`WorldWidgetInput`)은 없다.
- 새 계획 문서: [AsyncResourceLoading](AsyncResourceLoading.md)(1/6, A2 착수 전 `design-reviewer` 검토, 텍스처 GPU 업로드의 워커 안전성은 A3 앞에 확인), [AudioChannelLayout](AudioChannelLayout.md)(U1 진행 중).
- 사용자 결정 대기: ReflectionFormats 는 진행 12/12 인데 상태가 구현중이다. 끝으로 바꿀지 확인한다. NetSettingsRemaining 은 설정 데이터 형식을 새로 정해야 해서 DataDriven 과 함께 다룬다.
- 앱 실행 후 스스로 종료되지 않는 문제: `-gv_screenshot` 으로 띄운 `App.exe` 가 캡처 뒤에도 안 꺼져 타임아웃(종료 코드 124)으로 끊긴다. 고칠 후보.
- GPU 나 독점 측정이 필요해 못 한 것: TypedComponents 인스펙터 선택기 화면 확인과 앱 측정 넷(8.4), IncrementalCook K0 측정, 프리팹 창 화면, Vfx V2 · V3.
- PairedAnimation 남은 것: P3(애니메이터 시각 지정 훅이 선행), 시나리오 테스트, M3 에디터. 재생 속도 소유자를 `SkeletalAnimatorComponent` 에 두는 구조 개선 후보.
- 야간 `Load (Linux | Release, nightly)` 근본 원인은 모른다(다음 야간 주석으로 읽는다). 측정 후보: 게임 스레드의 직렬 구간 Release 측정. 근본 원인 후보(미해결): `DiskPathVsResourceId`.

## 2. 첫 일

1. audiolayout 작업자 보고를 확인해 병합하고 `worktree-remove` 로 지운다.
2. push 하기 전에 `ci-jobs --runs` 로 직전 CI 를 확인한다. main 은 origin 보다 앞서 있다.
3. 병렬로 띄울 묶음(파일 소유가 겹치지 않으면): Roadmap 의 `[병렬 가능]` — Vfx V2 · V3 는 GPU 가 있어야 하니 `gpu-verifier` 와 함께, 카메라 디렉터(CameraRendererIntegration), 애니메이션 묶음(PairedAnimation · AnimationGraphLayers), 새로 쓴 정리(SerializerSilentSkips 등), AsyncResourceLoading A2(설계 검토 뒤). 동시 빌드는 최대 2.
4. 그다음은 [로드맵](Roadmap.md) 의 `## 지금 순서` 맨 위부터.

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
