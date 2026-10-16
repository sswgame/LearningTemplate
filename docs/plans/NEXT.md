# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법은 [검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../backlog/README.md)(영역별 파일 · 상태 태그, `py -3 -m Scripts backlog` 로 목록), 작업 방식은 [작업 방식](../11_Workflow.md), 병렬 작업자 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다. 이 문서에는 지금 상태 · 다음 할 일 · 함정 · 시작 절차만 둡니다.

## 1. 지금 상태 (2026-10-17 새벽)

main 은 push 되지 않았다(원격 `main` 하나). 이 세션이 main 에 병합한 워크트리: `tickfollowup` `gamescn` `smallfix` `internalrule` `mountgame` `qualitycleanup` `terrainserver` `editorscn2` `corefuzz` `enginefeature` `netfeature` `uifollow` `gfxfeature` `gameplayfeature`. 병합마다 전체 Release 빌드와 관련 스위트를 돌렸다(병합 중 의미 충돌 · Release 전용 실패 4 건을 main 에서 고쳤다: WildWest 탐침, `PhysicsSoftBodyTest` 허용치, 셰이더 쿠킹의 낡은 바이너리 80 개, `WildWest` 데이터 종류 등록).
Debug `-L lint` · `-L nogpu` · `-L race` 를 통과시키고(고친 둘: `WildWestHorseComponent` 의 단일 함수 `Internal`, 한 줄로 쓴 `prairie.scene.xml`) 2026-10-17 에 main 을 push 했다(`b21bf6c4f`).

남은 워크트리: `netfeature`(병합됨 — 단 HTTP 이름 해석을 전용 스레드로 옮기는 변경이 **커밋되지 않은 채** 작업 트리에 있다: `TestHTTP` · `TestPlatformLogin` · 결정 기록/백로그 문장 포함. 빌드가 되는 사람이 EngineTest 빌드 → HTTPTest · PlatformLogin → `-L race` 를 거쳐 커밋한 뒤 지운다), `editordata` · `enginefallback` · `toolfollowup`(병합 안 된 커밋 — 다른 세션의 일, 건드리지 않는다), `gamesdata` · `testisolation`(병합됐지만 변경이 남아 있다), `perfbase` · `perfbase2`(성능 기준선).

## 2. 다음 할 일

1. **CI 첫 결과 확인**(push 는 끝났다. 사용자가 실패를 알려 주면 `ci-jobs` 로 읽는다). 리눅스 · PCH 끈 Windows CI · `header-self-contained.yml`(Windows Debug 잡이 32 → 약 18 분인지) · 야간 `load.yml`. 리눅스 CI 가 `B-5`(퍼저 재확인) `B-7` `C-14`(린트 잡)를 풀 수 있다.
2. **`netfeature` 의 커밋 안 된 HTTP 이름 해석 스레드 변경**(위). 새로 찾은 결함도 있다: **Shipping 전용 서버가 게임 포트를 못 연다**(`gv_netListenPort` 가 Shipping 에 없는 테스트 전역 변수 — 백로그 network). `GameNetSession` 의 `-gv_netConnectAddress` 도 같은 호스트 이름 해석을 쓰게.
3. **`Internal` 소거**: 린트(`Naming/MultipleInternalHelper` · `LoneFunctionInternalHelper` · `DuplicateAnonymousFunction`)는 병합됐고 기존 위반은 `Scripts/lint/rules/CheckCodeConventions.toml` 의 `internalHelperBaseline`(180 개)에 있다. 디렉터별로 파일을 규칙에 맞게 고치고(함수 하나 → 익명 namespace 자유 함수, 둘 이상 → 파일당 구조체 하나) 고친 파일의 기준선 줄을 지운다. `mech-worker` 를 디렉터리별로 나눈다. 병합 뒤 `-DSW_ENABLE_UNITY_BUILD` 로 충돌을 한 번 본다(`anonymousFunctionBaseline` 의 `findProbeDirector` 둘 포함).
4. **함수 객체 · 람다 공통화**: 조사 요지 — 멤버 값으로 찾는 손 루프 약 337 건(214 파일), `VectorUtil::contains` 를 안 쓴 곳 115 건, 정렬 비교자 오름차순 45 · 내림차순 13 · 여러 키 약 41, 해시 구조체 15 개, erase-remove 약 67 건. 뿌리: `VectorUtil` 에 멤버 기준 찾기 · 비교 도구가 없고 `sw::unordered_map` 기본 해시가 `std::hash`. 제안: `VectorUtil::findBy<&T::_m>` · `findIndexBy` · `containsBy` · `removeIf` · `sortUnique`, 새 헤더 `Core/Container/CompareUtil.h`(`Ascending<&T::_m>` · `Descending` · `Lexicographic`), `HashUtil::makeHash`, 기본 해시 `sw::DefaultHash<Key>`(`computeHash()`). 위험: 순서가 결과에 나오는 곳(TickRegistry · PhysicsContact · NetPrioritizer · GPUSceneBuilder)은 전후 비교 테스트, 마지막 일치를 남기는 루프(`NetConnection.cpp:463`), 핫 경로 해시는 Release 재측정. 조사 표 원본은 이 PC 의 scratchpad `dup-shapes.md`(없으면 같은 지시로 다시 조사). 3 번 뒤에 한다.
5. **파일이 겹쳐 미룬 정리**: engine `C-4` `C-5` `C-13` `C-19` `C-22` `C-34`, project `C-16` `C-17` `C-18` — 3 번 뒤에.
6. **이 세션의 작업자가 끝내지 못해 남긴 것**(각 항목 본문에 이유가 있다): `D2-9` 반해상도 블룸 · `D2-5 (7)` 지형 벤치 Release 측정(독점 GPU 필요), `D7-1` 암호화 복제 Release 측정, `D1-4` 천 그리기(그래픽스 범위), `D4-3` `D4-5` `D4-6` `D4-7` `D4-9` `D4-10`, `D6-1`~`D6-6` ui 기능(손대지 않음), editor `D3-1` `D3-3`~`D3-10` `D3-12`, audio `D5-1` `D5-2`, `C-9` 글리프 SDF(프레임 예산 안 래스터화 측정이 먼저), `C-33`, `B-12` `B-14`(전체 hostgpu · Release 네 백엔드 스크린샷).
7. **백로그 역할 분담 정비**: 항목마다 "건드릴 파일 · 겹침 주의"를 적고 겹치지 않는 묶음을 뽑는 도구를 만든다. 사용자가 이전 세션에서 시켰다고 기억하는 일 — 이 세션에서는 지시를 찾지 못해 위 뜻으로 이해했다. 의도와 다르면 사용자에게 확인.
8. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃, [빌드 속도 계획](BuildSpeed.md)): 빌드가 도는 동안은 측정이 무효라 다른 작업이 모두 끝난 뒤 혼자.
9. 이름 조회 흩어짐(`SceneDevCommands` · Sequencer), `IFileWatcher` 를 쓰는 곳의 조용한 실패(리눅스 감시 마스크에 `IN_ATTRIB` 없음), `HUDViewModel` 의 슈터 전용 칸(WildWest 가 칸을 빌려 씀, `D4-15`).
10. 조건부 12 · 관찰 15 · 보류 3 · 조건이 붙은 기능은 조건이 올 때(백로그 안내 순서).

**병합 비용 교훈**: 작업자 둘을 따로 통과시킨 뒤 합치면 의미 충돌이 난다(`mountgame` + `gamescn`). 병합마다 전체 빌드를 돌리고 관련 스위트를 **Release 와 Debug 둘 다**(작업자가 Debug 만 돌려 Release 에서 깨진 테스트가 있었다) 돌린다. 작업자가 3 개 이상 빌드를 겹치면 서로 기다려 느려지고 측정이 무효가 되니 빌드 전 컴파일러 프로세스 수를 본다.

## 2-1. 이번 세션의 결정 (사용자가 상용 엔진 · AAA 기준으로 위임)

- `-gv_tickCostDispatch` 기본 **0**(옛 고정 임계값 16). 스위치와 API 는 남김. AAA 규모에서도 같다(결정 기록 5-6). `C-35`(`TickPhase` · 서브틱 우선순위) **유지**. `kParallelBucketCount`(8) 옮기지 않음. 서브틱은 같은 프레임, 같은 그룹 안의 스테이지로 돈다.
- 지형 레이어 가중치는 `.layerweights` 데이터 에셋(결정 기록 5-10), 캐릭터 프리셋은 데이터 프리셋 파일(5-11), 월드 UI 마커는 GPU 화면 앵커(5-7).
- 모든 새 글은 `docs/10_WritingDocs.md` 용어 대조표(시험 → 테스트), 문자열 서식은 `%#`(`formatstring`)이고 `to_string` · `+` 로 잇지 않는다. 한 곳만 쓰는 감싸는 함수를 만들지 않는다. `.cpp` 당 `Internal` 구조체는 하나, 함수 하나면 익명 namespace 자유 함수. 스크립트로 파일을 고칠 때 heredoc 금지(Write 로 `.py`). **`git commit -m x` 같은 임시 커밋으로 린트를 시험하지 않는다**(통과하면 그대로 커밋이 된다).

### 재현 명령 (이 PC)
- Release 빌드 폴더는 `-DSW_GAMES=ALL` 로 구성돼 있다(구성은 `shared-lock acquire vcpkg --owner <이름>` 안에서). 게임 모듈만: `cmake --build build/Ninja-Release --target SWGame_<게임>`.
- 벤치: `cd build/Ninja-Release/Bin && ../TestBin/EngineTest.exe --test_filter=GameObjectBenchTest.<이름> --load_suites=only --host_suites=all`. 합성 A/B: `py -3 -m Scripts perf-compare --app build/Ninja-Release/Bin/App.exe --cases tickCostMovers tickCostSmallScene tickCostChurn tickCostRagdolls --runs 5`.
- 시나리오: `App.exe -game=<게임> -dx12 -scenario=game/<게임>/automation/<이름>.scenario.xml`(종료 코드 0 = 통과). GPU 공유 잠금 `py -3 -m Scripts shared-lock acquire gpu --owner <이름> --shared`, 놓을 때도 `--shared`.
- 셰이더를 바꾸거나 병합이 `reflection.manifest` 에서 충돌하면 `App.exe --cook-shaders` 후 `ShaderCookRequestTest` 가 알리는 낡은 바이너리를 지운다(한 번에 8 개씩 알린다 — 0 이 될 때까지 반복).
- 린트가 막으면 `py -3 Scripts/lint/fixer/FormatModified.py`, 설정 문서는 `py -3 Scripts/generate/GenerateConfigReference.py`.

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
