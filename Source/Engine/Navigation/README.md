# 내비게이션 (Navigation) — 3D 내비메시 · 경로 · 군중

에이전트가 걸을 수 있는 면을 폴리곤으로 미리 계산해 두고(베이크), 그 위에서 경로를 찾고(A* + 줄 당기기), 여럿이 서로 비켜 걷게(군중 회피) 합니다.
비교 기준은 언리얼 `ARecastNavMesh` · `UNavigationSystemV1` · `UCrowdManager`, 유니티 `NavMeshSurface` · `NavMeshAgent` · `NavMeshObstacle` 입니다 —
둘 다 Recast & Detour 위에 서 있고, 이 엔진도 같은 라이브러리를 엔진 쪽 인터페이스 뒤에 감쌌습니다. 격자 내비게이션(RTS · 도시 건설)은 따로
`GameFramework/Base/Navigation`(`NavGrid` · `NavAgent`)에 있고, 둘은 공통 이동 창구(`INavMover`)로 묶입니다.

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
    <NavAgentTypeDef _name="Humanoid" _radius="0.4" _height="1.9" _maxClimb="0.45" _maxSlope="0.785398" _cellSize="0.2" _cellHeight="0.1" _tileSize="48" ... />
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

시험: `NavMeshBakeTest`(`Test/EngineTest/Navigation/TestNavMeshBake.cpp` — 바닥 · 계단 · 경사 · 구멍 · 쿠킹본 왕복 · 표), `NavMeshQueryTest`(돌아가는 경로 · 닿을 수
없는 섬 · 레이캐스트 · 영역 비용), `NavMeshCrowdTest`(복도에서 마주 오는 둘이 비켜 감).

## 7. 씬의 내비게이션 — `SceneNavigation`(Object, 티어 6)

`GameObjectManager` 가 `ScenePhysics` 처럼 소유만 하고 틱의 한 줄(`GT.Scene.tick.navigation` — 물리 앞 틱 결과 적용 뒤, 애니메이션 · 물리 앞)을 정합니다.
에이전트 종류마다 내비메시 · 군중 · 베이크 입력 사본을 듭니다(언리얼 `UNavigationSystemV1` + `ARecastNavMesh` + `UCrowdManager` 를 씬 단위로).

| 컴포넌트 | 하는 일 |
|---|---|
| `NavMeshSurfaceComponent` | 어느 에이전트 종류를 · 어떤 기하(`RenderMeshes` · `PhysicsColliders` · `Both`)로 · 어디까지(`_boundsHalfExtents`) 베이크하는지. 빼는 태그(`_listExcludeTag`). 유니티 `NavMeshSurface` 자리 |
| `NavMeshModifierComponent` | 이 오브젝트와 자식을 베이크에서 빼거나(`_bIgnoreFromBuild`) 영역을 준다(`_areaName`). 가장 가까운 조상이 이긴다. 유니티 `NavMeshModifier` |
| `NavMeshAgentComponent` | 목적지 → 경로 → 군중 조향. 오브젝트를 옮기거나 같은 오브젝트의 `CharacterControllerComponent` 에 원하는 속도를 넘긴다. 공통 이동 창구 `getMover()` |
| `NavMeshObstacleComponent` | 움직이는 장애물(상자 · 원기둥 발자국)을 몸 반지름만큼 넓혀 뚫는다. 움직이면 닿은 타일만 재베이크 |

**기하 모으기**(`SceneNavigation::collectGeometry` — 런타임 베이크 · 쿠킹이 같은 함수): 켜진 오브젝트의 보이는 `MeshComponent`(스킨드 제외) · Static
`RigidBodyComponent` 셰이프(물리와 같은 배율 · 자세). 움직이는 것이 든 계층(에이전트 · 캐릭터 컨트롤러 · 스킨드 메시 · Static 이 아닌 강체)은 통째로 뺀다 —
베이크하는 순간 그 자리에 있던 캐릭터 · 손에 든 무기가 바닥을 뚫지 않게. 메시 · 강체로 모양을 알 수 없는 오브젝트(파괴 — 붙어 있는 조각만)는 `INavGeometrySource`
를 등록해 기하를 직접 낸다.

**처음 쓸 때.** 플레이 중인 첫 갱신이 표면이 맡은 종류를 마련합니다 — 쿠킹본(`<씬>.navmesh`)이 있고 베이크 값 해시 · 입력 해시가 맞으면 끼우고(쇼케이스
16 타일 0.03 ms), 아니면 모든 타일을 워커로 베이크합니다(같은 씬 Debug 8 ms). 에이전트가 처음 등록될 때 그 종류가 없으면 그때 마련합니다. 시험 · 도구는
`ensureNavMesh` 로 지금 마련합니다. 질의(`findPath` · `findNearestPoint` · `raycast`)는 베이크한 종류에 대해 아무 틱에서나 부를 수 있습니다.

**에이전트 갱신 순서**(게임 스레드): 요청(목적지 · 멈춤 · 순간이동 — 에이전트의 틱이 적어 둔 것)과 자리(컨트롤러가 옮긴 자리, 또는 바깥이 0.5 m 넘게 옮긴
자리 = 순간이동)를 군중에 넣는다 → 종류마다 군중 `update` → 결과를 쓴다(컨트롤러면 `setMoveVelocity`, 아니면 자리 · 진행 방향 요). 목적지에서
`_stoppingDistance` 안이면 `Arrived` 이고 군중의 목표를 지운다.

**쿠킹.** `App --cook-scenes` 가 표면이 놓인 씬마다(글에 `NavMeshSurfaceComponent` 가 든 씬만 세운다) 플레이를 시작하지 않은 채 세워 `makeCookedEntry` 로
베이크하고 `<cooked-dir>/<씬 경로의 .navmesh>` 를 씁니다(`SceneNavigationCooker`). 팩 쿠커가 스테이징 폴더째 팩에 싣습니다. 장애물 · 플레이가 세운 것은 들지 않습니다.

시험: `NavMeshAgentTest`(상자 더미를 돌아 도착 · 캐릭터 컨트롤러로 · 수정자 빼기 · 영역), `NavMeshCookTest`(쇼케이스 — 쿠킹 타일 = 런타임 타일, 쿠킹본 끼우기, 낡은 것 버리기).

## 8. 동적 변경 — 장애물 · 파괴의 타일 재베이크

언리얼의 동적 내비메시(타일 재생성)와 같은 방식입니다 — TileCache(압축 층) 대신 닿은 타일만 처음부터 다시 베이크합니다. 타일 하나(48 셀)는 수 ms 라
워커에 넘기면 게임 스레드가 서지 않습니다.

1. **알림.** `NavMeshObstacleComponent` 는 갱신마다 자세를 보고 생김 · 사라짐 · 문턱(`_obstacleMoveThreshold`, 기본 0.25 m)보다 큰 이동 · 0.1 rad 넘는 회전이면 옛 ·
   새 발자국을 알립니다. 파괴(`FractureComponent`)는 붙어 있는 조각이 바뀔 때(처음 쪼갬 · 붙은 그룹이 갈라지거나 떨어짐 — `onAnchoredShapeChanged`) 자기 경계를
   알립니다. 수정자 속성을 바꿔도 알립니다. `invalidateArea` 는 아무 스레드에서 부를 수 있습니다(스핀 잠금 아래 쌓는다).
2. **입력 사본.** 정적 기하가 바뀐 알림이면 기하를 다시 모아 새 사본을 만들고, 장애물만 바뀌었으면 장애물 부피 목록만 새로 만듭니다. 둘 다 `shared_ptr<const>`
   라 베이크하는 중인 일은 옛 사본을 끝까지 읽습니다.
3. **줄 → 워커.** 알린 상자에 닿는 타일(베이크 테두리만큼 넓혀)을 줄에 넣고, 갱신마다 `_maxConcurrentTileBakeCount` 개까지 태스크로 넘깁니다(같은 타일은 하나씩 —
   베이크하는 중에 또 바뀐 타일은 줄에 남아 끝난 뒤 새 입력으로 다시 간다).
4. **끼우기.** 다음 갱신의 시작에 끝난 결과를 `replaceTile` 합니다(컴포넌트 틱이 질의하지 않는 구간). 군중은 다음 `update` 에서 통로가 지나는 폴리곤이 사라졌는지
   보고 경로를 다시 구합니다. 그 사이 에이전트는 옛 타일 위를 걷습니다.

파괴된 벽은 `FractureNavGeometrySource`(`INavGeometrySource`)가 기하를 냅니다 — 온전하면 보통 규칙(메시 · 강체)으로 모으게 두고, 쪼갠 뒤에는 붙어 있는 조각의
삼각형만(쪼갤 때의 오브젝트 자세에 선 정적 바디와 같은 자리). 떨어진 덩어리 · 파편은 움직이는 것이라 들지 않습니다. 시험 · 도구는 `flushTileBakes` 로 줄을
지금 비웁니다.

## 9. 디버그 그리기 — `gv_navDebugDraw`

`-gv_navDebugDraw=<비트>`: 1 폴리곤 테두리(바깥 경계는 진하게, 안쪽 이음은 옅게) · 2 에이전트 경로(모퉁이) · 4 에이전트 실제 속도(초록) · 원한 속도(파랑) ·
8 장애물 발자국 — 이 넷은 선이라 `DebugDrawQueue` 로 내고 편집기 뷰포트(게임 뷰 포함)가 그립니다(물리 디버그와 같은 어댑터 — `EngineLoop`). 16 은 걷는 면(영역 색 · 폴리곤마다 명암)과
에이전트 경로 띠를 메시 하나로 지어 씬의 `NavMeshDebugView` 오브젝트에 겁니다 — 편집기 없는 게임 화면 · `-gv_screenshot` 에도 보입니다(베이크에서는 빠진다). 31 이면 모두.

## 10. 공통 이동 창구 — `INavMover`

`NavMeshAgentComponent::getMover()`(3D 내비메시 + 군중)와 `GameFramework/Base/Navigation/NavGridMover`(격자 A* 행위자)가 같이 구현합니다 — `moveTo` · `stopMoving` ·
상태(`Idle` · `Moving` · `Arrived` · `Failed`) · 속도 · 자리. 행동 트리의 이동 노드 · 감독이 이것만 보면 격자 게임과 3D 게임에서 같은 코드로 걷습니다.

시험: `NavMeshDynamicTest`(장애물 — 닿은 타일만 재베이크 · 문턱 아래 그대로 · 비키면 곧은 길, 기하 소스 바꿈, 파괴 쇼케이스의 벽), `NavigationTest.GridMoverSpeaksTheCommonMoverInterface`,
`NavMeshBenchTest`(베이크 · 질의 1000 · 군중 100/500 — Release 로 읽는다).

## 11. 상용 엔진과 다른 곳 · 남은 것

- 오프 메시 링크(사다리 · 점프 · 문)와 그 애니메이션이 없습니다(Detour 는 지원 — 베이크 입력과 군중의 오프 메시 상태를 열어야 한다).
- 지형(`Environment` 높이장) · 식생은 아직 베이크 기하가 아닙니다 — 높이장을 삼각형으로 내는 `INavGeometrySource` 가 필요하다.
- 타일 하나를 통째로 다시 베이크합니다. 장애물이 아주 많이 자주 움직이면 TileCache(압축 층 + 장애물 칠하기)가 더 쌉니다.
- 경로 다듬기는 줄 당기기(모퉁이)까지입니다 — 곡선 다듬기 · 경로 비용 미리보기(언리얼 `FNavPathPoint` 의 영역 표시)는 없습니다.
- 에디터의 내비메시 보기 · 베이크 버튼은 없습니다(`gv_navDebugDraw` 로 본다).
