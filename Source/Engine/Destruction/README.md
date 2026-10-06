# Destruction — 파괴 가능 메시

미리 쪼갠 메시(언리얼 Chaos 의 Geometry Collection 자리)와 그 위의 구조 · 피해 · 런타임입니다. **티어 8**(렌더러 · 모듈과 같은 줄): 캐릭터 형상의
자르기 도구(`Character/Fit/GeometryCut` 의 `GeometryCutUtil`, 7) · 컴포넌트 모델(6) · 물리(3) 위에 서고, 렌더러는 모릅니다.

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

**닫히지 않은 모델**(판자 · 겹친 부품 · 열린 바닥 — Kenney 상자 · 벽은 대개 그렇다)은 `volume` 으로 닫힌 **대리 부피**를 고릅니다: `bounds`(경계 상자) ·
`hull`(정점의 볼록 껍질, `MeshFractureUtil::makeConvexHull`). 대리를 쪼개 부피 · 무게 중심 · 껍질 · 안쪽 면 · 연결을 얻고, 겉면은 원래 메시를 같은
평면으로 막지 않고 잘라 조각마다 붙입니다(대리의 바깥 면은 그리지 않는다). 기본 `mesh` 는 닫혀 있어야 하고 아니면 임포트 오류입니다.

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

시험: `FractureTest`(`Test/EngineTest/Destruction/TestFracture.cpp`), 임포트는 `ModelImporterTest.FractureRuleWritesFractureAssetBesideTheMesh`.

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

**파괴 재질 표**(`*.destruction.xml`, `DestructionProfile`): 깊이별 변형 문턱 · 연결 세기 · 지지 세기 · 밀도 · 물리 재질 · 충격 → 변형 · 파편 예산 ·
네트워크(`<Network poseRate>` — 서버가 움직이는 덩어리 자세를 보내는 빈도, 선택, 기본 10 Hz).
기본은 `Resource/engine/destruction/default.destruction.xml`(돌), `wood.destruction.xml`(나무). 모르는 원소 · 속성 · 틀린 수 · 같은 원소 두 번은 로드
오류이고(`CharacterDataReader`), `ResourceDataSchemaTest` 가 저장소의 모든 표를 읽습니다. 같은 `.fracture` 를 표만 바꿔 다르게 부숩니다(다시 쿠킹하지 않는다).

시험: `DestructionStateTest`(`Test/EngineTest/Destruction/TestDestructionState.cpp`).

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

**스냅숏**(`DestructionState::writeSnapshot` · `readSnapshot`): 끊긴 노드 · 연결 · 앵커 잎 비트, 0 이 아닌 노드 · 연결 변형(비트 그대로), 그룹(번호 ·
부모 · 앵커 · 활성 노드), 다음 그룹 번호, 사건 수. 읽은 쪽의 `computeStateHash` 가 쓴 쪽과 같고 뒤따르는 사건도 같은 결과를 냅니다(쌓인 변형까지 넘어간다) —
늦은 참가 · 어긋남 바로잡기가 사건열을 처음부터 다시 돌리지 않습니다. 다른 그래프 · 잘린 바이트는 거절하고 상태를 그대로 둡니다. 시험: `DestructionSnapshotTest`.

시험: `DestructionDamageTest`(`Test/EngineTest/Destruction/TestDestructionDamage.cpp`).

## 4. 런타임 — `FractureComponent`(3D) · `FractureComponentBase`

오브젝트 구성: 뿌리 `RigidBodyComponent`(오브젝트 자세를 맡는다 — 벽은 Static, 상자는 Dynamic) + 그릴 `MeshComponent`(`.mesh`) + `FractureComponent`
(`_fracturePath` 를 비우면 메시 옆 `.fracture`, `_profilePath` 를 비우면 기본 표, 앵커 모드 None · Bottom · World + 앵커 볼륨, 권한).
**강체가 뿌리여야 합니다** — 메시를 뿌리로 두면 물리는 강체 컴포넌트만 옮기고 오브젝트 자세(쪼갤 때 쓰는)는 그대로다.

- **온전**: 그리기 하나(오브젝트 메시) · 바디 하나(오브젝트 강체), 이 컴포넌트는 상태만 든다.
- **처음 떨어진 것이 생기면**(그룹이 갈라지면) 쪼갠 상태: 메시를 숨기고 강체를 끄고(그 바디를 곧바로 빼 새 조각과 겹치지 않게), 조각마다 본 하나인
  **스킨드 메시 둘**(겉면 · 안쪽 면 — `FractureRenderUtil`, 조각이 수백이어도 칸마다 그리기 하나, GPU 스키닝)을 같은 오브젝트에 붙인다. 앵커 그룹의
  잎은 잎마다 **정적 바디**(미리 지은 잎 껍질 셰이프를 나눠 쓰고 `createBodies` 한 번), 떨어진 그룹은 그룹마다 **동적 바디 하나**(지어 둔 잎 셰이프를
  묶은 `createCompoundShape` — 껍질을 다시 짓지 않는다, 잎 하나면 그 셰이프). 붙은 그룹이 갈라져도 붙은 잎의 정적 바디는 그대로 둔다(`syncStaticBodies`).
- **잎 셰이프는 상태를 시작할 때(플레이 첫 물리 프레임) 모두 짓습니다.** 볼록 껍질 짓기가 깨지는 프레임 비용의 거의 전부라서다 — 쇼케이스 벽(200 조각)
  첫 파괴의 사건 처리가 Release 에서 10 ~ 14 ms → 2 ~ 4 ms, 대신 시작 프레임이 껍질 수 × 약 80 us(쇼케이스 파괴물 여섯 · 잎 312 개 ≈ 25 ms) 늘어난다.
  상태 갱신(그래프 · 지지 · 피해) 자체는 0.1 ms 남짓. 배율이 바뀐 채 깨지면 그때 다시 짓는다.
- **갈라질 때**: 자식 그룹은 부모 바디 자세에서, 부모 질량 중심 속도 + 각속도 × 거리로 태어나고, 사건의 충격량을 받는다(폭발은 바깥 · 거리 감쇠, 맞은
  자리는 맞은 방향, 씨앗 흩기 — 상태에는 들지 않는다). 같은 오브젝트 조각끼리의 부딪힘은 태어난 지 0.3 초 안이면 피해가 아니다(맞닿은 채 태어난다).
- **매 물리 프레임**: 동적 바디 자세를 읽어 잎마다 본 로컬(오브젝트 메시 공간: 회전 = 오브젝트⁻¹ × 그룹, 옮김)을 쓰고 `applyExternalPose`(물리가 애니메이션
  평가보다 늦으므로 그 자리에서 팔레트를 다시 구한다). 수명 · 잠 · 페이드는 이번 프레임에 돈 고정 스텝 시간으로 센다.
- **정리**: 잠들어 `sleepRemoveTime` 쉰 그룹은 바디를 뺀다(`keepCollisionVolume` 이상은 잠든 바디를 남겨 충돌을 지킨다) · 작은 파편은 `lifetime` 뒤
  `fadeTime` 동안 본 배율이 0 으로 줄며 사라진다 · 떨어진 그룹 바디가 `maxBodies` 또는 `gv_destructionMaxDebrisBodies`(씬마다 — `ScenePhysics::getDebrisBodyCount`,
  기본 512. 한 프로세스에 월드가 여럿이어도 서로의 예산을 먹지 않는다)를 넘으면 오래된
  것(작은 것 먼저)부터 사라지게 한다 · 30 프레임 동안 움직인 것이 없으면 지금 자세를 **정적 메시 둘에 구워** 스킨드 메시를 숨긴다(프레임 비용 0).
  다시 움직이면 스킨드로 돌아온다.
- **피해 입구**(어느 틱에서든 — 잠금 아래 쌓였다가 다음 물리 프레임에 게임 스레드에서): `applyDamage`(메시 공간 — 네트워크로 받은 사건),
  `applyPointDamageAtWorld`(무기, 맞은 바디가 떨어진 덩어리면 그 그룹만), `applyRadialDamageAtWorld`(폭발 — 붙은 구조와 반경 안의 덩어리마다 그 자세로
  사건 하나), `applyRaycastDamage`(광선 → 바디 사용자 값 → 오브젝트), 부딪힘(시작 충격량 > `minImpulse`, 권한일 때). 적용한 사건은 `getEventLog()` 에
  쌓인다 — 받는 쪽(`setAuthority( false )`)은 그것을 같은 순서로 `applyDamage` 하면 같은 구조 상태다(조각의 물리 자세는 각자 — 꾸밈이다). 흩기 씨앗의
  번호는 상태의 사건 수다(스냅숏을 받은 쪽도 서버와 같은 번호). 떨어진 덩어리를 맞힌 사건은 맞기 직전 그 덩어리 자세를 `getEventGroupPoses()` 에 적고,
  받는 쪽은 `applyDamage( event, groupPose )` 로 그 자리로 옮긴 뒤 가른다.
- **네트워크 창구**(`GF_NetDestruction` 이 쓴다): `collectGroupPoses`(떨어진 그룹의 원점 · 질량 중심 · 회전 · 부피 · 멈춤 · 사라짐 · 몰림 · 레이어),
  `makeNetworkSnapshot` · `applyNetworkSnapshot`(상태 스냅숏 + 떨어진 그룹 자세 — 받으면 그룹 바디 · 그림을 다시 짓는다, 받는 쪽의 덩어리는 그 자세에
  키네마틱), `driveGroup( 그룹, 질량 중심, 회전 )`(받는 쪽 덩어리 — 바디를 키네마틱으로 바꾸고 스텝마다 그 자세로 옮긴다), `isChunkVolume`(표의
  `keepCollisionVolume` 이상이면 덩어리 — 서버가 자세를 보내고 플레이어와 부딪힌다, 아래는 파편 — Debris 레이어 · 클라이언트 꾸밈),
  `findRecentStateHash`(받는 쪽이 사건마다 적어 둔 최근 64 개 (사건 수, 해시) — 사건이 프레임마다 묶여 적용돼도 서버의 해시와 같은 순간을 비교한다),
  `hasPendingDamage`.
- `SkeletalMeshComponent` 에 둘을 더했다: `setSkeleton` 은 런타임 지정이라 경로가 바뀔 때까지 유지(시작의 렌더 에셋 해석이 덮지 않는다),
  `applyExternalPose`(평가 밖에서 고친 로컬 포즈로 팔레트를 그 자리에서).

시험: `FractureComponentTest`(`Test/EngineTest/Destruction/TestFractureComponent.cpp`).

## 5. 기믹과 잇기(GameFramework)

`ExplosiveBarrelComponent` 의 폭발은 반경이 경계 구에 닿은(`isReachedBy`) 파괴 오브젝트에 `applyRadialDamageAtWorld`(중심 변형 `_fractureStrain`, 충격량
`_blastImpulse`)를 주고, 자기에게 파쇄 데이터가 있으면 몸을 끄지 않고 스스로 부서집니다 — 사슬 폭발이 근처 벽 · 상자를 그 자리에서 깹니다.
`DestructibleComponent` 는 단계마다 중심에 변형을 주어 깎고 마지막 단계에 통째로 부숩니다(파쇄 데이터가 없으면 몸을 끈다).
`GameFramework/Base/Gimmick/README.md` 참고. 시험: `GimmickFractureTest`.

## 6. 2D — `Fracture2DComponent` · `PolygonFractureUtil`

2D 는 **쪼개기의 출처와 물리 씬만 다르고 나머지는 같은 코드**입니다(그래프 · 묶음 계층 · 구조 · 지지 · 피해 · 사건 기록 · 런타임 · 스킨드 조각 그림).
`PolygonFractureUtil` 이 XY 다각형(오목 가능, 시계면 뒤집는다)을 같은 `FractureSettings`(uniform · clustered · slices 의 X · Y 칸)로 반평면마다 잘라
`FractureAsset` 을 냅니다 — 부피 칸은 넓이, 연결은 맞닿은 변 길이, 그림은 Z = 0 의 앞뒤 두 면(UV 는 경계 상자로 [0, 1]), 껍질은 조각 꼭짓점.
쪼개기가 싸서 시작할 때 합니다(씨앗 `_fractureSeed` + 모양이 같으면 모든 기계에서 같은 조각 — 씨앗과 사건만 보낸다).

오브젝트 구성: 뿌리 `RigidBody2DComponent`(온전할 때의 충돌) + `Fracture2DComponent`(`_listBorder` 또는 `_size` 상자 — 바닥 가운데가 원점,
`_pieceCount` · `_pattern` · `_listLevelCount`). 그릴 메시가 없으면 쉬는 조각을 구운 평평한 메시를 `MeshComponent` 로 더합니다. 조각 바디는 Box2D
(`IFracturePhysics` 의 2D 구현 — 껍질은 8 점 이하 볼록 다각형으로 줄이고, 자세는 XY · Z 축 회전, 깊이는 오브젝트 것).

시험: `Fracture2DTest`(`Test/EngineTest/Destruction/TestFracture2D.cpp`).

**삼각형 수.** 자를 때마다 앞 막음의 대각선 교점이 다음 막음의 고리에 일직선으로 쌓입니다. 칸마다 자르기를 마치면 안쪽 면을 평면마다 다시 짓습니다
(`simplifyCaps` — 그 점을 쓰는 **모든** 면이 안쪽 면이고 그 모든 고리에서 일직선인 점만 함께 뺀다, 겉면이 쓰는 점은 그대로). 쪼개기 결과를 바꾸는 고침은
`MeshFractureUtil::kAlgorithmVersion` 을 올립니다 — 임포트 원본 해시에 섞여 쿠킹한 `.fracture` 가 어긋남이 된다.

## 7. 쇼케이스 — `game/empty/maps/destructionshowcase.scene.xml`

벽돌 벽(200 조각, 앵커 바닥) · 나무 상자 셋(대리 부피 24 조각, `wood.destruction.xml`) · 폭발 통 둘(20 조각). `BarrelFuse` 가 1.5 초 도화선(`_fuseTime`)으로
터지고 반경 3 m 안의 `BarrelChain` 이 사슬로 이어 터집니다 — 벽 아래쪽이 깨지고 받침을 잃은 위쪽이 큰 덩어리로 무너진다(지지 붕괴). 모델 원본은
`Resource/game/empty/models_raw/`, 어떤 모델을 어떻게 쪼갤지는 `Config/Editor/ModelImportConfig.json` 의 `Destruction_*` 규칙(`App --import-models` 가
`.mesh` 옆에 `.fracture` 를 쓴다). 보려면 `Resource/game/empty/data/gamesettings.xml` 의 `startMap` 을 이 씬으로 바꿔(또는 `-gv_firstScene=`) App 을 띄웁니다
(`-gv_screenshotFrame=900 -gv_screenshot=<경로>.ppm` 로 깨진 뒤를 찍는다). 시험: `DestructionShowcaseTest`(도화선 → 사슬 → 벽 · 상자가 깨진다).

## 함정 · 계약

- **파쇄(평면 자르기)** — 모서리 교점은 끝점을 자리 순으로 정렬해 구한다(이웃 칸이 같은 모서리를 반대 방향으로 자르면 비트가 달라 틈이 생긴다). 세 칸이 만나는 곳에는 거의 같은
  점이 생겨 그때만 용접 + 퇴화 삼각형 정리(`cleanPiece`)를 돈다(늘 돌리면 느리다). 귀 자르기는 일직선 점을 삼각형 없이 버리면 안 된다(T 자 틈). 안쪽 면 다시 짓기는 그 점을
  쓰는 **모든** 면이 안쪽 면일 때만 뺀다. 쪼개기 결과가 바뀌면 `MeshFractureUtil::kAlgorithmVersion` 을 올린다(임포트 해시).
- **도는 덩어리는 질량 중심으로 보간한다** — 그룹 원점(오브젝트 원점)은 덩어리에서 수 미터 떨어질 수 있어 원점을 직선으로 이으면 오차가 1 m 를 넘는다(p99 0.44 → 0.07 m).
- **부서지기 전 통째 움직임은 오브젝트 이동 복제(`ReplicationServer` 엔티티)의 몫이다**(사용자 결정 2026-10-06) — 파괴 키트는 부서진 뒤 조각만 보낸다(언리얼도
  GC 액터의 통째 움직임은 `bReplicateMovement`). 게임이 안 보내면 클라이언트 조각은 클라이언트의 그 자리에서 태어난다.
