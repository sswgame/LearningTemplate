# 공간 분할 및 가속 구조 (Spatial Acceleration Guide)

이 문서는 초보 개발자와 기여자를 위해 **공간 분할(Spatial Partitioning)**의 기본 원리, 수학적 배경, 알고리즘, 그리고 SW Engine에서 제공하는 **2D Spatial Hash Grid**와 **3D BVHTree**의 사용법을 상세히 설명합니다.

---

## 1. 왜 공간 분할(Spatial Partitioning)이 필요한가?

### 1.1 무차별 대입(Brute-Force)의 한계
게임 씬에 $N$개의 오브젝트(몬스터, 총알, 장애물, 파티클 등)가 있을 때, "내 캐릭터 주변 5m 안의 몬스터는 누구인가?" 또는 "이 총알이 어떤 몬스터에 맞았는가?"를 알아내기 위해 모든 오브젝트를 하나씩 비교한다면 다음과 같은 비용이 발생합니다:

$$\text{비교 횟수} = O(N^2)$$

- 오브젝트가 **100개**일 때: 약 **10,000번** 비교 (CPU에서 순식간에 처리 가능)
- 오브젝트가 **10,000개**일 때: **1억 번(100,000,000번)** 비교 $\rightarrow$ **심각한 프레임 드랍 발생!**

### 1.2 공간 분할의 핵심 아이디어
공간을 구역별로 나누어 **"물리적으로 멀리 떨어진 오브젝트는 아예 검사 대상에서 제외"**함으로써 검색 범위를 획기적으로 줄입니다.

```
[Brute-Force (무차별 대입)]          [공간 분할 (Spatial Partitioning)]
전체 10,000개 전수 조사 (O(N))       해당 구역 내 3~5개만 검사 (O(1) ~ O(log N))
┌─────────────────────────┐         ┌────┬────┬────┬────┐
│ •   •      •   •     •  │         │ •  │    │ •  │    │
│    •    ★(나)   •     • │   ==>   ├────┼────┼────┼────┤
│ •    •      •      •    │         │    │ ★(나)  │    │
│   •      •    •      •  │         ├────┼────┼────┼────┤
└─────────────────────────┘         │ •  │    │ •  │ •  │
                                    └────┴────┴────┴────┘
```

SW Engine은 **2D 평면용 해시 그리드**와 **3D 공간용 동적 BVH 트리**의 2가지 최적화 구조체를 제공합니다.

---

## 2. 2D 공간 해시 그리드 (`SpatialHashGrid2D`)

[SpatialHashGrid2D.h](file:///d:/Projects/Personal/LearningTemplate/Source/Engine/Scene/SpatialHashGrid2D.h)는 2D 탑다운 뷰, 사이드뷰 플랫폼, 대규모 탄막 슈팅 등 **2D 평면 상에서 균일하게 분포된 오브젝트**를 초고속 $O(1)$로 검색하기 위한 구조체입니다.

### 2.1 동작 원리
1. **격자(Grid Cell) 분할**: 전체 월드를 일정한 크기(`cellSize`, 기본 64픽셀)의 바둑판 모양 격자로 나눕니다.
2. **좌표 $\rightarrow$ 셀 변환**:
   $$\text{cellX} = \lfloor x / \text{cellSize} \rfloor, \quad \text{cellY} = \lfloor y / \text{cellSize} \rfloor$$
3. **64비트 정수 키 패킹**: 셀 좌표 `(cellX, cellY)`를 64비트 단일 정수 키로 조합하여 해시 테이블(`unordered_map`)에 저장합니다.
   $$\text{Key} = (\text{cellX} \ll 32) \mid (\text{cellY} \ \& \ \text{0xFFFFFFFF})$$
4. **오브젝트 등록**: 오브젝트의 2D 바운딩 박스(AABB)가 걸쳐있는 모든 셀에 해당 `Entity` 핸들을 추가합니다.

```
      Cell (0,1)        Cell (1,1)
    ┌────────────────┬────────────────┐
    │                │   [Entity B]   │
    │                │    ┌─────┐     │
    │                │    │     │     │
────┼────────────────┼────┴─────┴─────┼────
    │        ┌───────┼───────┐        │
    │        │       │       │        │
    │        │   [Entity A]  │        │
    │        └───────┼───────┘        │
    │                │                │
    │   Cell (0,0)   │   Cell (1,0)   │
    └────────────────┴────────────────┘
  * Entity A는 (0,0), (1,0), (0,1), (1,1) 4개 셀에 동시 등록됩니다.
```

### 2.2 고속 쿼리 알고리즘
- **AABB 범위 쿼리 (`queryAABB`)**: 검색 영역이 걸치는 셀들만 순회하며 $O(1)$로 후보 엔티티 추출.
- **원형 반경 쿼리 (`queryCircle`)**: 원의 중심과 엔티티 AABB의 최근접점 거리를 계산하여 범위 내 엔티티 선별.
  $$\text{dist}^2 = (x - \text{clamp}(x, \min_x, \max_x))^2 + (y - \text{clamp}(y, \min_y, \max_y))^2 \le r^2$$
- **DDA 레이캐스트 (`queryRay`)**: 광선(Ray)이 지나가는 격자 선을 DDA(Digital Differential Analyzer) 방식으로 한 칸씩 전진하며 충돌 검사.

### 2.3 사용 예제 코드 (C++)
```cpp
#include "Engine/Spatial/SpatialHashGrid2D.h"

// 1) 64픽셀 단위의 그리드 생성
sw::SpatialHashGrid2D grid{ 64.0f };

// 2) 엔티티 등록 (Entity, minX, minY, maxX, maxY)
grid.insert( playerEntity, 100.0f, 100.0f, 132.0f, 132.0f );
grid.insert( monsterEntity, 120.0f, 110.0f, 150.0f, 140.0f );

// 3) 특정 반경(반경 50px) 내의 엔티티 검색
sw::vector<sw::Entity> nearbyEnemies;
grid.queryCircle( 100.0f, 100.0f, 50.0f, nearbyEnemies );

// 4) 엔티티 이동 시 업데이트
grid.update( playerEntity, 105.0f, 100.0f, 137.0f, 132.0f );

// 5) 엔티티 사망/제거 시
grid.remove( monsterEntity );
```

---

## 3. 3D 동적 바운딩 볼륨 계층 (`BVHTree3D`)

[BVHTree3D.h](file:///d:/Projects/Personal/LearningTemplate/Source/Engine/Scene/BVHTree3D.h)는 **3D 공간, 넓은 야외 씬, 오브젝트 크기/밀도가 불규칙한 환경**에서 최고의 검색 성능을 제공하는 3차원 이진 트리(Binary Tree) 가속 구조체입니다.

### 3.1 계층 구조 원리
- **리프 노드 (Leaf Node)**: 실제 게임 엔티티와 그 엔티티의 3D AABB 바운딩 박스를 담고 있습니다.
- **내부 노드 (Internal Node)**: 두 자식 노드를 완벽히 감싸는 최소 크기의 결합된 AABB(Enclosing AABB)를 가집니다.

```
                 [Root AABB]
              /               \
       [Node 1 AABB]      [Node 2 AABB]
       /          \        /          \
   [Leaf A]    [Leaf B] [Leaf C]    [Leaf D]
 (Entity 1)  (Entity 2) (Entity 3) (Entity 4)
```

어떤 광선(Ray)이나 시야 절두체(Frustum)가 `Node 1`의 AABB와 겹치지 않는다면, **하위의 `Leaf A`와 `Leaf B`는 검사조차 하지 않고 한 번에 건너뜁니다(Pruning).**

### 3.2 SAH (Surface Area Heuristic, 표면적 휴리스틱) 삽입
트리에 새로운 오브젝트를 삽입할 때, 어디에 붙여야 전체 트리의 검색 성능이 가장 좋을지 결정하기 위해 **SAH 표면적 비용 함수**를 사용합니다:

$$\text{Cost} = 2 \times \text{Area}(\text{CombinedAABB}) + \text{InheritanceCost}$$

> **왜 체적(Volume) 대신 표면적(Surface Area)을 쓸까요?**  
> 기하학적으로 임의의 직선(Raycast)이나 움직이는 물체가 3D 박스와 충돌할 확률은 박스의 부피가 아니라 **표면적(Surface Area)**에 정확히 비례하기 때문입니다.

### 3.3 AVL 스타일 자가 균형 (Self-Balancing Tree Rotations)
오브젝트가 한쪽 구역에 계속 추가되면 트리가 한 줄로 길어지는 **편향 트리(Degenerate Tree, $O(N)$)**가 되어 성능이 떨어집니다. `BVHTree3D`는 노드 삽입/삭제 시 좌우 자식의 높이 차이(Balance Factor)를 검사하여 자동으로 회전(Rotation)시킴으로써 항상 $O(\log N)$의 균형 잡힌 높이를 유지합니다.

```
       A (높이 3)                     C (높이 2)
      / \                           /   \
     B   C (높이 2)      ===>       A     G
        / \                        / \
       F   G                      B   F
   [회전 전: 오른쪽 치우침]         [좌회전 후: 완벽한 균형]
```

### 3.4 지원하는 4대 고속 3D 공간 쿼리

| 쿼리 종류 | 함수명 | 사용 알고리즘 및 원리 | 주요 사용처 |
| :--- | :--- | :--- | :--- |
| **3D AABB 박스** | `queryAABB` | 3차원 축별 겹침($\min \le \max$) 검사 | 물리 Broadphase 충돌 감지, 범위 폭발 공격 |
| **3D 광선 투사** | `queryRay` | **Slab Method (슬랩 교차 검사)**: $t_{\min}, t_{\max}$ 구간 교차 계산 | 총기 히트스캔 사격, 마우스 피킹(클릭 선택) |
| **3D 구형 반경** | `querySphere` | 중심점과 AABB 최근접점 거리($\Delta x^2 + \Delta y^2 + \Delta z^2 \le r^2$) | 사운드 전파 범위, 몬스터 인식 반경 |
| **시야 절두체** | `queryFrustum` | 6개 평면(Left/Right/Top/Bottom/Near/Far) 부호 검사 | **카메라 시야 밖 렌더링 컬링 (Zero DrawCall)** |

### 3.5 사용 예제 코드 (C++)
```cpp
#include "Engine/Spatial/BVHTree3D.h"

// 1) 3D BVH 트리 생성
sw::BVHTree3D bvh;

// 2) 3D 바운딩 박스를 가진 엔티티 등록
sw::AABB enemyBounds{ { 10.0f, 0.0f, 50.0f }, { 12.0f, 2.0f, 52.0f } };
bvh.insert( enemyEntity, enemyBounds );

// 3) 총기 사격 레이캐스트 (시작점, 발사방향, 최대거리)
sw::vector<sw::Entity> hitResults;
bvh.queryRay( sw::float3{ 0.0f, 1.5f, 0.0f }, sw::float3{ 0.2f, 0.0f, 1.0f }, 100.0f, hitResults );

// 4) 카메라 시야 절두체 컬링 (View-Projection 행렬 전달)
sw::vector<sw::Entity> visibleEntities;
bvh.queryFrustum( cameraViewProjMatrix, visibleEntities );
```

---

## 4. 2D Spatial Hash Grid vs 3D BVHTree 비교 요약

| 비교 항목 | `SpatialHashGrid2D` | `BVHTree3D` |
| :--- | :--- | :--- |
| **차원** | 2D / 2.5D 평면 | 3D 입체 공간 |
| **적합한 씬** | 오브젝트 크기가 균일하고 밀도가 일정한 씬 | 야외 씬, 크기/밀도가 불규칙한 3D 씬 |
| **검색 시간 복잡도** | $O(1)$ (상수 시간) | $O(\log N)$ (로그 시간) |
| **메모리 할당** | 0-Alloc 버킷 재사용 | 0-Alloc 고정 풀 및 비재귀 스택 순회 |
| **동적 이동 비용** | 셀 간 재해싱 ($O(1)$) | 트리 노드 리핏 및 회전 ($O(\log N)$) |

---

## 5. 자주 묻는 질문 (FAQ)

### Q1. 둘 중 어떤 구조체를 선택해야 하나요?
- **2D 탑다운 RPG, 타일맵, 횡스크롤 게임**: `SpatialHashGrid2D`를 사용하세요. 셀 크기만 타일 크기(예: 32px, 64px)에 맞추면 가장 빠릅니다.
- **3D 3인칭 액션, FPS, 복잡한 지형과 다양한 크기의 오브젝트가 있는 씬**: `BVHTree3D`를 사용하세요. 프러스텀 컬링과 정밀 3D 레이캐스트를 동시에 지원합니다.

### Q2. 쿼리 실행 시 메모리 할당(Alloc)으로 인한 GC/렉이 발생하지 않나요?
- 전혀 발생하지 않습니다. `BVHTree3D`와 `SpatialHashGrid2D`는 내부적으로 **비재귀(Non-recursive) 고정 크기 스택 배열**을 사용하여 쿼리 중 힙 메모리 할당을 0으로 억제합니다.
