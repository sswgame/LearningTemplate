# Games — 게임 모듈과 새 게임 만들기

## 이것은 무엇이고 왜 있나

엔진 위에서 실제로 도는 게임이 이 폴더에 있습니다. 게임 하나는 폴더 하나(`Source/Games/<게임>/`)이고, 리소스는 게임 팩(`Resource/game/<게임 소문자>/`)에 있습니다.
여기 있는 게임은 모두 테스트 게임입니다. 장르 키트와 엔진 기능이 실제 게임 흐름에서 맞물리는지 확인하고, 새 게임을 만들 때 본보기로 씁니다.

한 번에 빌드되는 게임은 하나입니다. CMake 옵션 `SW_ACTIVE_GAME` 이 고르고, 게임마다 Debug 프리셋 `Ninja-Debug-<게임>` 이 있습니다.
어느 게임을 고르든 빌드 타깃 이름은 늘 `SWGame` 입니다. 핫 리로드가 다시 로드할 모듈을 고정된 이름으로 찾기 때문입니다.

## 머릿속 그림

```text
Source/Games/<게임>/SWGame.module.json   → 프로젝트: 링크할 키트, 끌 모듈
Source/Games/<게임>/CMakeLists.txt        → sw_addGameModule(SWGame) 한 줄
Source/Games/<게임>/*Game.cpp             → 게임 인스턴스(GameInstanceBase 파생)
Config/Game/<게임>.json                   → 게임 프리셋: 팩 루트, 창 제목
Resource/game/<게임 소문자>/               → 팩: data/gamesettings.xml, maps/, prefabs/, automation/
```

**게임 매니페스트가 프로젝트입니다.** `SWGame.module.json` 은 언리얼의 `.uproject`, 유니티의 `package.json` 에 해당합니다. 링크할 키트와, 켜고 끌 모듈을 적습니다.

**게임 인스턴스.** `GameInstanceBase` 를 상속한 클래스 하나가 첫 씬을 요청하고, 게임 서비스(카탈로그 등)를 등록하고, 디렉터를 상태 스냅샷에 올립니다.

**씬과 프리팹과 디렉터.** 고정 배치는 씬, 런타임에 생기는 것은 프리팹, 규칙은 키트의 보통 클래스, 그것을 돌리는 것은 디렉터 컴포넌트입니다. 아래 "새 게임 = 씬 + 프리팹 + 디렉터와 뷰 컴포넌트" 절에서 설명합니다.

## 들어 있는 게임

| 폴더 | 장르 | 이 게임으로 배우는 것 |
|------|------|----------------------|
| `Empty` | 최소 템플릿 | 새 게임의 출발점, 렌더 벤치 하네스(`-gv_benchMeshes=N`) |
| [`AbilityArena`](AbilityArena/README.md) | 탑다운 웨이브 아레나 | 어빌리티 시스템(GAS), 폰과 AI 조종자 |
| [`HarvestValley`](HarvestValley/README.md) | 농장 생활 | 키트 하나(`GF_Farming`), 공유 상태, 자동 플레이 빙의 |
| [`MeadowVillage`](MeadowVillage/README.md) | 농장 + 생물 마을 | 키트 둘을 한 게임에 섞기 |
| [`NileCity`](NileCity/README.md) | 도시 건설 | 명령형 디렉터, 절차 지형, 커서 아래 땅 고르기 |
| [`Shooter3D`](Shooter3D/README.md) | 1인칭 슈터 | 캐릭터 외형과 애니메이션, 페이싱 감독, HUD, 카메라 프리셋 |
| [`StarSkirmish`](StarSkirmish/README.md) | 실시간 전략 | RTS 키트, 끌어 고르기, AI 대 AI |
| [`ThemeParkTycoon`](ThemeParkTycoon/README.md) | 롤러코스터 타이쿤 | 씬, 프리팹, 디렉터, 뷰 구조의 본보기 |
| [`VoxelCraft`](VoxelCraft/README.md) | 복셀 샌드박스 | 절차 메시, 1인칭 몸, 청크 다시 만들기 |

CI 는 기본 게임 `Empty` 만 빌드합니다. 다른 게임을 고쳤다면 그 게임의 프리셋으로 한 번 빌드해 확인합니다.

## 따라 해 보기 — 게임 하나를 빌드하고 시나리오 돌리기

```powershell
cmake --preset Ninja-Debug-HarvestValley          # 빌드 폴더 build/Ninja-Debug-HarvestValley
cmake --build --preset Ninja-Debug-HarvestValley
cd build/Ninja-Debug-HarvestValley/Bin
./App.exe -dx12                                     # 에디터 없이, 시작 씬이 바로 플레이 중
./App.exe -dx12 -gv_farmAutoPlay=1                  # 자동 플레이
./App.exe -dx12 -scenario=game/harvestvalley/automation/control.scenario.xml
echo $LASTEXITCODE                                  # 0 통과, 10 실패, 11 읽기 오류, 12 시간 초과, 13 건너뜀
```

프리셋마다 빌드 폴더가 따로이므로, 게임을 바꿀 때 한 빌드 폴더를 다시 configure 하지 않습니다. 두 작업이 한 폴더를 나눠 쓰면 서로의 빌드를 깨뜨립니다.

**자동화 시나리오**는 팩의 `automation/*.scenario.xml` 입니다. 고정 프레임 시간과 가상 입력으로 돌고, 결과를 종료 코드로 냅니다. 형식은 [Automation](../Engine/Automation/README.md)에 있습니다.
그 게임의 시나리오를 모든 백엔드로 한꺼번에 돌리려면 그 프리셋의 호스트 테스트를 씁니다. GPU 와 창이 필요해서 CI 는 돌리지 못합니다.

```powershell
ctest --test-dir build/Ninja-Debug-HarvestValley -R AppTest_HostOnly --output-on-failure
```

`AppScenarioTest` 가 엔진 시나리오(`engine/automation`)와 활성 게임 팩의 시나리오를 백엔드마다 띄웁니다. 시나리오 파일을 놓기만 하면 돌고, CMake 에 목록을 적지 않습니다.
로그는 `Bin/Saved/Automation/<시나리오>_<백엔드>.log` 에 남습니다.

## 새로운 게임 추가하는 방법

1. **템플릿 복사.** `Source/Games/Empty/` 를 `Source/Games/<게임>/` 으로 복사합니다.
2. **벤치 하네스 지우기.** `BenchScene.*`, `BenchSceneRig.cpp`, `BenchMoverComponent.*`, `BenchCombatComponent.*` 를 지우고, `EmptyGame` 의 `_benchScene` 멤버와 그것을 쓰는 곳을 지웁니다.
   이 파일들은 측정용이고 게임 코드가 아닙니다(아래 "Empty 에 벤치가 있는 이유").
3. **키트 연결.** `<게임>/SWGame.module.json` 의 `_listDependency` 에 쓸 키트를 적습니다. `_kind` 는 `Game` 입니다.
4. **리소스 폴더와 게임 프리셋.** `Resource/game/<게임 소문자>/` 를 만들고, `Config/Game/<게임>.json` 에 `_packRoot`(`"game/<게임 소문자>"`)와 창 제목 `_windowTitle` 을 적습니다.
   파일 이름은 게임 폴더 이름과 같아야 하고, 프리셋이 없으면 configure 가 멈춥니다. 시작 씬은 팩의 `data/gamesettings.xml` 의 `startMap`(타이틀 화면이 있으면 `titleScene`) 하나입니다.
5. **CMake 프리셋.** `CMakePresets.json` 에 `Ninja-Debug-<게임>`(configure 와 build, `SW_ACTIVE_GAME=<게임>`)을 더합니다. `CheckGamePresets.py` 가 검사합니다.
6. **쓰지 않는 키트 끄기(선택).** `_listModuleOverride` 에 `{ "_name": "GF_…", "_bEnabled": false }` 를 적으면 그 키트는 이 게임의 빌드와 실행에서 빠집니다.
7. **씬과 디렉터.** 아래 절의 구조로 첫 씬과 디렉터를 만듭니다. 자동화 시나리오 하나를 `automation/` 에 두면 호스트 테스트가 함께 돌립니다.

### 게임 매니페스트

```json
{
    "_name": "SWGame", "_version": "1.0.0", "_kind": "Game",
    "_listDependency": [ { "_name": "GameFramework" }, { "_name": "GF_Voxel" } ],
    "_listPlatform": [ "Windows", "Linux" ], "_listConfiguration": [ "Dev", "Shipping" ], "_listTarget": [ "Client", "Server" ],
    "_listModuleOverride": [ { "_name": "GF_Fighting", "_bEnabled": false } ]
}
```

- `_listTarget`(필수)은 모듈이 들어가는 빌드 타깃입니다. 게임과 공유 키트는 `["Client", "Server"]`, 서버 키트 `GF_Server_<X>` 는 `["Server"]`, 에디터와 RHI 는 `["Client"]` 입니다.
  나뉘는 규칙은 [Kits](../GameFramework/Kits/README.md)의 "클라이언트와 서버로 나뉘는 기능"에 있습니다.
- 게임이 링크하는 키트는 `_listDependency` 가 정합니다. `sw_addGameModule` 이 이 목록을 읽으므로 CMake 에 다시 적지 않습니다.
- `_listModuleOverride` 로 끈 모듈은 빌드하지 않고, 테스트 실행 파일에서도 그 키트를 include 하는 테스트가 빠지며, App 도 로드하지 않습니다.
  Shipping 은 켜진 키트만 정적 링크합니다. 켜진 모듈이 꺼진 모듈에 의존하면 configure 가 무엇이 왜 꺼졌는지 알려 줍니다.
- 키트의 리로드 의존(그 키트가 다시 로드되면 같이 다시 로드될 것)도 키트 매니페스트의 `_listDependency` 입니다.
- 빌드 쪽(`cmake/Engine/ModuleManifest.cmake`)과 런타임 쪽(`Engine/Module/ModuleCatalog`)이 같은 규칙으로 해석하고, `ModuleCatalogTest.BuildAndRuntimeAgree` 가 둘의 답을 비교합니다.

## 새 게임 = 씬 + 프리팹 + 디렉터와 뷰 컴포넌트

게임 클래스가 코드로 오브젝트를 만들고 매 프레임 값을 밀어 넣는 대신, 상용 엔진처럼 역할을 나눕니다. 테스트 게임 일곱 개가 모두 이 구조이고, 본보기는 `ThemeParkTycoon` 입니다.

| 무엇 | 어디 |
|------|------|
| 고정 배치(땅, 해, 카메라, 건물, 장식 설정, 디렉터) | 씬 `Resource/game/<팩>/maps/*.scene.xml`. 팩의 `data/gamesettings.xml` 의 `startMap` |
| 런타임에 데이터로 생기는 것(유닛, 손님, 탄, 건설 중인 건물) | 프리팹 `prefabs/*.prefab.xml` |
| 규칙과 상태 | 키트의 보통 클래스. 씬 없이 테스트합니다 |
| 규칙을 돌리고 스폰을 지시 | 디렉터 컴포넌트 하나(`GameDirectorComponent` 파생) |
| 엔티티의 모습 | 뷰 컴포넌트. 디렉터를 읽기만 하고 자기 오브젝트에만 씁니다(`TickGroup::PostUpdate`) |
| 엔티티 하나의 입력이나 AI | 폰과 조종자. 몸 컴포넌트는 폰의 의도만 읽습니다 |
| 장르 무관 카메라와 장식 | GameFramework `Camera/`(`OrthoCameraRigComponent`), `World/`(`PropScatterComponent`) |
| 게임 클래스 | `requestFirstScene()` 과, 생성자의 `registerDirector<디렉터>()` 한 줄 |

### 디렉터가 구현하는 것

디렉터는 `GameFramework/Base/Framework/GameDirectorComponent` 를 상속합니다. 언리얼의 `AGameModeBase` 와 `AGameStateBase` 를 합친 것에 해당하고, Lyra 처럼 게임 상태를 한 컴포넌트에 둡니다.
틱 그룹, 상태 데이터 보류, 틱 뒤 플러시, 대기 소리, 스폰한 것 정리, 자동 플레이, 디렉터 찾기는 베이스가 가지고, 게임마다 다른 것만 구현합니다.

| 함수 | 언제 불리나 |
|------|------------|
| `startGame()` | `onBeginPlay`. 데이터를 읽고 새 게임을 엽니다. 못 열면 알리고 false 를 돌려주며, 틱도 돌지 않습니다 |
| `writeState`, `readState` | 상태 데이터. 첫 값은 `StateArchiveUtil::writeHeader` 의 태그와 버전입니다 |
| `onStateRestored( bRestored )` | 복원 데이터를 적용한 뒤. 실패면 새 게임으로 되돌립니다 |
| `onGameStarted()` | 게임이 열리고 첫 플러시 뒤. 조작 안내와 카메라 |
| `tickGame( dt )` | 게임이 열린 뒤 매 틱(PrePhysics) |
| `onFlush( manager, bRespawnViews )` | 틱 뒤 게임 스레드. 쌓인 스폰을 만듭니다 |
| `onViewsDespawned()`, `hasPendingSpawn()` | 정리 뒤 핸들 목록 비우기, 틱 끝에 플러시를 잡을지 |

`onFlush` 의 `bRespawnViews` 가 참이면 처음이거나 정리한 뒤라서, 지금 상태의 모습을 모두 만들어야 합니다. 시작 전에 받은 복원 데이터는 베이스가 가지고 있다가 시작한 뒤 적용합니다.
상태 형식을 바꾸면 버전을 올립니다.

스폰은 `spawnPrefab`(정리 목록에 들어갑니다)과 `destroySpawned`, 소리는 `getSoundQueue()` 의 `queueClip`, `queueEvent`, `queueEventAt`(틱 뒤에 냅니다)입니다.
색만 다른 모습은 `MaterialTintCache`, 뷰와 컨트롤러가 디렉터를 찾는 것은 `GameDirectorComponent::resolve<디렉터>( manager, handle )` 입니다.
자동 플레이 PROPERTY 는 베이스의 `_bAutoPlay` 하나이고, 게임은 `SW_GAME_AUTOPLAY` 로 전역 변수(`-gv_<게임>AutoPlay`)를 등록합니다. `isAutoPlayOn()` 이 둘을 모두 봅니다.

## Empty 에 벤치가 있는 이유

`Empty` 는 템플릿이면서 렌더 경로 측정용 벤치 하네스를 가지고 있습니다. `-gv_benchMeshes=N` 을 주면 큐브 N 개를 격자로 놓고 매 프레임 흔듭니다.
렌더 비용을 재려면 그릴 것이 씬에 있어야 하고, 씬을 만드는 것은 엔진이 아니라 게임의 일이라서 여기 있습니다.
`Scripts/dev/RunBackendSmoke.py` 와 [Graphics](../Engine/Graphics/README.md)의 측정 조건이 이 플래그에 기대므로 타깃과 플래그 이름은 바꾸지 않습니다.

벤치는 `BenchScene` 과 `BenchSceneRig.cpp` 에 있습니다. `-gv_benchTickMovers=N` 은 틱 안에서 위치를 쓰는 `BenchMoverComponent` 를 붙이고,
`-gv_benchCombat=1` 은 KayKit 스켈레톤이 맞고 쓰러지는 전투 연출(`BenchCombatComponent`, 로그 `[BenchCombat]`)을 돌립니다.
새 게임을 시작할 때 지울 경계가 파일 경계와 같도록 나눠 두었습니다. 벤치가 아니면 `EmptyGame` 은 첫 씬을 요청합니다. 에디터를 켜면 에디터의 시작 씬 요청이 나중에 와서 그쪽이 열립니다.

## 함정과 주의

- **디렉터의 시뮬레이션은 `writeState` 와 `readState` 로 넘깁니다.** 키트의 보통 클래스는 PROPERTY 가 아니라서 핫 리로드와 세이브에서 사라집니다. 게임 모듈의 정적 변수와 컴포넌트 멤버는 모듈과 함께 언로드됩니다.
  게임 인스턴스가 생성자에서 `registerDirector<디렉터>()` 로 올립니다. 디렉터가 아닌 상태 컴포넌트는 `registerStatefulComponent<T>()`,
  상태 없이 스폰한 것만 정리하는 컴포넌트는 `registerViewOwner<T>()`(`despawnViews()` 를 둡니다)입니다.
- **틱 안에서는 구조를 바꾸지 않습니다.** 디렉터는 스폰 요청을 쌓고(`hasPendingSpawn`), 베이스가 `executeOrDeferPostTick` 한 번으로 틱 뒤에 `onFlush` 를 부릅니다. 틱 안의 `addComponent` 는 nullptr 를 돌려줍니다.
- **같은 틱 그룹은 병렬입니다.** 뷰는 자기 오브젝트에만 쓰고, 다른 오브젝트의 컨테이너는 첨자(`operator[]`) 대신 `data()` 나 const 참조로 읽습니다. Debug 경합 검출기가 첨자 접근을 쓰기로 세기 때문입니다.
  다른 오브젝트에 값을 넣어야 하면(디렉터가 카메라 리그에) 읽는 쪽보다 앞 그룹에서 넣습니다.
- **런타임에만 쓰는 머티리얼 에셋을 두지 않습니다.** 프리팹만 가리키는 머티리얼은 처음 스폰할 때 게임 스레드에서 로드되고, 마지막 것이 사라질 때 언로드되어 렌더 스레드의 병렬 기록과 겹칩니다.
  색만 다르면 씬이 늘 가지고 있는 머티리얼(팔레트, 엔진 기본)에서 디렉터가 머티리얼 인스턴스를 만들어 나눠 씁니다(`MaterialTintCache`).
- **프레임을 넘겨 가지고 있는 것은 핸들입니다.** 디렉터와 카메라와 스폰한 오브젝트를 `GameObjectHandle` 로 가지고 매 프레임 대상을 찾습니다.
  씬의 다른 엔티티를 가리키는 PROPERTY 가 `GameObjectHandle` 이면 파일 id 로 저장되어 로드와 쿠킹 뒤에도 이어집니다.
- **씬과 프리팹 파일은 엔진 직렬화기로 씁니다.** 에디터를 쓰거나, 오브젝트를 만든 뒤 `SceneManager::saveActiveScene` 이나 `PrefabAsset::saveToXmlFile` 로 저장합니다. 손으로 쓴 XML 은 형식을 깨기 쉽습니다.
- **게임 컴포넌트에 처음 `REFLECT` 를 넣으면 다시 configure 해야 등록됩니다.** EngineTest 는 게임 모듈을 링크하지 않습니다.
  그래서 게임 팩의 씬과 프리팹 검사(`ResourceDataSchemaTest`)는 `Source/Games` 헤더에 선언된 타입을 모르는 타입으로 넘깁니다.
  쿠킹은 활성 팩만 대상이라 다른 게임 팩의 그런 씬은 건너뜁니다.
- **고르지 않은 게임은 빌드되지 않습니다.** 게임 소스를 고친 변경은 그 게임 프리셋으로 빌드해야 확인됩니다.

## 더 볼 곳

- [GameFramework](../GameFramework/README.md) — 기반, 폰과 조종자, 디렉터 베이스
- [Kits](../GameFramework/Kits/README.md) — 키트 목록과 키트 여럿을 섞는 규칙
- [Automation](../Engine/Automation/README.md) — 시나리오 형식과 종료 코드
- [시작하기](../../docs/01_GettingStarted.md) — 첫 빌드와 첫 게임 오브젝트
