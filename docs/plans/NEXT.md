# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법은 [검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../backlog/README.md)(영역별 파일 · 상태 태그, `py -3 -m Scripts backlog` 로 목록), 작업 방식은 [작업 방식](../11_Workflow.md), 병렬 작업자 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다. 이 문서에는 지금 상태 · 다음 할 일 · 함정 · 시작 절차만 둡니다.

## 000. 2026-10-18 세션 인계 — 통합 `integ7` · `integ8` · `integ9` (가장 먼저 읽는다)

- **main 에 들어간 것**(이 세션 전체):
  - 정리 여덟 브랜치: `cl-a` C-37, `cl-b` C-5, `cl-c` C-40 · C-63, `cl-d` C-46 일부, `cl-f` C-34 · C-48, `cl-g` C-19 · C-64, `cl-h` C-44 일부 · C-50, `cl-i` C-12 · C-15(imgui-node-editor 오버레이 제거).
  - 설계 구현 `impl` · `impl-h` 39 단위 중 26: C-1 E1 · E2 · E5 · E6, C-53 A1 ~ A4, C-52 T1 · T2, C-42 K1 ~ K5(작업 입력 기록 · 로컬 DDC · 팩 증분), C-30 D0 · D1 · D2 ~ D5(키트 카탈로그를 리플렉션 스키마로 — 각도 칸 일곱은 라디안 `Units=rad`, 도 단위 API 와 섞이는 곳 없음을 확인), C-18 L3, C-41 V1, 함수 크기 보고.
  - 기능 `feat` · `feat-net` · `feat-pv`: F-1, D1-5.3, D7-3 ①②③⑥, D3-10 워프 내보내기, D4-5.1, D4-3, D4-6 ①, D5-1 (2), D3-4 `cheat.god`, D3-7 (2) 선택 접근자.
  - 리소스 `resl`: 리소스 나열은 `ResourceIO::collectResources` 하나, 디스크 경로는 `LooseFilePath` 형식(문자열로 저절로 바뀌지 않음), 런타임 게임 코드의 디스크 접근은 `CheckRuntimeResourceDiskAccess` 가 막는다.
  - vcpkg 환경 수정 `dcdc813e2`: 프리셋이 `VCPKG_OVERLAY_TRIPLETS` 를 환경 변수로만 넘겨, 재구성(ninja · ctest `BuildTestBinaries` · `ModuleCompiler`)이 기본 triplet(MSVC)으로 공유 설치를 덮던 결함이다. 이제 캐시에 박는다.
- **통합에서 푼 것**: 백로그 C-44 · C-46, `MakePackage.py` 스테이징(cl-d 의 이름으로 팩 고르기), `ko.tm.json`, `ResourceIO.cpp`(resl 의 팩 람다 형식 + impl 의 쿠킹 입력 기록), feat-pv 테스트의 옮겨진 `CommandStack.h` 경로.
- **사용자 결정(2026-10-18, 반영함)**: R1 — 리플렉션 별칭(`Alias` · `ValueAlias`)과 별칭 읽기 경로는 기능을 두되 출시 데이터가 이관할 것이 생기기 전에는 쓰지 않는다(결정 기록 5-4). 지금 쓰는 곳은 `Test/ReflectionTest` 의 테스트 타입뿐이다. C-42 — 공유 DDC(옛 K6)는 하지 않고 로컬 캐시만 둔다.
- **다음 일**:
  1. 측정 · GPU 묶음(한 에이전트): E3, T3, K0(단계별 쿠킹 시간), V2(CI 소프트웨어 렌더러 보고 전용 해제). 함께 볼 것: 팩 빌드에서 외형 프리셋 로드, 미리보기 창 UI, 기능 쪽 화면 확인(넷 재생 창 배치 · 합친 트레이스 Timeline, 오디오 이벤트 창, 워프 단추, Shooter3D 상체 레이어), `-L hostgpu`, 네 백엔드 × Empty · 에디터 · Shooter3D · NileCity.
  2. 데이터화: C-30 D6 ~ D9, 계획의 P1 · P2 · P4 · G1 · G2.
  3. `feat-pv` 의 남은 D3-7 (1) 프리팹 창과 그 선행 둘.
  4. 기존 문서의 "뿌리"를 "근본 원인"으로 일괄 치환(`mech-worker` 별도 작업, 용어 대조표 `docs/10_WritingDocs.md`).
- **백로그 후보**: 워크트리마다 `VCPKG_INSTALLED_DIR` 를 나눈다. `dcdc813e2` 로 triplet 은 맞췄지만, 매니페스트가 다른 워크트리(오버레이 포트가 다른 옛 main 기준 등)가 같은 `build/vcpkg_installed` 에 서로 다른 포트를 번갈아 깐다.
- **함정**:
  - `feat-net` 워크트리는 추적하지 않는 로그만 남아 `worktree-remove` 가 건너뛰었다. 사용자가 `py -3 -m Scripts worktree-remove feat-net --force` 를 직접 실행한다.
  - EngineTest 샤드는 `-j 8` 에서 104 s 시한을 넘길 수 있다(혼자 40 s). `-j 6` 이하로 돌린다.
- **작업 방식(이 세션에서 정함)**: 준비된 브랜치부터 통합한다. 작업자 최종 검증은 생략하고, 통합 워크트리에서는 빌드만 한 뒤 main 에 합치고 main 에서 한 번에 검증한다(검증 전에는 push 하지 않는다 — [작업 방식](../11_Workflow.md)).
- **사용자 결정 대기**: 없음.

## 이전 인계 요약 (2026-10-17 ~ 10-18 오후)

- 2026-10-18 오후: 결함 A-20 · A-23 ~ A-27, 정리 C-39 · C-54(일부) · C-56 · C-58 ~ C-60 · C-62 를 병합했다. 합친 뒤 검증이 작업자 단독 검증이 놓친 회귀 셋(PythonTest 둘, Vulkan 백버퍼 `Load`, STL `make_unique` 모호성)을 잡았다.
  남은 것: CI 결과 확인(`py -3 -m Scripts ci-jobs --runs`; 야간 `Load (Linux | Release, nightly)` 가 `d8037cd` 부터 1 s 로 진다, 조사 안 함), `B-16` GPU 확인, `C-61` 생성기 규칙, `C-57` 백로그 하위 번호 정리.
- 2026-10-18 새벽 `integ6`: feat2 ~ feat22 를 병합했다. 재지 못한 측정 — feat16 `RT.GPUScene.vertexPool` · `RT.Deform.rebuildMorphPool`, feat10 `saveState` 시간 · B 바이트 크기, feat9 캐스트 적중 경로. Ninja-Release 프리셋에 `SW_GAMES` 가 비어 hostgpu 의 editorstartup `PanelCount` 41/42 · AppSmoke themepark 둘이 진다(구성 문제, 프리셋에 둘지는 다음 결정).
  Shooter3D 는 1 인칭이라 캐릭터 걷기 · 조준 · 발 IK · 마스크 레이어를 화면에서 보지 못했다.
- 2026-10-17: 러너 이미지 `ubuntu-24.04` · `windows-2025`(라벨 정본 `.github/runners.json`), CC0 오디오 · 3D · 이미지 도입(`res1` ~ `res3`, 남은 확인 `B-15`), 새 게임 `PixelTown`. `gamesdata` 워크트리의 커밋 안 된 W4 변경은 적용하지 않았다(검증 안 됨, 충돌) — 이어 할 때는 W4(`DataDriven.md` 3절)로 다시 맡긴다.
  결정의 정본은 [결정 기록](../09_Decisions.md) 5-12 표다. 스크립팅 층은 두지 않는다. 비공개 전환 · GPU 러너 · 자체 호스트 러너는 하지 않는다.

## 2. 다음 할 일

0. **검증 묶음 마무리**(1절 "다음 세션 첫 일"). 단계 1 끝에 기록하기로 한 실행 파일 크기 증가(단계 1 전후 Release `App.exe` · 모듈 DLL 크기)를 `ReflectionFormats.md` 에 적습니다(`reflect7ab` 작업자가 7-a 만의 크기를 잰 값이 있다: `Engine.dll` +0.08 %).
   - **구현 대기 중인 설계(순서)**: ① `GimmickCircuit` 단계 2 는 브랜치 `wt/feat10`(워크트리 `D:\w\feat10`)에 코드 · 테스트로 커밋했고 빌드 · 테스트 · GPU 확인(2-d)을 하지 않았다 — 통합 워크트리에서 새 `REFLECT` 헤더 셋(`GimmickCircuitState.h`, `GimmickStateBundle.h` 가 REFLECT 로 바뀜)을 위해 다시 구성하고 `TestGimmick*` · `TestGimmickScene` · `TestGimmickState` 를 돌린다. 측정 방법은 `GimmickCircuitState.md` 8절 ② 서버 버스 A 전환(`ServerBusFormat.md` 단계 0~6, 새 읽기 정책 "모르는 이름만 버리고 아는 필드를 못 읽으면 거절", `ServerBusTopic<T>`) ③ 7-c 타입별 버킷(정확한 타입마다 버킷, 조회 때 `isDerivedFrom` — `TypedComponents.md`) 다음 7-d(호출부 52곳을 파일이 겹치지 않는 A~E 다섯 그룹으로 병렬) ④ 4-b 인스펙터가 이름 형태 `Min` · `Max` 를 읽게(`InspectorPropertyLayout.cpp:139`, `InspectorPropertyManager.cpp:414-433`, `ReflectionTypes.h:102-110` 메타가 float 하나뿐인 형식이 뿌리). 구현 전에 `design-reviewer` 로 한 번 검토합니다.
   - **`ServerBusFormat.md` 구현 전에 반영할 `design-reviewer` 검토 결과(결론: 수정해서 진행, 방향 A + 주제 정의 + 의미 버전은 맞음)**: ⓐ **핵심 결함**: 문서 3.3 의 "`SchemaOrphanPolicy` 에 값 하나 더하기"는 최상위 필드에만 듣습니다. 중첩 구조체는 `SerializerUtil.cpp:645-655` 에서 언제나 엄격 `deserialize` 로 읽고, 엄격 경로는 `allowsUnknownProperties` 하나로 "모르는 이름"과 "읽지 못한 아는 필드"를 함께 정합니다(`BinarySerializer.cpp:193`, `:249-252`). 그래서 `MatchFormed::_listTicket`(`MatchmakingTypes.h:125`) 원소에 필드 하나만 더해도 플래그를 끄면 메시지 전체가 거절되고, 켜면 중첩 안의 모르는 열거자가 기본값으로 조용히 통과합니다. ⓑ **권고 구조**: 컨텍스트 플래그를 둘로 나눕니다(모르는 이름: 건너뛰고 한 번 경고 / 못 읽은 아는 필드: 거절). 같은 컨텍스트가 중첩까지 내려가 모든 깊이에서 정책이 같고, 버스 읽기는 엄격 태그 루프 하나입니다. 의미 버전 머리는 버스 코덱이 직접 비교하고 다르면 이관 레지스트리를 보지 않고 거절합니다 — 그러면 레코드 버전 수열과 묶일 이유가 없어져 열린 질문 6 · 위험 7(이관 키 충돌)이 함께 풀립니다. "한 번만 경고"는 `warnSkippedFieldOnce`(`BinarySerializer.cpp:90-101`)로 이미 있습니다. 혼합 배포 중 소프트 경로의 메시지마다 orphan 복사와 전역 잠금 아래 이관 표 선형 탐색도 없어집니다. 플래그를 읽는 텍스트 직렬화기 쪽 사용처는 미확인이고 오브젝트 상태처럼 지금 플래그를 쓰는 곳의 동작은 두 값을 함께 켜는 축약으로 그대로 둡니다. ⓒ 소프트 경로에 숨은 폴백이 하나 더 있습니다: 기록 타입이 다르고 변환이 실패해도 "읽힌 만큼은 받는다"(`BinarySerializer.cpp:229-231`, 끝까지 읽었는지 보지 않음) — 백로그 후보. ⓓ **숫자 필드 ID 를 버스에만 선택 키로 두는 안은 권하지 않습니다**: 크기 이득은 번호가 아니라 인코딩(고정 u32 세 개)에서 나오는데 인코딩을 바꾸면 혼합 빌드에서 모든 캐시와 버스가 읽히지 않는 위험에 걸리고, 정체성 체계가 둘이 되면 `ChatMessage` 처럼 레코드와 버스가 함께 쓰는 타입이 갈립니다. 이 설계는 protobuf(번호)가 아니라 Avro 스키마 해석(이름 + alias, 쓴 쪽에만 있는 필드는 무시, 읽는 쪽에만 있는 필드는 기본값 없으면 오류, 모르는 열거자는 default 없으면 오류)과 거의 같아 이름 기반에 산업 선례가 있습니다(Avro 1.11 명세 Schema Resolution). ⓔ 열린 질문 권고: 필수 필드 키는 버스에 두지 않음(새 필드에 Required 를 붙이면 의미 버전을 올리는 것과 같음), 의미 버전이 다르면 거절, push 중계는 넣되 받는 서버가 "알림 이름 → 기대하는 본문 타입" 표로 FQN 을 확인하게 하거나 도메인 이벤트를 타입 있는 주제로 보내 받는 키트가 자기 빌드로 알림을 만드는 안이 더 낫습니다, `config.changed` 는 타입 있는 주제로 연결하거나(같은 모양의 `liveops.changed` · `sd.changed` 는 구독되어 있음) 발행과 `RemoteConfig.h:3` 주석을 함께 지움, **레코드의 orphan 정책은 나눠야 합니다**(캐시 `readCache` 는 버스와 같은 정책, 영속 `readDurable` 은 옛 빌드가 읽고 고쳐 다시 쓰면 새 빌드가 쓴 값이 지워지므로 모르는 필드 보관이 먼저), 의미 버전은 생성기를 바꾸지 않고 타입 안 `static constexpr uint32` 멤버로 두고 `ServerBusTopic<T>` 가 읽게 하면(없으면 컴파일 오류) 같은 타입의 주제 둘이 값을 따로 적는 일이 없습니다. ⓕ 원시 `publish` 를 막는 방법은 작업자에게 맡기지 말고 문서에서 정합니다: 바이트 전송 인터페이스와 타입 있는 앞을 나누고 키트 의존성에는 앞만 넘기며, 받는 쪽 `ServerBusMessage::_bytes` 가 공개라 `BitReader` 로 직접 읽을 수 있는 것도 함께 막습니다(`ServerBus.h:18-24`). ⓖ 놓친 위험: 여러 단계를 거치는 중계에서 필드 유실(`mm.queue` → 옛 매처 → `MatchFormed`, `MatchMaker.cpp:202`; "새 필드는 옛 홉을 지나면 기본값" 규칙을 3.6 에 추가), 6절의 "모르는 열거자" 테스트는 열거형 둘이면 기록 타입 해시가 달라 실제 혼합 빌드 경로(`readEnumBinary` false)가 아니라 변환 경로를 탑니다(같은 열거형으로 쓴 바이트에서 열거자 이름 해시를 덮어쓰는 방식이 실제 경로). 측정 질문(Release, 벤치 3~5회 중앙값): `ChatMessage` 짧은 글 · 512자의 A 대 B 코덱 비용, 혼합 배포 중 소프트 대 엄격 경로, 서버 둘(`LocalServerBusHub`, 가능하면 RESP)에서 초당 1k / 10k 메시지일 때 서비스 틱 p50 / p99 와 코덱 비율, 버스 타입을 `MaxCount` 로 가득 채웠을 때 A 바이트 ≤ 60 KiB. 판단 기준("코덱이 전송 왕복의 5 % 아래면 최적화하지 않는다" 같은 것)은 측정 전에 정합니다.
   - **`TypedComponents.md` 구현 전에 반영할 `design-reviewer` 검토 결과(결론: 고쳐서 진행)**: 7-c 의 "정확한 타입마다 버킷 + 조회 때 버킷마다 `isDerivedFrom`"은 이 규모(오브젝트 1만~10만, 타입 수백)에서 조상 목록 원안보다 낫고 언리얼 `GetObjectsOfClass` 의 "정확한 클래스 버킷 + 파생 열거"와 같은 계열입니다. 단 구현 전에 고칠 것: ⓐ **버킷을 컴포넌트 풀 항목(`_mapComponentPool`, 이미 FQN 키)에 합칩니다**(`{ 풀, 버킷 목록, _pType }`, `Component::_pPool` 을 그 항목 포인터로): 맵 하나가 줄고 추가 때 해시 조회가 없으며 `Component` 에는 위치 `uint32` 하나만 늘어 패딩에 들어갈 가능성이 큽니다(`_pPool` 사용처는 `GameObjectStore.cpp:441`, `GameObject.cpp:570-588`, `GameObject.h:468`뿐; `sizeof` 미측정). `Component.h` 와 `GameObjectStore.h` 가 바뀌므로 7-c 도 전체 빌드가 필요합니다. ⓑ **7-d 전제 "한 핸들이 가리키는 타입은 바뀌지 않는다"에 구멍**: id 복원(`takeRestoredComponentID`)이 FQN 이 아니라 `TypeInfo::_name` 으로 맞춥니다(`GameObject.cpp:36-52`, `Component.cpp:351-355`, `ObjectStateSerializer.cpp:469`; `_name` 이 짧은 이름인지는 미확인). 복원 키를 FQN 으로 바꾸고(7-d-1 보다 먼저 또는 함께) `resolveComponent<T>` 확인을 Debug 만이 아니라 Shipping 이 아닌 모든 구성에 둡니다(타입이 바뀌는 길인 핫 리로드는 Shipping 에 없음). ⓒ **7-d-1 API**: `executeOrDeferPostTickOnComponent` 를 `( TComponent& self, func )` 로 바꾸고 `TComponent` 는 추론하게 하면 호출부 23곳(약 20파일: WildWest, VoxelPlayer, VoxelChunk, ShooterPlayer, Water, Terrain, Foliage, AnimationNotify, PropScatter, Vehicle 3, WesternHorseMount, TileMapRenderer, CharacterAppearance, GameDirector, CameraDirector, CameraManager)에서 `getHandle()` 과 템플릿 인자가 함께 사라지고 둘이 어긋나는 일이 형식으로 막힙니다. 이 파일들이 7-d-2 그룹과 겹치므로 7-d-1 에서 한 번에 끝내고 그룹은 그 뒤에 시작합니다. ⓓ 그룹 A(조종)의 파일 목록에 `getPawn()` 비교 호출부가 빠졌습니다: `VoxelDirectorComponent.cpp:118,125`, `ShooterDirectorComponent.cpp:510,519`, `ShooterPlayerComponent.cpp:150`(그 밖에 `FarmDirectorComponent.cpp:513`, `ArenaDirectorComponent.cpp:548`). 타입 핸들과 타입 없는 `getHandle()` 의 비교를 섞은 `==` 허용으로 할지 `untyped()` 명시로 할지 3.1 에 정합니다(비교가 주소를 쓰지 않아 섞은 비교는 안전하다고 추정). ⓔ **저장 id 공간의 비우기 폴백(`ObjectStateSerializer.cpp:651-658`)**: 사용처가 0 이므로 7-d-3 의 형식 결정을 기다리지 말고 7-d-1 에서 `PROPERTY` 의 타입 없는 `ComponentHandle`(컨테이너 원소 · 키 · 중첩 구조체 필드 포함)을 파서 생성 오류로 막고(`Source` 사용처 0, 테스트 `TestSampleActor.h:607` 하나), 남는 "옮길 수 없는 컨테이너 모양의 `GameObjectHandle`" 비우기는 경고 대신 로드 오류로 바꿉니다. ⓕ 같은 뿌리: `resolveObjectReferences` 가 컴포넌트 타입의 최상위 `PROPERTY` 만 봅니다(`ObjectStateSerializer.cpp:623-643`, `holdsComponentHandle` `:210-221`) — 리플렉션 구조체 안의 핸들은 다시 매겨지지도 비워지지도 않을 수 있습니다(그런 필드가 실제로 있는지는 미확인). ⓖ 7-d-2 는 API 경계를 넘는 핸들(인자 · 대기 항목 · 접근자 반환값)이 결함이 숨을 곳이라 그룹 A(조종: `ControlSystem::queuePossess( const ComponentHandle& controller, const ComponentHandle& pawn )` 가 서로 다른 타입의 핸들 둘을 같은 형식으로 받아 인자 바꿔치기가 실제 후보)부터 하고, 3.4 표에 "핸들이 `T*` 에서 만들어졌는가" 열을 더합니다. ⓗ 타입별 목록이 `ComponentRegistry` · `TickBatchRegistry` · 버킷 셋이 되어 `09_Decisions.md:731` 의 근거(훑기 190 µs)가 약해지므로 어느 등록형 목록을 버킷으로 옮길지 기준을 백로그에 남깁니다. B 안(조회 타입마다 맞는 버킷 목록 캐시)은 측정 뒤로 미룹니다 — 넣을 때는 타입 레지스트리 세대로 무효화. 측정 질문(Release, VSync 끔, 3~5판 번갈아, p50 · p99): 오브젝트 1만 · 10만 × 정확한 타입 수 K = 50 · 200 · 500 합성의 `forEachComponentOfType`(말단 타입 대 기반 타입), 실제 8게임의 K, 추가 · 제거 잠금(`tickCostChurn`, 비동기 씬 로드 중 조회) A/D 비교, `sizeof(Component)` 전후와 큐브 20,000 개 `GT.Frame`, 개발 빌드 `resolveComponent<T>` 확인 비용(`BenchScene.cpp:1341`). 확인하지 못한 것: 7-b(다중 상속 금지)가 7-c 보다 먼저인지는 이제 `reflect7ab` 가 병합되면 충족(버킷에서 꺼낸 포인터를 `static_cast` 로 바꾸는 전제), 비동기 씬 로드 워커가 `_listComponent` 에 넣는 동안 게임 스레드 순회가 같은 목록을 읽는지.
   - **작은 후속(빌드 필요)**: `getUnscaledDeltaTime( fallback )` 폴백 인자 API 를 적지 않았으면 단언으로 멈추게(`TerrainBrushTool.cpp:503`, `SceneViewPanel.cpp:43`), `FrameStall` 본문을 `SW_DEV_COMMANDS_ENABLED` 로 감쌈, `Automation/README.md:30` 의 "에디터 코드는 unscaled 경과" 범위를 사실대로, 검증 레이어 조건에 `AND NOT SW_SHIPPING_BUILD`. 같은 뿌리의 남은 곳: 녹화는 벽시계 · 재생은 `fixedDelta` 인 `EditorScenarioRecordCommands.cpp:100` · `InputMapPanel.cpp:299`, 미리보기 패널 넷(`SequencerPanel` · `AnimationGraphPanel` · `DialogueGraphPanel` · `InputMapPanel`). 근본 해법은 플랫폼 `NewFrame` 뒤 `ImGui::NewFrame` 앞에서 `io.DeltaTime` 을 엔진 경과로 맞추기(프로파일러 성능 탭은 벽시계를 따로 읽어야 함).
   - **지금 풀 만한 구조 결함(우선순위 순)**: `SequenceBuffer` 크기를 2의 거듭제곱 템플릿 상수로 + 생성자 `size>0?size:1` 폴백 제거(잠복 결함 확인됨), `BitStream.cpp:51 · :174` `bitCount` 를 32 로 조용히 자르는 곳, B 코덱이 `Count` 없는 열거형을 8비트 전폭으로 쓰는 것(`G-14` 뿌리 — 생성기가 오류로 멈출지 결정, 온라인 11 키트 열거형 폭 미확인), `A-19` 일반 바이너리 · JSON · XML 읽기가 범위 검사를 하지 않음(`A-15` 잔여 — `A-15` · `A-16` · `A-17` 은 2026-10-17 통합 `integ1` 에서 해결), 프로세스 정적 "한 번만" 경고(`warnUnknownEnumeratorOnce` · `warnSkippedFieldOnce` · `warnUnreadableFieldOnce`)가 테스트 순서에 따라 `HostLogJudge` 결과를 바꿈, 저장 id 공간의 `ComponentHandle` 이 경고와 함께 비워지는 폴백(`ObjectStateSerializer.cpp:651-658`), `ComponentStateStore` 가 다른 실행의 세이브를 열거 순서에 맞춤. `D7-7` 넷 채널 등록 API 는 스냅샷을 싣는 게임이 생길 때.
   - **`Common/Workspace` 의 ImGui 17개 분리**(`EditorAssetType.h` · `EditorContext.h` · `EditorLayoutStore.*` · `EditorRenderViews.*` 등)가 `EditorModel` 경계를 이 폴더로 넓히는 선행입니다.
1. **CI 첫 결과 확인**(push 는 두 번 끝났다 — `b21bf6c4f`, `97dfd14fd`. 사용자가 실패를 알려 주면 `ci-jobs` 로 읽는다). **리눅스 CI 잡 전부와 야간 부하 잡은 10-16 부터 configure 에서 실패 중이었다**(잡 38933303050 · 38950521625 · 38972498033, 마지막 전체 통과 38794063672 은 10-15) — 보고된 원인은 `Scripts/common/Archive.py:52` 의 `hashlib.file_digest`(Python 3.11 이상 전용, 미확인)이고 Windows STL 잡도 `TestHashUtil.cpp(87)` 의 `DefaultHash<sw::string>` 에서 컴파일 실패한다. `cifix` 로 고쳐 main 에 병합했다(`4cf16d097`, 러너 ubuntu-22.04 의 python3 은 3.10 이라 `hashlib.file_digest` 가 `AttributeError`; 08-26 부터 있던 코드가 `bf31aaeb2` 가 예외를 삼키던 `safeCallInternal` 을 걷어내면서 드러남; 해시는 청크 읽기 + `hashlib.new`, Python 최소 버전 정본은 `Scripts/common/Constants.py` 의 `kMinimumPythonVersion = (3, 10)` 하나로 `common` import 때 검사, STL 구성의 `DefaultHash<sw::string>` 은 `string.h` 특수화로 정본을 옮김). **푸시 후 CI 로 리눅스가 초록이 되는지 확인해야 한다** — 로컬 3.10 실행은 못 해 코드로만 판단했다. STL 잡의 `ReflectionTest` 컴파일 실패(`A-18`, `vector<bool>` 원소 주소)는 `integ1` 에서 해결했다(`sw::vector<bool>` 금지, `DynamicBitset`). 다른 별칭의 구성 차이 점검은 `A-20`. 리눅스 관찰 항목(`E-1` `E-4` `E-10` `C-29 ⑤`)은 리눅스가 초록이 된 뒤 볼 수 있다. 리눅스 · PCH 끈 Windows CI · `header-self-contained.yml`(Windows Debug 잡이 32 → 약 18 분인지) · 야간 `load.yml`. 리눅스 CI 가 `B-5`(퍼저 재확인) `B-7` `C-14`(린트 잡)를 풀 수 있다.
   - 러너 이미지를 ubuntu-24.04 · windows-2025 로 올리고 라벨 정본을 `.github/runners.json` 하나로 모았다(`runners.yml` 재사용 워크플로, 액션 판은 `dependabot.yml`). **미푸시 · 첫 실행 미확인** — 실패하면 먼저 22.04 · 2022 로 되돌려 원인이 이미지인지 가른다. `libtinfo5` → `libtinfo6`, vcpkg 캐시 키가 이미지 버전을 가르지 않아 첫 리눅스 빌드는 포트를 다시 지을 수 있다.
2. 넷 설정의 남은 것: `gv_netLoopbackOnly` 도 테스트 전역 변수에서만 읽는 같은 모양, `_gamePortShardCount` 는 계약만 있고 구현이 없다(`D7-3` ⑤), Shipping 클라이언트에는 `gv_netConnectAddress` 가 없다.
3. `Internal` 소거의 남은 것: `-DSW_ENABLE_UNITY_BUILD` 로 충돌을 한 번 본다(`DuplicateAnonymousFunction` 은 `Source/` 만 보므로 `Test/` 이름 충돌은 직접 확인), 병합 때 작업자끼리 겹친 익명 함수 이름이 한 건 났다(`makeRandomSeed`).
4. 함수 객체 · 람다 공통화 1 단계(`VectorUtil::findBy` · `CompareUtil.h` · `DefaultHash`)는 `containers` 로 병합됐다. 남은 것은 11 번 "공통화 2단계"다.
5. **파일이 겹쳐 미룬 정리**: engine `C-4` `C-5` `C-13` `C-19` `C-22` `C-34`, project `C-16` `C-17` `C-18` — 3 번 뒤에.
6. **이 세션의 작업자가 끝내지 못해 남긴 것**(각 항목 본문에 이유가 있다): `D2-9` 반해상도 블룸 · `D2-5 (7)` 지형 벤치 Release 측정(독점 GPU 필요), `D7-1` 암호화 복제 Release 측정, `D1-4` 천 그리기(그래픽스 범위), `D4-3` `D4-5` `D4-6` `D4-7` `D4-9` `D4-10`, `D6-1`~`D6-6` ui 기능(손대지 않음), editor `D3-1` `D3-3`~`D3-10` `D3-12`, audio `D5-1` `D5-2`, `C-9` 글리프 SDF(프레임 예산 안 래스터화 측정이 먼저), `C-33`, `B-12` `B-14`(전체 hostgpu · Release 네 백엔드 스크린샷).
7. **백로그 역할 분담 정비**: 항목마다 "건드릴 파일 · 겹침 주의"를 적고 겹치지 않는 묶음을 뽑는 도구를 만든다. 사용자가 이전 세션에서 시켰다고 기억하는 일 — 이 세션에서는 지시를 찾지 못해 위 뜻으로 이해했다. 의도와 다르면 사용자에게 확인.
8. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃, [빌드 속도 계획](BuildSpeed.md)): 빌드가 도는 동안은 측정이 무효라 다른 작업이 모두 끝난 뒤 혼자.
9. 이름 조회 흩어짐(`SceneDevCommands` · Sequencer), `IFileWatcher` 를 쓰는 곳의 조용한 실패(리눅스 감시 마스크에 `IN_ATTRIB` 없음), `HUDViewModel` 의 슈터 전용 칸(WildWest 가 칸을 빌려 씀, `D4-15`).
10. 조건부 12 · 관찰 15 · 보류 3 · 조건이 붙은 기능은 조건이 올 때(백로그 안내 순서).
11. **구조 최적화(2026-10-17 조사, 측정이 아니라 구조 기준 — 근거는 읽은 코드)**. 재평가(2026-10-17, 읽기만으로 대조): 스핀 늘리기(2→50 us) · hot pool 은 09-20 측정(조건 변수 시절, 0.7 ms 프레임, ±40% 흔들림, p99 없음)이 근거라 Futex 개편 뒤로 재지 않았습니다 — 값 2 us 는 유지하되 무거운 프레임(8 ms 이상)에서 다시 잴 것, hot pool 은 숫자가 없어 "금지"가 아니라 조건부 보류. 깨우기 사슬 변경은 09:137 에 숫자와 날짜가 없어 미확인, "레인 없이 GT 병렬화" 기각은 High · Normal · Low 큐가 생겨 전제가 사라졌으니 재검토. **"큐 칸 패딩 · `TaskNode` 정렬이 잡음"은 폐기** — 측정한 기록이 없고 잰 것은 vector 머리 24 B 패딩 하나(`38c2449be`)이며 `TaskNode` · 데크는 이미 `alignas(64)`. `NetConnection` 수신 아레나의 "측정 기각 `C-4`"는 출처가 틀림(`C-4` 는 열린 정리 항목, 측정 기각은 `N18c`, 아레나는 `a108c1adf` 로 측정 없이 들어감). Math SIMD 는 측정이 아니라 사용자 결정이고 보류 이유는 플랫폼 파편화 우려이며 다시 볼 조건은 결정 기록 105행 아래에 있습니다. 유지 확정: `notify_all` 금지, `-gv_tickCostDispatch` 기본 0, `PostUpdate` · `TickPhase`. 이 게임들이 아직 적어 측정이 작은 규모라는 점을 항상 고려합니다(`design-reviewer` 정의 참조).
    - **무효가 된 측정(재측정 대상)**: `D6-6`, `D7-5` 온라인 부하 봇 Debug 값 · `C-29 ⑤` 야간 기준선(온라인 B 이전 전), `D2-7` "BC7 Debug 10분"(Release 병렬 압축으로 이미 8초), `B-10` 서버 스레드 %(다른 빌드의 CPU 20~46 % 부하가 섞임). 재측정 필요: `D2-9` 반해상도 블룸, `D2-5 ⑺`(Debug → Release), `D2-5 ⑹`, `C-2` 5단계, `C-13 ②`, `C-29 ②`. 폐기 권고: `E-6` `E-12` `E-15` `E-16` `C-14`, `E-7`(로컬 WSL 실행은 WSL 규칙과 충돌 — CI 리눅스 잡의 경고 줄 수로 바꾸지 않으면 폐기). 태그: `E-4` 와 `E-10` 합침, `E-1` 의 통과 확인된 줄 삭제, `E-2` 는 `결함`, `G-9` · `G-10` 은 조건 문장을 적거나 폐기 결정. 근거표는 이 세션의 scratchpad `audit-*.md` 4개(없으면 같은 조사를 다시).
    - **문서 낡음**: `EditorCharacterEditor.md`(첫 단계가 `Source/GameFramework/PlugIn/Editor/CharacterEditor/` 에 들어감), `EnginePartition.md` 3단계(DLL 승격, 근거 없음), `DataDriven.md` 4절 "Alias 를 없앤다"(별칭 유지로 바뀜). 결정 기록에 측정 조건 칸(기계 · 규모 · 날짜 · 근거 커밋)이 없어 조건이 떨어져 나가 무조건 금지가 되는 것이 뿌리라서, 측정이 근거인 결정에 한 줄 형식을 두고 "하지 말 것"은 결정 번호를 링크합니다.
    - **feat19(2026-10-18, 빌드 없이 커밋만)**: A ①~⑤ · B ① · ④ · ⑤ 를 `wt/feat19` 에 단위별 커밋(벤치 커밋이 먼저). 통합 때 전체 빌드 → CoreTest(`ShardedCounterTest` · `TaskManagerTest` race · `MemoryProfilerTest` · `LogTest` · `BitStreamWireTest` · `NetworkTest`) → Release 벤치 3~5회로 [구조 최적화 측정 표](StructuralOptimization.md)를 채우고 이득 없는 단위만 되돌린다. B ② deque · ③ 해시 표는 남음. feat18 과 `BitStream.cpp` · `SequenceBuffer.h` 가 겹친다.
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

## 2-2. 기능 대기열 (2026-10-17 분류, 약 140 작업 단위)

백로그 `기능` 50건을 영역별로 읽기 전용 분류했다. 결정은 [결정 기록](../09_Decisions.md) 5-12. 막힘 없음 약 75, 결정 약 30(대부분 5-12에서 정함), 조건부 · 보류 · GPU 확인 약 35. 한 번에 빌드는 둘까지라 묶음을 두 개씩 돌린다. 끝난 단위는 백로그에서 지운다.

**2026-10-18 `wt/feat` 진행.** 끝낸 것: `F-1`, `D1-5.3`, `D7-3` ①②③⑥, `D3-10` 워프 내보내기, `D4-5.1`, `D4-3`(오른쪽 스틱 줌은 남음), `D4-6` ①, `D5-1` (2), `D3-4` `cheat.god`(Shooter3D 는 남음).
`wt/feat-pv` 의 `D3-7` (2) 선택 접근자는 병합했다(integ9). (1) 프리팹 창과 그 선행 둘이 남았다. 화면 확인은 000절 다음 일 1.
EngineTest 샤드는 기계가 붐비면 병렬에서 104 초 시한을 넘긴다. `-j 1` 로 다시 돌리면 통과한다.

1. (끝남) 포토 모드 + UI 소소한 것 — 2026-10-17 밤 feat1 이 넣었다(포토 모드 전용 엔진 입력 맵 · 롤, 끝내기 · 타이틀로 명령, UI 맵 키 바인딩, Windows 클립보드, 상태 브러시 전환, 알림 글 키, 15 초 되돌림 시나리오).
   남은 것은 백로그 `D4-14`(Simulate 중 Pause) · `D6-2` (4) 리눅스 클립보드 · `D6-4` (5) 재시작 뒤 키 유지. Shipping 에서 `game/empty/rigs_raw/*.rig.json` 을 못 읽어
   `ResourceDataSchemaTest` · `SyntheticRigImportTest` 가 진다(main 에서 온 것, 이 묶음과 무관 — 쿠킹에서 빠지는 원본 폴더를 Shipping 테스트가 읽는다).
2. 카메라 디렉터 · 직교 리그 이전(`D4-5.5` → `5.1` → `3.1` → `3.2`) + 에디터 시나리오(`D3-5.1` `8.1` `8.6`)
3. 프리팹 오버라이드(`D1-5.1` `5.2`, 코드와 대조부터) + 미리보기 창(`D3-7.2` `7.3` `7.1` `7.7`, 같은 파일은 한 작업자) → 스카이 컴포넌트(`D3-7.4`)
4. 지형 · 식생(`D2-5.6` → `5.7` → `5.8` → `5.4`, `F-1.2` `1.3`) + 서버 파괴 호스트(`D7-1.3` → `1.4` → `D7-6.1`)
5. 오디오(`D5-1.*`, `D6-3.1`) + 온라인 키트(`D7-5.1` → `5.10` → `5.3` → `5.6`)
6. 애니메이션(`D4-6.*`), 천 동적 메시(`D1-4.*`), Shooter3D(`D4-10.*`), `GameNetSession`(`D7-5.2` + `D7-3.7`, 한 작업자)
7. 그래픽스 형식 묶음(한 작업자, 순서대로, 셰이더 재베이크 · 에셋 재작성): `D2-6.1` 탄젠트 → `D2-6.3` `D2-5.5` 칸 16개 → `D2-6.4` G버퍼 → `D2-10.2` → `D2-10.3`
8. 뒤 순서: 센서 이식(`D1-2.1`), 서버 렌더러 제거(`D7-2.1`), Core 이전(`D3-1.1`, 단독 시간대), 워프 분리(`D3-10.3`)

작업 전에 항목의 현재 완성도를 코드로 확인한다(분류는 헤더 존재 여부 수준). 같은 파일을 건드리는 단위는 같은 작업자에게 묶는다. GPU 확인이 필요한 단위는 구현만 하고 `gpu-verifier`로 캡처한다. 낮은 확신이던 `D2-6.4` `D2-10.3`도 사용자가 승인했다.

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
