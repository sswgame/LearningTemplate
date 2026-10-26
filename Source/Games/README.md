# Games — 게임 모듈과 새 게임 만들기

## 이것은 무엇이고 왜 있나

엔진 위에서 실제로 도는 테스트 게임이 이 폴더에 있습니다. 게임 하나는 폴더 하나(`Source/Games/<게임>/`)이고 리소스는 게임 팩(`Resource/game/<게임 소문자>/`)에 있습니다. 장르 키트와 엔진 기능이 실제 게임 흐름에서 맞물리는지 확인하고 새 게임의 본보기로 씁니다.

빌드 하나(`Ninja-Debug`)가 게임 전부를 짓고 돌릴 게임은 `App.exe -game=<게임>` 으로 고릅니다(게임별 CMake 프리셋은 없다). `SW_ACTIVE_GAME`(기본 `Empty`)은 `-game=` 이 없을 때의 게임이자 Shipping 이 정적으로 링크해 굽는 게임입니다.
빌드 타깃은 `SWGame_<게임>`, 모듈 이름 · 산출물은 늘 `SWGame` 입니다(핫 리로드가 고정된 이름으로 찾는다). 활성 게임은 `Bin/Modules/SWGame` 이고 Shipping 링크 · 테스트 의존은 전역 속성 `SW_ACTIVE_GAME_TARGET` 으로 묻습니다.

`SW_GAMES`(Dev 전용)로 다른 게임도 같은 빌드에서 짓습니다. `Ninja-Debug` 는 `ALL` 이라 엔진 · 프레임워크 · 키트는 한 번만 컴파일하고 게임 산출물은 `Bin/Games/<게임>/SWGame.dll`(PDB 는 `Bin/Symbols/Games/<게임>/`)이며 게임끼리 PCH 하나(`sw_game_pch`)를 나눠 씁니다.
`App.exe -game=<게임>`(Dev)은 그 게임의 프리셋(`Config/Game/<게임>.json`) · 매니페스트 · DLL 을 쓰고 핫 리로드도 그 DLL 을 봅니다(`GameSelection`). `-game=` 이 없으면 활성 게임이고 짓지 않은 게임을 고르면 기동이 오류로 섭니다. 게임 이름은 엔진 TU 에 굽지 않아(`Bin/Modules/ActiveGame.txt`) 활성 게임을 바꿔도 다시 컴파일되는 것은 게임 모듈뿐입니다.
`Ninja-Debug-Lean` 은 `SW_BUILD_UNUSED_KITS=OFF` 라 고른 게임이 닿는 키트의 합집합만 짓고(`-DSW_GAMES=NileCity;Shooter3D`), 실행할 때는 고른 게임이 닿지 않고 다른 게임 팩만 닿는 키트를 올리지 않습니다(`ModuleCatalogLoader`).

## 머릿속 그림

```text
Source/Games/<게임>/SWGame.module.json   → 프로젝트: 링크할 키트, 끌 모듈
Source/Games/<게임>/CMakeLists.txt        → sw_addGameModule(SWGame) 한 줄
Source/Games/<게임>/*Game.cpp             → 게임 인스턴스(GameInstanceBase 파생)
Config/Game/<게임>.json                   → 게임 프리셋: 팩 루트(창 제목은 팩 gamesettings.xml 의 <Engine>)
Resource/game/<게임 소문자>/               → 팩: data/gamesettings.xml, maps/, prefabs/, automation/
```

**게임 매니페스트가 프로젝트입니다**(언리얼의 `.uproject`). **게임 인스턴스**(`GameInstanceBase` 파생 하나)가 첫 씬을 요청하고 게임 서비스(카탈로그 등)를 등록하고 디렉터를 상태 스냅샷에 올립니다.

**게임 데이터는 데이터가 정본입니다.** 게임 코드에 경로 · 이름 문자열을 적지 않습니다.

- 카탈로그는 `onInitialize` 에서 `loadRequiredCatalog( _catalog )` 한 줄로 읽습니다. 경로는 카탈로그 타입의 `kDataKind` 등록 줄(`SW_RESOURCE_DATA_KIND`)과 팩 루트로 만들어지고(`GamePackDataUtil`) 못 읽으면 파일과 이유를 오류로 남기고 게임 초기화가 실패합니다(다른 읽기는 `loadRequiredData( kDataKind, 읽기 )`).
- 사운드 이벤트 · 로딩 화면은 팩의 `data/gamesettings.xml` 의 `audioEvents` · `loadingScreen` 이고 베이스가 올리고 내립니다(적었는데 못 읽으면 게임이 뜨지 않는다).
- 디렉터 · 컴포넌트가 쓰는 데이터 경로 · 프리팹 · 입력 액션 · 카탈로그 id 는 생성자 초기값이 아니라 씬의 PROPERTY 값입니다. 액션 이름은 판을 열 때 `InputActionCheckUtil::hasAllActions` 로, id 는 카탈로그로 확인하고 빠지면 오류로 멈춥니다(본보기 `SkirmishDirectorComponent`).

## 들어 있는 게임

| 폴더 | 장르 | 이 게임으로 배우는 것 |
|------|------|----------------------|
| `Empty` | 최소 템플릿 | 새 게임의 출발점, 렌더 벤치 하네스(`-gv_benchMeshes=N`) |
| [`AbilityArena`](AbilityArena/README.md) | 탑다운 웨이브 아레나 | 어빌리티 시스템(GAS), 폰과 AI 조종자 |
| [`FrontierTown`](FrontierTown/README.md) | 도시 + 실시간 전략 | 격자 키트 둘이 공유 땅 하나를 나누기(`bindLand`) |
| [`HarvestValley`](HarvestValley/README.md) | 농장 생활 | 키트 하나(`GF_Farming`), 공유 상태, 자동 플레이 빙의 |
| [`MeadowVillage`](MeadowVillage/README.md) | 농장 + 생물 마을 | 키트 둘을 한 게임에 섞기 |
| [`NileCity`](NileCity/README.md) | 도시 건설 | 명령형 디렉터, 절차 지형, 커서 아래 땅 고르기 |
| [`PixelTown`](PixelTown/README.md) | 탑다운 RPG(2D 픽셀 아트) | 오버월드 키트의 한 칸 걸음, 타일 레이어 둘의 충돌, 주민 대화 |
| [`Shooter3D`](Shooter3D/README.md) | 1인칭 슈터 | 캐릭터 외형과 애니메이션, 페이싱 감독, HUD, 카메라 프리셋 |
| [`StarSkirmish`](StarSkirmish/README.md) | 실시간 전략 | RTS 키트, 끌어 고르기, AI 대 AI |
| [`ThemeParkTycoon`](ThemeParkTycoon/README.md) | 롤러코스터 타이쿤 | 씬, 프리팹, 디렉터, 뷰 구조의 본보기 |
| [`VoxelCraft`](VoxelCraft/README.md) | 복셀 샌드박스 | 절차 메시, 1인칭 몸, 청크 다시 만들기 |
| [`WildWest`](WildWest/README.md) | 오픈월드 승마 | 탈것 타기 · 내리기, 걸음새와 스태미나, 주인을 따르는 말 AI |

CI 는 기본 게임 `Empty` 만 빌드하고 로컬 `Ninja-Debug` 는 모두 컴파일하므로 게임 코드의 빌드 오류는 거기서 드러납니다. 호스트 테스트 `AppScenarioTest.<게임>.<시나리오>.<백엔드>`(`--test_filter=AppScenarioTest.<게임>.*`)가 함께 지은 게임마다 그 팩의 시나리오를 `-game=` 으로 네 백엔드에서 돕니다.

## 따라 해 보기 — 게임 하나를 빌드하고 시나리오 돌리기

```powershell
cmake --preset Ninja-Debug                          # 빌드 폴더 build/Ninja-Debug — 게임 전부(SW_GAMES=ALL)
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -game=HarvestValley -dx12                 # 에디터 없이, 시작 씬이 바로 플레이 중
./App.exe -game=HarvestValley -dx12 -gv_farmAutoPlay=1   # 자동 플레이
./App.exe -game=HarvestValley -dx12 -scenario=game/harvestvalley/automation/control.scenario.xml
echo $LASTEXITCODE                                  # 0 통과, 10 실패, 11 읽기 오류, 12 시간 초과, 13 건너뜀
```

게임을 바꿀 때 다시 configure 하지 않고 같은 빌드에서 `-game=` 만 바꿉니다. **자동화 시나리오**는 팩의 `automation/*.scenario.xml` 이고 형식은 Automation 에 있습니다. 모든 백엔드로 한꺼번에 돌리려면 호스트 테스트를 씁니다(GPU 와 창이 필요해 CI 는 못 돌린다).
시나리오 파일을 놓기만 하면 돌고 CMake 에 목록을 적지 않으며 로그는 `Bin/Saved/Automation/<시나리오>_<백엔드>.log` 입니다.

```powershell
ctest --test-dir build/Ninja-Debug -R AppTest_HostOnly --output-on-failure
```

## 새로운 게임 추가하는 방법

1. **템플릿 복사.** `Source/Games/Empty/` 를 `Source/Games/<게임>/` 으로 복사합니다.
2. **벤치 하네스 지우기.** `BenchScene.*`, `BenchSceneRig.cpp`, `BenchMoverComponent.*`, `BenchCombatComponent.*` 와 `EmptyGame` 의 `_benchScene` 멤버 · 사용처를 지웁니다(측정용이고 게임 코드가 아니다 — "Empty 에 벤치가 있는 이유").
3. **키트 연결.** `<게임>/SWGame.module.json` 의 `_listDependency` 에 쓸 키트를 적습니다(`_kind` 는 `Game`).
4. **리소스 폴더와 게임 프리셋.** `Resource/game/<게임 소문자>/` 를 만들고 `Config/Game/<게임>.json`(파일 이름은 게임 폴더 이름과 같아야 하고 없으면 기동 · configure 가 멈춘다)에 `_packRoot`(`"game/<게임 소문자>"`)를 적고, 창 제목은 팩의 `data/gamesettings.xml` 에 `<Engine _windowTitle="…" />` 로 적습니다. 시작 씬은 같은 파일의 `startMap`(타이틀 화면이 있으면 `titleScene`) 하나입니다.
5. **빌드와 실행.** CMake 프리셋은 더하지 않습니다. `Ninja-Debug` 가 새 게임도 짓고 `App.exe -game=<게임>` 으로 돌립니다(프리셋 · 매니페스트는 `CheckGamePresets.py` 가 검사).
6. **쓰지 않는 키트 끄기(선택).** `_listModuleOverride` 에 `{ "_name": "GF_…", "_bEnabled": false }` 를 적으면 그 키트는 이 게임의 빌드와 실행에서 빠집니다.
7. **씬과 디렉터.** 아래 절의 구조로 첫 씬과 디렉터를 만들고 자동화 시나리오 하나를 `automation/` 에 두면 호스트 테스트가 함께 돌립니다.

### 게임 매니페스트

```json
{
    "_name": "SWGame", "_version": "1.0.0", "_kind": "Game",
    "_listDependency": [ { "_name": "GameFramework" }, { "_name": "GF_Voxel" } ],
    "_listPlatform": [ "Windows", "Linux" ], "_listConfiguration": [ "Dev", "Shipping" ], "_listTarget": [ "Client", "Server" ],
    "_listModuleOverride": [ { "_name": "GF_Fighting", "_bEnabled": false } ]
}
```

- `_listTarget`(필수)은 모듈이 들어가는 빌드 타깃입니다(게임과 공유 키트 `["Client", "Server"]`, 서버 키트 `GF_Server_<X>` `["Server"]`, 에디터와 RHI `["Client"]` — 규칙은 Kits 의 "클라이언트와 서버로 나뉘는 기능").
- 링크할 키트는 `_listDependency` 가 정하고 `sw_addGameModule` 이 이 목록을 읽으므로 CMake 에 다시 적지 않습니다. 키트의 리로드 의존도 키트 매니페스트의 `_listDependency` 입니다.
- `_listModuleOverride` 로 끈 모듈은 빌드하지 않고 그 키트를 include 하는 테스트도 빠지며 App 도 로드하지 않습니다. 켜진 모듈이 꺼진 모듈에 의존하면 configure 가 이유를 알려 줍니다. 빌드 쪽(`cmake/Engine/ModuleManifest.cmake`)과 런타임 쪽(`ModuleCatalog`)은 같은 규칙으로 해석하고 `ModuleCatalogTest.BuildAndRuntimeAgree` 가 비교합니다.

## 새 게임 = 씬 + 프리팹 + 디렉터와 뷰 컴포넌트

게임 클래스가 코드로 오브젝트를 만들고 매 프레임 값을 밀어 넣는 대신 역할을 나눕니다. 본보기는 `ThemeParkTycoon` 입니다.

| 무엇 | 어디 |
|------|------|
| 고정 배치(땅, 해, 카메라, 건물, 디렉터) | 씬 `Resource/game/<팩>/maps/*.scene.xml` |
| 런타임에 데이터로 생기는 것(유닛, 손님, 탄) | 프리팹 `prefabs/*.prefab.xml` |
| 규칙과 상태 | 키트의 보통 클래스(씬 없이 테스트) |
| 규칙을 돌리고 스폰을 지시 | 디렉터 컴포넌트 하나(`GameDirectorComponent` 파생) |
| 엔티티의 모습 | 뷰 컴포넌트. 디렉터를 읽기만 하고 자기 오브젝트에만 씁니다(`TickGroup::PostUpdate`) |
| 엔티티 하나의 입력이나 AI | 폰과 조종자. 몸 컴포넌트는 폰의 의도만 읽습니다 |
| 장르 무관 카메라와 장식 | GameFramework `Camera/`(`CameraDirectorComponent` + 팩의 `data/*.cameras.xml`), `World/`(`PropScatterComponent`) |
| 게임 클래스 | `requestFirstScene()` 과 생성자의 `registerDirector<디렉터>()` 한 줄 |

### 디렉터가 구현하는 것

디렉터는 `GameFramework/Base/Foundation/Framework/GameDirectorComponent` 를 상속합니다(언리얼의 `AGameModeBase` + `AGameStateBase`). 틱 그룹, 상태 데이터 보류, 틱 뒤 플러시, 대기 소리, 스폰한 것 정리, 자동 플레이, 디렉터 찾기는 베이스가 갖고 게임마다 다른 것만 구현합니다:
`startGame()`(`onBeginPlay` 에서 데이터를 읽고 새 게임을 연다, 못 열면 false 이고 틱도 돌지 않는다), `writeState` · `readState`(첫 값은 `StateArchiveUtil::writeHeader` 의 태그와 버전), `onStateRestored( bRestored )`, `onGameStarted()`, `tickGame( dt )`(PrePhysics),
`onFlush( manager, bRespawnViews )`(틱 뒤 게임 스레드에서 쌓인 스폰을 만든다), `onViewsDespawned()`, `hasPendingSpawn()`.

`bRespawnViews` 가 참이면 처음이거나 정리한 뒤라 지금 상태의 모습을 모두 만들어야 합니다. 시작 전에 받은 복원 데이터는 베이스가 갖고 있다 시작 뒤 적용하며 상태 형식을 바꾸면 버전을 올립니다.
스폰은 `spawnPrefab`(정리 목록에 들어감)과 `destroySpawned`, 소리는 `getSoundQueue()` 의 `queueClip` · `queueEvent` · `queueEventAt`(틱 뒤에 낸다), 색만 다른 모습은 `MaterialTintCache`, 뷰와 컨트롤러가 디렉터를 찾는 것은 `GameDirectorComponent::resolve<디렉터>( manager, handle )` 입니다.
자동 플레이 PROPERTY 는 베이스의 `_bAutoPlay` 하나이고 게임은 `SW_GAME_AUTOPLAY` 로 전역 변수(`-gv_<게임>AutoPlay`)를 등록하며 `isAutoPlayOn()` 이 둘을 모두 봅니다.

## Empty 에 벤치가 있는 이유

`Empty` 는 템플릿이면서 렌더 경로 측정용 벤치 하네스를 가집니다(`-gv_benchMeshes=N` 이 큐브 N 개를 격자로 놓고 매 프레임 흔든다 — 씬을 만드는 것은 엔진이 아니라 게임의 일). `Scripts/dev/RunBackendSmoke.py` 와 [검증과 측정](../../docs/08_Verification.md)의 측정 조건이 이 플래그에 기대므로 타깃과 플래그 이름은 바꾸지 않습니다.
`-gv_benchTickMovers=N` 은 `BenchMoverComponent` 를, `-gv_benchCombat=1` 은 KayKit 스켈레톤 전투 연출(`BenchCombatComponent`)을 붙이며, 새 게임에서 지울 경계가 파일 경계와 같도록 나눠 두었습니다.

## 함정과 주의

- **디렉터의 시뮬레이션은 `writeState` 와 `readState` 로 넘깁니다.** 키트의 보통 클래스는 PROPERTY 가 아니라 핫 리로드와 세이브에서 사라지고 게임 모듈의 정적 변수와 컴포넌트 멤버는 모듈과 함께 언로드됩니다. 디렉터는 생성자에서 `registerDirector<디렉터>()`, 디렉터가 아닌 상태 컴포넌트는 `registerStatefulComponent<T>()`, 상태 없이 스폰한 것만 정리하는 컴포넌트는 `registerViewOwner<T>()` 로 올립니다.
- **틱 안에서는 구조를 바꾸지 않습니다.** 디렉터는 스폰 요청을 쌓고 베이스가 `executeOrDeferPostTick` 로 틱 뒤에 `onFlush` 를 부릅니다(틱 안의 `addComponent` 는 nullptr).
- **같은 틱 그룹은 병렬입니다.** 뷰는 자기 오브젝트에만 쓰고 다른 오브젝트의 컨테이너는 첨자(`operator[]`) 대신 `data()` 나 const 참조로 읽습니다(Debug 경합 검출기가 첨자를 쓰기로 센다). 다른 오브젝트에 값을 넣어야 하면 읽는 쪽보다 앞 그룹에서 넣습니다.
- **런타임에만 쓰는 머티리얼 에셋을 두지 않습니다.** 프리팹만 가리키는 머티리얼은 처음 스폰할 때 게임 스레드에서 로드되고 마지막 것이 사라질 때 언로드되어 렌더 스레드의 병렬 기록과 겹칩니다. 색만 다르면 씬이 늘 갖고 있는 머티리얼에서 `MaterialTintCache` 로 인스턴스를 만듭니다.
- **프레임을 넘겨 가지고 있는 것은 핸들입니다.** 디렉터와 카메라와 스폰한 오브젝트는 `GameObjectHandle` 로 갖고 매 프레임 찾습니다(씬의 다른 엔티티를 가리키는 PROPERTY 가 `GameObjectHandle` 이면 파일 id 로 저장되어 로드와 쿠킹 뒤에도 이어진다).
- **씬과 프리팹 파일은 엔진 직렬화기로 씁니다**(에디터, 또는 `SceneManager::saveActiveScene` · `PrefabAsset::saveToXMLFile`). 손으로 쓴 XML 은 형식을 깨기 쉽습니다.
- **게임 컴포넌트에 처음 `REFLECT` 를 넣으면 다시 configure 해야 등록됩니다.** EngineTest 는 게임 모듈을 링크하지 않아 게임 팩의 씬과 프리팹 검사(`ResourceDataSchemaTest`)는 `Source/Games` 헤더의 타입을 모르는 타입으로 넘기고 쿠킹은 활성 팩만 대상입니다.
- **고르지 않은 게임은 빌드되지 않습니다.** 게임 소스를 고친 변경은 그 게임 프리셋으로 빌드해야 확인됩니다.

## 더 볼 곳

- 시나리오 형식: [Automation](../Engine/Automation/README.md)
