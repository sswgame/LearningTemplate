# Destruction — 파괴 가능 메시

미리 쪼갠 메시(언리얼 Chaos 의 Geometry Collection 자리)와 그 위의 구조 · 피해 · 런타임입니다. **티어 8**(렌더러 · 모듈과 같은 줄): 캐릭터 형상의
자르기 도구(`Character/GeometryCutUtil`, 7) · 컴포넌트 모델(6) · 물리(3) 위에 서고, 렌더러는 모릅니다.

## 1. 쿠킹 — 보로노이 파쇄(`MeshFractureUtil`) → `.fracture`

모델 임포트 규칙에 `fracture` 가 있으면 임포터가 `.mesh` 옆에 `<이름>.fracture` 를 씁니다(`Config/Editor/ModelImportConfig.json`, 키는
`Editor/Common/Asset/ModelImportConfig.h`). 규칙에서 빠지면 다시 임포트가 옛 파일을 지우고, 스탬프(`models_raw/import.stamp`)가 그 파일까지 봅니다.

```json
{ "name": "Wall", "include_patterns": [ "*/empty/models_raw/wall_*" ],
  "fracture": { "pattern": "uniform", "pieces": 200, "seed": 7, "levels": [ 6, 24 ], "interior_color": [ 0.6, 0.55, 0.5, 1 ] } }
```

| 패턴 | 씨앗점 |
|---|---|
| `uniform` | 메시 안에 고르게(`pieces` 개 — 메시 밖 칸은 빠진다) |
| `clustered` | `impact_point` 둘레 `cluster_radius` 안에 `cluster_fraction` 몫, 나머지는 고르게 — 맞은 자리는 잘게, 멀리는 크게 |
| `slices` | `slices` 격자(축마다 칸 수)에 `slice_jitter` 흔들림 — 한 축이면 평면 조각, 둘이면 벽돌 |

**자르기.** 칸 i = 모든 j 에 대해 이등분 평면 안쪽의 교집합입니다. 메시를 가까운 씨앗의 평면부터 차례로 자르고(조각이 평면에 닿지 않으면 건너뛰고,
남은 평면이 조각 반경보다 멀면 끝) 끊긴 고리를 이어 귀 자르기(`PolygonTriangulationUtil` — 오목 · 구멍 단면)로 막습니다. 조각은 **자리가 비트까지
같은 정점끼리 닫힌 메시**입니다(`MeshFractureUtil::isClosedMesh`). 그것을 지키는 셋:

- 교점은 모서리의 두 끝점을 자리 순으로 정렬해 구합니다 — 그 모서리를 나누는 두 삼각형이 같은 비트의 점을 얻습니다.
- 평면에서 1e-5 m 안의 정점은 평면 위로 봅니다(정점마다 정하므로 이웃이 같은 판정).
- 칸 꼭짓점(세 평면이 만나는 점)은 서로 다른 모서리에서 따로 구해져 1e-7 m 쯤 어긋난 두 점이 됩니다. 자른 자리에 그런 쌍이 생기면 조각을 용접하고
  겹친 삼각형 · 서로 뒤집힌 쌍을 뺍니다 — 안 하면 한 모서리에 면 넷이 붙어(씨앗 서른 × 조각 마흔 중 10 %) 다음 자르기의 고리가 끊깁니다.

**결과**(`FractureAsset`): 조각마다 삼각형 목록(`RHIVertex`, 칸 0 = 겉면 · 1 = 안쪽 면 — 안쪽은 `interior_color` · 평면 투영 UV), 무게 중심 기준 볼록
껍질 점(`max_hull_points` 넘으면 고른 방향의 끝점), 무게 중심 · 부피, 경계. 연결(`FractureLink`)은 맞닿은 안쪽 면 넓이입니다 — 조각 i 에 j 쪽 평면이 낸
면과 j 에 i 쪽 평면이 낸 면 중 작은 넓이. 묶음 계층(`FractureGraphUtil::buildHierarchy`)은 레벨마다 묶음 수(`levels`, 위 → 아래)를 받아 아래에서
위로 짓고(가장 먼 점 고르기 + k-평균), 연결로 이어지지 않은 묶음은 덩어리마다 나누고, 잎을 깊이 우선으로 다시 매겨 노드마다 잎 구간이 이어지게 합니다.
같은 메시 · 규칙 · 씨앗이면 바이트까지 같습니다(`DestructionRandom` — splitmix64, 표준 분포를 쓰지 않는다).

**형식**(`.fracture`, 리틀 엔디언, `FractureAsset::kVersion`): 매직 `SWFR` · 버전 · 잎 · 노드 · 자식 · 연결 · 정점 · 껍질 점 수 · 씨앗 · 경계 → 노드 →
자식 → 연결 → 조각 구간 → 정점(float 12) → 삼각형 칸(바이트) → 껍질 점. 읽기는 지금 형식만 받고 구조를 검사합니다(틀리면 거절). 캐시는
`FractureAssetCache`(약한 참조, 핫 리로드는 새 객체로 바꾸고 `getReloadGeneration` 을 올린다).

| 타입 | 자리 |
|---|---|
| `MeshFractureUtil` · `FractureSettings` · `FracturePattern` | `MeshFracture.h` |
| `FractureGraph` · `FractureNode` · `FractureLink` · `FractureGraphUtil` | `FractureGraph.h` (2D · 3D 공용) |
| `FractureAsset` · `FracturePiece` · `FractureSurfaceSlot` | `FractureAsset.h` |
| `FractureAssetCache` | `FractureAssetCache.h` |
| `PolygonTriangulationUtil` | `PolygonTriangulation.h` |
| `DestructionRandom` | `DestructionRandom.h` |

시험: `FractureTest`(`Test/EngineTest/TestFracture.cpp`), 임포트는 `ModelImporterTest.FractureRuleWritesFractureAssetBesideTheMesh`.
