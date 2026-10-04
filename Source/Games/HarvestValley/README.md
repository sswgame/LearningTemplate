# HarvestValley — 농장 생활 시험 게임

`GF_Farming` 키트(달력 · 작물 카탈로그 · 밭 · 인벤토리 · 출하)를 실제 게임 흐름에서 쓰는 하베스트 문 장르입니다. 시점은 비스듬히 내려다보는 직교
카메라(2D 농장 게임의 화면)이고 칸 · 작물 · 농부는 내장 도형입니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug -DSW_ACTIVE_GAME=HarvestValley   # 기존 빌드 디렉터리는 옛 값을 들고 있으니 다시 구성한다
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
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
| B | 가게(파란 상자) 옆에서 고른 씨앗 하나 사기 |
| F | 출하함(갈색 상자) 옆에서 수확물을 모두 넣기 — 그날 밤 정산 |
| Z | 자기 — 정산 → 다음 날 → 날씨(비 25%) → 밭 → 체력 회복 |
| Tab | 지금 상태를 로그로 |

시간은 실제 1 초에 10 분(6:00 – 26:00, 하루 2 분)입니다. 26:00 이 되면 쓰러져 체력이 반만 찬 채 아침을 맞습니다. 규칙(물 받은 날만 자람,
다시 열림, 계절이 바뀌면 시듦, 비는 갈아 둔 칸에 물)은 키트의 `FarmField` 가 정하고 `FarmingTest` 가 고정합니다.

## 파일

- `HarvestValleyGame` — 작물 카탈로그(`Resource/game/harvestvalley/data/crops.xml`)를 읽고 게임 서비스로 겁니다.
- `FarmWorld` — 농부 · 도구 · 체력 · 시간 · 가게 · 출하 · 잠 · 날씨와 그 모습(`PrimitiveStage`).

작물 값 · 자라는 날 · 계절은 전부 `crops.xml` 에 있습니다.
