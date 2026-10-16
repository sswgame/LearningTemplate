# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법은 [검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../backlog/README.md)(영역별 파일 · 상태 태그, `py -3 -m Scripts backlog` 로 목록), 작업 방식은 [작업 방식](../11_Workflow.md), 병렬 작업자 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다. 이 문서에는 지금 상태 · 다음 할 일 · 함정 · 시작 절차만 둡니다.

## 1. 지금 상태 (2026-10-16 밤)

main 은 push 되지 않았다(원격 `main` 하나). 이 세션이 main 에 병합한 것: 틱 디스패치 최적화 일습(`runParallelByCost` · 선행 조건 스테이지 증분 · 애니메이션 비용 기반 분배 · 분배 기본 0), `tickfollowup`(증분 순서 = 재구축 순서, 테스트 결정화, `kParallelBucketCount` 는 옮기지 않기로), `gamescn`(자동화 대상 이름 규칙 · 시나리오 넷 · 앵커 삭제 · 마을 사람 NavAgent), `smallfix`(시계 주입 · 순서 키 비교 공유), `internalrule`(Internal 구조체 린트 + 기준선). 병합 때마다 Release 빌드와 해당 스위트만 돌렸다 — **Debug `-L race` · `-L nogpu` 는 아직 돌리지 않았다. push 전에 돌린다.**
**이 세션의 에이전트 결과는 이 세션에만 도착한다.** 새 세션은 아래 표의 워크트리를 `git log main..wt/<이름>` 로 보고 끝난 것을 병합한다(`git merge --no-ff`, 병합 뒤 `git merge-base --is-ancestor` 로 확인하고 `py -3 -m Scripts worktree-remove <이름>`).

| 워크트리 | 맡은 일 | 상태 |
|---|---|---|
| `editorscn2` | `D3-11` 에디터 시나리오: (1)(3)(4) 끝(커밋 3개), (2) 캐릭터 편집 → 씬 · 미검증 항목 · 프리팹 판정 한 함수로 모으기는 진행 중. main 의 `AutomationTarget` 규칙을 받아 `flowprefab target="Lantern"` 이 통과하는지 확인하라고 전했다 | 진행 중 |
| `terrainserver` | `F-1` 서버 지형 레이어 질의(가중치를 CPU 형식으로 서버 패키지에) | 진행 중 |
| `qualitycleanup` | quality `C-14` `C-15` `C-28` `C-33` `C-29` 와 이 PC 에서 되는 검증 | 진행 중 |
| `uifollow` | `C-8` `C-9` 측정 후 결정, `B-8` | 진행 중 |
| (워크트리 없음) 읽기 전용 조사 | 같은 모양의 함수 객체 · 람다 목록 → scratchpad `dup-shapes.md` | 진행 중 |

그 밖의 워크트리: `editordata` · `enginefallback` · `toolfollowup` 은 병합 안 된 커밋이 있다(다른 세션의 일 — 건드리지 않는다). `gamesdata` · `testisolation` 은 병합됐지만 변경이 남아 있다(132 · 10 개). `perfbase` · `perfbase2` 는 성능 기준선이다.

## 2. 다음 할 일

1. **에이전트 결과 거두기**: 위 표의 워크트리를 3 ~ 4 개씩 병합하고 그때마다 빌드 + `nogpu`. 마지막에 **Debug `-L race` · `-L nogpu`**.
2. **`Internal` 소거**: 린트(`Naming/MultipleInternalHelper` 23 파일 · `LoneFunctionInternalHelper` 165 구조체 · `DuplicateAnonymousFunction`)는 병합됐고 기존 위반은 `Scripts/lint/rules/CheckCodeConventions.toml` 의 `internalHelperBaseline`(180 개)에 있다. **디렉터리별로 파일을 규칙에 맞게 고치고(함수 하나 → 익명 namespace 자유 함수, 둘 이상 → 파일당 구조체 하나) 고친 파일의 기준선 줄을 지운다.** `mech-worker` 를 디렉터리별로 나누고 병합한 뒤 `-DSW_ENABLE_UNITY_BUILD` 로 유니티 충돌을 한 번 본다(`anonymousFunctionBaseline` 의 `findProbeDirector` 둘이 실제 충돌인지 포함).
3. **함수 객체 · 람다 공통화**: 조사 결과(`dup-shapes.md`, 없으면 다시 조사)에서 위험이 낮은 모양부터 공용 템플릿(`compareBy( &T::_x )` 류)으로 모은다. 2 번 뒤에 한다(같은 파일을 건드린다).
4. **정리 항목 중 파일이 겹쳐 미룬 것**: engine `C-4` `C-5` `C-13` `C-19` `C-22` `C-34`, project `C-16` `C-17` `C-18`. 2 번 뒤에.
5. **백로그 역할 분담 정비**: 항목마다 "건드릴 파일 · 겹침 주의"를 적고 겹치지 않는 묶음을 뽑는 도구(`py -3 -m Scripts backlog --batches` 류)를 만든다. 사용자가 이전 세션에서 시켰다고 기억하는 일 — 이 세션에서는 지시를 찾지 못해 위 뜻으로 이해했다. 의도와 다르면 사용자에게 확인.
6. **push 와 CI 첫 결과**(사용자 승인함: "CI 해보고"): 1 번 검증 뒤 push, 리눅스 · PCH 끈 Windows CI · `header-self-contained.yml`(Windows Debug 잡이 32 → 약 18 분이 되는지) · 야간 `load.yml`(`C-29` ⑤) 첫 결과. 리눅스 CI 가 `B-5` `B-6` `B-7` 을 풀 수 있다.
7. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃, [빌드 속도 계획](BuildSpeed.md)): 빌드가 도는 동안은 측정이 무효라 **다른 작업이 모두 끝난 뒤 혼자** 한다.
8. **이름 조회 흩어짐**: `SceneDevCommands` · Sequencer 의 이름 조회도 `AutomationTarget` 처럼 한 규칙으로(범위 밖이라 남았다).
9. 조건부 12 · 관찰 15 · 보류 3 · 기능 중 조건이 붙은 것은 조건이 올 때(백로그 안내 순서).

## 2-1. 이번 세션의 결정 (사용자가 상용 엔진 · AAA 기준으로 위임)

- `-gv_tickCostDispatch` 기본 **0**(옛 고정 임계값 16). 스위치와 API 는 남김. AAA 규모에서도 같다: 틱 수천 개는 어느 쪽이든 바로 나뉘고, 무거운 항목이 앞에 오면 비용 기반이 직렬로 돌려 꼬리가 길다. 근거는 결정 기록 5-6.
- `C-35`(`TickPhase` · 서브틱 우선순위): **유지**. 사용법은 `Component.h` 주석.
- `kParallelBucketCount`(8): 옮기지 않음(군중 공유 1000 · 300 에서 비용 기반이 더 느림, 결정 기록 5-6).
- 서브틱은 같은 프레임, 같은 그룹 안의 스테이지로 돈다(다음 프레임으로 넘어가지 않는다).
- 모든 새 글은 `docs/10_WritingDocs.md` 용어 대조표(시험 → 테스트), 문자열 서식은 `%#`(`formatstring`)이고 `to_string` · `+` 로 잇지 않는다. 한 함수를 위한 감싸는 함수를 만들지 않는다. `.cpp` 당 `Internal` 구조체는 하나, 함수 하나면 자유 함수.

### 재현 명령 (이 PC)
- Release 빌드 폴더는 `-DSW_GAMES=ALL` 로 구성돼 있다(구성은 `shared-lock acquire vcpkg --owner <이름>` 안에서). 게임 모듈만 짓기: `cmake --build build/Ninja-Release --target SWGame_<게임>`.
- 벤치(GameObjectBenchTest): `cd build/Ninja-Release/Bin && ../TestBin/EngineTest.exe --test_filter=GameObjectBenchTest.<이름> --load_suites=only --host_suites=all`.
- 합성 A/B: `py -3 -m Scripts perf-compare --app build/Ninja-Release/Bin/App.exe --cases tickCostMovers tickCostSmallScene tickCostChurn tickCostRagdolls --runs 5`.
- 랙돌 군중: `-gv_benchRagdolls=N -gv_benchRagdollPeriod=P`(IK 는 `-gv_benchRig=M` 을 같이).
- 시나리오: `App.exe -game=<게임> -dx12 -scenario=game/<게임>/automation/<이름>.scenario.xml`(종료 코드 0 = 통과).
- GPU 공유 잠금은 `py -3 -m Scripts shared-lock acquire gpu --owner <이름> --shared`, 놓을 때도 `--shared`(빼면 안 놓인다). 독점은 `--shared` 없이.
- 린트가 막으면 `py -3 Scripts/lint/fixer/FormatModified.py`, 설정 문서는 `py -3 Scripts/generate/GenerateConfigReference.py`. **`git commit -m x` 같은 임시 커밋으로 린트를 시험하지 않는다**(통과하면 그대로 커밋이 된다).

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
