# 게임별 씬이 새로 들인 CC0 메시를 부르게 한다

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-17 · 진행: 1/2

2026-10-17 `res3` 가 여섯 게임에 CC0 모델 팩(KayKit Dungeon · Space Base, Kenney Nature · Fantasy Town · Space · City)의 원본을 배치했지만 씬 · 프리팹은 자리 표시 큐브를 썼습니다.

## 1. 결정

- 배치는 데이터(`Config/Editor/Cc0PropPlacement.json`)이고 스크립트(`Scripts/dev/MakeCc0SceneProps.py`)는 형식만 가진다. 게임마다 두 규칙이다.
  `replace` 는 이름 접두어의 **메시만 든** 자리 표시 엔티티를 메시 하나로 바꾼다(첫 큐브의 바닥 자리 · 회전을 지킨다 — 다른 컴포넌트가 든 엔티티면 멈춘다).
  `showcases` 는 남은 메시를 게임 영역 밖 줄에 놓는다(규칙에 끼지 않는다). 같은 이름이 있으면 다시 놓지 않는다.
- 다 놓은 뒤 `models/` 의 메시 가운데 아무도 부르지 않는 것이 있으면 실패한다(`--check` 는 고치지 않고 같은 판정). 에셋 검증 `orphans` 와 같은 기준이다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| C1 | 여섯 게임 씬에 놓기 — WildWest 는 선인장 · 바위 · 울타리 큐브를 메시로 바꾸고, Shooter3D(KayKit Dungeon 19) · StarSkirmish(KayKit Space 9 · Kenney Space 15) · HarvestValley(3) · MeadowVillage(4) · NileCity(17)는 전시 줄. 키트 머티리얼 여섯을 더했다(`kaykit_dungeon` · `kaykit_space` · `kenney_space` · `town_palette` · `nature` 둘) | 놓음 · `--check` 통과(부르지 않는 메시 0) |
| C2 | 화면 확인 — 크기(전시 줄의 배율 1 이 맞는지), 머티리얼 색(텍스처 없는 `kenney_space` · `nature` 는 정점 색 추정), WildWest 울타리 방향, 전시 줄이 게임 카메라에 걸리지 않는지 | 남음(GPU) |

## 남은 것

- C2 뒤 크기 · 자리를 데이터에서 고치고 스크립트를 다시 돌린다. 게임 규칙에 쓰일 모델(엄폐물 · 건물)은 전시 줄에서 게임 쪽 자리로 옮기는 일을 그 게임의 계획에서 한다.
- `empty` 의 `avatarsample_d.mesh` 는 이 배치의 대상이 아니다(res3 밖).
