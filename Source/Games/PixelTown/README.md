# PixelTown — 탑다운 RPG 2D 테스트 게임

## 이 게임으로 무엇을 배우나

젤다 · 포켓몬 계열의 탑다운 마을입니다. 주인공이 타일을 한 칸씩 걷고, 물 · 집 · 나무 · 주민에 막히며, 마주 본 주민에게 말을 겁니다.
`GF_Overworld` 키트(한 칸 걸음 `OverworldTileMover`, 걷는 맵 `TileMap`)를 처음으로 실제 게임 흐름에서 씁니다.

- 그리는 맵과 걷는 맵을 **한 데이터**로 두는 방법 — 디렉터가 씬의 `TileMapRendererComponent` 가 읽은 맵 · 타일셋을 그대로 받고, 막힘은 타일셋 브러시의 `solid` 하나가 정한다
- 타일 레이어 둘(땅 · 소품)을 겹치는 방법 — 소품 칸은 바탕이 투명이라 땅이 비치고, 소품 레이어의 `solid` 브러시도 막는다
- 4 방향 스프라이트 애니메이션 — 클립(`.sprite.json`)의 구간 이름 `<idle|walk>_<down|left|right|up>` 을 뷰가 고른다
- 대화를 워커 틱이 아니라 틱 뒤 게임 스레드(디렉터 플러시)에서 돌리는 방법

그림은 엔진 팩의 CC0 픽셀 아트 16 px 세트입니다(`Resource/engine/credits.md`): 땅은 Shade "Puny World", 집 · 나무는 ArMM1998 "Zelda-like",
주인공은 Zelda-like `character`, 촌장은 Shade "Puny Characters" 의 마법사입니다. 2D 픽셀 화면이라 카메라의 후처리(톤 매핑 · 블룸)를 끄고 원본 색을 그대로 냅니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -game=PixelTown -dx12
./App.exe -game=PixelTown -dx12 -scenario=game/pixeltown/automation/talk.scenario.xml
```

`talk.scenario.xml` 은 가상 키로 주인공을 걷게 해 집 벽에 막히는지, 촌장이 선 칸에 막히고 위를 보는지, Space 로 말을 걸어 두 줄을 넘기면
대화가 끝나는지, 대화 중에는 걷지 않고 끝나면 다시 걷는지를 봅니다.
탐침은 `Town.HeroTileX` · `Town.HeroTileY` · `Town.HeroFacing`(0 아래, 1 왼, 2 오른, 3 위) · `Town.Talking` · `Town.ShownLines` · `Town.FinishedTalks` 입니다.

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD, 방향키 | 한 칸 걷기(막힌 칸이면 그쪽을 보기만 한다) |
| Space, E | 바라보는 칸의 주민에게 말 걸기, 대화 중이면 다음 줄 |

## 데이터

| 파일 | 내용 |
|------|------|
| `Resource/game/pixeltown/maps/town.scene.xml` | 카메라, 땅 · 소품 레이어, 주인공, 촌장, 디렉터 |
| `Resource/game/pixeltown/maps/town.tilemap.xml` | 땅(풀 · 길 · 물) 20 x 12 칸과 스폰 칸 |
| `Resource/game/pixeltown/maps/townprops.tilemap.xml` | 소품(집 · 나무 · 덤불) — 땅과 같은 크기 |
| `Resource/game/pixeltown/tilesets/town.tileset.xml` | 땅 브러시 — 길 · 물은 규칙 타일, 물은 `solid` |
| `Resource/game/pixeltown/tilesets/townprops.tileset.xml` | 소품 브러시 — 모두 `solid` |
| `Resource/game/pixeltown/sprites/*.sprite.json` | 주인공 4 방향 걷기 · 서기, 촌장 숨쉬기 |
| `Resource/game/pixeltown/dialogue/elder.dialogue.json` | 촌장 대화 두 줄 |

주민은 `DialogueRunnerComponent` 를 단 오브젝트이고, 선 칸이 막힙니다. 선택지가 있는 대화는 이 게임에 선택 UI 가 없어 오류로 알리고 닫습니다.

## 코드

| 파일 | 하는 일 |
|------|---------|
| `PixelTownGame` | 첫 씬을 열고 디렉터를 상태 스냅샷에 올린다 |
| `TownDirectorComponent` | 걷는 맵을 만들고, 주인공 폰의 의도로 한 칸씩 걷게 하고, 대화 요청을 플러시에서 러너에 넘긴다 |
| `TownWalkerViewComponent` | 주인공 스프라이트를 디렉터의 자리로 옮기고 방향 · 걸음에 맞는 애니메이션을 고른다. 앞뒤는 깊이(Z = 발 Y × 0.01)로 가린다 |
