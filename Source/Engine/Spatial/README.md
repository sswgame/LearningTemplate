# Spatial indexes

`Engine/Spatial` is an **opt-in** query toolkit (2D hash grid, quadtree, octree, BVH). It is not the physics broadphase.

`PhysicsWorld` keeps its own 3D cell map for AABB overlap because physics needs layer filters and CCD that these general indexes do not own. Gameplay or tools that need a standalone spatial query (overworld picks, editor overlays) should instantiate these types directly rather than going through `PhysicsWorld`.
