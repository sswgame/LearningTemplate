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

- main 은 origin 보다 앞서 있다(**push 안 함**). 병합 결과의 `full-build` 는 성공했고 증거가 있다(Debug, 경고 0). push 전에 `ci-jobs --runs` 로 직전 CI 를 확인한다.
- 병합한 것: 야간 Load 대응(`Scripts/qa/Load.py`), GimmickCircuitState 검증과 측정, PairedAnimation P0 ~ P2 검증과 상호작용 시작 · 에셋 캐시 · 세트 고르기. 새 후보 `AsyncResourceLoading`(`Candidates-engine.md`, 방향 정함).
- 야간 `Load (Linux | Release, nightly)` 근본 원인은 모른다. 다음 야간 실행의 주석(봇 종료 코드와 오류 줄)으로 읽는다. 다른 `Scripts/qa` 스크립트의 자식 산출물 읽기도 같은 모양인지 확인하지 않았다.
- GpuVerification V2(보고 전용 해제)는 `swrender` 단계가 실제로는 실패라 못 했다. 먼저 `RHIDeviceTest.DeviceCreationAllBackends`(Linux) · `BackendSweepVisitsEveryBackendThatStandsUp`(Windows)를 고치거나 의도한 로그를 `SW_TEST_DEFENSIVE_SCOPE` 로 감싼다.
- GPU 나 독점 측정이 필요해 못 한 것: TypedComponents 인스펙터 선택기 화면 확인과 앱 측정 넷(8.4), GimmickCircuitState 2-d 화면 확인, IncrementalCook K0 측정.
- PairedAnimation 남은 것: P3(애니메이터 시각 지정 훅이 선행), 시나리오 테스트(Shooter3D 활 · 화살 리소스 없음), M3 에디터. 재생 속도 소유자를 `SkeletalAnimatorComponent` 에 두는 구조 개선 후보.
- ReflectionFormats 는 진행 12/12 인데 상태가 구현중이다. 끝으로 바꿀지 사용자에게 확인한다.
- 측정 후보: 게임 스레드의 직렬 구간(`StructuralChangeBuffer::drain`, 프레임 시스템, 그룹 경계)을 Release 로 재서 더 병렬화할 만한지 본다. 컴포넌트 틱 자체는 이미 포크-조인 병렬이다. 루트의 `Multithreaded_Game_Simulation_Architecture.md` 는 추적되지 않는 초안이며, 이미 있는 것(태스크 의존성 · 작업 훔치기 · 병렬 for · 명령 버퍼)을 처음부터 쌓는 순서라 그대로 계획으로 올리지 않는다.
- 후보 아이디어: 메시 컴포넌트가 머티리얼 슬롯을 하나만 가져 서브메시별 염색이 안 된다(`CharacterAppearanceComponent` 는 부품을 별도 메시로 나눠 처리).
- 근본 원인 후보(미해결): `DiskPathVsResourceId`, `WorktreeVcpkgInstalledDir`.

## 2. 첫 일

1. push 전 CI 확인(`py -3 -m Scripts ci-jobs --runs`), 문제 없으면 push.
2. `AsyncResourceLoading` 착수(핸들 형식 → 텍스처 · 메시 → 등장 비율과 디더 → 나머지). 시작 전에 비동기 씬 로드 워커와 `_listComponent` 순회가 겹치는지 확인한다.
3. 그다음은 [로드맵](Roadmap.md) 의 `## 지금 순서` 맨 위부터. `[병렬 가능]` 은 파일 소유가 겹치지 않으면 병렬로 띄운다.

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
