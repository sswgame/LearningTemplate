# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법은 [검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../backlog/README.md)(영역별 파일 · 상태 태그, `py -3 -m Scripts backlog` 로 목록), 작업 방식은 [작업 방식](../11_Workflow.md), 병렬 작업자 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다. 이 문서에는 지금 상태 · 다음 할 일 · 함정 · 시작 절차만 둡니다.

## 1. 지금 상태 (2026-10-16)

main 에 들어갔고 push 되지 않은 것: finalverify 병합(배포본 테스트 둘 개발 빌드 전용 표시 등 — 2026-10-15 CI 의 Shipping 4 잡 실패 원인), 백로그를 `docs/backlog/` 로 나눔, CI 교착 검출 컴파일 확인을 매일 잡으로 이동. 원격 브랜치는 `main` 하나.
진행 중인 작업자(결과 미확인): `testspeed`(테스트 시간 줄이기 · `race` 라벨 · `-j` 병렬화, `D:\w	estspeed`), `shaderrefactor`(셰이더 공통화 · 매크로 제거 · 파일 이름 lower_snake_case). 끝났는지 `git worktree list` 와 `git log` 로 본다. 테스트 시간의 근거: Debug `AppTest_HostOnly` 한 바퀴 57 분, 같은 검증이 Shipping 에서 65 초(`DataRaceDetector` 가 `SW_DEBUG` 에서만 켜져 레이스 검사는 Debug 가 맡는다).

## 2. 다음 할 일 (백로그 번호)

1. **랙돌 + IK 스트레스는 측정했다**(`-gv_benchRagdolls=N`, `perf-compare --cases tickCostRagdolls`): 틱 분배는 병목이 아니고 비용은 `GT.Animation.evaluate` ~0.5 ms. 남은 것은 `AnimationCrowd` 의 `kParallelBucketCount`(8) 비용 기반 이전 여부(애니메이션 스위치가 없어 `git stash` 바이너리 A/B) — 2-1 절. 커밋은 아직이다.
2. **CI 첫 결과 확인** — 리눅스 · PCH 끈 Windows CI 는 빌드 속도 변경(PCH 에서 헤더 여섯 제거, 임포트 라이브러리 갱신)으로 아직 돌지 않았다. 교착 검출 컴파일 확인을 옮긴 `header-self-contained.yml` 도 첫 실행을 본다(Windows Debug 잡이 32 → 약 18 분이 되는지). 야간 `load.yml` 은 한 번도 돌지 않았다(`C-29` ⑤).
3. **`D5-4` · `D5-3` · `D3-11`** — 시나리오 자동화의 남은 것(탑승 · 마을 사람 길 찾기 · 에셋 임포트 흐름).
4. **빌드 속도 12 단계** — 리플렉션 헤더 팬아웃([빌드 속도 계획](BuildSpeed.md)).

## 2-1. 이번 세션이 남긴 것 (2026-10-16, **커밋 전** — 작업 트리 21 개 파일)

틱 디스패치 최적화 일습이다. 측정 근거와 결정은 [결정 기록](../09_Decisions.md) 5-6, 방법은 [검증과 측정](../08_Verification.md)의 "틱 디스패치 벤치".
주제별로 나눠 커밋한다(커밋 전에 `git diff --stat` 로 CRLF 가 깨진 파일이 없는지 본다 — 지금은 모두 CRLF 그대로다).

| 묶음 | 파일 | 내용 |
|---|---|---|
| 병렬 API | `Source/Core/Task/TaskManager.{h,cpp}` · `Source/Engine/Common/EngineParallel.h` · `Test/CoreTest/Task/TestTaskManager.cpp` | `runParallelByCost`(앞 항목을 1, 2, 4 … 개씩 직접 돌아 비용을 재고 나눌지 고른다. 4096 개 이상은 재지 않고 나눔). 테스트 3 개 |
| 씬 틱 | `Source/Engine/Object/GameObject/Tick/SceneTickScheduler.cpp` | 보통 길 · 스테이지가 새 API 를 쓴다. `-gv_tickCostDispatch=1` 이 비용 기반(기본 0 = 옛 고정 임계값 16) |
| 선행 조건 스테이지 | `Tick/TickRegistry.{h,cpp}` · `Test/EngineTest/Object/TestTickPrerequisite.cpp` · `TestComponentTick.cpp` | 재구축을 평평한 표 · CSR 로(559 → 275 us), 말단 오브젝트의 증분 끼우기 · 빼기(→ 69 us), 선행 조건 순환을 오류 로그로. 회귀 테스트 2 개 |
| 애니메이션 | `Source/Engine/Object/SceneAnimation/AnimationSystem.{h,cpp}` · `AnimationCrowd.cpp`(주석만) | 단계 평가를 비용 기반으로(`kParallelUnitCount` 삭제). 캐릭터 20 개 122 → 81 us, 100 개 196 → 163 ~ 180 us, 400 개 같음 |
| 시나리오 · 탐침 | `Source/Games/StarSkirmish/SkirmishUnitComponent.{h,cpp}` · `Resource/game/starskirmish/automation/viewsync.scenario.xml` | `Skirmish.ViewLagMax` 탐침과 뷰 동기 시나리오(뷰가 디렉터와 같은 그룹이면 레이스 검출기가 잡는다) |
| 랙돌 군중 벤치 | `Source/Games/Empty/BenchScene.{h,cpp}` · `BenchCombatComponent.{h,cpp}` | `-gv_benchRagdolls` · `-gv_benchRagdollPeriod`, `spawnEnemy` 분리 |
| 벤치 · 하니스 | `Test/EngineTest/Object/TestGameObjectBench.cpp` · `Scripts/qa/PerfCompare.py` | 벤치 케이스 여섯(`SmallGroupTickKnee` · `PrerequisiteChainDepthSweep` · `PrerequisiteFanOut` · `PrerequisiteSpawnChurn` · `FollowerOrderingGroupVersusPrerequisite` · `IntraObjectPhaseOrdering`), `perf-compare` 케이스 `tickCostMovers` · `tickCostSmallScene` · `tickCostChurn` |
| 문서 | `docs/08_Verification.md` · `docs/09_Decisions.md` · `docs/backlog/engine.md` · 이 문서 | |

### 결정 (2026-10-16, 사용자가 상용 엔진 기준으로 위임)
1. `-gv_tickCostDispatch` 기본은 **0**(옛 고정 임계값 16)으로 바꿨다. 스위치와 API 는 남겼다. 근거는 결정 기록 5-6.
2. `C-35`(`TickPhase` · 서브틱 우선순위): **유지**로 닫고 백로그에서 지웠다. 근거는 결정 기록 5-6, 사용법은 `Component.h` 주석.

### 다음에 할 일 — 랙돌 + IK 스트레스 (사용자 지시)
지금 측정은 걷기 · 대기 캐릭터와 가벼운 무버뿐이고 IK 그래프와 랙돌은 재지 않았다. `-gv_benchCombat` 은 적 하나를 정해진 시간표로 쓰러뜨리는 연출이라 대량 부하가 못 된다.
- 새 벤치 스위치(예: `-gv_benchRagdolls=N`, `Source/Games/Empty/BenchScene.cpp`)로 `RagdollComponent`(`Source/Engine/Character/Hit/`, 모양은 `BenchCombatComponent.cpp`) 캐릭터 N 개를 세우고 프레임마다 일부를 래그돌 켜기 · 끄기(기상 클립)로 번갈아 틱 멤버십 · 애니메이션 유닛 활성이 자주 바뀌게 한다. IK 는 `RigIKSolver`(`Source/Engine/Animation/Rig/`)를 쓰는 캐릭터를 같이 둔다.
- 재는 값: `GT.Animation.evaluate` · `GT.Scene.tick.registry` · `GT.Scene.tick.components` · 물리 step. 틱 쪽 전후는 `-gv_tickCostDispatch=0|1`, 애니메이션은 스위치가 없어 `git stash` 바이너리 A/B(옛 코드는 `git diff` 로 되살린다).
- 이 결과로 `AnimationCrowd` 의 `kParallelBucketCount`(8)도 비용 기반으로 옮길지 정한다. 다른 호출처(트랜스폼 계층 · GPU 씬)는 이미 재 보고 옮기지 않기로 했다(결정 기록 3절).
- 남은 구조 후보: 선행 조건 사슬 깊이 8 ~ 16 에서 단계당 ~28 us(스테이지마다 포크-조인).

### 재현 명령 (이 PC)
- Release 빌드 폴더는 `-DSW_GAMES=ALL` 로 다시 구성해 게임 모듈이 들어 있다(로컬, 구성은 `shared-lock acquire vcpkg` 안에서).
- 벤치(GameObjectBenchTest): `cd build/Ninja-Release/Bin && ../TestBin/EngineTest.exe --test_filter=GameObjectBenchTest.<이름> --load_suites=only --host_suites=all`(작업 폴더가 `Bin` 이 아니면 DLL 을 못 찾아 종료 코드 -1073741515).
- 합성 A/B: `py -3 -m Scripts perf-compare --app build/Ninja-Release/Bin/App.exe --cases tickCostMovers tickCostSmallScene tickCostChurn --runs 5`.
- 실제 게임 A/B: `App.exe -game=<이름> <Test/QA/Games.json 의 arguments> -gv_profileFrames=900 -dx12 -gv_tickCostDispatch=0|1` 을 번갈아 3 판, `Scripts/common/AppRun.py` 의 `runApp` · `parseProfileTable` 로 `GT.Scene.tick.components` p50 · p99 를 읽었다(측정 스크립트는 저장소에 없다 — 이 줄이 전부다).
- 캐릭터 애니메이션: `-gv_benchMeshes=0 -gv_benchCharacters=<N> -gv_benchCharacterColumns=20 -gv_benchCharacterStagger=1 -gv_profileFrames=600`, 값은 `GT.Animation.evaluate`.
- GPU 공유 잠금은 `shared-lock release gpu --shared` 로 놓는다(`--shared` 를 빼면 놓이지 않아 다음 잠금이 영원히 기다린다). 독점 잠금은 `--shared` 없이 놓는다.

## 3. 함정

- 빌드 로그를 bash `grep` 으로 세지 않는다: `Start-Process *>` 로그는 UTF-16 이라 항상 0 이다. `Out-File -Encoding utf8` 로 쓰고 `Select-String | Measure-Object` 로 센다.
- `worktree-remove` 는 브랜치도 지운다. 병합 뒤 `git merge-base --is-ancestor` 로 확인하고 지운다(커밋이 없는 변경이 있으면 건너뛴다 — 버려도 되면 `--force`).
- 앱 창을 전경으로 올려 캡처하지 않는다. 게임 뷰는 `-gv_screenshot`, 에디터 UI 는 `-gv_screenshotEditor` · `<CaptureEditorUI>`([에디터 UX 기반](EditorUX.md)).
- Edit 도구와 `sed -i` 가 CRLF 파일을 LF 로 바꾼 적이 있다. 수정 뒤 `git diff --stat` 로 파일 전체가 바뀌지 않았는지 본다.
- 정렬 할당 객체를 기반 포인터로 지우면 배포본 힙이 깨진다(Debug 는 조용). 모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 않는다.
- 무효 bindless 인덱스(`0xFFFFFFFF`)는 DX12 페이지 폴트, Vulkan DEVICE_LOST 다. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders`.
- `castTo<Component>( Component*, 타입 )` 은 항상 성공한다. 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )`.
- 새 .cpp 가 "incomplete type" 으로 서면 `Component.h` · `GameObject.h` · `Scene.h` · `SceneManager.h` · `RHITypes.h` 는 PCH 에 없으니 직접 include 한다.
- 에디터 모듈의 새 REFLECT 헤더는 `Source/Editor/CMakeLists.txt` 의 `REFLECTION_HEADERS` 에 넣는다(안 넣으면 `StaticType` 미정의 링크 오류).
- 같은 PC 에서 빌드 · 테스트를 겹치면 시간 제한에 걸려 다시 돈다. 검증 중에는 다른 빌드를 하지 않는다. 비결정적 실패는 3~5 회 재실행해 재현율을 적는다.
- `D:\w\perfbase` · `perfbase2` 워크트리는 성능 기준선이라 남긴다.

## 4. 새 세션 시작 절차

1. 이 문서, [작업 방식](../11_Workflow.md), [작업자 규칙](WorkerRules.md) 순서로 읽는다. 백로그는 `py -3 -m Scripts backlog` 의 표와 `--id <번호>` 로 내 항목만. C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 의 해당 절.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
