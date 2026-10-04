# 내비게이션 (Navigation) — 3D 내비메시 · 경로 · 군중

에이전트가 걸을 수 있는 면을 폴리곤으로 미리 계산해 두고(베이크), 그 위에서 경로를 찾고(A* + 줄 당기기), 여럿이 서로 비켜 걷게(군중 회피) 합니다.
비교 기준은 언리얼 `ARecastNavMesh` · `UNavigationSystemV1` · `UCrowdManager`, 유니티 `NavMeshSurface` · `NavMeshAgent` · `NavMeshObstacle` 입니다 —
둘 다 Recast & Detour 위에 서 있고, 이 엔진도 같은 라이브러리를 엔진 쪽 인터페이스 뒤에 감쌌습니다. 격자 내비게이션(RTS · 도시 건설)은 따로
`GameFramework/Navigation`(`NavGrid` · `NavAgent`)에 있고, 둘은 공통 이동 창구(`INavMover`)로 묶입니다.

**티어 4**(공간 분할과 같은 줄): 물리의 셰이프 서술자 · `AABB`(3) · 직렬화(2) · 리플렉션(1)을 읽고, 씬의 내비게이션(`Object/GameObject/SceneNavigation`, 6)과
컴포넌트(`Object/Component/Navigation`, 6)가 이것을 씁니다.

## 1. 감싸기 — 라이브러리는 `Recast/` 안에만

| 무엇 | 자리 |
|---|---|
| 낱말 — 폴리곤 참조(64 비트 불투명) · 영역 · 질의 거름 · 경로 · 레이캐스트 결과 · 군중 에이전트 값 | `NavigationTypes.h` |
| 내비메시 인터페이스(타일 베이크 · 끼우기 · 질의 · 디버그 선 · 군중 만들기) · 군중 인터페이스 · 백엔드 고르기 | `INavMesh.h` (`INavMesh` · `INavCrowd` · `NavMeshBackend`) |
| 공통 이동 창구(내비메시 에이전트 · 격자 행위자) | `INavMover.h` |
| 베이크 입력 — 월드 삼각형(영역 하나씩) · 볼록 부피(장애물 · 영역 칠하기) · XZ 칸 색인 · 입력 해시 | `NavMeshGeometry` |
| 사람이 고치는 표 — 에이전트 종류 · 영역 · 재베이크 예산 | `NavMeshSettings` · `Resource/engine/navigation/navmeshsettings.xml` |
| 쿠킹본(`.navmesh`) · 모든 타일을 워커로 베이크하는 도우미 | `NavMeshAsset` (`NavMeshAsset` · `NavMeshBakeUtil`) |
| Recast(베이크) · Detour(타일 내비메시 · 질의) 구현 | `Recast/RecastNavMesh` |
| DetourCrowd 구현 | `Recast/DetourNavCrowd` |

라이브러리 헤더(`<recastnavigation/...>` — `Recast*.h` · `Detour*.h` · `DebugDraw.h` 도)는 `Recast/` 에서만 include 하고, 라이브러리(`recastnavigation` ·
`RecastNavigation::*`)는 `Source/Engine/CMakeLists.txt` 에서만 링크합니다(`Scripts/lint/gate/CheckThirdPartyIsolation.py`). 시험 · 게임 · 키트도 인터페이스만 씁니다.
백엔드를 바꾸면 `INavMesh` · `INavCrowd` 를 구현하는 두 클래스와 `NavMeshBackend` 의 세 함수(`createNavMesh` · 이름 · 판)가 바뀝니다. 쿠킹본은 백엔드
이름 · 판을 적어 두므로 다른 백엔드의 바이트를 읽지 않습니다.

Recast · Detour 의 할당은 엔진 할당기(메모리 태그 `Navigation`, 예산 `Config/Engine/MemoryBudget.json`)로 갑니다(`rcAllocSetCustom` · `dtAllocSetCustom`).
vcpkg 의 Detour 는 32 비트 폴리곤 참조(`DT_POLYREF64` 없음)라 타일 · 폴리곤 비트가 22 개입니다 — 타일이 16384 개를 넘는 격자는 거절합니다(타일 크기를 키운다).

## 2. 베이크 — 타일 하나

내비메시는 XZ 타일 격자이고(`NavTileGrid`, 타일 한 변 = `_tileSize` 셀 × `_cellSize`), 타일 하나가 베이크 · 끼우기 · 쿠킹의 단위입니다.
`INavMesh::bakeTile` 은 입력만 읽는 순수 함수라 워커 여럿이 같이 돌고(`NavMeshBakeUtil::bakeAllTiles` 가 `engine::runParallel` 로 나눈다),
`replaceTile` 은 게임 스레드에서 질의가 없는 동안 합니다. 같은 입력 · 같은 표면 바이트까지 같은 타일입니다(쿠킹본과 런타임 베이크가 같다).

Recast 순서(`RecastNavMesh::bakeTile`):

1. 타일 + 테두리(몸 반지름 + 3 셀)에 닿는 삼각형을 칸 색인으로 모아 복셀화한다. 경사가 `_maxSlope` 를 넘는 삼각형은 걷는 면이 아니다(막기는 한다).
2. 낮게 걸린 장애물 · 턱(떨어지는 가장자리) · 낮은 천장(`_height`)을 거른다. `_maxClimb` 안의 단은 오른다.
3. 압축 높이장 → 몸 반지름만큼 깎기(벽에서 떨어져 걷는다).
4. 볼록 부피를 칠한다. 뚫는 부피(`kNotWalkableArea` — 장애물)는 깎은 뒤에 칠하므로 몸 반지름만큼 넓혀 칠한다(유니티 Carve 와 같은 결과).
5. 분수령 영역(`_minRegionSize` 보다 작은 섬은 버린다) → 외곽선(`_maxEdgeError` · `_maxEdgeLength`) → 꼭짓점 6 개까지의 폴리곤 → 높이 디테일.
6. Detour 타일 바이트(`dtCreateNavMeshData`).

영역 번호 i(표의 순서)는 Recast 영역 i + 1(0 은 걷지 못함)이고, 폴리곤 표시 비트는 1 << i 입니다 — 질의 거름의 영역 비트가 그대로 표시 비트입니다.
그래서 영역은 16 개까지입니다.

## 3. 질의

| 함수 | 하는 일 |
|---|---|
| `findNearestPoint` | 반 크기 상자 안의 가장 가까운 내비메시 위 점(폴리곤 참조와 함께) |
| `findPath` | 두 점을 붙이고 A*(영역 비용) → 줄 당기기(모퉁이만 남긴 직선 경로). 끝에 닿지 못하면 닿은 마지막 폴리곤에서 끝에 가장 가까운 점까지의 `Partial`, 시작 · 끝이 내비메시 밖이면 `Failed` |
| `raycast` | 걷는 면을 따라 곧게(시선) — 경계에 막히면 막힌 자리 · 법선 |

질의는 `const` 이고 아무 스레드에서 불러도 됩니다 — 스레드마다 자기 Detour 질의 객체를 씁니다(스크래치 슬롯마다 하나, 처음 쓸 때 만든다. 슬롯 밖의
스레드는 잠금 아래 하나를 나눠 쓴다). 끼우기(`replaceTile`)와는 겹치면 안 되므로 씬의 내비게이션은 컴포넌트 틱 밖에서만 끼웁니다.

**거름**(`NavQueryFilter`)은 영역마다 비용 배율과 막을 영역 비트입니다. 기본은 표의 영역 비용(`NavMeshSettings::makeDefaultFilter`). 비용은 1 이상이어야
합니다 — 경로 찾기의 거리 추정이 1 을 가정하므로 1 보다 싼 영역은 최단 경로를 놓칩니다.

## 4. 군중 — `INavCrowd`(DetourCrowd)

경로 통로(corridor) 따라가기 · 모퉁이 미리 돌기 · 표본 회피(RVO 계열, 품질 넷 — Low · Medium · Good · High 는 적응 표본의 나눔 · 고리 · 깊이) ·
떨어지기를 `update` 한 번에 모든 에이전트에 합니다. 게임 스레드 하나가 다룹니다. 다른 것이 에이전트를 옮기면(캐릭터 컨트롤러) `syncAgentPosition` 으로
통로를 그 자리로 끌어 옮기고(벽을 넘지 않는다), 멀리 옮겼으면 `teleportAgent` 입니다. 목적지를 내비메시에서 찾지 못한 요청은 `Failed` 로 남습니다.

## 5. 표 — `navmeshsettings.xml`

```xml
<NavMeshSettings _defaultAgentType="Humanoid" _maxConcurrentTileBakeCount="4" _obstacleMoveThreshold="0.25">
  <_listAgentType>
    <NavAgentTypeDef _name="Humanoid" _radius="0.4" _height="1.9" _maxClimb="0.45" _maxSlope="45" _cellSize="0.2" _cellHeight="0.1" _tileSize="48" ... />
  </_listAgentType>
  <_listArea><NavAreaDef _name="Default" _cost="1" /><NavAreaDef _name="Mud" _cost="4" /></_listArea>
</NavMeshSettings>
```

에이전트 종류마다 내비메시가 따로 베이크됩니다(언리얼 `SupportedAgents` · 유니티 Agent Type). 모르는 키 · 겹친 이름 · 모르는 기본 종류 · 범위 밖 값은
읽기 오류이고 `ResourceDataSchemaTest` 가 저장소의 표를 읽습니다. 쿠킹본은 종류마다 베이크 값의 해시(`computeAgentTypeHash`)를 적어 두어 표를 바꾸면 낡습니다.

## 6. 쿠킹본 — `.navmesh`

리틀 엔디언: 매직 `SWNV` · 형식 판 · 백엔드 이름 · 백엔드 판 · 항목 수 → 항목마다 [종류 이름 · 베이크 값 해시 · 입력 해시(`NavMeshGeometry::computeHash`) ·
경계 상자 · 타일 수 → 타일마다 (x, z, 바이트)]. 읽기는 지금 형식만 받고 잘린 바이트 · 다른 매직 · 다른 백엔드를 거절합니다. 이름은 씬 이름 + `.navmesh`
(`NavMeshAsset::makeCookedPath` — `maps/arena.scene.xml` → `maps/arena.navmesh`).

시험: `NavMeshBakeTest`(`Test/EngineTest/TestNavMeshBake.cpp` — 바닥 · 계단 · 경사 · 구멍 · 쿠킹본 왕복 · 표), `NavMeshQueryTest`(돌아가는 경로 · 닿을 수
없는 섬 · 레이캐스트 · 영역 비용), `NavMeshCrowdTest`(복도에서 마주 오는 둘이 비켜 감).
