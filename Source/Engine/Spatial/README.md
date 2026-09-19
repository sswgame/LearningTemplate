# Spatial indexes

`Engine/Spatial` is an **opt-in** query toolkit (2D hash grid, quadtree, octree, BVH). It is not the physics broadphase.

`PhysicsWorld` keeps its own 3D cell map for AABB overlap because physics needs layer filters and CCD that these general indexes do not own. Gameplay or tools that need a standalone spatial query (overworld picks, editor overlays) should instantiate these types directly rather than going through `PhysicsWorld`.

## Query out-parameters are overwritten, never appended to

Every `query*` here clears its `outList*` before filling it, including when the index is empty and the
call has nothing to do. Callers reuse one vector across frames, so "the tree is empty, leave the vector
alone" reads back as last frame's answer. `SpatialHashGrid2D` and `PhysicsWorld` already cleared;
`BVHTree3D` and `SpatialTree` appended, and `BVHTree3D` returned early without touching the vector at
all — the three families disagreed and no test noticed, because each test passes a fresh vector.
