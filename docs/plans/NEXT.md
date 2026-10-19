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

- **작업자 브랜치 넷이 남아 있다(병합 안 함, `.claude/worktrees/agent-*`).** 하니스의 `isolation: worktree` 로 만들어 `Tools/vcpkg` · `Tools/LLVM` 이 비어 있고, 작업자가 vcpkg 를 30분 넘게 처음부터 설치해 세 명을 중단시켰다. **작업자는 `py -3 -m Scripts worktree-make <이름> --base main` 으로 만든 워크트리에서만 돌린다.** 아래 브랜치의 커밋 · 변경은 새 워크트리로 옮겨 이어 간다.
  - `worktree-agent-a7aa1e7675ea7ab2b`(커밋 `035f8cb98`): RealElapsedTime — `io.DeltaTime` 을 엔진 경과로 맞춰 패널 다섯 곳을 한 곳에서 푼다. **빌드 · 테스트 안 함.** 남은 일: `Candidates-engine.md` RealElapsedTime 절과 `Automation/README.md:30` 정리.
  - `worktree-agent-a313581f5dbaafb7a`(커밋 `3daf83ecf` "작업 중" + 변경 2개): EditorMultiWorld 6절 (1) 프리팹 창. 빌드 안 함.
  - `worktree-agent-aec0f4ff3753fc47c`(커밋 없음, 변경 6개): Vfx V0 → V3. 코드를 쓰던 중에 멈춤.
  - `worktree-agent-a0979596d02674a46`(커밋 `189e0343e`): GpuVerification V2 절반 — 지는 RHI 케이스를 `isBackendUnderTest` 로 풂. **컴파일 확인 안 함.** `continue-on-error` 해제는 못 했다(Linux `gl` 원인 모름, 연속 세 번 초록 필요).
- 계획 문서를 새로 올렸다: [AsyncResourceLoading](AsyncResourceLoading.md)(0/6, 첫 단위 A1 조사), [AudioChannelLayout](AudioChannelLayout.md)(0/5, U1 이 먼저. 결정 다섯은 문서 2절에 있다).
- 확인함: 비동기 씬 로드 워커는 활성 씬의 `_listComponent` 와 겹치지 않는다. 인스펙터 4-b 는 이미 끝났다(`7ce9eafe2`). SerializerSilentSkips 의 범위 검사와 한 번만 경고는 이미 풀렸다. NetSettingsRemaining 은 설정 데이터 형식을 새로 정해야 해서 DataDriven 과 함께 다룬다.
- 후보 아이디어: GPU 셰이더 쪽 ASSERT · 로그(오류 버퍼에 원자적으로 기록해 CPU 가 읽음, Shipping 에서 제거). 셰이더 재빌드가 들어 `AsyncResourceLoading` B1 과 함께 한다. 아직 후보 절로 올리지 않았다.
- main 은 origin 보다 앞서 있다(**push 안 함**, 문서 커밋 몇 개). 앞서 push 한 `1a9dc0537` 까지는 `full-build` 증거가 있다.
- 앞서 병합한 것: 야간 Load 대응(`Scripts/qa/Load.py`), GimmickCircuitState 검증과 측정, PairedAnimation P0 ~ P2 검증과 상호작용 시작 · 에셋 캐시 · 세트 고르기.
- 야간 `Load (Linux | Release, nightly)` 근본 원인은 모른다. 다음 야간 실행의 주석(봇 종료 코드와 오류 줄)으로 읽는다. 다른 `Scripts/qa` 스크립트의 자식 산출물 읽기도 같은 모양인지 확인하지 않았다.
- GpuVerification V2(보고 전용 해제)는 `swrender` 단계가 실제로는 실패라 못 했다. 먼저 `RHIDeviceTest.DeviceCreationAllBackends`(Linux) · `BackendSweepVisitsEveryBackendThatStandsUp`(Windows)를 고치거나 의도한 로그를 `SW_TEST_DEFENSIVE_SCOPE` 로 감싼다.
- GPU 나 독점 측정이 필요해 못 한 것: TypedComponents 인스펙터 선택기 화면 확인과 앱 측정 넷(8.4), GimmickCircuitState 2-d 화면 확인, IncrementalCook K0 측정.
- PairedAnimation 남은 것: P3(애니메이터 시각 지정 훅이 선행), 시나리오 테스트(Shooter3D 활 · 화살 리소스 없음), M3 에디터. 재생 속도 소유자를 `SkeletalAnimatorComponent` 에 두는 구조 개선 후보.
- ReflectionFormats 는 진행 12/12 인데 상태가 구현중이다. 끝으로 바꿀지 사용자에게 확인한다.
- 측정 후보: 게임 스레드의 직렬 구간(`StructuralChangeBuffer::drain`, 프레임 시스템, 그룹 경계)을 Release 로 재서 더 병렬화할 만한지 본다. 컴포넌트 틱 자체는 이미 포크-조인 병렬이다. 루트의 `Multithreaded_Game_Simulation_Architecture.md` 는 추적되지 않는 초안이며, 이미 있는 것(태스크 의존성 · 작업 훔치기 · 병렬 for · 명령 버퍼)을 처음부터 쌓는 순서라 그대로 계획으로 올리지 않는다.
- 후보 아이디어: 메시 컴포넌트가 머티리얼 슬롯을 하나만 가져 서브메시별 염색이 안 된다(`CharacterAppearanceComponent` 는 부품을 별도 메시로 나눠 처리).
- 근본 원인 후보(미해결): `DiskPathVsResourceId`.

## 2. 첫 일

1. 남은 작업자 브랜치 넷을 `worktree-make` 워크트리로 옮겨 빌드 · 테스트로 확인하고 병합한다(1절). 워크트리는 메인의 vcpkg 폴더를 정션으로 공유하고 다시 설치하지 않는다(분리하지 않는다).
2. `AudioChannelLayout` U1(동작 불변의 평면 블록 형식) 또는 `AsyncResourceLoading` A1(조사)을 착수한다.
3. 문서 커밋을 push 하기 전에 `ci-jobs --runs` 로 직전 CI 를 확인한다.
4. 그다음은 [로드맵](Roadmap.md) 의 `## 지금 순서` 맨 위부터. `[병렬 가능]` 은 파일 소유가 겹치지 않으면 병렬로 띄운다.

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
