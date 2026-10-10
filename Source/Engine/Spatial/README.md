# Spatial — 공간 인덱스

## 이것은 무엇이고 왜 있나

"이 점 근처에 무엇이 있나", "이 광선에 무엇이 걸리나" 같은 질문에 물체 전부를 훑지 않고 답하려면 공간 인덱스가 필요합니다.
이 폴더에는 그런 인덱스가 네 가지 있고, 쓰는 쪽이 필요한 것을 골라 직접 만듭니다. 언리얼의 `TOctree2` 처럼 범용 자료 구조로 쓰는 것들입니다.

이 인덱스는 물리 엔진의 광역 판정(broadphase)이 아닙니다. `PhysicsWorld` 는 자기 용도의 3D 셀 맵을 따로 가지고 있습니다.
물리에는 충돌 레이어 필터와 연속 충돌 판정(ContinuousCollision)이 필요한데, 범용 인덱스는 그 기능을 갖지 않기 때문입니다.
게임 로직이나 도구가 물리와 상관없는 공간 질의를 원하면 `PhysicsWorld` 를 거치지 않고 이 폴더의 타입을 씁니다. 오버월드에서 클릭한 물체를 찾거나 에디터 오버레이를 그릴 때가 그런 경우입니다.

## 머릿속 그림

| 타입 | 차원 | 구조 | 이럴 때 씁니다 |
|---|---|---|---|
| `SpatialHashGrid2D` | 2D | 균일 셀 해시 | 점이나 작은 상자가 많고 매 틱 움직일 때 |
| `SpatialQuadTree` | 2D | 사분 트리 | 크기가 제각각인 2D 영역 |
| `SpatialOctree` | 3D | 팔분 트리 | 크기가 제각각인 3D 영역 |
| `BVHTree3D` | 3D | 동적 BVH | 광선, 구, 절두체 질의가 많은 3D 물체 |

`SpatialQuadTree` 와 `SpatialOctree` 는 공용 템플릿 `SpatialTree` 하나에서 나옵니다. 차원만 다르고 삽입, 분할, 질의 코드는 같습니다.

**핸들.** `SpatialHashGrid2D` 와 `BVHTree3D` 는 원소를 `SlotHandle` 로 구분합니다. 인덱스는 핸들과 경계 상자만 보관하고, 핸들이 가리키는 실제 물체는 쓰는 쪽이 관리합니다.

**질의 결과는 덮어씁니다.** 모든 `query*` 함수는 결과를 채우기 전에 출력 벡터를 비웁니다. 이유는 "함정과 주의"에 있습니다.

## 따라 해 보기 — 반경 안의 이웃 찾기

XZ 평면의 점 엔티티를 2D 해시 그리드에 넣고, 한 점 주변 반경 안의 엔티티를 찾습니다. NetMmo 키트의 관심 영역이 이 방식을 씁니다.

<!-- snippet: SpatialHashGrid2D 에 점을 넣고 queryCircle 로 이웃 찾기 — 5b U7 에서 대조 -->
```cpp
SpatialHashGrid2D grid{ 16.0f }; // 셀 한 변 16 m

const SlotHandle key = SlotHandle::make( entityId, 1u );
grid.insert( key, position._x, position._z, position._x, position._z ); // 점은 최소와 최대가 같은 상자
// 매 틱 위치가 바뀌면
grid.update( key, position._x, position._z, position._x, position._z );

vector<SlotHandle> listNear;
grid.queryCircle( center._x, center._z, 30.0f, listNear ); // listNear 는 먼저 비워진다
```

결과는 핸들 순서로 나옵니다. 결과 순서에 따라 동작이 달라지는 코드라면 직접 정렬해야 합니다.
엔티티가 사라지면 `remove` 로 빼고, 씬을 비울 때는 `clear` 를 부릅니다.

## 작동 원리

**BVH 질의는 순회 하나를 공유합니다.** `BVHTree3D` 의 네 질의(`queryAABB`, `queryRay`, `querySphere`, `queryFrustum`)는 모두 내부 함수 `collectOverlapping` 하나로 트리를 돌고, 겹침 판정만 다르게 넘깁니다.
그래서 새 질의 형태를 더할 때는 판정 함수 하나만 쓰면 됩니다. 스택 순회를 새로 쓰지 않습니다.

**절두체 질의는 렌더러와 같은 평면을 씁니다.** `queryFrustum` 은 뷰 프로젝션 행렬에서 `Frustum::fromViewProjection` 으로 평면을 뽑습니다.
렌더러가 GPU 컬링에 넘기는 `RenderView::_frustum` 도 같은 함수로 만들므로, CPU에서 고른 물체와 GPU가 그리는 물체가 어긋나지 않습니다.

**해시 그리드의 상한.** 해시 그리드에는 상한이 두 개 있습니다. 둘 다 `SpatialHashGrid2D.h` 의 상수 주석에 이유가 적혀 있습니다.

- 한 핸들이 덮는 셀이 `kMaxHandleCellCount`(1024)를 넘으면 셀에 흩뿌리지 않고 별도 목록에 모읍니다. 질의는 그 목록을 항상 함께 봅니다. `AABB2D::infinite()` 같은 큰 상자가 셀 수천만 개를 요구하는 일을 막습니다.
- 한 질의가 훑을 셀이 `kMaxQueryCellCount`(4096)를 넘으면 셀 대신 등록된 핸들 전부를 훑습니다. 셀 수가 핸들 수보다 많아지면 그리드를 도는 쪽이 오히려 느리기 때문입니다.

**경계만 바뀌는 업데이트.** `SpatialHashGrid2D::update` 는 새 경계가 덮는 셀 범위가 지금과 같으면 셀 목록을 건드리지 않고 경계만 바꿉니다.
매 틱 조금씩 움직이는 점 엔티티는 대부분 이 경우에 해당합니다. `PhysicsWorld` 도 같은 방법을 씁니다.

## 함정과 주의

- **결과 벡터에 덧붙는다고 기대하지 마세요.** 모든 `query*` 는 채우기 전에 `outList*` 를 비웁니다. 인덱스가 비어 있어서 할 일이 없을 때도 비웁니다.
  쓰는 쪽은 보통 벡터 하나를 매 프레임 다시 씁니다. 빈 인덱스에서 벡터를 그대로 두면 지난 프레임의 결과를 이번 결과로 읽게 됩니다. `PhysicsWorld` 의 질의도 같은 규칙을 따릅니다.
  이 동작을 테스트할 때는 원소가 이미 들어 있는 벡터를 넘겨야 덧붙이는 결함이 드러납니다(`SpatialTest.QueriesOverwriteTheOutListInsteadOfAppending`).
- **2D 근접 질의는 `SpatialHashGrid2D` 하나로 합니다.** 키트마다 비슷한 그리드를 새로 만들지 않습니다.
  예외는 RTS 키트의 버킷(`RtsWorld` 의 `_listBucketHead` 와 `_listBucketNext`)입니다. 이 버킷은 시뮬레이션 스텝마다 다시 만들고, 결과 순서가 자동 목표 선택과 채취, 밀어내기 결과에 영향을 줍니다. 그래서 이 그리드로 옮기지 않습니다.

## 더 볼 곳

- 테스트: `Test/EngineTest/Spatial/TestSpatial.cpp`
- 쓰는 곳: `GameFramework/Kits/Feature/Network/NetMmo/MmoReplicator`(해시 그리드), `Engine/Character/Fit/SurfaceBvh`(BVH)
- 상위 문서: [Engine/README.md](../README.md)
