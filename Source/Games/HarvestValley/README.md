# HarvestValley — 농장 생활 시험 게임

`GF_Farming` 키트(달력 · 작물 카탈로그 · 밭 · 인벤토리 · 출하)를 실제 게임 흐름에서 쓰는 하베스트 문 장르입니다. 시점은 비스듬히 내려다보는 직교
카메라(2D 농장 게임의 화면)이고, 작물 · 천막 · 울타리 · 나무는 Kenney Nature Kit 모델(`Resource/game/harvestvalley/credits.md`), 칸 · 출하함 · 농부는
내장 도형입니다. 칸 색이 상태(풀 · 갈았음 · 물 줌)를 말합니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug-HarvestValley        # 빌드 폴더 build/Ninja-Debug-HarvestValley
cmake --build --preset Ninja-Debug-HarvestValley
cd build/Ninja-Debug-HarvestValley/Bin
./App.exe -dx12
./App.exe -dx12 -gv_farmAutoPlay=1      # 농부도 AI — 갈기 → 심기 → 물 → 거두기 → 출하 → 잠(하루 약 2 분)
```

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD · 방향키 | 걷기(바라보는 칸이 노랗게 표시된다) |
| 1 · 2 · 3 · 4 | 괭이 · 물뿌리개 · 씨앗 · 손 |
| Space · J | 바라보는 칸에 도구 쓰기(체력: 괭이 4, 물 2, 씨앗 · 손 1) |
| Q · E | 씨앗 고르기(이번 계절이 아니면 그렇다고 알려 준다) |
| B | 가게(열린 천막) 옆에서 고른 씨앗 하나 사기 |
| F | 출하함(갈색 상자) 옆에서 수확물을 모두 넣기 — 그날 밤 정산 |
| Z | 자기 — 정산 → 다음 날 → 날씨(비 25%) → 밭 → 체력 회복 |
| Tab | 지금 상태를 로그로 |

시간은 실제 1 초에 10 분(6:00 – 26:00, 하루 2 분)입니다. 26:00 이 되면 쓰러져 체력이 반만 찬 채 아침을 맞습니다. 규칙(물 받은 날만 자람,
다시 열림, 계절이 바뀌면 시듦, 비는 갈아 둔 칸에 물)은 키트의 `FarmField` 가 정하고 `FarmingTest` 가 고정합니다.

## 구조 — 씬 · 프리팹 · 컴포넌트

농장은 씬 하나(`Resource/game/harvestvalley/maps/farm.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 에디터에서 열면 고정 배치가 계층에 보이고,
Play 를 누르면 디렉터가 밭을 세우며, Stop 은 플레이 전 씬으로 돌아갑니다. `ThemeParkTycoon` 과 같은 모양입니다(`Source/Games/README.md` 레시피).

| 무엇 | 어디 |
|------|------|
| 땅 · 해 · 카메라 · 집 · 출하함 · 가게 · 울타리 · 나무 · 장작 · 덤불 · 꽃 · 농부 · 바라보는 칸 표시 · 디렉터 | 씬(엔티티) — 에디터에서 옮긴다 |
| 밭 칸의 흙 · 작물(12 × 8) | 프리팹 `prefabs/farmsoil.prefab.xml` · `farmcrop.prefab.xml` — 디렉터가 플레이 시작에 세운다 |
| 규칙 · 상태(달력 · 밭 · 인벤토리 · 작물 카탈로그) | 키트의 보통 클래스(`FarmCalendar` · `FarmField` · `FarmInventory` · `CropCatalog`) — 씬 없이 시험한다(`FarmingTest`) |
| 농부 · 도구 · 체력 · 시간 · 가게 · 출하 · 잠 · 날씨 · 자동 농부 | `FarmDirectorComponent`(씬에 하나 — 언리얼 GameMode/GameState 자리) |
| 모습 | 뷰 `FarmSoilComponent` · `FarmCropComponent`(칸 하나) · `FarmerComponent`(농부 · 바라보는 칸) · `FarmSunComponent`(시각 · 비 → 해) — 디렉터를 **읽기만** 해 자기 오브젝트를 맞춘다 |
| 카메라 | GameFramework 공용 `OrthoCameraRigComponent`(입력 끔) — 디렉터가 초점을 밭 가운데와 농부 사이에 넣는다 |
| 머티리얼 | 색 머티리얼 `materials/meadow` · `shippingbin` · `farmer` · `target`(반투명). 흙 세 상태 · 시든 작물 · 작물 색은 디렉터가 만들어 나눠 쓰는 머티리얼 인스턴스 |

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 입력(또는 자동 농부) · 시간 · 행동을 돌리고 카메라 리그에 초점을 넣습니다. 뷰와 리그는 `PostUpdate` 에서 그 결과를
읽습니다(그룹은 차례로 돈다). 같은 그룹의 오브젝트는 병렬이므로 뷰는 자기 오브젝트에만 씁니다. 칸은 틱 안에서 세울 수 없으므로 디렉터가 `executeOrDeferPostTick`
한 번으로 세우고, 효과음도 그때 냅니다. 작물 모델은 디렉터가 미리 쥐고 있어 단계가 바뀔 때 뷰가 워커에서 파일을 읽지 않습니다.

**핫 리로드 · 상태 저장.** 농장 상태(달력 · 밭 · 인벤토리 · 농부 자리 · 체력 · 도구 · 날씨 난수)는 PROPERTY 가 아니라 디렉터의 `writeState` 로 상태 스냅샷의
컴포넌트 섹션에 실려 넘어갑니다(`ComponentStateStore`). 상태를 쓰기 전에 게임 인스턴스(생성자의 `registerDirector` 한 줄 — `GameInstanceBase`)가 농장을 싣고 디렉터가 세운 칸을 걷으며, 다시 만든
디렉터가 데이터를 읽은 뒤 농장을 되살리고 칸을 다시 세웁니다. 형식이 맞지 않으면(디렉터의 `kStateVersion` 을 올렸다) 알리고 새 농장으로 시작합니다.
`App -gv_farmAutoPlay=1 -gv_reloadGameAtFrame=1500` 이면 리로드 앞뒤의 `[Farm]` 줄이 같은 날 · 시각 · 체력으로 이어진다.

## 파일

- `HarvestValleyGame` — 첫 씬을 열고, 상태 저장 전에 디렉터가 세운 칸을 걷습니다.
- `FarmDirectorComponent` — 작물 카탈로그 읽기 · 농부 · 도구 · 체력 · 시간 · 가게 · 출하 · 잠 · 날씨 · 자동 농부(`_bAutoPlay`, `-gv_farmAutoPlay=1` 도 켠다) · 로그 · 칸 스폰.
  작물 데이터 · 프리팹 경로 · 흙 색 · 카메라 리그 · 출하함 · 가게(있으면 그 자리가 농부가 서는 곳) · 농부 시작 자리는 PROPERTY 입니다.
- `FarmSoilComponent` · `FarmCropComponent` · `FarmerComponent` · `FarmSunComponent` — 뷰.
- `Resource/game/harvestvalley/maps/farm.scene.xml` · `prefabs/` · `materials/` — 엔진 직렬화기가 쓴 파일입니다(손으로 고치면 씬 · 프리팹 형식을 깨기 쉽다 — 에디터로).

작물 값 · 자라는 날 · 계절은 전부 `crops.xml` 에 있습니다.
