# Spatial indexes

`Engine/Spatial` is an **opt-in** query toolkit (`SpatialHashGrid2D`, `SpatialQuadTree` / `SpatialOctree` over the shared `SpatialTree`, `BVHTree3D`).
It is not the physics broadphase.

`PhysicsWorld` keeps its own 3D cell map for AABB overlap because physics needs layer filters and CCD that these general indexes do not own. Gameplay or tools that need a standalone spatial query (overworld picks, editor overlays) should instantiate these types directly rather than going through `PhysicsWorld`.

## Query out-parameters are overwritten, never appended to

Every `query*` here clears its `outList*` before filling it, including when the index is empty and the
call has nothing to do — the same contract as `PhysicsWorld`. Callers reuse one vector across frames, so
"the tree is empty, leave the vector alone" would read back as last frame's answer.
Caution for tests: pass a vector that already holds stale entries, or an append bug goes unnoticed.

## BVH queries share one traversal; the frustum is `Core/Math/Frustum`

`BVHTree3D`'s four queries (box · ray · sphere · frustum) walk the tree through one
`collectOverlapping( predicate )` and differ only in the overlap test — adding a query shape is one predicate,
not another twenty-line stack walk. The frustum planes come from `Frustum::fromViewProjection`, the same
extraction the renderer uploads for GPU culling (`RenderView::_frustum`), so a CPU pick and a GPU cull cannot
disagree about what the camera sees.
