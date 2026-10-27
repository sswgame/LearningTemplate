# 5-8. 게임프레임워크와 키트

> 질문: 5-8. 게임프레임워크와 키트 영역에서 이미 정한 것은 무엇이고 근거는 무엇인가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 결정 기록 5 절의 한 영역이다. 번호 체계와 쓰는 법은 [결정 기록](../09_Decisions.md) 5절에 있다.

- **게임 · 에디터 서비스는 범위(`SubsystemScope`)에서 타입으로 찾고, 없으면 호스트 서비스 표를 본다 — 범위는 게임 인스턴스 · 에디터 컨텍스트가 소유한다**(2026-10-14).
  언리얼은 서브시스템을 범위(엔진 · 에디터 · 게임 인스턴스)마다 그 범위 객체가 소유하고 `GetSubsystem<T>()` 로 찾는 것으로 알고 있다(기억에 의존). 같은 모양을 Core 에 두고(`ISubsystemLocator` · `SubsystemScope` ·
  `locateSubsystem` 이 상위 범위로 올라간다), 게임 · 에디터 창구의 복제 템플릿(타입 해시 · 서비스 칸 trait · 로컬 표)을 지웠다. 이전의 "건 이미지" 를 반환 주소로 기록해 언로드 때 훑어 회수하던
  장치는 없앴다 — 범위가 인스턴스와 함께 사라져 풀지 않은 칸이 남을 수 없다. 테스트는 자기 범위를 `ScopedGameScope` · `ScopedEditorScope` 로 잠시 건다.
- **`EditorContext` 는 범위를 들고 하위 객체를 만드는 얇은 껍데기다 — 상태는 모두 에디터 서브시스템이 든다**(2026-10-14). 하위 객체마다 두던 포인터 · 접근자 열다섯은
  `getSubsystem<T>()` 하나로(언리얼 `GEditor->GetEditorSubsystem<T>()`), 컨텍스트에 직접 있던 뷰 렌더 타깃 · RHI 디바이스 · 렌더러 백엔드 · 게임 뷰 입력은 `EditorRenderViews`,
  플레이 세션 상태는 `EditorPlaySessionState`, 테마 값은 `EditorTheme` 으로 옮겼다. 셸의 독 레이아웃은 범위에 빌려 건다(`findSubsystem<EditorDockLayout>()`).
  활성 컨텍스트 정적 포인터도 지웠다 — 컨텍스트가 자기 범위에 자신을 걸어 `editor::getSubsystem<EditorContext>()` 는 지금 에디터 범위의 주인을 찾는다(옛 `EditorContext::get()` 은 5-11 이 지웠다). 정적 상태는 "지금 에디터 범위" 하나뿐이다.
- **게임 인스턴스 서브시스템은 수명 · 순서가 인스턴스에 묶인 것만이다 — 화면 전환 · 로딩 화면 · 네트워크 세션**(2026-10-14, `GameInstanceSubsystem`). 게임 범위가 소유하고
  `onInitialize` 앞에 초기화, `onShutdown` 뒤에 역순 종료한다(언리얼 `UGameInstanceSubsystem` 과 같은 모양으로 알고 있다).
  카탈로그 · gamesettings 는 데이터라 서브시스템으로 만들지 않고 빌려 건다(`bindScopedService`). 스냅샷 상태 저장소 · 씬 로드 대기 목록은 인스턴스 흐름의 일부라 멤버로 둔다.
  GameFramework 의 남은 정적 상태(정의 약 75 곳)는 상수 · 빈 값 · 불변 표 · 상태 없는 드라이버 · 링크 때 등록 표 · 의도된 프로세스 공유(메모리 DB 드라이버)라 남긴다.
  에디터 플러그인의 함수 정적(`AdminToolSession` · `ParkLayoutPreview`)은 확장 모듈이 서브시스템을 기여하는 창구가 생기면(`C-27` ④ 와 같은 장치) 에디터 범위로 옮긴다.

- **GameFramework 최상위에는 `Base/`(장르 공통 기반)와 `Kits/`(장르 키트)만 둡니다**(2026-10-06). 게이트가 다른 폴더를 막습니다.
- **기반은 `Base/<층>/<폴더>/` 두 단이고, 층은 아래에서 `Online` < `Foundation` < `World` < `Actor` < `UI` < `Gameplay` 입니다**(2026-10-10).
  include 그래프를 재서 정했습니다. 계획의 첫 안(World · UI 가 맨 위)은 AI · 기믹 · 상호작용 → 월드 13 곳, 어빌리티 → 데미지 숫자가 거꾸로라 기각했습니다.
  UI 가 게임플레이 아래인 것은 언리얼에서 UMG 가 게임플레이 모듈 아래에 있는 것과 같습니다. 전투 수치(체력 신호)는 HP 바가 읽으므로 Actor 층에 둡니다.
- **키트 헤더는 `SW_KIT_API`, 서버 키트 헤더는 `SW_KIT_SERVER_API`, Base 헤더는 `SW_GF_API` 를 씁니다 — 키트 TU 는 `SW_GF_EXPORTS` 를 정의하지 않습니다**(2026-10-15, 백로그 C-3).
  키트 TU 가 `SW_GF_EXPORTS` 를 정의하니 include 한 Base 클래스(`Inventory` · `StatBlock` 등)가 키트 DLL 에서도 dllexport 로 풀려, 키트 DLL 55개가 `GameFramework.dll` 과 같은 심볼 4909개를 한 번 더 내보내고 있었다(`GF_WitcherRPG.dll` 은 326개 중 116개). 키트는 이제 `SW_KIT_EXPORTS`(PRIVATE) · `SW_KIT_IMPORTS`(INTERFACE)만 쓰고, 그 뒤 키트 DLL 에서 `GameFramework.dll` 과 겹치는 심볼은 0 이다. 언리얼이 모듈마다 `<MODULE>_API` 를 클래스에 달고 다른 모듈의 선언은 import 로 두는 것과 같은 모양으로 알고 있다(기억에 의존).
  함정: 겹침이 가리던 결함이 드러났다. 순수 인터페이스(`IAccountNameIndex` 등)의 `= default` 생성자 · 소멸자는 그 헤더를 include 하는 `.cpp` 가 DLL 에 있을 때만 내보내져서(clang-cl), 서버 키트가 파생 클래스를 만들며 dllimport 로 찾다 링크가 깨졌다. 그래서 **순수 인터페이스(순수 가상 · `= default` · `= delete` · `using` 뿐)는 내보내기 매크로를 달지 않는다**(인라인 멤버를 쓰는 쪽이 만든다). 같은 모양 28개(Base 15 · 키트 13)에서 매크로를 뺐고, `CheckExportMacros` 가 새로 생기는 것을 막는다(헤더가 속한 DLL 의 매크로인지도 본다).
  서버 키트는 `SW_KIT_SERVER_EXPORTS`(`SW_KIT_EXPORTS` 가 아니라)만 정의하고 폴더 `Server/` 의 헤더는 `SW_KIT_SERVER_API` 를 쓴다. 서버 키트가 include 한 공유 키트 헤더의 `SW_KIT_API` 는 dllimport 로 남는다. 모든 키트 TU 가 `SW_KIT_IMPORTS` 를 함께 정의한다(자기 내보내기 매크로가 먼저 읽힌다). 키트 PCH 하나(`sw_kit_pch`)를 나눠 쓰려면 명령줄 정의가 같아야 해서 PCH 소유자도 같은 정의를 갖는다(전에는 `SW_KIT_IMPORTS` 가 달라 PCH 경고 43개). Linux 는 `-fvisibility=hidden` 을 주지 않아 세 매크로가 모두 `visibility("default")` 로 같다 — 동작 차이가 없고 심볼 보간이라 중복 내보내기로 링크가 깨지지 않는다. Release 는 Dev 와 같은 SHARED 라 dllexport 로 풀리고, Shipping(STATIC)은 `SW_KIT_*_EXPORTS` · `SW_KIT_IMPORTS` 를 정의하지 않아 매크로가 빈다. 서버 키트 11개와 공유 키트의 겹침은 100 → 0 이다.
- **키트는 성격으로 나눕니다: 장르 키트는 `Kits/Genre/<그룹>/`, 장르를 가리지 않는 기능 키트(네트워크 · 온라인 · 저장 · 타일 월드 · 복셀)는 `Kits/Feature/<그룹>/`**(2026-10-10).
  언리얼 플러그인 폴더가 Runtime · Online · Experimental 처럼 성격으로 묶이는 것과 같은 단입니다. 모듈 이름(`GF_<키트>`)은 경로와 상관없이 그대로입니다.
  클라이언트 · 서버로 나뉘는 기능은 기능 폴더 하나 아래 `Shared/` · `Server/` · `Client/` 짝으로 둡니다(Nakama · PlayFab 과 같은 단위).
- **모듈 이름은 공유 `GF_<X>`, 서버 전용 `GF_Server_<X>`, 클라이언트 전용 `GF_Client_<X>` 입니다**(2026-10-06). `CheckModuleTargets` 가 접두와 매니페스트의 대상(`_listTarget`)이 맞는지 봅니다.
  서버 키트는 같은 기능의 공유 키트만 include 할 수 있고, 키트끼리는 include 하지 않습니다.
- **키트 조립 규칙**(2026-10-06): 조립 테스트 게임은 `MeadowVillage` 입니다. 플레이어와 세계가 들고 있는 가방은 `Inventory`, "아이템과 개수" 값 목록은 `ItemStackList` 입니다.
  가방의 칸 구조체는 `InventorySlot` 이고, 달력은 `WorldClock` 하나입니다. 키트 하나만 쓰는 게임은 디렉터가 기반 상태를 들고 키트에 빌려 줍니다(언리얼 Lyra 도 단일 기능 게임은 GameMode 나 GameState 에 직접 상태를 둠).
- **실시간 키트의 체력 직접 관리(`_hp -= damage`)를 `Vitality` 로 옮기지 않습니다**(2026-10-11). 대상은 `ConquestWorld` · `RTSWorld`(유닛 수천) · `ActionRoom` · `UnitStatsComponent` · `GhostEncounter` · `FightingMatch` 입니다.
  `Vitality` 는 실드 · 기절 · 부활 · 경직 · 무적을 가진 큰 상태 기계(필드 수십 개 + 알림 버퍼)라 유닛마다 하나씩 들면 메모리와 상태 바이트가 커지고, 키트마다 체력 의미(정수 · 실수, 0 에서 멈춤 · 1 에서 멈춤, 죽음 알림 시점)가 달라
  옮기면 수치와 세이브 · 되감기 바이트가 바뀝니다. 같은 상태 기계가 필요한 키트(배틀로얄 · 소울라이크 · 데드 바이 데이라이트 · 위쳐)만 이미 씁니다. 단순 체력은 각 키트의 값으로 두는 것이 언리얼 `UHealthComponent` 샘플 · 유니티 개별 스크립트와 같은 쪽입니다.
- **온라인 게임의 로컬 지갑은 서버 원장의 읽기 사본입니다**(2026-10-06).
- **시퀀서는 카메라를 직접 움직이지 않고 신호만 냅니다**(2026-10-13). 엔진(시퀀서)은 GameFramework 카메라를 모르므로 컷 · 시네마틱 시작 · 끝 · 대기 뷰 · 흔들림을 신호로 내고,
  GameFramework 의 `CinematicCameraComponent` 가 디렉터 프리셋 · 블렌드로 카메라를 움직입니다(언리얼 Sequencer 의 Camera Cut · Blend In/Out 과 같은 나눔).
- **포토 모드는 게임 일시정지(`GameTimeScale` 의 정지 요청 + 크기 조절 안 된 dt)에서만 움직이고, 에디터 Simulate 의 Pause(씬 틱 정지)에서는 움직이지 않는다**(2026-10-27, 후보 `PhotoModePauseUpdate` 닫음).
  유니티 에디터 Pause 는 모든 `Update` 를 멈추고 언리얼 Simulate 는 게임 카메라가 아닌 에디터 뷰포트 카메라로 보므로, 씬 틱 밖에 포토 모드 갱신 길을 따로 두지 않는다.
- **엔진 쪽 세이브(스냅샷 봉투 · 컴포넌트 `writeState`)는 단위 테스트로 닫고, 세이브 파일을 쓰는 시점은 게임이 정한다 — 세이브 호출이 없는 게임에 확인용 세이브 명령을 따로 두지 않는다**(2026-10-27, 후보 `NpcScheduleFollowUp` 닫음).
  언리얼 `USaveGame` · 유니티 모두 저장 시점은 게임 코드이고 엔진은 직렬화만 준다. NPC 일정의 저장 왕복은 `ScheduleTest.SaveStateRoundTripsThroughTheArchive`, 일정 창은 에디터 시나리오 `harvestvalley.scheduleeditor` 가 지킨다.
- **옵션 메뉴의 카메라 설정(시야각 · 흔들림 배율 · 머리 흔들림)은 컴포넌트가 읽어 디렉터에 넣고, 모드 계산은 모른다**(2026-10-17, `CameraUserSettings`).
  시야각은 1인칭 · 3인칭 모드에만 건다 — 시네마틱 · 고정 카메라는 저작한 렌즈로 둔다(언리얼 · 유니티 게임과 같음).
  값을 그대로 덮지 않고 변수 기본값 대비 반각의 tan 비율로 프리셋 시야각을 넓힌다. 기본값이면 저작한 화면 그대로이고, 좁게 저작한 조준 프리셋도 같은 비율로 따라간다.
  흔들림 배율은 손떨림 잡음과 충격 오프셋에 곱한다(접근성 옵션 "카메라 흔들림" 이 모든 흔들림을 줄이는 것과 같다). 머리 흔들림은 프리셋 `<HeadBob>` · `FirstPersonCameraComponent::_headBob`
  데이터로 켜고(진폭 0 이 기본), 위상은 시간이 아니라 간 거리로 간다(걸음 빠르기와 어긋나지 않음; 언리얼 걷기 카메라 셰이크 · 유니티 FPS 템플릿 head bob 과 같은 자리).
  엔진 서비스가 없는 실행(엔진 없이 도는 테스트)은 사용자 설정이 없는 상태가 맞아 기본값이고, 변수 관리자가 있는데 변수가 없으면 `SW_ASSERT` 로 멈춘다.
- **RTS 땅 격자 다시 칠하기는 전체를 훑되 바뀐 칸만 쓴다**(2026-10-17, `D4-4`). 64 × 64 는 4096 칸이고 땅 리비전은 다른 키트가 땅을 얻거나 반납할 때만 바뀌어 훑기 자체는 문제가 아니다.
  비용은 훑기가 아니라 부작용이었다 — 지형 · 땅으로 칠한 뒤 건물 발자국을 다시 덮어 그대로인 건물 칸이 풀렸다 막히며 격자 리비전이 올라, 상관없는 땅 변경(남의 도로)에도 모든 흐름장이 다시 구해졌다.
  막힘을 임시 배열에 다 구한 뒤 쓰므로 같은 값이면 리비전이 오르지 않는다(`RealTimeStrategyTest.WorldBuildsOnlyOnUsableLandAndRepaintsTheGridFromIt`). 바뀐 사각형만 칠하는 길은 땅 레지스트리가 변경 범위를 알려야 해서 두지 않았다(언리얼 내비 메시도 변경 영역을 받아야 타일만 다시 짓는다).
- **외형의 체형 모프와 장비 피팅은 GPU 가중치가 아니라 구운 메시 사본으로 그린다**(2026-10-22, 외형 통합 E2). 피팅은 체형을 적용한 바인드 형상에서 풀므로 체형이 바뀌면
  어차피 다시 풀어야 하고, 사본에 구우면 매 프레임 가중치를 내는 작업이 없어 같은 차림의 캐릭터가 군중 포즈를 나눈다 — 구운 메시는 결과 키(메시 해시 · 원본 내용 · 데이터 판)로 나눠 쓴다
  (`AppearanceBakedMeshCache`, 언리얼 Mutable 이 결과 메시를 굽고 캐시하는 것과 같은 자리). 구운 모프 타깃은 사본에서 빠지고 얼굴 같은 나머지 타깃은 GPU 모프 풀이 건다.
  피팅 데이터(`*.partfit.xml`)는 표와 따로 읽는 공유 에셋이고 이름은 조립이 표와 만날 때 대조한다(`FitPartData::validateNames`). 한 메시로 병합(그리기 줄이기)은 이 위의 다음 단계다.
- **외형 부품 풀은 씬 서비스 하나(`SocketPartPool`)다 — 컴포넌트마다 두는 풀은 두지 않는다**(2026-10-22, 통합 파동 C). animwave(S6)와 gfxshader(E5)가 같은 일을 따로 만들었다. 씬 서비스는 캐릭터끼리(적을 걷어 다른 적이 입을 때) 인스턴스를 나누고
  `destroyOwnerStowingParts` 로 주인을 지울 때도 거두지만, 컴포넌트 풀(`_partPoolSize`)은 그 캐릭터 안에서만 돈다. 같은 캐릭터가 무기를 다른 칸으로 옮겨도 조립이 먼저 거두고 뒤에 꺼내므로 같은 인스턴스다. 언리얼은 이 자리에 엔진 기본이 없고 게임이 액터 풀을 둔다.

- **걸음 조우 판정은 기반의 `StepEncounterRule` · `StepEncounterCounter` 하나입니다**(2026-10-22, 걸음 조우 판정 하나로). `GameFramework/Base/World/Encounter/StepEncounter.h` 에 규칙(방식 `Period` · `Chance`, rate, 유예 걸음)과 셈(총 걸음 · 조우 뒤 걸음, `advance` · `roll` · `markEncounter`)을 두고 Overworld(`Period`, 난수 없음, 유예 0, 조우 칸 걸음만 셈)와 ClassicJRPG(`Chance`, 자기 난수)가 같이 씁니다. 세이브 바이트는 바꾸지 않았고(Overworld 는 예전 `uint32` 칸에 조우 칸 걸음 수, JRPG 는 난수 · 조우 뒤 걸음 · 총 걸음) 범위 검사는 `StepEncounterCounter::restore` 한 곳입니다.

- **표면 상태 머티리얼 이름은 셰이더 필드 이름(`_surface<채널>` · `_surface<채널>Mask`)이고, 영역별 덮개는 영역 벡터가 아니라 UV 마스크가 정한다**(2026-10-25, 캐릭터 통합 E6). 셰이더 필드 규칙(`_camelCase`)과 문자열로 묶인 이름이 어긋나 옛 `Surface_<채널>` 은 필드가 될 수 없었다. 이름을 고치고 정점 영역 칸은 만들지 않았다(메시 형식 유지). 언리얼은 페인트 마스크 텍스처 · 머티리얼 파라미터 컬렉션으로 같은 일을 한다.
- **걸린 이펙트가 대상에 주는 것(부여 태그 · 부여 어빌리티 · 모디파이어 집계)은 `engageActiveEffect` / `disengageActiveEffect` 짝 하나로만 붙고 뗀다**(2026-10-22, 어빌리티 시스템 확장 A1 ~ A5). 걸 때 · 해제할 때 · 걸린 동안의 태그 조건이 바뀔 때가 모두 이 짝을 지나므로 주는 것이 하나 늘어도 빠뜨릴 곳이 없다.
  태그 변화의 알림은 `_ownedTagCounts` 하나에서 나오고(이펙트 · 어빌리티 · 이펙트 없는 태그 모두) 큐 태그 → 연출은 데이터(카탈로그의 `<GameplayCue>`)가 정하며 큐를 내는 길(`dispatchCues` → `playCueDef`)은 하나다. 가장 가까운 조상 태그의 정의가 맡고 보여 줄 것이 없는 정의는 로드 오류이며, 클라이언트 코드가 없는 빌드(서버)는 연출을 내지 않는다. 무적 API 는 따로 두지 않는다(상태 태그 `State.Invulnerable`).
- **근접 판정은 `MeleeHitWindowUtil::sweep`(`Engine/Character/Hit/MeleeHitWindow.h`) 한 길이다**(2026-10-25). 칼날 표본점마다 지난 → 지금 위치와 지금 칼날 자체를 쓸고 토막마다 걸린 바디를 모두 본다(`CharacterHitUtil::sweepAll` — 처음 닿은 하나에서 멈추면 이미 맞힌 오브젝트가 뒤의 표적을 가린다). 맞음은 `onHitReceived` 한 길로 가고 겹침 이벤트로 접촉을 따로 세던 길은 지웠다.
  **서버 권위**: `sweep` 은 `MeleeHitAuthority` 를 기본값 없이 받아 `Authority`(혼자 하는 게임 · 서버)만 맞음을 전달하고 `Predicted`(클라이언트)는 돌려주기만 한다(프로세스의 권위는 `getProcessAuthority` 한 곳이 `GameNetSession` 역할에서 읽는다). 클라이언트의 예측 맞음 주장(`MeleeHitClaim`)은 입력 바이트 안에 실어 보내고(키트 와이어 형식을 바꾸지 않는다) 서버가 자기 스윕 기록(`MeleeHitLedger`)의 칼날 토막 + 두께 + 여유로 `Confirmed` · `NoSweep` · `OutOfBlade` 를 판정한다 — 인정된 주장만 기록된 피해로 전달하고 한 창에 오브젝트 하나는 한 번이다. MechArena(롤백용 결정적 시뮬레이션)와 AbilityArena(사거리 안 가장 가까운 적을 고르는 어빌리티 규칙)는 칼날 스윕이 아니라 이 길에 넣지 않았다.
- **움직이는 바닥의 속도는 물리 백엔드(`moveCharacter`) 한 곳이 싣고, 네트워크 틱 동기는 바닥을 틱의 순수 함수로 움직여 푼다**(2026-10-20 ~ 25). 상태가 바닥이 실어 나른 속도(`_carriedVelocity`)와 위 축 각속도(`_groundYawRate`)를 내보내고 방향을 돌리고 관성을 잇는 일은 떠나는 순간을 아는 컴포넌트가 한다. 바닥을 '쌓은 시간' 으로 움직이지 않고 `TickedPathMoverComponent::computeOffset` 이 틱만으로 자리를 정하므로 서버 · 클라이언트 · 되감아 다시 시뮬레이션한 쪽이 같은 자리다(서버 틱 − 입력 틱 차이를 씬의 바닥에 한 번에 넣는다). 큰 Kinematic 메시(공중 요새)는 캐릭터 쓸기 충돌 비용이 크니 측정 전에 단순 상자로 대신할 수 있는지 먼저 본다.
- **의도 기록의 넷 입력 형식은 의도 하나(`ReplicationClient::sendIntent` / `ReplicationServer::popIntent`)이고 원격 조종자는 받은 의도를 사본 링으로 소유한다**(2026-10). 서버의 받는 버퍼는 연결 목록 원소라 연결이 늘면 주소가 바뀌므로 빌려 주지 않는다. **되감기는 키 상태 + 같은 고정 프레임으로 다시 돌기이고 플레이 씬이 아니라 그 스냅샷으로 띄운 별도 리플레이 씬에서 돈다**(플레이 중인 상태 · 세션을 건드리지 않는다): 간격(`IntentRewindConfig::_intervalTicks` 60 틱)마다 스냅샷 바이트를 넣고 메모리 상한(64 MB, 언리얼 Replay 의 체크포인트 간격 · 보존 한도와 같은 두 값)을 넘으면 오래된 키부터 버리며, 되돌린 뒤 목표 틱 뒤의 키는 버린다(미래가 갈라진다). 재생의 틱 번호는 재생을 시작한 틱을 0 으로 센 값이다.
- **페어드 애니메이션(여러 개체가 한 타임라인으로 도는 애니메이션)은 에셋 `*.pairedanim.xml` + 씬 프레임 시스템 `PairedAnimationSystem` 의 세션이다**(2026-10-22 ~ 25, 결정 기록 5-12 `D4-6` 행에 이어). 시계는 앵커 역할의 플레이어 시각 하나가 정본이고 예측 · 역할 시각이 어긋나면 오류 로그 + 중단한다(사본 시계가 없어 어긋날 수 없고 외부가 속도를 바꾼 원인을 조용히 고치지 않는다). 정렬은 `none` · `snap`(진입 때 한 번) · `warp`(`warpEnd` 까지 휨, 목표는 매 프레임 앵커에서 재계산)이고 계속 붙어 있는 것(말 타기)은 소켓 부착 + `align="none"` 이다. 세션은 게임 스레드 · 컴포넌트 틱 · 평가 앞의 `FrameSystems` 단계에서 돈다(다른 오브젝트의 애니메이터를 바꾸므로). 포즈는 각 애니메이터가 그래프 밖 클립으로 낸다(시퀀서 덮어쓰기 슬롯은 루트 모션 · 알림 · 워핑이 안 돈다). 세트 고르기(`<Select>`)에서 통과한 세트의 시작 실패는 다음 세트로 넘어가지 않는다(데이터 · 배치 결함을 덮지 않는다). 에셋 핫 리로드는 제자리 교체 없이 표에서 떼어 내(`detachShared`) 다음 시작 전에 다시 받는다. 복제는 서버가 시작을 결정하고 (에셋 경로, 역할 → 오브젝트 번호, 세션 스냅샷)을 보내며, 롤백은 '이미 낸 시각' 커서로 공유 알림을 두 번 내지 않는다.
- **후처리 리그 편집 결과는 리그 에셋(`*.rig.xml`)의 노드 `weight` · 공통 키 `enabled`와 대상 `translation` · `rotation` 에 쓴다**(2026-10-21). `RigAsset::rewriteSettings` 가 JSON 문서에서 이름의 노드 · 대상만 그 키를 바꾸고 다른 키는 그대로 둔 뒤 런타임 로더로 다시 읽어 규칙을 본다(노드 종류마다 키를 쓰는 전체 쓰기는 종류가 늘 때마다 쓰기 · 읽기가 어긋난다). `enabled` 는 끈 노드이거나 파일에 이미 있을 때만 쓴다('적지 않음 = 켜짐'). 대상 기즈모는 자기 유닛 대상(본 · 소켓)만 옮기고(바깥 대상의 부모 공간은 호스트가 프레임마다 넣는 값이라 창이 되짚을 수 없다) 끌기를 놓을 때 한 번 파일에 쓰며 파일 글 전후를 되돌리기 하나로 기록한다.
- **캐릭터 외형 조립의 부품 병합 · 표면 상태 · 시퀀서 항목 규칙**(2026-10-24, 외형 통합 E1 ~ E6). 부품 병합(`CharacterMeshUtil::mergeMeshes`)은 같은 스켈레톤의 스킨드 부품을 머티리얼 그룹마다 구간 하나로 합치고, 합치지 못하면 오류를 남기고 몸 원본만 그린다(폴백 금지); 합친 메시는 LOD 를 갖지 못한다. 표면 상태는 프로퍼티 `Surface Channels` 가 있을 때만 `CharacterSurfaceState` 를 쥐고 상태 번호가 바뀐 프레임에만 머티리얼 인스턴스에 `_surface<채널>` 스칼라와 마스크 CPU 텍스처를 올린다 — **영역 번호 칸은 두지 않았다**(부위별 표현은 마스크 `stampHit` 이 하므로 정점 속성을 새로 정하지 않아 임포트 형식이 그대로다); 찢김은 알파 컷이라 켠 캐릭터 수만큼 프리패스 비용이 는다. 시퀀서 외형 항목은 엔진이 GameFramework 를 모르는 문제를 카메라 컷과 같은 나눔으로 푼다(엔진 `SequenceItemKind::Appearance` 이벤트, `SequenceAppearanceComponent` 가 받아 `setPreset`). 아틀라스 굽기는 엔진에 순수 계산 셋(`OutfitAtlasLayout` · `OutfitAtlasHooks` · `OutfitAtlasComposer`)만 두고 에디터 `OutfitAtlasBaker` 가 합성을 메모리에서 끝낸 뒤에만 쓴다(0..1 밖 UV · 크기가 다른 텍스처 · 아틀라스에 안 앉는 부품은 아무것도 쓰지 않고 멈춘다).
- **립싱크는 모음을 LPC 포먼트 + 대역 형태 코사인으로, 자음을 터짐 · 마찰 단서로 따로 가르고 실시간 얼굴 입력은 `IFacialLiveSource` 창구 하나로 받는다**(2026-10-22). 자음 단서가 있는 프레임은 포먼트를 재지 않는다(잡음 포락선의 봉우리는 공명이 아니다). 분석의 프리엠퍼시스가 실제 목소리의 -6 dB/옥타브 기울기를 편다고 보므로 합성 모음 테스트 신호에는 1/f 기울기를 넣는다. Live Link Face 판 번호 · 채널 순서(ARKit 52 + 머리 · 눈 회전 9)는 데이터(`livelinkface.json`)가 정본이고 다른 판 · 깨진 패킷은 오류다. 스킨 없는 모프 메시는 `Mesh::createRigidSkinSource`(본 하나, 가중치 1)로 GPU 스키닝 컴퓨트에 든다.
- **키트 여럿을 한 게임에 조립할 때 땅은 놓기 전에 빌린다**(`bindLand`; 이미 놓인 건물은 땅을 얻지 않는다). SRPG 는 전투 끝에 `releaseLand` 를 하고 공원 놀이기구 발자국은 상태 바이트에 싣지 않는다(지은 칸은 땅이 든다). 땅 나누는 규칙은 `KitCompositionTest` 가 단언하고 게임(FrontierTown)은 판을 열고 도는 흐름과 모습을 보인다.
- **Shooter3D 발 미끄러짐은 재생 속도 매칭과 보폭 워핑으로 나눠 푼다**(2026-10-24). 앞으로 걷기 · 달리기는 재생 속도 매칭(`LocomotionSpeedUtil`, 범위 0.6 ~ 1.8)으로 맞추고 범위를 넘는 몫은 몸 프리팹의 후처리 리그 노드 `StrideWarping` 이 맡는다(`ShooterAvatarComponent::applyStrideWarping` 이 프레임마다 `stride_scale` · 모델 공간 이동 방향을 넣고, 뒷걸음 · 옆걸음도 같은 길이다). 배율 범위(`min_scale` 0.6 · `max_scale` 1.6 · `max_pelvis_drop` 0.2)는 데이터라 화면을 보고 고친다.

- **NPC 일정의 세이브 경로는 디렉터 `writeState` 와 같은 호출 모양(`BinarySerializer` + `SaveGame`)의 시스템 단위 테스트가 지킨다**(2026-10-27, NPC 일정 후보 정리). 게임에 세이브 호출이 생기길 기다려 시나리오를 쌓지 않는다. 에디터 패널 실물 확인은 에디터 시나리오(`uiaudit`)의 일이라 이 후보에서 뺀다.
- **상호작용 대상은 씬당 하나의 공유 공간 격자(`InteractionSpatialIndex`)로 찾고 하는 쪽이 둘 이상인 공간만 격자를 맞춘다**(2026-10-22, 사용자 결정이 착수 조건 "하는 쪽 · 대상이 모두 수천"을 갈음; G-4 I1 ~ I3). 하는 쪽(`InteractorComponent`)은 등록된 대상 목록을 한 번 돌았다(대상 16 · 10k 오브젝트에서 +2 ~ 6 us). `InteractionSpatialIndex`(`ISceneFrameSystem`, `GameFramework/Base/Gameplay/Interaction`)가 `FrameSystems` 단계(PrePhysics 틱 앞, 게임 스레드)에서 공간(3D · 2D)마다 `SpatialHashGrid2D` 를 맞추고 셀은 그 프레임 대상들의 최대 도달 거리다. 하는 쪽이 하나면 목록을 도는 비용과 격자를 맞추는 비용이 같아(`kMinInteractorCountForGrid`) 맞추지 않은 공간은 예전처럼 목록을 돈다. 평면은 3D 가 XZ, 2D 가 XY 이고 평면 거리는 공간 거리보다 길지 않아 반경 = 최대 도달 거리 원 질의는 닿는 대상을 빠뜨리지 않으며 판정(`considerCandidate`)은 두 길이 같은 함수로 현재 위치에 한다. 위치는 물리 앞 값이라 그 프레임 물리로 바깥에서 들어온 대상은 한 프레임 늦게 후보가 된다. 사용자 수는 하는 쪽의 `onBeginPlay` · `onEndPlay` · `setSpace` 가 세고 마지막이 빠지면 매니저에서 뗀다. 격자가 이득인지는 `InteractionBenchTest.SharedGridTickWithManyInteractors`(하는 쪽 1 · 8 · 64 × 대상 2,000 · 5,000)로 재고 하는 쪽 수로 나눈 비용이 격자에서 더 낮지 않으면 `kMinInteractorCountForGrid` 를 올린다(`Candidates-gameplay.md` `InteractionGridThreshold`).
