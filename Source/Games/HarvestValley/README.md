# HarvestValley — 농장 생활 테스트 게임

## 이 게임으로 무엇을 배우나

하베스트 문 장르의 게임입니다. `GF_Farming` 키트의 작물 카탈로그, 밭, 출하함을 실제 게임 흐름에서 씁니다.

- 키트 하나를 링크하고, 달력과 돈과 가방은 기반의 공유 상태(`GameStateComponent`)에 두는 방법
- 디렉터가 입력 맵을 읽지 않고 **폰의 의도**로 규칙을 처리하는 방법
- 자동 플레이를 몸 안의 분기가 아니라 **AI 조종자의 빙의**로 만드는 방법
- 핫 리로드 앞뒤로 같은 날과 시각이 이어지는지 확인하는 방법

시점은 비스듬히 내려다보는 직교 카메라입니다. 작물, 천막, 울타리, 나무는 Kenney Nature Kit 모델(`Resource/game/harvestvalley/credits.md`)이고, 밭과 출하함과 농부는 내장 도형입니다.
밭의 색이 상태(풀, 갈았음, 물 줌)를 나타냅니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-HarvestValley
cmake --build --preset Ninja-Debug-HarvestValley
cd build/Ninja-Debug-HarvestValley/Bin
./App.exe -dx12
./App.exe -dx12 -gv_farmAutoPlay=1      # 자동 농부 AI 가 농부를 잡는다: 갈기, 심기, 물, 거두기, 출하, 잠
./App.exe -dx12 -gv_farmAutoPlay=1 -gv_reloadGameAtFrame=1500   # 1500 프레임에 게임 모듈 핫 리로드
./App.exe -dx12 -scenario=game/harvestvalley/automation/control.scenario.xml
```

`control.scenario.xml` 은 가상 키로 농부를 걷게 하고(D 를 반 초, 2m 이동) 도구를 바꾼 뒤(2 키, 물뿌리개),
자동 플레이를 켜서 자동 농부가 폰을 잡고 밭을 일구는지, 끄면 플레이어 조종자가 되찾는지 봅니다.
탐침은 `Farm.FarmerX`, `Farm.FarmerTool`, `Farm.TilledCount`, `Farm.CropCount`, `Farm.FarmerControllerKind`(0 플레이어, 1 AI)입니다. 종료 코드 0 이 통과입니다.

핫 리로드 실행에서는 리로드 앞뒤의 `[Farm]` 로그 줄이 같은 날과 시각과 체력으로 이어져야 합니다.

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD, 방향키 | 걷기(바라보는 타일이 노랗게 표시됩니다) |
| 1, 2, 3, 4 | 괭이, 물뿌리개, 씨앗, 손 |
| Space, J | 바라보는 타일에 도구 쓰기(체력은 괭이 4, 물 2, 씨앗과 손 1) |
| Q, E | 씨앗 고르기(이번 계절이 아니면 알려 줍니다) |
| B | 가게(열린 천막) 옆에서 고른 씨앗 하나 사기 |
| F | 출하함(갈색 상자) 옆에서 수확물을 모두 넣기. 그날 밤 정산합니다 |
| Z | 자기. 정산, 다음 날, 날씨(비 25%), 밭, 체력 회복 순서로 진행합니다 |
| Tab | 지금 상태를 로그로 |

키 배치는 `data/farm.input.xml` 에 있습니다. 시간은 실제 1초에 게임 10분이고(6:00–26:00, 하루 2분), 26:00 이 되면 쓰러져 체력이 반만 찬 채 아침을 맞습니다.
물 받은 날만 자라고, 다시 열리고, 계절이 바뀌면 시들고, 비가 갈아 둔 타일에 물을 주는 규칙은 키트의 `FarmField` 가 정하고 `FarmingTest` 가 고정합니다.

## 구조

농장은 씬 하나(`Resource/game/harvestvalley/maps/farm.scene.xml`)입니다. Play 를 누르면 디렉터가 밭을 만들고, Stop 은 플레이 전 씬으로 돌아갑니다.

| 무엇 | 어디 |
|------|------|
| 땅, 해, 카메라, 집, 출하함, 가게, 울타리와 나무 같은 장식, 농부, 바라보는 타일 표시, 디렉터 | 씬 |
| 농부 폰 | 씬의 농부 오브젝트의 `PawnComponent`(이동 `Farm.Move`, 버튼 `Farm.Use` 등, 자동 빙의 `Player0`) |
| 농부를 잡는 조종자 | 플레이어는 GameFramework `PlayerControllerComponent`, 자동 플레이는 `FarmAutoFarmerAiComponent`(`prefabs/autofarmer.prefab.xml`) |
| 밭의 흙과 작물(12 × 8) | 프리팹 `farmsoil.prefab.xml`, `farmcrop.prefab.xml`. 디렉터가 플레이 시작에 만듭니다 |
| 규칙과 상태 | 키트의 `FarmField`, `FarmShippingBin`, `CropCatalog` |
| 달력, 돈, 가방 | 공유 상태 `GameStateComponent`. 디렉터 오브젝트의 맨 앞 컴포넌트이고, 하루 144초, 계절 네 개, 가방 24슬롯입니다 |
| 농부의 위치, 도구, 체력, 시간, 가게, 출하, 잠, 날씨, 빙의 바꾸기 | `FarmDirectorComponent` |
| 모습 | 뷰 `FarmSoilComponent`, `FarmCropComponent`, `FarmerComponent`, `FarmSunComponent` |
| 카메라 | GameFramework `OrthoCameraRigComponent`(입력 끔). 디렉터가 초점을 밭 가운데와 농부 사이에 넣습니다 |

농부의 위치와 체력과 도구는 게임 상태(세이브 대상)라서 디렉터가 가지고, 무엇을 할지는 농부 폰의 의도로 받습니다.

**조종.** 디렉터는 입력 맵을 읽지 않습니다. 조종 시스템이 틱 전에 농부를 잡은 조종자의 의도를 폰에 넣고, 디렉터는 틱에서 그 의도(이동 축, 발동한 버튼)를 규칙대로 처리합니다.
그래서 플레이어와 자동 농부가 같은 경로를 탑니다. 자동 플레이 스위치(`-gv_farmAutoPlay`, 에디터 툴바)를 켜면 디렉터가 틱 뒤 플러시에서 자동 농부 AI 를 농부에 빙의시키고, 끄면 플레이어 조종자가 되찾습니다.
자동 농부는 디렉터의 게임 상태를 읽기만 하고(판단은 틱 밖), 타일에 도구를 쓸 때는 타일 아래로 갔다가 위로 걸어 들어가 위를 보고 섭니다. 디렉터가 걷는 쪽으로 바라보는 쪽을 정하기 때문입니다.

**틱.** 디렉터는 `PrePhysics` 에서 의도와 시간과 행동을 돌리고 카메라 리그에 초점을 넣습니다. 뷰와 리그는 `PostUpdate` 에서 결과를 읽습니다.
밭은 틱 안에서 만들 수 없으므로 디렉터가 틱 뒤에 만들고, 효과음도 그때 냅니다. 작물 모델은 디렉터가 미리 로드해 두어, 단계가 바뀔 때 뷰가 워커에서 파일을 읽지 않습니다.

**핫 리로드와 상태 저장.** 달력, 밭, 인벤토리, 농부의 위치와 체력과 도구, 날씨 난수가 디렉터의 `writeState`(상태 버전 3)로 실립니다.
다시 만든 디렉터가 데이터를 읽은 뒤 농장을 복원하고 밭을 다시 만듭니다. 형식이 맞지 않으면(디렉터의 `kStateVersion` 을 올렸다면) 알리고 새 농장으로 시작합니다.

## 데이터

- `Resource/game/harvestvalley/data/crops.xml` 에 작물 값, 자라는 날, 계절이 모두 있습니다.
- 디렉터의 PROPERTY 로 작물 데이터, 프리팹 경로, 흙 색, 카메라 리그, 출하함, 가게, 농부 폰, 농부 시작 위치를 정합니다.
- 자동 농부의 할 일 고르기, 행동 간격, 돌볼 타일 수, 한 번에 살 씨앗 수는 프리팹의 PROPERTY 입니다.

## 더 볼 곳

- [MeadowVillage](../MeadowVillage/README.md) — 같은 농장 키트를 다른 키트와 섞은 게임
- [GameFramework](../../GameFramework/README.md) — 폰, 조종자, 자동 플레이 빙의
- [Games](../README.md) — 씬, 프리팹, 디렉터 구조
