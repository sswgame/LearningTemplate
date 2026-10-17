# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법은 [검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../backlog/README.md)(영역별 파일 · 상태 태그, `py -3 -m Scripts backlog` 로 목록), 작업 방식은 [작업 방식](../11_Workflow.md), 병렬 작업자 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다. 이 문서에는 지금 상태 · 다음 할 일 · 함정 · 시작 절차만 둡니다.

## 1. 지금 상태 (2026-10-17 밤)

**(2026-10-17 늦은 밤) 지금 main 은 push 하지 않았습니다(마지막 push `97dfd14fd`).** 그 뒤 main 에 병합한 것은 리플렉션 단계 1(`reflectb` — 생성기 · `FixedLayout`) · 3(`FightingMatch` 롤백 B · 세이브 A) · 4-a(Mech · Horror 스냅샷 B) · 5-a(Hello 첫 필드 온라인 스키마 해시 · `UpdateRequired`) · 5-b(온라인 11 키트 B, `ServiceRecordUtil`, `OnlineMethodTable`), 스키마 이관 R4 · R5(`schemamigrate` — (타입, 기록된 버전)으로 등록하는 변환 함수 레지스트리), 에디터 EditorModel 경계(`editorboundary`)입니다.
마지막 두 병합(`schemamigrate`, `reflectstage5b`) 뒤 두 구성을 `shared-lock acquire vcpkg` 아래에서 다시 구성하고 `59a9e7d38` 에서 검증했습니다. Release · Debug `all AllTests` 경고 0, 두 구성 `-L lint`(73) · `-L nogpu`(53), Debug `-L race`(5) 모두 통과했고 고친 것은 없습니다. Shipping · `-L hostgpu` · 네 백엔드 × 대표 셋은 아직 돌리지 않았습니다.
**보류**: `wt/renameredirect`(이름 변경 리다이렉트 R1~R4)는 병합하지 않았습니다. 옛 이름 표가 "데이터에 별칭을 두지 않는다"(결정 기록 5-4)와 충돌합니다. 계획 문서 `RenameRedirects.md` 머리에 보류를 적었습니다.
**사용자 결정 대기**: ① `wt/renameredirect` 병합 여부(위 보류) ② `GimmickCircuit` 단계 2 구조 — 상태 바이트 `_stateBytes` 가 씬 · 프리팹 XML 과 레벨 세이브에 남아 세이브 형식을 먼저 정해야 합니다(`ReflectionFormats.md` 9절) ③ 서버 버스 메시지의 빌드 혼합 정책 `F-2`(한 빌드 클러스터 정책 또는 구독 때 빌드 해시 확인) ④ 지금 main 을 push 할지.
**돌고 있는 작업자는 없습니다.** 병합한 워크트리는 모두 지웠습니다. 남은 워크트리: `renameredirect`(보류), `editordata` · `enginefallback` · `toolfollowup`(병합 안 된 커밋 — 다른 세션의 일, 건드리지 않는다), `gamesdata` · `testisolation`(병합됐지만 변경이 남아 있다), `perfbase` · `perfbase2`(성능 기준선).
새 `REFLECT` 헤더가 들어오면 두 구성 모두 `cmake --preset` 으로 다시 구성해야 `CheckReflectionCandidates` 가 통과합니다(`shared-lock acquire vcpkg` 아래에서).

아래는 그 전 기록입니다. main 에 병합한 워크트리: `tickfollowup` `gamescn` `smallfix` `internalrule` `mountgame` `qualitycleanup` `terrainserver` `editorscn2` `corefuzz` `enginefeature` `netfeature` `uifollow` `gfxfeature` `gameplayfeature`. 병합마다 전체 Release 빌드와 관련 스위트를 돌렸다(병합 중 의미 충돌 · Release 전용 실패 4 건을 main 에서 고쳤다: WildWest 탐침, `PhysicsSoftBodyTest` 허용치, 셰이더 쿠킹의 낡은 바이너리 80 개, `WildWest` 데이터 종류 등록).
Debug `-L lint` · `-L nogpu` · `-L race` 를 통과시키고(고친 둘: `WildWestHorseComponent` 의 단일 함수 `Internal`, 한 줄로 쓴 `prairie.scene.xml`) 2026-10-17 에 main 을 push 했다(`b21bf6c4f`).

**(2026-10-17 오후) 아래가 모두 main 에 병합돼 push 됐다(`97dfd14fd`).** Release · Debug 모두 `-L lint` `-L nogpu` 통과, Debug `-L race` 통과(Release 에는 `race` 가 없다). 병합한 것: `netfeature`(HTTP 이름 해석 스레드) `internalgf` `internalrest` `internaltest`(`Internal` 소거 끝 — `internalHelperBaseline` 은 빈 목록) `serverport`(전용 서버가 `ServerConfig::_gamePort` 로 포트를 연다, `_gamePort` 0 = OS 가 고름) `containers`(`VectorUtil::findBy` 등 · `CompareUtil.h` · `DefaultHash`) `reflectfmt`(`ReflectionFormats.md` 설계) `c6graphics`(`C-16` 주석, Graphics · Renderer) `smalldebt`(이름 조회 한 곳 · `IFileWatcher` 끊김 · HUD 뷰모델 게임별) `visualizer`(시각화 색 테마 토큰 · LootTable/QuestCatalog 스키마) `shaderdoc`(bindless 입문 절) `loaders`(`C-19` R4: 스프라이트 · 시퀀스 · 스켈레톤 · 대화를 리플렉션으로). 병합 뒤 Debug 빌드가 낸 회귀 둘을 고쳤다(`TestLatticeModifier` 의 `static` 누락 — 지역 배열 주소 반환, `@struct` 문서 태그).

## 2. 다음 할 일

0. **리플렉션 병합 묶음 마무리**: Shipping 빌드와 `-L nogpu` · `-L hostgpu`, 네 백엔드 × 대표 셋(Empty · 에디터 · Shooter3D · NileCity), FightingMatch · Mech · Horror 게임 × 네 백엔드와 자동화 시나리오를 돌립니다. push 는 사용자 결정(1절 ④)을 따릅니다. 단계 1 끝에 기록하기로 한 실행 파일 크기 증가(단계 1 전후 Release `App.exe` · 모듈 DLL 크기)를 `ReflectionFormats.md` 에 적습니다.
   - 리플렉션 남은 단계(`ReflectionFormats.md` 9절 표 순서): `D7-7` 넷 채널 등록 · 6 컴팩트 모드 삭제 · `cloneObject` B · 7-a 타입 캐시 · 열거형 이름 배열 · 7-b 다중 상속 생성 오류 · 7-c 타입별 컴포넌트 목록 · 7-d `TComponentHandle<T>`. 4-b(인스펙터 `Min` · `Max`)도 남았습니다. 단계 2 는 보류(1절 ②)입니다.
   - 직렬화 결함과 정리: `C-38` `REFLECT( Alias )`(`_listAlias`) 제거, `A-13` 자기 기술형 바이너리가 모르는 타입을 1 바이트 건너뛰고 성공으로 읽음, `A-14` `BitStream` 손 코덱이 범위 밖 값을 조용히 자름, `A-15` `PROPERTY( Min · Max )` 를 고정 배치형만 지킴, `A-16` 계정 서버 응답 쓰기 실패 뒤 세션이 남음.
1. **CI 첫 결과 확인**(push 는 두 번 끝났다 — `b21bf6c4f`, `97dfd14fd`. 사용자가 실패를 알려 주면 `ci-jobs` 로 읽는다). 리눅스 · PCH 끈 Windows CI · `header-self-contained.yml`(Windows Debug 잡이 32 → 약 18 분인지) · 야간 `load.yml`. 리눅스 CI 가 `B-5`(퍼저 재확인) `B-7` `C-14`(린트 잡)를 풀 수 있다.
2. 넷 설정의 남은 것: `gv_netLoopbackOnly` 도 테스트 전역 변수에서만 읽는 같은 모양, `_gamePortShardCount` 는 계약만 있고 구현이 없다(`D7-3` ⑤), Shipping 클라이언트에는 `gv_netConnectAddress` 가 없다.
3. `Internal` 소거의 남은 것: `-DSW_ENABLE_UNITY_BUILD` 로 충돌을 한 번 본다(`DuplicateAnonymousFunction` 은 `Source/` 만 보므로 `Test/` 이름 충돌은 직접 확인), 병합 때 작업자끼리 겹친 익명 함수 이름이 한 건 났다(`makeRandomSeed`).
4. 함수 객체 · 람다 공통화 1 단계(`VectorUtil::findBy` · `CompareUtil.h` · `DefaultHash`)는 `containers` 로 병합됐다. 남은 것은 11 번 "공통화 2단계"다.
5. **파일이 겹쳐 미룬 정리**: engine `C-4` `C-5` `C-13` `C-19` `C-22` `C-34`, project `C-16` `C-17` `C-18` — 3 번 뒤에.
6. **이 세션의 작업자가 끝내지 못해 남긴 것**(각 항목 본문에 이유가 있다): `D2-9` 반해상도 블룸 · `D2-5 (7)` 지형 벤치 Release 측정(독점 GPU 필요), `D7-1` 암호화 복제 Release 측정, `D1-4` 천 그리기(그래픽스 범위), `D4-3` `D4-5` `D4-6` `D4-7` `D4-9` `D4-10`, `D6-1`~`D6-6` ui 기능(손대지 않음), editor `D3-1` `D3-3`~`D3-10` `D3-12`, audio `D5-1` `D5-2`, `C-9` 글리프 SDF(프레임 예산 안 래스터화 측정이 먼저), `C-33`, `B-12` `B-14`(전체 hostgpu · Release 네 백엔드 스크린샷).
7. **백로그 역할 분담 정비**: 항목마다 "건드릴 파일 · 겹침 주의"를 적고 겹치지 않는 묶음을 뽑는 도구를 만든다. 사용자가 이전 세션에서 시켰다고 기억하는 일 — 이 세션에서는 지시를 찾지 못해 위 뜻으로 이해했다. 의도와 다르면 사용자에게 확인.
8. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃, [빌드 속도 계획](BuildSpeed.md)): 빌드가 도는 동안은 측정이 무효라 다른 작업이 모두 끝난 뒤 혼자.
9. 이름 조회 흩어짐(`SceneDevCommands` · Sequencer), `IFileWatcher` 를 쓰는 곳의 조용한 실패(리눅스 감시 마스크에 `IN_ATTRIB` 없음), `HUDViewModel` 의 슈터 전용 칸(WildWest 가 칸을 빌려 씀, `D4-15`).
10. 조건부 12 · 관찰 15 · 보류 3 · 조건이 붙은 기능은 조건이 올 때(백로그 안내 순서).
11. **구조 최적화(2026-10-17 조사, 측정이 아니라 구조 기준 — 근거는 읽은 코드)**. 하지 말 것: 스핀 늘리기 · hot pool · 깨우기 사슬 변경(결정 기록 09:102 · 136 · 137에서 손해 확인), Math SIMD(금지 결정), 큐 칸 패딩 · `TaskNode` 정렬(잡음으로 끝난 전례).
    - **A 동시성** — 먼저 Core 에 '스레드 칸에 쓰고 읽는 쪽이 합치는' 샤드 카운터 형식을 설계(`integrator`), 그 위에서 순서대로: ① 워크 훔치기 데크 빈 확인(`WorkStealingDeque.h:82-126`, 비어 있으면 펜스 전에 relaxed 로 반환) ② 대기자 스핀을 워커 루프처럼 세대(`_workEpoch`)로 막기(`TaskManager.cpp:892-922`) ③ 메모리 태그 통계(`MemoryProfiler.cpp:319-386`, 먼저 `alignas(64)` 다음 스레드별 블록) ④ 활성 태스크 수 샤드(`TaskManager.cpp:457` 외) ⑤ 비동기 로거 생산자(`AsyncLogSink.cpp`, 날짜 캐시 thread_local · 불변 스냅샷 · 대기자 있을 때만 깨우기).
    - **B 컨테이너 · 네트워크** — ① `SequenceBuffer` 크기를 2의 거듭제곱 템플릿 상수로 + 칸 용량 유지(잠복 결함: 크기가 65536 약수가 아니면 uint16 감김 때 칸이 겹침) ② `sw::deque` 를 바이트 기준 블록으로 직접 구현 + 고정 용량 큐는 링 버퍼(MSVC STL deque 블록 16B → 원소마다 할당; 주소 안정에 기대는 곳 `NetTransport.h:121` 주의) ③ 해시 표 노드 `uint32 next + 해시 태그`(map · set 표 코어 하나, `containers` 병합 뒤) ④ `BitStream` 64비트 스크래치 워드(와이어 바이트 동일성 테스트 먼저) ⑤ `Memory::copy/compare` 헤더 inline. 보류: `NetConnection` 수신 아레나(측정 기각 `C-4`), `NetPrioritizer` 힙, UDP `recvmmsg`(`B-10`).
    - **C 리플렉션** — 설계: `docs/plans/ReflectionFormats.md`(자기 기술형 유지 + 고정 배치형 신설, 스키마 해시 1개, 코드젠이 타입별 write/read · `==` 생성, `castTo` 는 클래스 ID + 상속 구간 정수 비교 검토). 이 문서가 단계 표를 가진다. 단계 1 · 3 · 4-a · 5-a · 5-b 는 끝났고 2 는 보류다. 별도로 ②③⑤⑥ 적용 후보(위 워크트리). 리플렉션 코드젠은 헤더만 읽으므로 `.cpp` 내부(익명 namespace) 형식에는 못 쓴다 → 모듈 내부 헤더로 옮겨 쓴다.
    - **D 셰이더 bindless 입문** — `shaderdoc` 결과를 받은 뒤 분기를 숨기는 진입 함수 구조 변경을 따로 결정.
    - **공통화 2단계**: `containers` 가 남긴 후보 58 파일 80 건(`find_if` · `remove_if` · 해시 구조체)을 디렉터리별로. 손 멤버 루프를 못 쓰게 하는 형식 또는 린트(형식으로 못 막는 이유를 적는다).
    - 에이전트: 설계 · 모듈을 넘는 일은 `integrator`(Opus), 국소 구현은 `worker`(Opus), 이름 · 주석 · 반복은 `mech-worker`(Sonnet) — 이 세션에서 `worker` 정의가 Opus 인 것을 확인했다.

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
