# Games (게임 프로젝트 관리)

엔진을 기반으로 실제 개발할 게임 컨텐츠(팩)들이 모여있는 폴더입니다.
어떤 게임을 활성화하여 빌드할지는 CMake 설정인 `SW_ACTIVE_GAME` 변수를 통해 결정합니다. 기본 템플릿은 `Empty`입니다 (`-DSW_ACTIVE_GAME=Empty`).

이때 컴파일되는 실행 파일과 타겟의 이름은 어떤 게임을 선택하든 항상 **SWGame**으로 고정됩니다. 이는 런타임에 게임 로직을 갈아끼우는 핫리로드(LiveReload) 기능이 고정된 모듈 이름을 안정적으로 찾을 수 있도록 하기 위함입니다.

## 들어 있는 게임

| 폴더 | 무엇 | 빌드 |
|------|------|------|
| `Empty` | 최소 템플릿 + 렌더 벤치 하네스(`-gv_benchMeshes=N`). 기본값 | `-DSW_ACTIVE_GAME=Empty` |
| `AbilityArena` | 어빌리티 시스템(`GameFramework/Base/Ability`)을 실제로 쓰는 탑다운 웨이브 아레나 — 근접 · 화염구(화상 스택) · 회복(데이터만) · 대시(무적) · 가시 | `-DSW_ACTIVE_GAME=AbilityArena` |
| `HarvestValley` | 농장 생활(하베스트 문 장르, `GF_Farming`) — 갈기 · 물 · 심기 · 거두기 · 출하 · 잠, 계절 · 비, 직교 탑다운 시점 | `-DSW_ACTIVE_GAME=HarvestValley` |
| `MeadowVillage` | 키트 조립 시험(농장 `GF_Farming` + 생물 마을 `GF_CreatureLife`) — 공유 상태 `GameStateComponent` 와 키트 디렉터 둘이 한 오브젝트에, 밭이 번 돈으로 마을이 과수원을 심어 부탁이 끝난다 | `-DSW_ACTIVE_GAME=MeadowVillage` |
| `NileCity` | 도시 건설(파라오 장르, `GF_CityBuilder`) — 절차 나일 강 · 범람원 · 사막, 도로 · 우물 · 농장 → 창고 → 바자 → 집 사슬, 순회 일꾼, 집 진화, 범람, 마우스 짓기 · 허물기, `-gv_nileAutoPlay=1` | `-DSW_ACTIVE_GAME=NileCity` |
| `Shooter3D` | 1인칭 슈터(기반 `Combat`) — 소총 · 산탄총 · 권총, 히트스캔 · 퍼짐 · 반동, 드론 웨이브, Kenney 조준선(CC0) | `-DSW_ACTIVE_GAME=Shooter3D` |
| `StarSkirmish` | 실시간 전략(스타크래프트 장르, `GF_RealTimeStrategy`) — 절차 맵(두 기지 · 광물 · 간헐천 · 절벽), 채취 · 생산 · 건설 · 전투 · 안개, 끌어 고르기 · 오른쪽 클릭 · 부대, 사람 대 AI 또는 `-gv_skirmishAutoPlay=1` AI 대 AI | `-DSW_ACTIVE_GAME=StarSkirmish` |
| `ThemeParkTycoon` | 놀이공원 경영(롤러코스터 타이쿤 장르, `GF_ThemePark`) — 코스터를 짓고 시험 운행이 평가, 손님 · 줄 · 표 · 평점, 아이소메트릭 직교 시점 · 코스터 탑승. **씬 · 프리팹 · 컴포넌트 구조의 본보기**(아래 레시피) | `-DSW_ACTIVE_GAME=ThemeParkTycoon` |
| `VoxelCraft` | 복셀 샌드박스(마인크래프트 장르, `GF_Voxel`) — 지형 · 나무 · 광석, 부수기 · 놓기 · 핫바, 청크 다시 짓기 | `-DSW_ACTIVE_GAME=VoxelCraft` |

고르지 않은 게임은 빌드되지 않습니다(CI 는 `Empty` 만 짓습니다). 다른 게임을 바꿨으면 그 게임을 골라 한 번 지어 확인합니다.

#
모듈마다 소스 폴더에 매니페스트 `<모듈>.module.json` 이 있습니다(언리얼 `.uplugin` · 유니티 `package.json` 자리). 게임의 것은
`Source/Games/<게임>/SWGame.module.json` 이고, 이것이 **프로젝트**입니다 — 의존(링크하는 키트)과 다른 모듈의 켜기/끄기 표를 듭니다.

```json
{
    "_name": "SWGame", "_version": "1.0.0", "_kind": "Game",
    "_listDependency": [ { "_name": "GameFramework" }, { "_name": "GF_Voxel" } ],
    "_listPlatform": [ "Windows", "Linux" ], "_listConfiguration": [ "Dev", "Shipping" ], "_listTarget": [ "Client", "Server" ],
    "_listModuleOverride": [ { "_name": "GF_Fighting", "_bEnabled": false } ]
}
```

- `_listTarget`(필수)은 모듈이 들어가는 빌드 타깃입니다 — 게임 · 공유 키트는 `[ "Client", "Server" ]`, 서버 전용 `GF_Server_<X>` 는 `[ "Server" ]`,
  에디터 · RHI 는 `[ "Client" ]`(`Source/GameFramework/README.md` "클라이언트 · 서버로 나뉘는 기능").
- 게임이 링크하는 키트는 `_listDependency` 가 정합니다(`sw_addGameModule` 이 읽는다 — CMake 에 다시 적지 않는다).
- `_listModuleOverride` 로 끈 모듈은 **짓지 않고**(CMake), 시험 실행 파일에서도 그 키트를 include 하는 시험이 빠지며, App 도 올리지 않습니다.
  Shipping 은 켜진 키트만 정적 링크합니다. 켜진 모듈이 꺼진 모듈에 기대면 구성이 서고 무엇이 왜 꺼졌는지 말합니다.
- 키트의 리로드 의존(그 키트가 다시 올라오면 같이 다시 올라올 것)도 키트 매니페스트의 `_listDependency` 입니다.
- 규칙은 `cmake/Engine/ModuleManifest.cmake` 와 `Engine/Module/ModuleCatalog` 가 같고, `ModuleCatalogTest.BuildAndRuntimeAgree` 가 둘의 답을 견줍니다.

## 새로운 게임 추가하는 방법

1. **템플릿 복사하기**: `Source/Games/Empty/` 를 `Source/Games/<게임>/` 으로 복사합니다.
2. **벤치 하네스 지우기**: `BenchScene.*` · `BenchMoverComponent.*` 를 지우고, `EmptyGame` 의
   `_benchScene` 멤버와 그것을 쓰는 곳(초기화 · 업데이트 · 상태 직렬화 전후)을 지웁니다. 이건 측정용이고 게임 코드가 아닙니다
   (아래 "Empty 는 왜 비어 있지 않은가" 참고).
3. **필요한 키트 연결하기**: `<게임>/SWGame.module.json` 의 `_listDependency` 에 필요한 키트를 적습니다(`_kind` 는 `Game`).
4. **게임 리소스 폴더 · 프리셋 만들기**: `Resource/game/<게임 소문자>/` 을 만들고, 게임 프리셋 `Config/Game/<게임>.json`(파일 이름 = 게임 폴더 이름)에
   `_packRoot` 를 `"game/<게임 소문자>"`, 창 제목 `_windowTitle` 을 적습니다. 시작 씬은 팩의 `data/gamesettings.xml` `startMap`(타이틀이 있으면 `titleScene`) 하나입니다.
   프리셋이 없으면 configure 가 멈춥니다.
5. **CMake 프리셋 더하기**: `CMakePresets.json` 에 `Ninja-Debug-<게임>`(configure · build, `SW_ACTIVE_GAME=<게임>`, 빌드 폴더는 게임마다 따로)을
   더합니다. 게임은 프리셋으로 바꾸고 한 빌드 폴더를 다시 구성하지 않습니다(`CheckGamePresets.py`).
6. **쓰지 않는 키트 끄기(선택)**: `_listModuleOverride` 에 `{ "_name": "GF_…", "_bEnabled": false }` 를 적으면 그 키트는 이 게임의 빌드 · 실행에서 빠집니다.

## 새 게임 = 씬 + 프리팹 + 디렉터 · 뷰 컴포넌트

게임 클래스(`XxxWorld`)가 코드로 오브젝트를 만들고 매 프레임 밀어 넣는 대신, 상용 엔진처럼 나눕니다. `ThemeParkTycoon` 이 이 모양입니다
(`ThemeParkTycoon/README.md`), `HarvestValley` · `NileCity` · `StarSkirmish` 도 같은 모양입니다. `AbilityArena` 도 이 모양입니다 — 유닛마다 입력 · AI 컨트롤러 컴포넌트가 붙고, 어빌리티는 그 컨트롤러가 든 디렉터 핸들로
디렉터를 찾습니다. `Shooter3D` 도 이 모양입니다 — 1인칭 시점은 GameFramework `FirstPersonCameraComponent` 이고, 플레이어 컴포넌트가 같은 오브젝트에서
그 시점으로 걷고 쏩니다. `VoxelCraft` 도 같다 — 청크 메시는 프리팹 스폰이 아니라 청크마다 `VoxelChunkComponent` 가 절차로 짓습니다.
이제 일곱 시험 게임 모두 이 모양입니다.

| 무엇 | 어디 |
|------|------|
| 고정 배치(땅 · 해 · 카메라 · 건물 · 장식 설정 · 디렉터) | 씬 `Resource/game/<팩>/maps/*.scene.xml` — 팩의 `data/gamesettings.xml` 의 `startMap` |
| 데이터로 런타임에 생기는 것(유닛 · 손님 · 탄 · 짓는 건물) | 프리팹 `prefabs/*.prefab.xml` — `game::getService<AssetManager>()->getPrefabCache().spawn( … )` |
| 규칙 · 상태 | 키트의 보통 클래스 — 씬 없이 시험한다(컴포넌트로 만들지 않는다) |
| 규칙을 돌리고 스폰을 지시 | 디렉터 컴포넌트 하나(언리얼 GameMode/GameState) — GameFramework `GameDirectorComponent` 를 상속한다(아래) |
| 엔티티의 모습 | 뷰 컴포넌트 — 디렉터를 읽기만 하고 자기 오브젝트에만 쓴다, `TickGroup::PostUpdate` |
| 엔티티 하나의 입력 · AI | 컨트롤러 컴포넌트 — 뷰와 같은 규칙(디렉터를 읽기만, 자기 오브젝트에만 쓴다), 기본 그룹 `DuringPhysics` |
| 장르 무관 카메라 · 장식 | GameFramework `Camera/`(`OrthoCameraRigComponent` · `FirstPersonCameraComponent`) · `World/`(`PropScatterComponent` · `GravityComponent` …) |
| 게임 클래스 | `requestFirstScene()` 과, 생성자의 `registerDirector<디렉터>()` 한 줄 — 상태 저장 전에 시뮬레이션을 싣고 디렉터가 세운 것을 걷으며, 복원 뒤 돌려준다 |

**디렉터는 베이스를 쓴다** — `GameFramework/Base/Framework/GameDirectorComponent`(언리얼 `AGameModeBase` · `AGameStateBase` 의 자리, Lyra 처럼 게임 상태를 한 컴포넌트에).
골격(틱 그룹 · 상태 바이트 보류 · 틱 뒤 플러시 · 대기 소리 · 세운 것 걷기 · 자동 플레이 · 디렉터 찾기)은 베이스가 들고, 디렉터는 게임마다 다른 것만 적는다:

| 디렉터가 적는 것 | 언제 |
|------|------|
| `startGame()` | `onBeginPlay` — 데이터를 읽고 새 판을 연다. 못 열면 알리고 false(틱도 돌지 않는다) |
| `writeState` · `readState` | 상태 바이트 — 첫 값은 `StateArchiveUtil::writeHeader` 의 표 · 버전, 형식을 바꾸면 버전을 올린다 |
| `onStateRestored( bRestored )` | 복원 바이트를 적용한 뒤 — 로그, 실패면 새 판으로 되돌리기. 시작 전에 받은 바이트는 베이스가 들고 있다가 시작한 뒤 적용한다 |
| `onGameStarted()` | 판이 열리고 첫 플러시 뒤 — 조작 안내 · 카메라 |
| `tickGame( dt )` | 판이 열린 뒤 매 틱(PrePhysics) |
| `onFlush( manager, bRespawnViews )` | 틱 뒤 게임 스레드 — 쌓인 스폰을 세운다. `bRespawnViews` 면 처음(또는 걷은 뒤)이라 지금 상태의 모습 전부 |
| `onViewsDespawned()` · `hasPendingSpawn()` | 걷은 뒤 자기 핸들 목록 비우기 · 틱 끝에 플러시를 잡을지 |

세우기는 `spawnPrefab`(걷을 목록에 든다) · `destroySpawned`, 소리는 `getSoundQueue().queueClip / queueEvent / queueEventAt`(틱 뒤에 낸다), 색만 다른 모습은
`MaterialTintCache`, 뷰 · 컨트롤러가 디렉터를 찾는 것은 `GameDirectorComponent::resolve<디렉터>( manager, handle )`. 자동 플레이 PROPERTY 는 베이스의 `_bAutoPlay`
하나이고 게임은 `SW_GAME_AUTOPLAY` 로 전역 변수를 등록한다(`isAutoPlayOn()` 이 둘을 본다).

지킬 것:

- **디렉터의 시뮬레이션은 `writeState` · `readState` 로 넘긴다.** 키트의 보통 클래스는 PROPERTY 가 아니라 핫 리로드 · 세이브에서 사라진다 — 게임 모듈의 정적 ·
  컴포넌트 멤버는 모듈과 함께 내려간다. 넘길 것은 상태 바이트로만 넘기고, 게임 인스턴스가 생성자에서 `registerDirector<디렉터>()` 로 올린다(디렉터가 아닌 상태 컴포넌트는
  `registerStatefulComponent<T>()`, 상태 없이 세운 것만 걷는 컴포넌트는 `registerViewOwner<T>()` — `despawnViews()` 를 둔다).
- **틱 안에서는 구조를 바꾸지 않는다.** 디렉터는 스폰 요청을 쌓고(`hasPendingSpawn`) 베이스가 `executeOrDeferPostTick` 한 번으로 틱 뒤에 `onFlush` 를 부른다(틱 안의 `addComponent` 는 nullptr).
- **같은 그룹은 병렬이다.** 뷰는 자기 오브젝트에만 쓰고, 남의 컨테이너는 첨자(`operator[]` — Debug 경합 검출기가 쓰기로 센다) 대신 `data()` · const 참조로 읽는다.
  다른 오브젝트에 값을 넣어야 하면(디렉터 → 카메라 리그) 읽는 쪽보다 **앞 그룹**에서 넣는다.
- **런타임에만 쓰는 머티리얼 에셋을 두지 않는다.** 프리팹만 가리키는 머티리얼은 처음 스폰할 때 올라가고 마지막 것이 사라질 때 내려간다 — 색만 다르면
  씬이 늘 들고 있는 머티리얼(팔레트 · 엔진 기본)에서 디렉터가 머티리얼 인스턴스를 만들어 나눠 쓴다(백로그 1-3).
- **프레임을 넘겨 드는 것은 핸들이다.** 디렉터 · 카메라 · 세운 오브젝트를 `GameObjectHandle` 로 들고 매 프레임 푼다. 씬의 다른 엔티티를 가리키는 PROPERTY 는
  `GameObjectHandle` 이면 파일 id 로 저장되고 로드 · 쿠킹 뒤에도 이어진다.
- **씬 · 프리팹 파일은 엔진 직렬화기로 쓴다**(에디터, 또는 오브젝트를 지어 `SceneManager::saveActiveScene` · `PrefabAsset::saveToXmlFile`). 손으로 쓴 XML 은 형식을 깨기 쉽다.
- **게임 컴포넌트의 첫 `REFLECT` 는 다시 configure 해야 등록된다.** EngineTest 는 게임 모듈을 링크하지 않으므로 게임 팩의 씬 · 프리팹 검사
  (`ResourceDataSchemaTest`)는 `Source/Games` 헤더에 선언된 타입만 모르는 타입으로 넘긴다. 쿠킹은 다른 게임 팩의 그런 씬을 건너뛴다(활성 팩만 쿠킹 대상).

## Empty 는 왜 비어 있지 않은가

`Empty` 는 템플릿이면서 동시에 **렌더 경로 측정용 벤치 하네스**를 들고 있습니다.
`-gv_benchMeshes=N` 을 주면 큐브 N 개를 격자로 세우고 매 프레임 흔듭니다.

그릴 것이 씬에 올라가야 렌더 비용을 잴 수 있고, **씬을 만드는 것은 엔진이 아니라 게임의 일**이라
여기 있습니다. `Scripts/dev/RunBackendSmoke.py` 와 [검증과 측정](../../docs/08_Verification.md)의 측정 조건이 이
플래그에 기대고 있어 타깃·플래그 이름은 바꾸지 않습니다.

그래서 파일을 나눠 두었습니다 — `EmptyGame` 은 작은 템플릿이고, 벤치는 `BenchScene`(+ 틱 안에서 위치를 쓰는
`BenchMoverComponent`, `-gv_benchTickMovers=N`) · 전투 연출(`BenchCombatComponent`, `-gv_benchCombat=1` — KayKit 스켈레톤이 걸으며 발소리 알림 →
머리 · 가슴 맞음(히트 존 · 움찔) → 180 프레임 치명적 맞음(래그돌 · 손 소켓의 칼이 물리로 떨어짐) → 900 프레임 기상, 로그 `[BenchCombat]`)에 전부 들어 있습니다. 새 게임을 시작할 때 지울 경계가 파일 경계와 같아야 하기 때문입니다.
벤치가 아니면 `EmptyGame` 은 첫 씬을 요청합니다(`requestFirstScene`). 에디터가 뜨면 에디터의 시작 씬 요청이 나중이라 그쪽이 열립니다.
