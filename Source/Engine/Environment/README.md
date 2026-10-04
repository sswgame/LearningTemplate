# Environment (배치 규칙 · 지형 · 식생 · 물)

월드의 배경 시스템이 들어올 자리입니다. 컴포넌트(Object, 티어 6)가 메시 · 머티리얼(Graphics, 티어 5)로 그리는 기능이라 티어 7(Scene · Sequencer 와 같은 줄)에
둡니다. 지금은 배치 규칙 코어만 있습니다.

상위: [Engine/README.md](../README.md)

| 폴더 | 무엇 |
|------|------|
| `Placement/` | 규칙 기반 배치 코어 — `PlacementRule`(데이터) · `PlacementScatter`(계산) · `IPlacementSurface`(표면) · `PlacementTileSurface`(2D 타일 표면). 2D · 3D 공용 |

## 배치 규칙 — 2D · 3D 공용 코어

`PlacementScatter::scatter( 규칙, 평면 사각형, 표면, 제외 영역, 항목 비중, 결과 )` 하나입니다. 평면 좌표 (u, v) 위에서 돌고, 3D 는 (x, z) + 높이(지형),
2D 는 (x, y) + 타일 레이어입니다. 후보 수 = 밀도 × 면적, 후보마다 **(씨앗, 번호, 칸) 해시**로 난수를 뽑으므로 같은 입력은 비트까지 같은 결과이고,
필터 하나를 바꿔도 남는 자리는 그대로입니다. 순서: 제외 영역(원 · 사각형 기둥) → 표면(구멍 · 맵 밖) → 경사 · 높이 · 레이어 필터 → 밀도 레이어(가중치 확률)
→ 최소 거리(격자 해시 다트 던지기 — 먼저 놓인 것이 이긴다) → 항목 · 크기 · 요 · 노멀 맞춤. 같은 코어를 쓰는 곳: GameFramework `PropScatterComponent` 의
`Rules` 모드(게임플레이 오브젝트), 2D 타일 흩뿌리기(`PlacementTileSurface`).
