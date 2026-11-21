# TickPlacement

> 결론: 틱은 자기 오브젝트만 읽고 쓴다. 찾기는 `ComponentRegistry`, 오브젝트 사이 순서는 서브틱 선행 조건, 씬 전체를 한 번 봐야 하는 일은 프레임 시스템, 구조 변경 · 남의 오브젝트 쓰기만 틱 뒤 큐다. 씬 전체 순회는 열쇠(`SceneIterationKey`)를 받아야 해 틱 코드에서는 컴파일되지 않는다(형식 강제).

## Frame order

Frame order is one table, `Engine/Object/GameObject/Tick/SceneFrameStepList.xxx`: `FrameSystems` → `TickPrePhysics` (one object per worker) → `ApplyPrePhysics` (deferred structure → transform writes → post-tick queue) → `Physics` (fixed-step groups: register with `registerFixedStep<&T::method>( manager, group )` in `onRegister`) → `TickPostPhysics` → `ApplyPostPhysics`. **Objects in a group run in no defined order.**

## Where a job goes

Stop at the first yes.

1. **It reads and writes only its own object** → `onTick` (or a sub-tick for another group or phase). Use the `TickContext`: `getSelf()` is the one writable object, `getManager()` is read-only; write others through `context.deferPostTickOnComponent( *this, []( T& self, GameObjectManager& manager ) { … } )`. Never call `getMutableManager()` from a tick or a helper it calls.
2. **It must run after another object's tick this frame** → a sub-tick with a prerequisite: `registerSubTick( group, id )` + `addSubTickPrerequisite( id, other.getTickHandle(), kind )`. Kind `ReadsWrites` when it reads a transform the other wrote this frame (barrier), `OrderOnly` otherwise. A per-scene producer publishes its handle and readers bind it.
   - Pick a tick group only for its meaning relative to physics, never to come after a peer in the same phase — that is a prerequisite.
   - Phases (`TickPhase`) order items inside one object, not objects.
3. **It finds other components** → `ComponentRegistry`: register in `onRegister`/`onUnregister`, read `getAll<T>()`. Never `forEachGameObject` + `getComponent` or `forEachComponentOfType` on a frame path.
4. **Pick the mode by what the tick is** → `REFLECT( TickMode = Batch|Task|Main )`. `Batch` (default): waves of non-virtual calls per type. `Task`: few or uneven ticks (planners, solves, game modes). `Main`: main-thread-only APIs (input, UI, render queue).
5. **It looks at the whole scene once per frame** → an `ISceneFrameSystem` attached by the first user and removed by the last, reads/writes declared in `getFrameAccess`. `ISceneParallelFrameSystem` when it touches only its own members, else `ISceneMainFrameSystem`. Many readers share one build. A spatial lookup is a `SceneSpatialGrid` layer the owner fills in `onRegister`, not a frame system.
6. **It changes structure or writes another object** (create, destroy, re-parent, attach, register a primitive, damage someone else) → the post-tick queue, `executeOrDeferPostTickOnComponent( *this, … )` or `executeOrDeferPostTick`.
   - **Do not defer to get an order** — the queue runs in worker order. Read in a later group or use a prerequisite sub-tick.
   - Game-thread-only work (physics queries, camera registry, render state, ability event dispatch) goes through the queue only for an **event**. A type must not defer its whole tick every frame: the tick keeps own-object work and one place walks the components with `GameObjectManager::forEachActiveRegistered<T>`:
     - Engine types → a scene service, `SW_SCENE_SERVICE( Type, ApplyPrePhysics, access )`.
     - GameFramework types → a frame system whose `runMain` puts **one** job on the queue (front of the `ApplyPrePhysics` drain; re-defer for after the late groups).
     - An event-only deferral keeps a one-line reason at the call.
     - A reader needing another type's result earlier makes the order explicit with a "once per frame" call (`GroundSurfaceComponent::sampleGroundOnce`).

## SceneIterationKey

`forEachGameObject`, `forEachComponent`, `forEachComponentOfType` and `getAllGameObjects` take a `SceneIterationKey` component code cannot make. Frameworks that own scene iteration pass it in (automation watches, `SW_DEV_SCENE_COMMAND`, debug HUD sections, save); engine owners are its friends with reasons; tests use `SceneIterationTestAccess`. A public helper that iterates the scene takes the key as a parameter. Own-object iteration (`getOwner()->forEachComponentOfType`) needs no key.
