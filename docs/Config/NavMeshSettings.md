<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# NavMeshSettings

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/engine/navigation/navmeshsettings.xml` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `NavMeshSettings` (`ResourceCatalog`) |
| 언제 | 처음 쓸 때 · 내비메시 쿠킹 |
| 배포본 | 엔진 팩에 실림 |
| 커밋 | 한다 |

XML 속성(값 하나) · 자식 원소(목록 · 구조체)의 이름은 아래 칸 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 칸

내비게이션 표 하나입니다. 씬의 내비게이션(`SceneNavigation`)이 처음 쓸 때 읽습니다. 쿠킹본(`.navmesh`)은 베이크에 들어간 값의 해시(`computeAgentTypeHash`)를 함께 적어 두고, 표가 바뀌면 런타임이 다시 베이크합니다.

정본: [`Source/Engine/Navigation/NavMeshSettings.h`](../../Source/Engine/Navigation/NavMeshSettings.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listAgentType` | `vector<NavAgentTypeDef>` | — |  |  | Agent kinds; each bakes its own navmesh |
| `_listArea` | `vector<NavAreaDef>` | — |  |  | Areas in index order (at most 16); index 0 is plain ground |
| `_defaultAgentType` | `hashed_string` | — |  |  | Agent kind used when an agent or surface names none (empty = the first) |
| `_maxConcurrentTileBakeCount` | `uint32` | `4` | 1 ~ - |  | Tile rebakes running on workers at once; more wait for the next frame |
| `_obstacleMoveThreshold` | `float32` | `0.25` | 0.0 ~ - | m | An obstacle has to move this far before the mesh around it rebakes |

## `NavAgentTypeDef`

에이전트 종류 하나 — 이 몸이 걸을 내비메시를 베이크하는 값입니다.

정본: [`Source/Engine/Navigation/NavMeshSettings.h`](../../Source/Engine/Navigation/NavMeshSettings.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Name agents and surfaces pick it by |
| `_radius` | `float32` | `0.4` | 0.0 ~ - | m | Body radius; walls are eroded by this much |
| `_height` | `float32` | `2.0` | 0.0 ~ - | m | Body height; lower ceilings are not walkable |
| `_maxClimb` | `float32` | `0.4` | 0.0 ~ - | m | Highest step the body walks up |
| `_maxSlope` | `float32` | `0.785398` | 0.0 ~ 1.553343 | rad | Steepest walkable slope |
| `_cellSize` | `float32` | `0.2` | 0.01 ~ - | m | Voxel size on XZ (smaller is more exact and slower to bake) |
| `_cellHeight` | `float32` | `0.1` | 0.01 ~ - | m | Voxel size on Y |
| `_tileSize` | `uint32` | `48` | 8 ~ 256 |  | Tile edge in cells; a change rebakes one tile |
| `_minRegionSize` | `uint32` | `4` | 0 ~ - |  | Islands smaller than this many cells (as a square side) are dropped |
| `_mergeRegionSize` | `uint32` | `16` | 0 ~ - |  | Regions smaller than this many cells (as a square side) merge into neighbours |
| `_maxEdgeLength` | `float32` | `12.0` | 0.0 ~ - | m | Longest polygon edge along walls (0 = no limit) |
| `_maxEdgeError` | `float32` | `1.3` | 0.1 ~ - |  | How far the simplified outline may stray from the voxels, in cells |
| `_detailSampleDistance` | `float32` | `6.0` | 0.0 ~ - |  | Height detail sample spacing in cells (0 = no detail) |
| `_detailSampleMaxError` | `float32` | `1.0` | 0.0 ~ - |  | Height detail error in cell heights |
| `_maxCrowdAgentCount` | `uint32` | `512` | 1 ~ - |  | Agents one crowd of this type holds |

## `NavAreaDef`

영역 하나 — 이름과 지날 때의 비용 배율입니다. 표의 순서가 영역 번호(0..15)이고 0 번이 보통 땅입니다.

정본: [`Source/Engine/Navigation/NavMeshSettings.h`](../../Source/Engine/Navigation/NavMeshSettings.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Area name |
| `_cost` | `float32` | `1.0` | 1.0 ~ - |  | Path cost multiplier per metre (1 = plain ground) |
