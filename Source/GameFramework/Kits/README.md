# Kits — 장르 키트

## 이것은 무엇이고 왜 있나

키트는 한 장르의 규칙을 담은 모듈입니다. 농장 게임의 밭과 작물, 실시간 전략 게임의 채취와 생산, 격투 게임의 프레임 데이터가 각각 키트 하나입니다.
게임은 필요한 키트만 링크합니다. 키트가 없으면 같은 장르의 게임을 만들 때마다 규칙을 다시 짜야 합니다.
언리얼의 Game Feature 플러그인이나 유니티의 장르 템플릿 패키지와 비슷한 단위입니다.

키트는 [GameFramework 기반](../README.md) 위에 있습니다. 카메라, 인벤토리, 체력, 저장처럼 장르를 가리지 않는 것은 기반에서 가져다 쓰고,
키트에는 그 장르에만 있는 규칙만 둡니다. 이 문서는 지금 있는 키트의 목록과, 키트를 만들고 섞는 규칙을 설명합니다.

## 머릿속 그림

```text
Kits/<성격>/<그룹>/<키트>/            →  모듈 GF_<키트>          (클라이언트와 서버가 같이 쓴다)

클라이언트 · 서버로 나뉘는 기능(온라인 서비스, 저장 드라이버)은 기능 폴더 하나 아래 짝으로 둔다:
Kits/<성격>/<그룹>/<기능>/Shared/     →  모듈 GF_<기능>          (공유)
Kits/<성격>/<그룹>/<기능>/Server/     →  모듈 GF_Server_<기능>   (서버 전용)
Kits/<성격>/<그룹>/<기능>/Client/     →  모듈 GF_Client_<기능>   (클라이언트 전용, 필요할 때만)
```

**성격은 둘입니다.** `Genre/` 는 장르 규칙을 담은 장르 키트(`Action` · `Casual` · `Horror` · `Rpg` · `Simulation` · `Strategy`)이고,
`Feature/` 는 장르를 가리지 않고 쓰는 기능 키트(`Network` · `Online` · `Storage`, 타일 월드와 복셀 월드의 `World`)입니다.
경로만 다르고 모듈 이름은 `GF_<키트>` 그대로입니다. 성격 폴더 밖에 키트를 두면 `CheckGameFrameworkLayers` 가 실패시킵니다.

**키트 하나는 폴더 하나입니다.** 폴더에는 매니페스트 `GF_<키트>.module.json` 과, `sw_addGameFrameworkKit(GF_<키트>)` 한 줄짜리 `CMakeLists.txt` 가 있습니다.
키트 목록을 따로 적은 곳은 없습니다. `Kits/CMakeLists.txt` 가 매니페스트를 찾아 의존 순서대로 들어갑니다.

**키트끼리는 서로 모릅니다.** 키트는 다른 키트를 include 하지 않고 링크하지도 않습니다. 둘 이상의 키트가 같은 것을 필요로 하면 기반으로 내립니다.
같은 그룹의 키트끼리 나눠 쓰는 헤더는 그룹 폴더(`Kits/<성격>/<그룹>/x.h`)에 둘 수 있습니다. 예를 들어 `Kits/Feature/Network/NetKitMessageRange.h` 가 그렇습니다.

**규칙은 보통 클래스입니다.** `FarmField`, `RTSWorld`, `CitySimulation` 은 컴포넌트가 아닙니다. 테스트는 씬 없이 이 클래스를 만들고 돌립니다.
게임 쪽에서는 디렉터 컴포넌트가 이 클래스를 가지고 매 틱 돌립니다.

## 키트 목록

자세한 설명은 각 매니페스트의 `_description` 과 클래스의 헤더 주석에 있습니다. 여기에는 장르와, 그 키트를 실제로 쓰는 테스트 게임만 적습니다.

| 그룹 | 키트 | 장르 | 쓰는 게임 |
|------|------|------|----------|
| `Genre/Action` | `ActionCombat` | 근접 히트박스, 투사체, 유닛 스탯, 액션 룸 | |
| | `ActionAdventure` | 젤다 류 액션 어드벤처 | |
| | `ActionPlatformer` | 스테이지형 액션 플랫포머 | |
| | `Metroidvania` | 메트로배니아와 2D 소울라이크 | |
| | `Fighting` | 철권 류 3D 격투 | |
| | `MechArena` | 3인칭 팀 기체 대전 | |
| | `BattleRoyale` | 배틀로얄 | |
| `Genre/Horror` | `SurvivalHorror` | 생존 공포와 조사 | |
| | `AsymmetricHorror` | 1 대 4 비대칭 공포 | |
| | `CoopScavenger` | 협동 수집 공포 | |
| | `GhostHunt` | 루이지 맨션 류 유령 사냥 | |
| `Genre/Rpg` | `ClassicJrpg` | 클래식 JRPG | |
| | `MonsterCollector` | 포켓몬 류 몬스터 수집 | |
| | `OpenWorldWestern` | 오픈월드 서부극 | |
| | `WitcherRpg` | 위쳐 류 RPG | |
| `Genre/Strategy` | `RealTimeStrategy` | 스타크래프트 류 실시간 전략 | StarSkirmish |
| | `CityBuilder` | 파라오 류 도시 건설 | NileCity |
| | `TacticsSRPG` | 택틱스 SRPG | |
| | `SideScrollConquest` | 횡스크롤 정복 | |
| `Genre/Simulation` | `Farming` | 농장 생활 | HarvestValley, MeadowVillage |
| | `CreatureLife` | 생물 생활과 마을 | MeadowVillage |
| | `RestaurantSim` | 식당 경영 | |
| | `ThemePark` | 롤러코스터 타이쿤 | ThemeParkTycoon |
| `Genre/Casual` | `CardGame` | 포커, 맞고, 솔리테어, 우노, 덱 빌딩 | |
| | `Rhythm` | 건반 리듬 | |
| | `PartyArena` | 파티 아레나 | |
| | `KartRacing` | 카트 레이싱 | |
| `Feature/Network` | `NetClientServer` | 권위 서버 복제와 예측 | |
| | `NetLockstep` | 락스텝과 롤백 | |
| | `NetTurnRelay` | 턴제 중계 | |
| | `NetMmo` | 관심 영역 복제 | |
| | `NetDestruction` | 파괴 상태 복제 | |
| | `NetSimulation` | 한 프로세스 가상 서버 하니스 | |
| `Feature/World` | `Overworld` | 타일 걷기 필드와 존 | |
| | `Voxel` | 마인크래프트 류 복셀 샌드박스 | VoxelCraft |
| `Feature/Online` | `Account` 외 10개 | 온라인 서비스 | [Online](../Base/Online/README.md) |
| `Feature/Storage` | `SQLStore`, 서버 `CacheStore` | 서비스 저장 드라이버 | [Online](../Base/Online/README.md) |

쓰는 게임이 비어 있는 키트는 테스트(`Test/EngineTest/GameFramework/Kits/`)만 씁니다. 그런 키트도 기반을 쓰는 방식은 같으므로, 새 게임을 만들 때 그대로 링크할 수 있습니다.

## 작동 원리

### 클라이언트와 서버로 나뉘는 기능

매니페스트의 `_listTarget` 은 모듈이 들어가는 빌드 타깃입니다. 언리얼 모듈의 ClientOnly, ServerOnly 타입에 해당합니다.
CMake 는 빌드 타깃(`SW_TARGET_TYPE`)과 겹치지 않는 모듈을 빌드하지 않고, 실행할 때는 호스트가 한 번 더 거릅니다. App 은 빌드 마스크를, Server 실행 파일은 Server 를 씁니다.

한 기능은 모듈을 최대 셋까지 가질 수 있습니다.

- **공유 키트** `GF_<X>`(`["Client", "Server"]`)는 메시지 id, 직렬화, 프로토콜 상수, 클라이언트 요청 함수, 게임플레이를 가집니다.
- **서버 키트** `GF_Server_<X>`(`["Server"]`)는 인증, 세션, 저장소, 관리 명령을 가집니다. 공유 키트에 의존합니다.
- **클라이언트 키트** `GF_Client_<X>`(`["Client"]`)는 UI 와 위젯을 가집니다. 지금은 하나도 없습니다.

의존과 include 는 서버 키트에서 공유 키트로, 클라이언트 키트에서 공유 키트로만 갑니다. 서버 키트는 같은 기능의 공유 키트만 include 할 수 있습니다.
`CheckModuleTargets` 가 이름 접두와 의존을, `CheckGameFrameworkLayers` 가 키트 사이 include 를 검사합니다.
DB 와 캐시 드라이버, 그리고 그 서드파티 라이브러리는 `["Server"]` 모듈 안에만 둡니다.

### 키트 여럿을 한 게임에

키트는 게임 전체를 쥐지 않습니다. 농장 키트와 생물 마을 키트를 한 게임에 섞으면, 둘이 같은 돈과 같은 시계와 같은 퀘스트 일지를 봐야 합니다.
그래서 공유 상태는 기반의 `GameStateComponent` 하나가 가지고, 키트는 그것을 빌려 씁니다. 조립 테스트는 `KitCompositionTest` 이고, 테스트 게임은 `MeadowVillage` 입니다.

**공유 상태 컴포넌트를 디렉터들과 같은 오브젝트의 맨 앞에 붙입니다.** 한 오브젝트의 틱은 붙은 순서로 한 워커에서 돕니다.
공유 상태를 만지는 디렉터를 다른 오브젝트에 두면 같은 틱 그룹에서 동시에 돌아 데이터 경쟁이 생깁니다.
씬을 나눠야 하면 뒤쪽 디렉터의 PROPERTY `_tickAfter` 로 앞 오브젝트의 디렉터를 가리킵니다. 그러면 규칙이 서브틱으로 옮겨 그 디렉터 뒤에 돕니다.

**키트 시뮬레이션은 기반 상태를 빌립니다.** 키트 클래스는 지갑이나 가방을 값으로 가지지 않고, `GameStateRefs`(기반 상태 포인터를 모은 구조체)로 받습니다.
키트 하나만 쓰는 게임은 디렉터가 기반 상태를 가지고 키트에 빌려 줍니다.

**땅에 무언가 놓는 키트는 땅을 빌립니다.** 밭, 마을, 도시, RTS, SRPG 전장, 공원, 복셀 키트가 `bindLand( LandRegistry*, 원점 )` 으로 공유 땅을 받습니다.
놓기 전에 땅을 얻고, 치우면 돌려줍니다. 다른 키트가 얻은 땅에는 놓지 못하고, 다른 키트가 막아 둔 땅은 길찾기가 피합니다. 키트 자기 격자(밭 셀, 도시 셀)는 그대로 키트의 것입니다.

**입력은 입력 맵 액션으로만 읽고, 카메라는 한 디렉터만 움직입니다.** 같은 키를 두 디렉터가 읽으면 액션 이름이 갈라서 드러납니다.

**새 게임을 시작할 때만 시작값을 넣습니다.** 게임 진행을 여는 것은 그 오브젝트의 첫 디렉터(`GameStateComponent::initialize`)입니다.
시작 돈이나 공유 일지에 알리는 시작 배치는 `isFreshGame()` 일 때만 합니다. 복원한 게임에 시작값을 다시 넣으면 돈이 두 번 들어옵니다.

**상태 데이터는 구간으로 씁니다.** 키트마다 `StateArchiveUtil::writeSection` 으로 태그, 버전, 길이가 붙은 구간 하나를 씁니다.
키트 조각을 그냥 이어 쓰면 한 키트의 형식이 바뀔 때 저장 전체를 잃기 때문입니다.

코드 없이 규칙으로 정한 것도 있습니다.

- 자동 플레이 스위치(`-gv_<게임>AutoPlay`)는 게임 전체의 것이라 모든 디렉터가 같이 켜집니다. 디렉터 하나만 켜려면 PROPERTY `_bAutoPlay` 를 씁니다.
- 한 게임 진행에 경기 흐름(`MatchState`)은 하나입니다. 파티 게임은 미니게임을 차례로 돌리고, 경기 사이의 점수는 `RoundSeries` 가 셉니다.
- 키트마다 고정 스텝이 달라도 서로의 스텝 중간 값을 읽지 않습니다. 공유 상태는 디렉터 틱 경계에서만 봅니다.
- 모듈 로드 순서는 매니페스트 의존 순서이고, 같으면 이름순이라 실행마다 같습니다(`ArchitectureTest.LiveReloadOneOfTwoKitsCascadesIntoTheGameOnly`).

### 네트워크 키트

네트워크 키트(`Kits/Feature/Network/`)는 장르별 동기화 방식입니다. 공통 전송 계층은 `Core/Network` 에 있고, 싱글 게임은 네트워크 키트를 링크하지 않습니다.
슈터와 액션은 `NetClientServer`, RTS 와 격투는 `NetLockstep`, 카드와 보드는 `NetTurnRelay`, MMO 는 `NetMmo` 를 씁니다.

메시지 첫 바이트의 범위는 키트마다 나뉘어 있습니다(`NetMessageRange`, `NetKitMessageRange.h`). 그래서 한 게임이 키트 둘을 같이 써도 메시지가 섞이지 않습니다.
키트의 서버와 클라이언트는 모두 `INetMessageHandler` 이고, `NetMessageRouter` 에 등록해 두면 `pump( host )` 가 범위대로 나눠 주고 게임 메시지만 돌려줍니다.

`ReplicationServer` 와 `MmoReplicator` 에 `setTaskManager( &engine::getTaskManager() )` 를 주면 관찰자마다의 스냅샷과 관심 영역 계산을 워커에 나눕니다.
결과는 한 스레드로 돌린 것과 바이트까지 같습니다(`NetParallelTest`). 관찰자 128명, 엔티티 8,000개에서 틱당 4.4ms 가 워커 3개로 1.5ms 가 되었습니다.
이때 정책 인터페이스(`IReplicationPolicy`, `IInterestPolicy`)는 여러 스레드에서 동시에 불리므로 읽기만 해야 합니다.

`NetSimulation` 은 한 프로세스 안에 서버 월드 하나와 클라이언트 월드 여럿을 띄우는 테스트 하니스(`NetSimHarness`)입니다.
언리얼 PIE 의 "Play As Client" 와 Network Emulation, 유니티 Multiplayer Play Mode 에 해당합니다.
월드마다 씬과 물리를 따로 가지고, 연결마다 지연과 손실 같은 회선 조건을 줍니다. 모든 난수가 씨앗에서 나오므로 같은 씨앗이면 같은 패킷이 같은 틱에 도착합니다.

## 확장하는 법

### 새 키트를 만들 때

1. 성격과 그룹 아래 폴더를 만듭니다(`Kits/Genre/<그룹>/<키트>/` · `Kits/Feature/<그룹>/<키트>/`). 클라이언트 · 서버로 나뉘는 기능이면
   기능 폴더 아래 `Shared/` · `Server/` · `Client/` 에 키트를 하나씩 둡니다(`Kits/Feature/Online/Chat/Shared/` · `Kits/Feature/Online/Chat/Server/`).
2. 매니페스트 `GF_<키트>.module.json` 을 둡니다. `_kind` 는 `Kit`, `_listDependency` 에는 `GameFramework` 와 필요하면 같은 기능의 공유 키트, `_listTarget` 은 위 규칙대로 적습니다.
3. `CMakeLists.txt` 에 `sw_addGameFrameworkKit(GF_<키트>)` 한 줄을 씁니다. 서버 키트는 `sw_linkSharedKit` 으로 공유 키트를 링크합니다.
4. 규칙은 보통 클래스로 짜고, 테스트를 `Test/EngineTest/GameFramework/Kits/<그룹>/` 에 씬 없이 둡니다. 테스트 실행 파일은 켜진 키트를 레지스트리로 링크합니다.
5. 다시 configure 합니다. 게임은 `SWGame.module.json` 의 `_listDependency` 에 키트 이름을 적어 링크합니다.

리플렉션 헤더는 자동 탐색이라 키트 `CMakeLists.txt` 에 헤더 목록을 적지 않습니다.

### 키트 안의 폴더

키트 루트의 소스 파일(`.h` · `.cpp`)이 10 개가 되기 전까지는 평평하게 둡니다. 10 개가 되면 아래 이름의 하위 폴더로 나눕니다.
모든 키트가 같은 이름을 쓰므로, 처음 보는 키트에서도 데이터 정의와 규칙이 어디 있는지 바로 압니다. 소스는 재귀로 모으므로 CMake 는 고치지 않습니다.

| 폴더 | 무엇 |
|------|------|
| `Catalog/` | XML 데이터 정의와 카탈로그(`*Catalog`, 블록 · 화폐 · 상품 정의) |
| `Rule/` | 씬 없이 도는 규칙 클래스(시뮬레이션 상태와 걸음) |
| `Component/` | 씬에 붙는 컴포넌트와 조종자 |
| `View/` | 화면에 내는 것을 만드는 도우미(메시 생성, 표시용 값) |
| `Protocol/` | 메시지 id, 직렬화, 프로토콜 상수, 공유 타입(온라인 · 네트워크 키트) |
| `API/` | 클라이언트가 부르는 요청 함수와 서버 상태의 읽기 사본(온라인 공유 키트) |
| `Service/` | 서비스 처리기와 스트림 바인딩(서버 키트) |

- 루트에는 매니페스트, `CMakeLists.txt`, 키트 전체가 쓰는 이벤트 · 타입 헤더(`ActionCombatEvents.h`)만 남깁니다.
- 한 기능에 묶인 파일이 여럿이면 기능 이름의 폴더를 따로 둡니다(`Platform/` 로그인 제공자, `Push/` 푸시 제공자, `Driver/` DB 드라이버).
- `Server` · `Client` · `Shared` 는 키트 안 폴더 이름으로 쓰지 않습니다. 게이트가 그 이름을 서버 · 클라이언트 키트 경로로 읽습니다.
- 게임 여럿을 담은 키트는 게임별 폴더로 나눕니다. `CardGame` 은 `Klondike` · `Matgo` · `Poker` · `Uno` · `DeckBattle` 이고, 덱 공통(`CardDeck`)만 루트에 둡니다.
  키트를 게임마다 쪼개지는 않습니다. 키트마다 DLL 이 하나라 쪼개면 DLL 이 늘어납니다.

### 키트를 만들 때 지킬 것 — 게임 하나가 아니라 장르의 뼈대

키트는 장르 전체가 쓰는 것이라, 게임 하나의 규칙이 타입에 박히면 같은 장르의 다른 게임이 키트를 못 씁니다. 규칙은 셋입니다.

**개수를 코드가 정하지 않습니다.** `MonsterDef` 의 사격 패턴(`_listShot`)은 `vector` 이고, XML 은 `<Shot>` 원소를 있는 만큼 읽습니다. 한 번에 몇 발인지는 데이터가 정합니다.

**종류를 코드가 정하지 않습니다.** `MonsterDef` 의 보상은 `_mapDrop` 이고, `<Drop exp="10" souls="3"/>` 처럼 속성 이름이 곧 보상 이름입니다.

**같은 문제는 같은 방식으로 풉니다.** id 로 행을 찾는 일은 `MonsterCatalog` 도 `MonsterCollectorCatalog` 도 맵으로 합니다. 한 프레임워크 안에서 같은 일을 두 방식으로 하면 읽는 사람이 어느 쪽이 기준인지 모릅니다.

새 타입을 넣기 전에 "이 장르의 다른 게임이 이 필드를 그대로 쓸 수 있나?"를 물어보세요. "슬롯 2개", "통화 2종", "스탯 이름 고정"이 나오면 거의 항상 아닙니다.

### 기반에 이미 있는 것

아래는 키트 셋 이상에 따로 있던 것을 기반으로 모은 것입니다. 키트에서 다시 만들지 않습니다.

| 필요한 것 | 쓸 것 |
|-----------|-------|
| XML 정의 목록(id, 읽은 순서) | `GameCatalog<T>`, `XMLCatalog<T>`, `GameDataXML` |
| 다시 재현되는 난수(테스트, 리플레이) | `GameRandom`(상태 있음), `GameHash`(좌표를 수로) |
| 가중치 고르기와 섞기 | `GameRandom::pickWeightedIndex`, `shuffle` |
| 프레임 수와 상관없는 시뮬레이션 | `FixedStepTimer` |
| 쿨다운과 반복 간격 | `Countdown` |
| 히트스캔, 클릭 고르기, 원뿔 시야 | `RayMath` |
| 여러 자원 비용 | `StatBlock::canAfford`, `trySpend` |
| 아이템과 개수의 값 목록 | `ItemStackList` |
| 격자의 셀 번호와 이웃 | `GridTopology`, `GridSearchScratch` |
| 상태 데이터의 머리와 구간 | `StateArchiveUtil` |
| 시뮬레이션 알림 버퍼 | `EventBuffer<T>` |
| 직교 카메라와 장식 흩뿌리기 | `OrthoCameraRigComponent`, `PropScatterComponent` |
| 1인칭 카메라와 마우스 잠금 | `FirstPersonCameraComponent`, `PawnComponent::_bLockMouse` |
| 무기, 피해 공식, 탄도 | `Base/Actor/Combat/` |
| 인벤토리, 장비, 전리품, 제작 | `Base/Gameplay/Inventory/` |
| 레벨, 스킬 트리, 평판 | `Base/Gameplay/Progression/` |
| 퀘스트, 시계, 날씨, 지역 그래프 | `Base/Gameplay/Quest/`, `Base/World/` |
| 팀, 점수, 부활, 라운드 | `Base/Gameplay/Match/` |
| 길찾기, 흐름장, 이동 범위 | `Base/Actor/Navigation/` |
| 행동 트리와 감각 | `Base/Actor/AI/` |
| 타이밍 판정, 커맨드 입력, 턴 순서, 록온 | `Base/Actor/Input/`, `TurnOrder`, `LockOnSelector` |

## 함정과 주의

- **빌린 기반 객체의 알림은 꺼내지 않습니다.** `EventBuffer::drainTo` 는 소비자가 하나라서, 키트가 꺼내면 게임 화면이나 다른 키트가 받을 알림이 사라집니다.
  키트는 상태를 봅니다(`QuestLog::getStatus`). 시계 알림은 `GameStateComponent::getClockEvents` 를 여럿이 읽습니다.
- **알림을 꺼내는 `drainEvents( outListEvent )` 는 받는 쪽 목록 뒤에 붙이고 자기 목록을 비웁니다.** 바꿔치기하지 않습니다. 매 프레임 같은 목록을 다시 쓰는 쪽은 먼저 `clear()` 합니다.
- **키트는 키를 직접 읽지 않고, 입력 액션 이름도 글자로 박지 않습니다.** 액션 이름은 키트 설정 필드로 받습니다. 입력 맵은 게임에 하나라 키트가 이름을 정하면 다른 키트와 부딪힙니다.
- **이름 공간을 나눕니다.** 키트가 읽는 게임 설정의 사용자 값은 `<키트>.` 로 시작하고(`Farming.startingGold`), 상태 태그(`kStateTag`, 네 글자)는 저장소 전체에서 하나여야 합니다.
  키트는 전역 변수를 정의하지 않습니다. 스위치는 게임과 엔진의 것입니다. 이 규칙들은 `CheckKitNamespaces` 가 검사합니다.
- **네트워크 메시지 범위가 겹치는 처리기는 라우터가 받지 않습니다.** `NetMessageRouter::addHandler` 가 false 를 돌려줍니다.
- **같은 틱 흐름을 보는 테스트는 그 틱에서 단언합니다.** 한 틱 뒤에 보면 붙인 순서를 바꿔도 통과합니다(`KitCompositionTest` 의 84 틱).
- **땅 검사의 변이 테스트는 막히지 않은 땅(도로)으로 합니다.** 막힌 땅에서는 RTS 의 건설 거절이 땅 격자에서 이미 나므로, 땅 검사를 빼도 테스트가 지지 않습니다.
  RTS 는 막힌 다른 키트의 땅을 자기 땅 격자에 칠하고, 땅 리비전이 바뀌면 다시 칠합니다. 도로처럼 막히지 않은 땅은 걸을 수는 있지만 짓지는 못합니다(`canPlaceBuilding`).
- **액션 룸의 적은 몬스터 정의입니다.** 종 id(`grunt`, `boss`)를 게임이 등록한 `MonsterCatalog` 서비스에서 찾고, 없으면 내장 정의를 씁니다.
  룸이 돌려주는 플레이어 피해(`_damageToPlayer`)는 방어 전 값입니다. 게임이 그것을 `UnitStatsComponent::takeDamage` 로 넣으면 방어가 한 번 빠집니다.
- **피해는 `UnitStatsComponent::applyTakeDamage` 한 곳에서만 깎습니다.** 방어 식은 `DamageMath::applyArmor`(최소 1)이고, 0 이하 피해는 맞지 않은 것으로 봅니다.
  `DamageAppliedEvent` 는 큐를 거치므로, 같은 프레임에 받아야 하면 `registerDamageApplied` 를 씁니다.
- **턴제 몬스터 전투는 `MonsterCollector` 하나입니다.** 전투 연출(단계 타이머, HUD 한 줄)은 게임이 맡습니다.
- **오버월드 맵은 조우가 일어나는 셀만 알려 줍니다.** 무엇을 만나는지는 장르 키트의 지역 테이블(`MonsterCollectorCatalog::rollEncounter`, `JrpgEncounterWalker`)이 정합니다.
  존 역할은 열거가 아니라 맵 `<role>` 의 태그 목록이고(`ZoneTracker::setFromMap`), 클리어 게이트는 `clear_gate` 태그입니다. 경로 이름에서 역할을 짐작하지 않습니다.
- **타일맵 레이어 테이블(`kArrTileFlagLayerInfo`)의 XML 속성 이름과 줄 순서는 파일 형식입니다.** 바꾸면 기존 맵의 그 레이어가 기본값으로 읽힙니다(`TileMapXMLTest.SavedBytesMatchTheExistingFormat`).
  레이어를 더할 때는 `TileFlagLayer` 값과 이 테이블 한 줄만 고칩니다.
- **반복 간격(연사, 스폰, 자동 공격)은 끝난 걸음에서 `Countdown::restart` 를 부릅니다.** 간격 값으로 덮어쓰면 지나친 시간을 버려서 빈도가 fps 에 묶입니다.
  float 로 빼면 0 에 조금 못 미쳐 한 걸음을 더 기다립니다(RTS 0.05초 걸음에서 1.2초가 1.25초가 됩니다). "원하는 동안 간격마다 한 번"은 `Countdown::tickRepeat` 한 줄입니다.
- **가중치 뽑기에서 가중치 0 은 후보가 아니라는 뜻입니다.** `pickWeightedIndex` 가 −1 을 돌려주면 아무것도 고르지 않은 것입니다.
  실수 가중치는 `pickWeightedIndex`, 정수 테이블은 `pickWeightedIndexInt` 를 씁니다. 둘을 바꾸면 난수 흐름이 달라져 같은 씨앗에서 결과가 바뀝니다.
- **격자를 가진 클래스는 `GridTopology _topology` 하나를 가집니다.** 너비와 높이를 따로 두지 않고, 셀 번호와 경계는 `toIndex`, `isInside`, `isRectInside` 로만 계산합니다.
  `y × 너비 + x` 를 손으로 쓰지 않습니다.
- **팀 적대 판정은 `Match/TeamAttitude.h` 의 `TeamAttitudeUtil` 하나입니다.** 팀은 정수 번호이고 `kNoTeam`(−1)은 누구와도 중립입니다.
  `Combat` 이 아니라 `Match` 에 있는 것은 `MatchState`(층 1)가 쓰기 때문입니다. 동맹 테이블이 생기면 판정기를 이곳에 붙입니다.
- **라운드 진행은 `Match/RoundSeries` 하나입니다.** 알림을 보내지 않고 결과를 돌려주므로 키트가 자기 이벤트로 보냅니다.
  롤백 상태에 실을 때는 맨 뒤에 둡니다. `readState` 는 끝까지 맞을 때만 바꾸므로, 마지막에 읽으면 키트의 상태 로드 전체가 원자적이 됩니다.
- **상태 데이터 테스트는 "다시 쓴 바이트가 같다"로 끝내지 않습니다.** 빠진 필드는 쓰기와 읽기 양쪽에서 빠져 있어 왕복 바이트가 늘 같습니다.
  같은 걸음을 양쪽에서 더 돌려 결과가 같은지까지 봅니다(`ElementGrid` 는 남은 시간 값을 빠뜨려 복원한 불이 처음부터 다시 탔습니다).
  필드 하나를 양쪽에서 빼 보는 변이로 테스트가 실패하는지 확인합니다.

## 더 볼 곳

- [GameFramework](../README.md) — 기반 폴더, 조종, 디렉터 베이스
- [Online](../Base/Online/README.md) — 온라인 서비스 키트와 저장 키트
- [Games](../../Games/README.md) — 키트를 링크하는 게임 만들기
- [Core](../../Core/README.md) — 네트워크 전송 계층(`Core/Network`)
- `Scripts/lint/gate/CheckKitNamespaces.py`, `CheckGameFrameworkLayers.py`, `CheckModuleTargets.py` — 키트 규칙을 검사하는 게이트
