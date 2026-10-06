# Spatial — 공간 색인

`Engine/Spatial` 은 **골라 쓰는** 질의 도구 모음입니다 — `SpatialHashGrid2D`, 공용 `SpatialTree` 위의 `SpatialQuadTree` · `SpatialOctree`, `BVHTree3D`.
물리의 광역 판정(broadphase)이 아닙니다.

`PhysicsWorld` 는 AABB 겹침용 3D 셀 맵을 따로 둡니다 — 물리에는 레이어 거르개와 연속 충돌(ContinuousCollision)이 필요하고, 이 범용 색인은 그것을 갖지 않습니다.
게임플레이 · 도구가 독립된 공간 질의를 원하면(오버월드 집기 · 에디터 오버레이) `PhysicsWorld` 를 거치지 않고 이 타입을 직접 만듭니다.

## 질의 출력 인자는 덮어쓴다 — 덧붙이지 않는다

모든 `query*` 는 채우기 전에 `outList*` 를 비웁니다. 색인이 비어 할 일이 없을 때도 비웁니다 — `PhysicsWorld` 와 같은 계약입니다.
호출부가 벡터 하나를 프레임마다 다시 쓰므로, "트리가 비었으니 벡터를 그대로 둔다" 는 지난 프레임의 답으로 읽힙니다.
시험의 주의: 낡은 원소가 든 벡터를 넘겨야 덧붙이는 결함이 드러납니다.

## BVH 질의는 순회 하나를 나눠 쓴다 — 절두체는 `Core/Math/Frustum`

`BVHTree3D` 의 질의 넷(상자 · 광선 · 구 · 절두체)은 `collectOverlapping( predicate )` 하나로 트리를 걷고 겹침 판정만 다릅니다 — 질의 모양을 하나 더하는 것은
판정 하나를 더하는 일이지 스택 순회를 또 쓰는 일이 아닙니다. 절두체 평면은 `Frustum::fromViewProjection` 에서 오고, 렌더러가 GPU 컬링에 올리는 것(`RenderView::_frustum`)과
같은 추출이라 CPU 집기와 GPU 컬링이 카메라가 보는 것을 다르게 판단하지 않습니다.
