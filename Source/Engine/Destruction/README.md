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

## 2. 구조 — 계층 · 연결 · 앵커 · 지지(`DestructionState`)

오브젝트 하나의 상태(2D · 3D 공용, 물리 · 렌더를 모른다). 세 가지가 따로 있습니다.

- **계층**은 나눔의 굵기입니다. 활성 노드가 한 몸으로 움직이는 단위이고, 묶음이 갈라지면(`breakNode`) 자식들이 활성이 됩니다 — 갈라졌다고
  떨어지지는 않습니다(Chaos 의 클러스터 레벨: 큰 덩어리가 먼저, 더 센 피해에 그 안의 조각).
- **연결**이 떨어짐을 정합니다. 활성 노드가 다른 두 잎 사이의 연결만 끊길 수 있습니다(갈라지지 않은 묶음 안은 한 몸). `detachLeaf` 는 잎이 활성이
  될 때까지 위를 가르고 그 잎의 연결을 모두 끊고 앵커에서 뗍니다.
- **앵커**(땅 · 고정 볼륨에 닿은 잎 — 런타임이 정해 넘긴다)가 지지를 정합니다(레드 팩션 게릴라). 그룹 = 끊기지 않은 연결로 이어진 활성 노드들,
  앵커 잎을 품으면 붙은(정적) 그룹, 아니면 떨어진(동적) 그룹. 붙은 그룹은 **무게를 앵커 쪽으로 흘립니다** — 앵커에서 너비 우선으로 깊이를 매기고,
  깊은 노드부터 제 무게(부피 × `density` × g) + 받은 하중을 더 얕은 이웃에게 맞닿은 세기(넓이 × `supportStrength`) 비율로 나눠 넘겨, 몫이 세기를
  넘는 연결을 끊습니다(끊기면 다시 나눠 최대 8 번). 받침이 줄면 위가 무너집니다.

한 덩어리 그대로 남은 그룹은 번호를 지킵니다(묶음이 갈라졌을 뿐이면 바디도 그림도 바뀌지 않는다). 갈라지면 옛 번호가 사라지고 새 번호(늘기만 한다)가
덩어리마다(가장 작은 노드 순) 생깁니다 — `DestructionChange`(사라진 · 새 그룹, 갈라진 노드 · 끊긴 연결 · 무게로 끊긴 연결 수). 모든 순서가 노드 ·
연결 · 그룹 번호 순이라 같은 그래프 · 표 · 앵커 · 같은 순서의 조작이면 같은 상태이고 `computeStateHash` 가 같습니다.

**파괴 재질 표**(`*.destruction.xml`, `DestructionProfile`): 깊이별 변형 문턱 · 연결 세기 · 지지 세기 · 밀도 · 물리 재질 · 충격 → 변형 · 파편 예산.
기본은 `Resource/engine/destruction/default.destruction.xml`(돌), `wood.destruction.xml`(나무). 모르는 원소 · 속성 · 틀린 수 · 같은 원소 두 번은 로드
오류이고(`CharacterDataReader`), `ResourceDataSchemaTest` 가 저장소의 모든 표를 읽습니다. 같은 `.fracture` 를 표만 바꿔 다르게 부숩니다(다시 쿠킹하지 않는다).

시험: `DestructionStateTest`(`Test/EngineTest/TestDestructionState.cpp`).

## 3. 피해 — 변형 문턱 · 사건 · 네트워크(`DestructionDamage.h`, `DestructionState::applyDamage`)

사건(`DestructionDamageEvent`)은 **메시 공간**의 중심 · 변형 · 반경 · 맞은 잎(선택) · 방향 · 충격량입니다. 종류: `Point`(무기 — 광선이 고른 잎에
감쇠 없이), `Radial`(폭발 — 반경 안 선형 감쇠 `1 - 거리 / 반경`, 떨어진 조각을 바깥으로 민다), `Impact`(물리 접촉의 시작 충격량 —
`makeImpactEvent`: `minImpulse` 를 넘은 몫 × `impulseToStrain`, 반경 `Impact.radius`).

적용 순서(모두 노드 · 연결 번호 순):

1. 잎마다 변형(`computeLeafStrain`).
2. 활성 노드마다 그 잎들의 최대 변형을 쌓고(`getNodeStrain`), 깊이의 문턱(`Strain thresholds` — 깊이 0 이 뿌리)을 넘으면 묶음은 자식으로 갈라지고
   **넘친 몫의 비율**(`(쌓인 값 - 문턱) / 이번 변형`)만큼 자식에게 다시 줍니다. 센 피해 한 번은 여러 레벨을 지나고, 약한 피해는 쌓여 큰 덩어리만
   가릅니다. 잎이 문턱을 넘으면 떨어져 나갑니다(`detachLeaf` 와 같다).
3. 활성 노드가 다른 두 잎 사이의 연결은 두 잎 변형의 평균을 쌓고 넓이 × `Links strength` 를 넘으면 끊깁니다 — 조각이 깨지지 않아도 떨어질 수 있다.
4. 맞은 그룹을 다시 나누고 지지를 잽니다(2 절).

**결정성 · 네트워크.** 같은 그래프(같은 씨앗으로 쿠킹) · 표 · 앵커에 같은 순서의 사건이면 어느 기계에서도 같은 상태입니다(`computeStateHash`,
`getEventCount`). 그래서 보내는 것은 변환이 아니라 `DestructionEventLog`(씨앗 + 사건열, `SWDE` 바이트)이고 받는 쪽이 같은 순서로 `applyDamage` 합니다.
순서를 바꾸면 다른 상태입니다(계약의 일부). 부딪힘 사건은 물리에서 나오므로 기계마다 다를 수 있습니다 — 권한 쪽이 만든 사건만 기록에 싣습니다.

시험: `DestructionDamageTest`(`Test/EngineTest/TestDestructionDamage.cpp`).
