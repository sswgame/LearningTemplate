# Gameplay

틱은 자기 오브젝트만 읽고 쓴다. 찾기는 `ComponentRegistry`, 오브젝트 사이 순서는 서브틱 선행 조건, 씬 전체를 한 번 봐야 하는 일은 프레임 시스템, 구조 변경 · 남의 오브젝트 쓰기만 틱 뒤 큐다. 씬 전체 순회는 `SceneIterationKey` 가 있어야 해 틱 코드에서 컴파일되지 않는다(형식 강제). 아래 절은 트리거 · 스트리밍 · 네트워크 · 게임 기능의 정한 계약이다.

## Frame order

`Engine/Object/GameObject/Tick/SceneFrameStepList.xxx` 한 표: `FrameSystems` → `TickPrePhysics` (one object per worker) → `ApplyPrePhysics` (deferred structure → transform writes → post-tick queue) → `Physics` (fixed-step groups: `registerFixedStep<&T::method>( manager, group )` in `onRegister`) → `TickPostPhysics` → `ApplyPostPhysics`. **Objects in a group run in no defined order.**

## Where a job goes

Stop at the first yes.

1. **It reads and writes only its own object** → `onTick` (or a sub-tick). `TickContext::getSelf()` is the one writable object, `getManager()` is read-only; write others through `context.deferPostTickOnComponent( *this, []( T& self, GameObjectManager& manager ) { … } )`. Never call `getMutableManager()` from a tick or a helper it calls.
2. **It must run after another object's tick this frame** → `registerSubTick( group, id )` + `addSubTickPrerequisite( id, other.getTickHandle(), kind )`: `ReadsWrites` when it reads a transform the other wrote this frame, `OrderOnly` otherwise. A per-scene producer publishes its handle and readers bind it. Pick a tick group only for its meaning relative to physics, never to come after a peer. Phases (`TickPhase`) order items inside one object, not objects.
3. **It finds other components** → `ComponentRegistry`: put `SW_REGISTERED_COMPONENT( T )` in the class body, read `getAll<T>()` (no mark, no compile). Never `forEachGameObject` + `getComponent` or `forEachComponentOfType` on a frame path.
4. **Pick the mode** → `REFLECT( TickMode = Batch|Task|Main )`: `Batch` (default) waves of non-virtual calls per type; `Task` few or uneven ticks (planners, solves, game modes); `Main` main-thread-only APIs (input, UI, render queue).
   - 주기마다만 할 일은 손 타이머 대신 형식의 `kTickInterval`(주 틱 전체) 또는 `registerSubTick` 의 `interval`(일부 · 인스턴스마다 다른 주기)로 선언한다. 프레임이 주기보다 길면 `TickContext::getIntervalCount` 만큼 돈다. 판의 시작에서 세는 규칙 시간(쿨다운 · 라운드)은 `Countdown` 그대로다.
5. **It looks at the whole scene once per frame** → an `ISceneFrameSystem` attached by the first user and removed by the last, reads/writes declared in `getFrameAccess`. `ISceneParallelFrameSystem` when it touches only its own members, else `ISceneMainFrameSystem`. A spatial lookup is a `SceneSpatialGrid` layer the owner fills in `onRegister`, not a frame system.
6. **It changes structure or writes another object** (create, destroy, re-parent, attach, register a primitive, damage someone else) → `executeOrDeferPostTickOnComponent( *this, … )` or `executeOrDeferPostTick`.
   - **Do not defer to get an order** — the queue runs in worker order. Read in a later group or use a prerequisite sub-tick.
   - Game-thread-only work (physics queries, camera registry, render state, ability event dispatch) goes through the queue only for an **event**. A type must not defer its whole tick every frame: the tick keeps own-object work and one place walks the components with `GameObjectManager::forEachActiveRegistered<T>` — Engine types a scene service (`SW_SCENE_SERVICE( Type, ApplyPrePhysics, access )`), GameFramework types a frame system whose `runMain` puts **one** job on the queue. An event-only deferral keeps a one-line reason at the call. A reader needing another type's result earlier makes the order explicit with a "once per frame" call (`GroundSurfaceComponent::sampleGroundOnce`).
- 병렬 틱이 남을 읽어 단언에 걸리면 `readOther` · 틱 뒤 작업 · 그룹 옮기기로 고친다. 틱 비용 기반 분배는 기본 정책으로 두지 않는다.
- 이미 안에서 나눠 도는 서비스(`MeshLODSystem` · `TextureStreamingSystem` · `SceneAudio` · `SceneVFXProvider` · `NetObjectTable` · `SceneNetLink`)는 서비스 파도에 넣지 않는다.
- 근접 기믹 · 감지 · 넷 관심 격자는 `SceneSpatialGrid` 로 옮기지 않는다(다시 볼 조건: 근접 노드 × 태그 후보가 프레임당 1 만 초과 → 노드에 필수 `range` 칸 + 태그 층 격자, 자극 64 개 넘게 모으는 키트 → `PhysicsCharacter3D` 층 질의). 캐릭터 일괄 이동은 유지한다.

## SceneIterationKey

`forEachGameObject`, `forEachComponent`, `forEachComponentOfType` and `getAllGameObjects` take a `SceneIterationKey` component code cannot make. Frameworks that own scene iteration pass it in (automation watches, `SW_DEV_SCENE_COMMAND`, debug HUD sections, save); engine owners are its friends with reasons; tests use `SceneIterationTestAccess`. A public helper that iterates the scene takes the key as a parameter. Own-object iteration (`getOwner()->forEachComponentOfType`) needs no key.

## 트리거 · 물리

- 겹침은 오브젝트 쌍(`PhysicsOverlapTracker`), 막는 접촉은 바디 쌍. 한 프레임에 끝 먼저 → 시작. 셀 · 하위 레벨 오름은 "안에서 생성" 진입, 내림 · 파괴 · 비활성은 이탈.
- "안에서 생김" 진입은 물리 표시(`OverlapInfo::_bOtherSpawnedInside` — 상대 첫 바디의 첫 스텝)로 가린다. 겹침 길에 프레임 수 창을 두지 않는다(스텝 없는 프레임이 끼면 틀린다). 새 바디 컴포넌트는 첫 바디를 만든 뒤 `markBodyCreated` 를 부른다.
- 활성자 볼륨 파생은 `TriggerAuthority`(Server · Local)를, 지역 모듈 처리기는 `getAuthority` 를 반드시 고른다.
- 지역 효과(용암 · 늪 · 독가스 …)는 모듈 한 줄(`SW_ZONE_MODULE`)로 더한다 — 영역마다 컴포넌트를 늘리지 않는다. 모듈은 서로를 모르고, 반응 컴포넌트는 태그 · 속성만 읽는다.
- 표면 변형 층은 CPU 높이 격자다(도장이 프레임당 수백 개면 컴퓨트로). 충돌 · 내비 · 높이장은 바꾸지 않는다.
- 탈것 · 관성의 바깥 힘은 `InertiaSettings::computeAlongAcceleration` 한 벌을 함께 쓴다. 배는 새 물리 없이 조합한다.

## 전투 · 게임 규칙

- 탄 효과는 탄 × 표면 맞음 표(`ProjectileCatalog::findImpact`)가 정한다 — 탄에 반응 수치를 베끼지 않는다. 무기 호환은 탄 태그 식(`_accepts`).
- 2D 는 평면 제약 3D — 차원마다 관통 · 튕김 · 유도를 두 벌 쓰지 않는다. 걸음 안에서 세계를 바꾸지 않는다(사건은 걸음 뒤). 복제가 생기면 시뮬레이션과 짐(`ProjectileCargo`)은 서버 권위.
- 리스폰: `MechArenaWorld`(비트 동일) · `TrampolineArena` · 카트 관문은 공통 키트로 옮기지 않는다. 목숨 · 시체 · 적 리셋 · 팀 코스트는 장르 규칙, 동료 부활은 `HealthStateMachine`. 클라이언트는 정책 종류 · 지점 id 해시만 보낸다 — 자리 · 비용 · 잔액은 서버가 판정.
- `GameActionCall` · `ActionRegistry` 와 GAS 형 `Ability` 는 합치지 않는다.
- 잔상은 품질 단계별 상한. 분신은 별도 프리팹 · 애니메이션 인스턴스로(포즈 재생형인지 독립 개체인지 먼저 가른다) — 임의 그래프 · 모듈 시스템을 먼저 들이지 않는다.
- 런타임 UI 화면 전환은 화면마다 문서 애니메이션(`Open` · `Close`), 스택 공통 전환은 두지 않는다. 결제 상품(`<Product>`)은 `StorePurchaseFlow` 길이라 상점 화면에 싣지 않는다. 인벤토리 명령의 요청 모양 · 검사는 `InventoryCommand`.
- 시퀀서: 넘치는 이징(Back · Elastic · Bounce)은 들이지 않는다(블렌드 가중치 계약 단조 · 0→0 · 1→1). 하우징 저장 정본은 부지 id · 안정 배치 id · 부품 id/판 · 변환(임시 핸들 금지), MMO 는 소유 · 판정 · 저장을 서버 권위로.
- `RTS` 유닛 슬롯은 `SlotHandleTable` 로 옮기지 않는다(순서 · LIFO 빈 칸이 결정적 결과와 저장 바이트에 든다). Meadow 밭 크기 · 사격장 탄착 표면 목록은 코드 상수.

## 시스템 사이 알림 — 세 길 중 하나

- **필수 일대일 효과**(주는 쪽이 결과를 알아야 하는 것 — 부스트 · 아이템 지급 · 피해)는 받는 쪽 리플렉션 인터페이스(`REFLECT( Interface )` + 포트 멤버, `findInterface<I>()`)로 부른다. 받는 쪽이 없거나 거절하면 결과로 돌려주고 경고한다(조용히 사라지지 않는다). 언리얼 `IAbilitySystemInterface` · `TakeDamage`, 유니티 `GetComponent<IDamageable>()` 자리. 예외: 반경 · 점유자 전체에 거는 지역 피해(폭발 · 용암)는 받는 쪽 없는 대상을 조용히 건너뛴다(언리얼 `ApplyRadialDamage`) — 경고는 지정한 한 대상에만.
- **같은 오브젝트 · 아는 상대의 알림**은 컴포넌트 델리게이트(`MulticastDelegate`, `SW_DELEGATE_*`). Godot 시그널 · 언리얼 컴포넌트 델리게이트 자리.
- **선택적 일대다 전역 알림**(보내는 쪽이 받는 쪽을 모르고, 받는 쪽이 없어도 정상 — 세이브 완료 · 씬 로드 · 상호작용 완료 · 방 클리어 · 쓰러짐 → UI · 퀘스트 · 점수)은 "game" 채널 이벤트 버스(`GameEventUtil::send`). Lyra `UGameplayMessageSubsystem` 자리. 효과가 필수인 일을 버스에만 싣지 않는다 — 받는 쪽이 없을 때 효과가 빠진다.

## 스트리밍 · 네트워크

- 늘 있어야 하는 서버 스폰은 `_bAlwaysLoaded` 를 켜거나 복제 번호를 매긴다.
- 원점 이동: 트랜스폼 밖에 쥔 씬 자리는 `Component::onWorldOriginShifted` 로 옮기고, 저장 · 데이터 · 시뮬레이션의 절대 자리는 쓸 때 `WorldOrigin` 으로 옮긴다. 디버그 그리기 · MMO 절대 자리를 놓는 게임 코드도 같다.
- 클라이언트 → 서버 셀 상태 보고는 두지 않는다(믿을 수 없는 입력의 공격면). 스트리밍 씬 두 프로세스 짝 시나리오는 그런 게임이 생길 때 — 그때까지 `NetMMOStreamingTest`.
- 로드 큐는 에디터 패널을 두지 않는다(개발 명령 · HUD 자리).

## 게임 기능 · 미니게임

- 기능은 Playground 장소 · 미니게임과 시나리오로 확인한다(게임 범위는 [NEXT](../NEXT.md) "지킬 결정"). 키트는 모두 링크하고 게임 기능(`GameFeatureRegistry`)이 등록 · 서비스 바인딩 · 컴포넌트(받는 이름 — `GameFeatureReceiverComponent`)만 켜고 끈다(런타임 모듈 로드 · 언로드 금지).
- 미니게임 카탈로그는 `GameFeatureScope::loadCatalog<T>()` 로 받는다(키트 등록 · `catalogs` 검사 안에 들게).
- 범용 스크립트 VM 대신 `BehaviorTreeAsset` · `ConditionExpr` · `DialogueGraphAsset` · `AnimationGraphPlayer` 에 노드 · 식을 더한다.
