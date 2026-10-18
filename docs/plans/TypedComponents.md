# 타입별 컴포넌트 목록과 타입 핸들 구현 계획 (2026-10-17)

> 상태: 구현대기 · 선행: ReflectionFormats · 영역: engine · 옛번호: C-49 · 진행: 2/6

[리플렉션 직렬화 형식 계획](ReflectionFormats.md) 9절의 단계 7-c 와 7-d 를 구현할 수 있는 수준으로 나눈 계획입니다.
배경과 판단은 그 문서의 7절(리플렉션 캐스트 비용)에 있고, 여기서는 반복하지 않습니다.
사용자 결정은 끝났습니다. 7-d(타입 핸들)를 도입하고, 7-c(매니저의 타입별 컴포넌트 목록)를 7-d 보다 먼저 합니다.
구현 전에 `design-reviewer` 검토(결론: 고쳐서 진행, 이 문서 끝의 "구현 전에 반영할 검토 결과" ⓐ~ⓗ)를 반영했습니다. 반영한 자리는 본문에 ⓐ 처럼 표시합니다.

이 문서에서 쓰는 말은 다음과 같습니다.

- **매니저 순회**는 `GameObjectManager::forEachComponentOfType<T>` 입니다. 씬 전체에서 `T` 와 그 파생 컴포넌트를 찾습니다.
- **오브젝트 순회**는 `GameObject::forEachComponentOfType<T>` 입니다. 오브젝트 하나의 컴포넌트 목록만 봅니다. 7-c 는 이것을 바꾸지 않습니다.
- **버킷**은 매니저가 보관하는, 정확히 한 타입의 컴포넌트 포인터 목록입니다.
- **타입 핸들**은 `TComponentHandle<T>` 입니다. 지금의 `ComponentHandle`(objectID 와 componentID)에 대상 타입 `T` 를 컴파일 시간 정보로 더한 값 타입입니다.

## 1. 요약

- 7-c 는 ReflectionFormats.md 7.2 의 "그 타입과 조상 타입의 목록"이 아니라 **정확한 타입마다 버킷 하나**를 보관합니다. 조회는 버킷마다 `isDerivedFrom` 을 한 번 묻습니다.
  조상 목록은 컴포넌트마다 조상 깊이만큼의 목록 위치를 보관해야 해서 메모리가 약 여덟 배이고, 추가와 제거도 깊이만큼 일을 합니다(2.2). 이 부분은 7.2 의 문장과 다르므로 확인을 받습니다.
- 7.3 이 적은 "`forEachComponentOfType<` 28 곳"은 지금 48 줄입니다. 매니저 순회 31 줄(Source 18, Test 13), 오브젝트 순회 14 줄, 정의와 주석 3 줄입니다.
  7.3 이 예로 든 `HealthListenerComponent.cpp:15` 와 `FirstPersonCameraComponent.cpp:160` 은 오브젝트 순회라서 7-c 와 관계가 없습니다.
- 순회 순서는 지금도 약속이 아닙니다. 오브젝트를 지우면 저장소가 마지막 오브젝트를 빈 위치로 옮기기 때문입니다(`GameObjectStore.cpp:483-489`). 순서가 결과에 나오는 곳은 2.7 의 세 곳입니다.
- 7-d 의 "찾고 바로 캐스트" 호출부는 52 곳입니다. 그 가운데 35 곳은 지금 확인 없는 `static_cast` 라서, 핸들의 타입이 틀리면 조용히 잘못된 주소를 씁니다. 타입 핸들은 이 패턴을 형식으로 막습니다.
- `Source` 에는 `PROPERTY` 인 `ComponentHandle` 필드가 없습니다. 하나 있는 것은 테스트 타입(`Test/ReflectionTest/TestSampleActor.h:607`)입니다.
  그래서 7-d 는 직렬화와 인스펙터를 바꾸지 않는 부분(7-d-1, 7-d-2)과, 사용처가 생길 때 하는 부분(7-d-3)으로 나눕니다. 그때까지 리플렉션 파서가 `PROPERTY` 의 타입 핸들을 생성 오류로 막습니다.

## 2. 7-c 매니저의 타입별 컴포넌트 목록

### 2.1 지금 구조

- 매니저 순회는 `GameObjectStore::forEachGameObject` 로 모든 오브젝트를 공유 잠금 아래 돌고, 오브젝트마다 컴포넌트 목록을 `castTo` 로 거릅니다(`GameObjectManager.h:132-141`, `GameObject.h:259-271`).
  비용은 씬의 오브젝트 수와 컴포넌트 수에 비례합니다.
- 방문 순서는 `_listGameObject` 순서, 그다음 이번 프레임에 만든 `_listPendingAdd` 순서입니다. 오브젝트 안에서는 컴포넌트 목록 순서입니다.
  삭제 대기 오브젝트와 삭제 대기 컴포넌트는 건너뜁니다(`GameObjectStore.h:106-121`, `GameObject.h:264`).
- 콜백은 공유 잠금과 `WalkScope` 안에서 불립니다. 콜백 안에서 오브젝트를 만들거나 지우거나 컴포넌트를 붙이면 Debug 단언에 걸립니다(`GameObjectStore.h:86-100`).
- 비슷한 기능이 이미 하나 있습니다. `ComponentRegistry` 는 컴포넌트가 `onRegister` 에서 스스로 등록하는 목록이고, 빛과 상호작용 대상처럼 프레임 경로에서 찾는 타입이 씁니다(`ComponentRegistry.h:1-23`).
  7-c 는 등록을 요구하지 않는 자동 목록이므로 둘은 겹칩니다. `ComponentRegistry` 는 등록 순서를 지키고 채널(한 타입 안의 하위 목록)이 있으며 잠금 없이 읽으므로 이번에 합치지 않습니다(6절 위험 7).

### 2.2 자료구조: 정확한 타입마다 버킷 하나

**ⓐ 버킷은 컴포넌트 풀 항목에 합칩니다.** 풀 맵(`_mapComponentPool`)은 이미 타입 FQN 이 키이고 정확한 타입마다 하나라서, 버킷과 키가 같습니다.
맵을 하나 더 두지 않고 풀 항목이 버킷을 함께 가집니다. 저장소(`GameObjectStore`)가 다음을 보관합니다. 매니저는 지금처럼 전달만 합니다.

| 멤버 | 형식 | 뜻 |
|---|---|---|
| `_mapComponentPool` | `unordered_map<hashed_string, unique_ptr<ComponentPoolEntry>>` | 타입 FQN 에서 항목으로 갑니다. 항목 주소는 매니저가 죽을 때까지 바뀌지 않습니다 |
| `_listComponentPoolEntry` | `vector<ComponentPoolEntry*>` | 항목을 처음 나온 순서로 둡니다. 조회가 이 목록을 돕니다 |
| `ComponentPoolEntry::_pool` | `PoolAllocator` | 그 타입의 블록 풀입니다(지금의 풀) |
| `ComponentPoolEntry::_listComponent` | `vector<Component*>` | 버킷입니다. 그 타입 인스턴스들입니다 |
| `ComponentPoolEntry::_pType` | `const TypeInfo*` | 버킷이 비어 있지 않을 때만 값이 있습니다. 비면 nullptr 입니다 |

컴포넌트는 두 값을 보관합니다.

| 멤버 | 형식 | 뜻 |
|---|---|---|
| `Component::_pPoolEntry` | `ComponentPoolEntry*` | 지금의 `_pPool` 을 바꿉니다. 해체가 이 항목의 풀로 반납하고 이 항목의 버킷에서 뺍니다. 힙에서 왔으면 nullptr 입니다 |
| `Component::_poolEntrySlot` | `uint32` | 버킷 안의 위치입니다. 제거를 O(1) 로 하려고 둡니다 |

- 키를 `TypeInfo` 포인터가 아니라 FQN 으로 잡습니다. 컴포넌트 풀과 같은 이유입니다(같은 맵입니다). 한 클래스에 `TypeInfo` 사본이 둘 생겨도 같은 버킷을 씁니다.
- 추가 때 해시 조회가 없습니다. 풀을 고를 때(`getOrCreateComponentPool`) 이미 찾은 항목을 컴포넌트가 가지고 있기 때문입니다.
- 힙에서 잡은 컴포넌트(타입 정보가 없어 풀이 없는 것)는 버킷에 들어가지 않습니다. 타입 정보가 없으면 지금도 `castTo` 가 맞추지 못하므로 결과는 같습니다.
- 조회 `forEachComponentOfType<T>` 는 `T` 의 `TypeInfo` 를 한 번 구한 뒤, `_pType` 이 있는 버킷마다 `_pType->isDerivedFrom( pToType )` 를 묻고, 맞는 버킷의 컴포넌트를 방문합니다.
  `isDerivedFrom` 은 깊이 8 까지 원자 로드 네 번과 비교 한 번입니다(ReflectionFormats.md 7.1).
- 런타임 `TypeInfo` 를 받는 비템플릿 버전 `forEachComponentOfType( const TypeInfo*, Func )` 도 둡니다. 7-d-3 의 인스펙터 선택기가 씁니다.

**조상 목록을 쓰지 않는 이유.** 7.2 는 "그 타입과 조상 타입의 목록에 넣고 뺀다"고 적었습니다. 그 구조는 조회가 맞는 컴포넌트만 정확히 돌지만, 비용이 큽니다.

| | 조상 목록(7.2) | 정확한 타입 버킷(이 계획) |
|---|---|---|
| 컴포넌트 하나의 추가 | 깊이 d 개 목록에 넣기 | 버킷 하나에 넣기 |
| 컴포넌트 하나의 제거(O(1)) | 목록마다 위치가 필요해서 위치 d 개를 보관 | 위치 하나 |
| 컴포넌트당 메모리, d = 8 | 96 B | 16 B |
| 조회 | 맞는 컴포넌트 수 | 버킷 수 K + 맞는 컴포넌트 수 |
| 순서 | 추가 순서(파생 타입이 섞임) | 타입별로 모인 뒤 추가 순서 |

버킷 수 K 는 씬에 실제로 있는 서로 다른 컴포넌트 타입 수입니다. 수십에서 많아야 200 정도로 추정합니다(미확인, 구현 때 대표 씬에서 셉니다).
K 가 200 이어도 `isDerivedFrom` 200 번은 1 µs 안쪽으로 추정하며, 지금 오브젝트 10,000 개를 훑는 비용(약 200 µs, `ComponentRegistry.h:7-8`)보다 두 자릿수 작습니다.
조회마다 맞는 버킷 번호를 캐시하는 방법도 있지만, 측정으로 필요가 확인될 때만 넣습니다.

### 2.3 갱신 규칙

모든 갱신은 저장소 `_mutex` 의 배타 잠금 안에서 합니다. 조회는 지금처럼 공유 잠금입니다.

1. **추가.** `GameObject::attachCreatedComponent` 가 컴포넌트를 오브젝트 목록에 넣는 줄(`GameObject.cpp:604`) 바로 옆에서 `registerTypedComponent` 를 부릅니다.
   지금 컴포넌트는 `onRegister` 보다 먼저 오브젝트 목록에 들어가 순회에 보입니다. 같은 시점에 버킷에 넣어야 `onRegister` 안에서 씬을 찾는 코드가 지금과 같은 답을 받습니다.
   버킷이 비어 있었으면 `_pType` 을 그 컴포넌트의 `_pTypeInfo` 로 채웁니다. 항목은 컴포넌트가 가진 `_pPoolEntry` 라서 맵을 다시 찾지 않습니다.
2. **제거.** 컴포넌트 해체는 `GameObjectStore::destroyComponentInstance` 하나를 지납니다(`GameObjectStore.cpp:421-450`). 그 함수의 맨 앞, `dispatchEndPlay` 보다 먼저 버킷에서 뺍니다.
   마지막 원소를 빈 위치로 옮기고 그 원소의 `_typeBucketSlot` 을 고칩니다. 지금도 `removeComponent` 가 목록에서 먼저 빼므로 `onEndPlay` 와 `onUnregister` 동안에는 순회에 보이지 않습니다. 같은 시점입니다.
3. **지연 제거와 오브젝트 파괴.** 삭제 대기 컴포넌트는 실제 해체까지 버킷에 남습니다. 조회가 `pComp->isPendingDestroy()` 와 `pComp->getOwner()->isPendingDestroy()` 를 보고 건너뜁니다. 지금 순회와 같은 결과입니다.
4. **이번 프레임에 만든 오브젝트.** 그 컴포넌트도 추가 때 바로 버킷에 들어갑니다. 지금 순회도 `_listPendingAdd` 를 돌므로 결과 집합은 같습니다.
5. **`clear`.** 오브젝트 소멸이 컴포넌트마다 `destroyComponentInstance` 를 부르므로 규칙 2 로 비워집니다. 끝나면 모든 버킷이 비고 `_pType` 이 모두 nullptr 인지 Debug 단언으로 확인합니다.
6. **버킷이 비면 `_pType` 을 nullptr 로 바꿉니다.** 이것이 모듈 언로드의 묘비(tombstone) 처리입니다.
   버킷은 인스턴스가 있을 때만 타입 포인터를 보관하므로, 언로드된 모듈의 `TypeInfo` 를 가리키는 버킷이 구조적으로 생기지 않습니다.
   모듈 언로드 전에는 `destroyComponentsOfModule` 이 그 모듈 타입의 인스턴스를 모두 지웁니다(`GameObjectStore.cpp:609-647`). 그러면 그 버킷들은 규칙 2 와 6 으로 비고 타입 포인터를 놓습니다.
   모듈 언로드 훅에 버킷 정리를 따로 넣지 않아도 되므로, 그 훅을 거치지 않는 매니저(프리팹 원형처럼 손수 만든 매니저)도 같은 규칙으로 안전합니다.
   비운 버킷의 벡터 용량과 FQN 항목은 남깁니다. 같은 이름으로 스폰과 파괴를 거듭하는 게임(총알)이 매번 할당하지 않게 하기 위해서입니다.
7. **핫 리로드 재등록.** 같은 FQN 의 타입이 다시 등록되면 `TypeInfo` 는 같은 객체에 덮어써집니다(`GameObjectStore.h:162`). 버킷은 FQN 키라서 영향이 없습니다.
8. **오브젝트가 매니저를 옮기는 경우는 없습니다.** `GameObject::_pOwnerManager` 는 만들 때 한 번만 씁니다(`GameObjectStore.cpp:113`). 씬을 넘기는 오브젝트는 새 매니저에서 다시 만듭니다.

### 2.4 순회 중 변경

- 규칙은 지금과 같습니다. 조회 콜백은 공유 잠금과 `WalkScope` 안에서 불리고, 그 안에서 구조를 바꾸면 Debug 단언에 걸립니다.
- `registerTypedComponent` 와 `destroyComponentInstance` 의 버킷 제거 앞에 `SW_ASSERT( WalkScope::isInsideWalk() == false )` 를 둡니다. 지금 `destroyComponentInstance` 에는 이 단언이 없습니다.
  배타 잠금을 새로 잡으므로, 순회 안에서 부르면 멈춤(deadlock)이 됩니다. 멈추는 대신 단언으로 알리는 것이 `WalkScope` 의 기존 방침입니다.
- 비동기 씬 로드는 워커에서 컴포넌트를 붙입니다(`GameObjectStore.h:304`). 배타 잠금이 게임 스레드의 조회와 워커의 추가를 직렬화합니다.
- 병렬 틱 중에는 구조 변경이 틱 뒤로 미뤄지므로(`StructuralChangeBuffer`) 틱 안의 조회와 버킷 갱신이 겹치지 않습니다. 그래도 조회는 공유 잠금을 잡습니다. 지금과 같은 비용입니다.

### 2.5 메모리와 비용 추정

가정은 컴포넌트 20,000 개입니다(큐브 20,000 개 벤치 규모). 측정값이 아니라 크기 계산입니다.

| 항목 | 정확한 타입 버킷 | 조상 목록, 깊이 8 |
|---|---|---|
| 목록의 포인터 | 8 B × 1 | 8 B × 8 |
| 컴포넌트의 위치 값 | 4 B(위치. 항목 포인터는 `_pPool` 자리를 씀) | 4 B × 8 + 목록 번호 |
| 컴포넌트당 합계 | 12 B | 약 96 B 이상 |
| 20,000 개 | 약 240 KB | 약 1.9 MB |
| 추가 한 번 | push 1(항목은 이미 앎) | 디스플레이 읽기 + 해시 조회 8 + push 8 |

- 버킷 하나의 고정 비용은 `TypeInfo*` 8 B, 벡터 24 B, 항목 목록의 포인터 8 B 입니다. 맵 노드는 풀이 이미 가지고 있습니다. 버킷 200 개면 10 KB 안쪽입니다.
- `Component` 에는 `uint32` 하나(4 B)가 늘어납니다. 플래그(`_bActive` 부터 비트 플래그까지 4 B) 뒤, `_listSubTick` 앞의 패딩 4 B 에 들어간다고 계산했습니다(`Component.h:581-588`, `sizeof` 미측정).
  그래서 7-c 는 `sizeof( Component )` 가 늘지 않았는지 `static_assert` 로 묶지 않고, 측정 질문(5절)으로 남깁니다.
- 추가와 제거마다 배타 잠금을 한 번 더 잡습니다. 추가는 이미 풀 조회(`getOrCreateComponentPool`)에서 같은 잠금을 잡지만, 그 사이에 생성자가 돌아서 하나로 합치지 않습니다.
- **ⓗ 등록형 목록과의 관계.** 타입별 목록이 `ComponentRegistry`, `TickBatchRegistry`, 버킷 셋이 됩니다. `09_Decisions.md` 의 "씬 훑기 190 µs" 근거는 버킷으로 약해지므로,
  어느 등록형 목록을 버킷으로 옮길지는 기준(등록 순서가 결과에 나오는가, 채널이 있는가, 잠금 없이 읽어야 하는가)과 함께 백로그에 남깁니다.
  조회 타입마다 맞는 버킷 목록을 캐시하는 안(B 안)은 측정 뒤로 미룹니다. 넣을 때는 타입 레지스트리 세대로 무효화합니다.
  잠금 비용은 경합이 없을 때 수십 ns 로 추정합니다. 벤치의 큐브 교체(churn) 경로에서 p50 과 p99 를 전후로 측정합니다(5절).

### 2.6 호출부 전체 목록

`forEachComponentOfType<` 의 모든 줄입니다(2026-10-17 기준). "순서 의존"은 결과가 방문 순서에 따라 달라질 수 있는지입니다.
"첫 것"은 맞는 컴포넌트가 둘 이상이면 어느 것을 고를지가 순서로 정해진다는 뜻입니다. 그런 곳은 대부분 씬에 하나만 있다고 가정하는 디렉터 찾기입니다.

**매니저 순회(7-c 가 바꾸는 곳), Source 18 줄**

| 위치 | 대상 타입 | 순서 의존 |
|---|---|---|
| `Games/WildWest/WildWestDirectorComponent.cpp:218` | `HorseFollowAIController` | 없음(모두 호출, 개수) |
| `Games/VoxelCraft/VoxelPlayerComponent.cpp:60` | `VoxelPlayerComponent` | 첫 것 |
| `Games/ThemeParkTycoon/ParkDirectorComponent.cpp:66` | `ParkDirectorComponent` | 첫 것 |
| `Games/StarSkirmish/SkirmishUnitComponent.cpp:55` | `SkirmishUnitComponent` | 없음(최댓값, 개수) |
| `Games/StarSkirmish/SkirmishDirectorComponent.cpp:79` | `SkirmishDirectorComponent` | 첫 것 |
| `Games/Shooter3D/ShooterPlayerComponent.cpp:100` | `ShooterPlayerComponent` | 첫 것 |
| `Games/Shooter3D/ShooterDirectorComponent.cpp:361` | `ShooterBlockerComponent` | 상자 목록 순서(충돌 결과는 집합으로 같다고 추정) |
| `Games/Shooter3D/ShooterDevCommands.cpp:34` | `ShooterPlayerComponent` | 목록 순서(치트 대상 전부) |
| `Games/NileCity/NileDirectorComponent.cpp:73` | `NileDirectorComponent` | 첫 것 |
| `Games/HarvestValley/FarmDirectorComponent.cpp:86` | `FarmDirectorComponent` | 첫 것 |
| `Games/AbilityArena/ArenaDirectorComponent.cpp:67` | `ArenaDirectorComponent` | 첫 것 |
| `GameFramework/Base/Gameplay/SessionState/SessionStateDevCommands.cpp:35` | `SessionStateComponent` | 첫 것(초기화된 것) |
| `GameFramework/Base/Gameplay/Interaction/InteractionInterruptionSystem.cpp:57` | `InteractorComponent` | 대상 목록 순서(집합으로 쓴다고 추정) |
| `Engine/Scene/SceneNavigationCooker.cpp:58` | `NavMeshSurfaceComponent` | **있음**(쿠킹 항목 순서) |
| `GameFramework/Base/Gameplay/Ability/AbilityDevCommands.cpp:36` | `AbilitySystemComponent` | 목록 순서(치트 대상 전부) |
| `GameFramework/Base/Foundation/Framework/Save/ComponentStateStore.h:70` | 템플릿 `TComponent` | **있음**(저장 항목 순서) |
| `GameFramework/Base/Foundation/Framework/Save/ComponentStateStore.h:90` | 템플릿 `TComponent` | **있음**(다른 실행의 세이브를 차례로 맞춤) |
| `GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h:280` | 템플릿 `TComponent` | 없음(모두 정리) |

**매니저 순회, Test 13 줄**

| 위치 | 대상 타입 | 순서 의존 |
|---|---|---|
| `Test/EngineTest/Object/TestComponentRegistry.cpp:55` | `LightComponent` | 없음(정렬 뒤 비교) |
| `Test/EngineTest/Object/TestComponentRegistry.cpp:68` | `ShadowCaster2DComponent` | 없음(정렬 뒤 비교) |
| `Test/EngineTest/Object/TestComponentRegistry.cpp:90` | 템플릿 `T` | 없음(개수) |
| `Test/EngineTest/Object/TestComponentRegistry.cpp:270` | `FoliageInfluencerComponent` | 없음(정렬 뒤 비교) |
| `Test/EngineTest/GameFramework/UI/TestWorldUI.cpp:181` | `DamageNumberComponent` | 마지막 것(하나만 있음) |
| `Test/EngineTest/GameFramework/TestShooter.cpp:428` | `PawnComponent` | 조건에 맞는 첫 것 |
| `Test/EngineTest/GameFramework/SessionState/TestSessionStateComponent.cpp:175` | `SessionStateComponent` | 첫 것 |
| `Test/EngineTest/GameFramework/Kits/Simulation/TestKitComposition.cpp:740` | `SessionStateComponent` | 첫 것과 개수 |
| `Test/EngineTest/GameFramework/Kits/Simulation/TestKitComposition.cpp:813` | `SessionStateComponent` | 첫 것과 개수 |
| `Test/EngineTest/GameFramework/Kits/Simulation/TestKitComposition.cpp:889` | `SessionStateComponent` | 마지막 것 |
| `Test/EngineTest/GameFramework/Interaction/TestInteraction.cpp:614` | `InteractableComponent` | 후보 목록 순서(점수로 고른다고 추정) |
| `Test/EngineTest/GameFramework/Framework/TestGameFramework.cpp:1145` | `MockRuntimeStateComponent` | 없음(두 순서를 모두 허용) |
| `Test/EngineTest/GameFramework/Framework/TestGameFramework.cpp:2878` | `ProjectileComponent` | 없음(개수) |

**오브젝트 순회(7-c 와 관계없음), 14 줄**

`GameFramework/Base/World/Lifetime/FadeOutComponent.cpp:40` 과 `:95`, `Games/VoxelCraft/VoxelChunkComponent.cpp:125`, `Games/Shooter3D/ShooterPlayerComponent.cpp:70`,
`Editor/Viewport/Visualizers/PhysicsShapeVisualizer.cpp:69` 와 `:74`, `GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.cpp:38`,
`GameFramework/Base/Gameplay/Automation/GameplayAutomationSteps.cpp:386`, `Engine/Object/Component/Navigation/NavGeometryCollector.cpp:99`, `:235`, `:256`,
`GameFramework/Base/Actor/Control/FirstPersonCameraComponent.cpp:160`, `GameFramework/Base/Actor/Combat/Health/HealthListenerComponent.cpp:15`,
`Test/EngineTest/Object/TestObjectStateRoundTrip.cpp:780` 입니다.

나머지 3 줄은 정의(`GameObjectManager.h:139`, `GameObject.h:251`)와 주석(`TestGameFramework.cpp:2857`)입니다.

### 2.7 순서

**바뀌는 것.** 지금 순서는 "오브젝트 순서, 그 안의 컴포넌트 순서"입니다. 새 순서는 "버킷 순서, 그 안의 추가 순서"입니다. 버킷 순서는 그 타입이 처음 나온 순서입니다.
대상 타입에 씬에 있는 파생 타입이 없으면, 지운 것이 없는 한 두 순서는 같습니다. 컴포넌트가 오브젝트 순서대로 붙기 때문입니다.
파생 타입이 섞이면 타입별로 모입니다. 지운 것이 있으면 두 구조 모두 마지막 원소를 빈 위치로 옮기지만 옮기는 단위가 달라서 결과가 다릅니다.

**약속.** 매니저 순회는 순서를 약속하지 않는다고 헤더 주석에 적습니다. 지금도 오브젝트 파괴가 순서를 바꾸므로 새로 생기는 약속 위반은 없습니다.

**순서가 결과에 나오는 곳.**

1. `ComponentStateStore.h:90` 은 컴포넌트 id 가 맞지 않을 때(다른 실행의 세이브) 같은 타입 안의 차례로 항목을 고릅니다(`ComponentStateStore.h:52`).
   대상 타입은 상태 컴포넌트의 구체 타입이라 파생이 섞이지 않는다고 추정합니다. 그러면 새로 로드한 씬에서 두 순서는 같습니다.
   다만 "열거 순서로 다른 실행의 컴포넌트를 맞춘다"는 구조 자체가 순서에 기대는 결함입니다. 오브젝트의 저장 id 와 컴포넌트 안정 키(`ComponentStableKey`, `타입#n`)로 맞추는 것이 근본 원인 해결이며, 7-c 범위 밖이므로 백로그 후보로 올립니다.
2. `SceneNavigationCooker.cpp:58` 은 에이전트 타입을 방문 순서대로 모아 쿠킹 항목을 만듭니다. 같은 타입이 두 번 들어갈 수도 있습니다.
   7-c 에서 모은 목록을 이름순으로 정렬하고 중복을 지웁니다. 그러면 쿠킹 결과가 방문 순서에 관계없이 같아집니다. 이 파일은 7-c 가 함께 고칩니다.
3. "첫 것"을 고르는 디렉터 찾기 열 곳은 씬에 하나만 있을 때 순서와 관계없습니다. 둘 이상이면 지금도 어느 것이 나올지 정해져 있지 않습니다.

## 3. 7-d 타입 핸들 `TComponentHandle<T>`

### 3.1 형식

`Core/Container/ComponentHandle.h` 에 `ComponentHandle` 과 함께 둡니다. 크기는 `ComponentHandle` 과 같은 16 B 이고 trivially copyable 입니다. 람다에 붙잡아도 델리게이트 인라인 버퍼를 넘지 않습니다.
Core 는 `Component` 를 모르지만, 템플릿 멤버는 인스턴스화될 때만 `T` 의 멤버를 보므로 include 가 필요 없습니다.

| 멤버 | 뜻 |
|---|---|
| 기본 생성자 | 무효 핸들입니다. "대상 없음"을 담는 정상 값입니다 |
| `explicit TComponentHandle( const T* pComponent )` | `T` 나 그 파생에서 만듭니다. 타입이 컴파일 시간에 정해지므로 확인이 없습니다. nullptr 이면 무효 핸들입니다 |
| `TComponentHandle( TComponentHandle<U> )`, `U` 가 `T` 의 파생일 때만 | 업캐스트입니다. 암시적으로 바뀝니다 |
| `untyped()` | 타입을 지운 `ComponentHandle` 입니다. 레지스트리, 지연 파괴 목록처럼 타입을 모르는 곳에 넘길 때 씁니다 |
| `objectID()`, `componentID()`, `isValid()`, `operator bool`, `==`, `!=` | `ComponentHandle` 과 같습니다 |

- **ⓓ 비교.** 타입 핸들과 타입 없는 `ComponentHandle` 을 섞은 `==` 와 `!=` 를 허용합니다. 비교는 id 두 개만 보고 주소를 만들지 않으므로 타입이 어긋나도 잘못된 주소를 쓸 수 없습니다.
  `pPawn->getHandle() == pController->getPawn()` 같은 호출부(`VoxelDirectorComponent.cpp`, `ShooterDirectorComponent.cpp`, `ShooterPlayerComponent.cpp`, `FarmDirectorComponent.cpp`, `ArenaDirectorComponent.cpp`)가 `untyped()` 없이 그대로 읽힙니다.
  서로 관계없는 두 타입 핸들의 비교는 연산자가 없어 컴파일 오류입니다. 파생과 기반은 업캐스트로 비교합니다.
- 이 저장소는 C++17 이라 비교 연산자를 양쪽 순서로 모두 적습니다(C++20 의 뒤집은 후보가 없습니다).
- 타입 없는 핸들에서 바로 만드는 공개 생성자는 두지 않습니다. 그 변환은 3.2 의 확인을 거쳐야만 합니다.
  확인 함수만 만들 수 있게 하는 패스키(passkey, 생성자를 특정 클래스만 부를 수 있게 하는 빈 태그 타입)를 Core 에 두고, 생성자가 그 태그를 받습니다. 태그의 생성자는 `GameObjectManager` 만 부를 수 있습니다.
- 다운캐스트 변환(`TComponentHandle<Base>` 에서 `TComponentHandle<Derived>`)도 3.2 를 거칩니다.
- `ComponentHandle` 을 키로 쓰는 해시나 정렬 특수화는 없습니다(2026-10-17 검색). 타입 핸들에도 두지 않습니다.

### 3.2 타입 없는 핸들에서 만들기

`GameObjectManager::makeTypedHandle<T>( ComponentHandle handle, TComponentHandle<T>& outHandle )` 가 `[[nodiscard]]` 결과를 반환합니다.

| 입력 | 결과 | 출력 |
|---|---|---|
| 무효 핸들 | 성공 | 무효 타입 핸들 |
| 대상이 있고 `isDerivedFrom( T )` | 성공 | 타입 핸들 |
| 대상이 있고 타입이 다름 | **타입 불일치 오류**, 오류 로그 | 바꾸지 않음 |
| 대상이 없음(파괴됨) | **대상 없음 오류**, 오류 로그 | 바꾸지 않음 |

- 실패를 빈 핸들로 바꾸지 않습니다. 빈 핸들은 "대상 없음"이라는 정상 값과 구분되지 않아서, 타입이 틀린 핸들이 조용히 사라지기 때문입니다.
- 대상이 없으면 타입을 확인할 수 없으므로 성공으로 치지 않습니다. 부르는 쪽이 대상 없음을 정상으로 보면 그 결과를 직접 처리합니다.
- 이 변환을 쓰는 곳은 적어야 합니다. 대부분의 핸들은 `T*` 에서 만들어지기 때문입니다(3.4). 남는 곳은 역직렬화(7-d-3), 에디터, 타입을 모르는 `Component*` 에서 만든 핸들입니다.

### 3.3 찾기

`GameObjectManager::resolveComponent<T>( TComponentHandle<T> handle ) const` 를 더합니다. 반환은 `T*` 입니다.

- 타입 없는 찾기(`resolveComponent( ComponentHandle )`)를 부르고 `static_cast<T*>` 로 바꿉니다. 삭제 대기의 처리는 타입 없는 찾기와 같습니다.
- **ⓑ Shipping 이 아닌 모든 구성에서 확인합니다**(Debug 단언만이 아닙니다). 결과가 nullptr 이 아니면 `findCachedTypeInfo()->isDerivedFrom( T::StaticType() )` 이어야 하고,
  아니면 오류 로그와 Debug 단언을 남기고 nullptr 을 반환합니다. 타입이 바뀌는 길(핫 리로드, 되돌리기의 id 복원)이 Shipping 에 없으므로 Shipping 만 확인을 뺍니다.
  확인 비용은 개발 빌드의 측정 질문입니다(`BenchScene.cpp:1341`, 5절).
- 확인이 결함을 드러내는 쪽인 이유는 다음과 같습니다. 오브젝트 id 와 컴포넌트 id 는 프로세스 전체에서 한 번만 발급됩니다(`GameObjectStore.h:294-299`). 같은 id 를 다시 쓰는 곳은 `ComponentIDRestoreScope` 뿐입니다(`GameObject.h:369-377`).
  **ⓑ 이 복원은 지금 `TypeInfo::_name`(짧은 이름)으로 맞추므로, 짧은 이름이 같은 다른 네임스페이스의 타입이 원래 id 를 가져갈 수 있습니다.** 7-d-1 에서 복원 키를 FQN 으로 바꿉니다
  (`ObjectIdentity::ComponentEntry::_typeFqn`, `takeRestoredComponentID`, `captureIdentity`). 그러면 한 핸들이 가리키는 타입은 바뀌지 않습니다. 복원 목록은 같은 프로세스에서만 읽으므로 저장 형식의 호환 문제는 없습니다.
- `static_cast` 는 `T` 가 `Component` 를 가상이 아닌 방식으로 상속할 때 포인터 보정까지 맞습니다. 다중 상속 `REFLECT` 타입은 단계 7-b 가 생성 오류로 막습니다(main 에 있음).
- **ⓒ `executeOrDeferPostTickOnComponent( const TComponent& self, func )`** 로 바꾸고 `TComponent` 는 인자에서 추론합니다(`GameObjectManager.h:269-287`). 함수가 `self` 에서 타입 핸들을 만들고, 람다 안의 `static_cast` 와 단언은 `resolveComponent<T>` 로 대신합니다.
  호출부 23곳(약 20파일)에서 `getHandle()` 과 템플릿 인자가 함께 사라져, 둘이 어긋나는 일(다른 컴포넌트의 핸들에 이 타입의 일을 거는 것)이 형식으로 막힙니다.
  이 파일들이 7-d-2 그룹과 겹치므로 7-d-1 에서 한 번에 끝내고 그룹은 그 뒤에 시작합니다. `const` 참조로 받는 것은 `const` 멤버 함수 안의 `*this` 도 받기 위해서입니다. 일은 그때 다시 찾은 `T&` 로 받습니다.
- `getComponent<T>` 처럼 `T` 가 자기 `REFLECT_BODY` 를 가졌는지 `static_assert` 로 확인합니다(`GameObject.h:283-285`).

### 3.4 "찾고 바로 캐스트" 호출부

`resolveComponent` 결과를 바로 `castTo` 나 `static_cast` 로 바꾸는 곳 전부입니다. 단계 0 의 틱 안 9 줄에는 "틱" 표시를 붙였습니다.
**ⓖ 결함이 숨는 곳은 API 경계를 넘는 핸들**(인자, 대기 항목, 접근자 반환값)입니다. 자기 핸들과 `T*` 에서 바로 만든 지역 핸들은 타입이 어긋날 수 없고, 경계를 넘는 핸들은 받는 쪽이 출처를 모릅니다.
그래서 "핸들 출처"에 "인자", "대기 항목", 접근자 이름이 적힌 줄이 마이그레이션의 우선이고, 그룹 A(조종)부터 합니다. 그 예가 `ControlSystem::queuePossess( const ComponentHandle& controller, const ComponentHandle& pawn )` 입니다.
서로 다른 타입의 핸들 둘을 같은 형식으로 받아 인자를 바꿔 넘겨도 컴파일됩니다. 타입 핸들로 바꾸면 그 실수가 컴파일 오류가 됩니다.
"자기 핸들"과 "지역 핸들"은 핸들이 `T*` 에서 만들어진 곳입니다. 나머지(필드, 인자, 대기 항목, 접근자)는 핸들이 다른 곳에서 넘어온 것입니다.
"핸들 출처"는 바꿀 필드나 반환값입니다. `getComponent<T>` 결과를 다시 캐스트하는 곳은 `Source` 에 없었습니다(`RenderViewCollector.cpp:51` 은 업캐스트입니다).

**지금 `castTo` 인 곳(실행 시간 확인이 있음), 17 곳**

| 위치 | 대상 타입 | 핸들 출처 | `T*` 에서 만듦 |
|---|---|---|---|
| `Engine/Object/SceneAnimation/MotionWarpingComponent.cpp:92` | `SkeletalAnimatorComponent` | `_animator` | 아니오 |
| `Engine/Object/SceneAnimation/AnimationSystem.cpp:299` | `SkeletalMeshComponent` | 지역 목록(미확인) | 아니오 |
| `Engine/Character/PoseModifier/PoseModifierComponent.cpp:326` | `SkeletalMeshComponent` | `_listDependency` | 아니오 |
| `Engine/Character/PoseModifier/PoseModifierComponent.cpp:381` | `SkeletalMeshComponent` | `ObjectBinding::_unit` | 아니오 |
| `Engine/Character/PoseModifier/PoseModifierComponent.cpp:429` | `SkeletalMeshComponent` | 지역 핸들 | 예 |
| `Engine/Character/PoseModifier/PoseModifierComponent.cpp:502` | `SkeletalMeshComponent` | `_listTableUnit` | 아니오 |
| `Engine/Character/PoseModifier/PoseModifierComponent.cpp:617` | `SkeletalMeshComponent` | 외부 바인딩 `_unit`, 틱 | 아니오 |
| `Engine/Object/GameObject/Serialization/ObjectStateSerializer.cpp:78` | `SceneComponent` | `ChildLink::_child` | 아니오 |
| `Engine/Object/Component/3D/SkeletalMeshComponent.cpp:375` | `SkeletalMeshComponent` | `_leader` | 아니오 |
| `Engine/Object/Component/3D/PoseRetargetComponent.cpp:119` | `SkeletalMeshComponent` | `_source` | 아니오 |
| `Engine/Object/Component/3D/FacialAnimationComponent.cpp:455` | `SkeletalAnimatorComponent` | `_animator`, 틱 | 아니오 |
| `GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.cpp:112` | `FractureComponentBase` | `_component` | 아니오 |
| `Games/Empty/BenchSceneRig.cpp:298` | `MeshComponent` | `_rigLookTarget`, 틱 | 아니오 |
| `Games/Empty/BenchSceneRig.cpp:305` | `SkeletalMeshComponent` | `_listRigBody`, 틱 | 아니오 |
| `Games/Empty/BenchScene.cpp:723` | `RagdollComponent` | `_listRagdoll`, 틱 | 아니오 |
| `Games/Empty/BenchScene.cpp:732` | `RagdollComponent` | `_listRagdoll`, 틱 | 아니오 |
| `Games/Empty/BenchCombatComponent.cpp:215` | `BenchCombatComponent` | 자기 핸들, 틱 | 예 |

**지금 확인 없는 `static_cast` 인 곳, 35 곳**

| 위치 | 대상 타입 | 핸들 출처 | `T*` 에서 만듦 |
|---|---|---|---|
| `Engine/Object/GameObject/GameObjectManager.h:276` | 템플릿 `TComponent` | 인자 | 아니오 |
| `Engine/Object/Component/SceneComponent.cpp:331` | `SceneComponent` | 자기 핸들 | 예 |
| `Engine/Object/Component/SceneComponent.cpp:402` | `SceneComponent` | 자기 핸들 | 예 |
| `Engine/Object/Component/SceneComponent.cpp:403` | `SceneComponent` | 부모 핸들 | 아니오 |
| `Engine/Object/Component/SceneComponent.cpp:441` | `SceneComponent` | 자기 핸들 | 예 |
| `Engine/Scene/Scene.cpp:573` | `CameraComponent` | `_activeGameCamera`, `_gameCameraOverride` | 아니오 |
| `Games/Empty/BenchScene.cpp:1341` | `MeshComponent` | `_listBenchMesh`, 틱 | 아니오 |
| `Games/AbilityArena/ArenaDirectorComponent.cpp:374` | `ArenaDirectorComponent` | 자기 핸들 | 예 |
| `GameFramework/Base/Gameplay/Vehicle/MountUtil.cpp:133` | `ControllerComponent` | `PawnComponent::getController()` | 아니오 |
| `GameFramework/Base/Gameplay/Vehicle/MountUtil.cpp:206` | `ControllerComponent` | `_occupantController` | 아니오 |
| `GameFramework/Base/Gameplay/Vehicle/MountUtil.cpp:209` | `ControllerComponent` | `_previousVehicleController` | 아니오 |
| `GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.cpp:457` | `LightComponent` | 지역 핸들 | 예 |
| `GameFramework/Base/Gameplay/Gimmick/Genre/PlatformerGimmicks.cpp:154` | `ConveyorComponent` | 자기 핸들 | 예 |
| `GameFramework/Base/Gameplay/Gimmick/Genre/HorrorStealthGimmicks.cpp:132` | `LightComponent` | 지역 핸들, 틱 | 예 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.cpp:165` | `InteractableComponent` | `_focusComponent` | 아니오 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.cpp:170` | `InteractableComponent` | `_focusComponent` | 아니오 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.cpp:182` | `InteractableComponent` | 인자 | 아니오 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.cpp:207` | `InteractableComponent` | `_focusComponent`, 틱 | 아니오 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.cpp:236` | `InteractableComponent` | `_focusComponent`, 틱 | 아니오 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.cpp:253` | `InteractableComponent` | `_focusComponent` | 아니오 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.cpp:279` | `InteractableComponent` | `_focusComponent` | 아니오 |
| `GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.cpp:421` | `SkeletalMeshComponent` | 지역 핸들 | 예 |
| `GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.cpp:532` | `SkeletalMeshComponent` | `_taskUnit` | 아니오 |
| `GameFramework/Base/Actor/Control/Pawn/PawnComponent.cpp:75` | `ControllerComponent` | `_controller` | 아니오 |
| `GameFramework/Base/Actor/Control/ControlSystem.cpp:190` | `ControllerComponent` | 대기 항목 `_controller` | 아니오 |
| `GameFramework/Base/Actor/Control/ControlSystem.cpp:193` | `PawnComponent` | 대기 항목 `_pawn` | 아니오 |
| `GameFramework/Base/Actor/Control/ControlSystem.cpp:230` | `PawnComponent` | `_listAutoPossessPawn` | 아니오 |
| `GameFramework/Base/Actor/Control/Controller/ControllerComponent.cpp:70` | `ControllerComponent` | `PawnComponent::getController()` | 아니오 |
| `GameFramework/Base/Actor/Control/Controller/ControllerComponent.cpp:92` | `ControllerComponent` | 자기 핸들 | 예 |
| `GameFramework/Base/Actor/Control/Controller/ControllerComponent.cpp:121` | `PawnComponent` | `_pawn` | 아니오 |
| `GameFramework/Base/UI/Marker/HealthBarComponent.cpp:193` | `HealthBarComponent` | 자기 핸들(다음 줄에서 캐스트) | 예 |
| `GameFramework/Base/UI/Marker/DamageNumberComponent.cpp:166` | `DamageNumberComponent` | 자기 핸들(다음 줄에서 캐스트) | 예 |
| `GameFramework/Base/Gameplay/Ability/AbilitySystemComponentEffect.cpp:163` | `AbilitySystemComponent` | 대상 핸들(다음 줄에서 캐스트) | 아니오 |
| `GameFramework/Base/Gameplay/Ability/AbilitySystemComponentAbility.cpp:377` | `AbilitySystemComponent` | 대상 핸들(다음 줄에서 캐스트) | 아니오 |
| `GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h:284` | 템플릿 `TComponent` | 매니저 순회로 모은 핸들 | 아니오 |

`castTo` 를 타입 핸들의 찾기로 바꾸면 동작이 하나 달라집니다. 지금은 타입이 다르면 nullptr 을 받고 넘어가지만, 바꾼 뒤에는 Debug 단언에 걸립니다.
그래서 바꾸기 전에 그 핸들에 다른 타입이 들어갈 수 있는지 출처를 확인합니다. 들어갈 수 있으면 타입 없는 채로 둡니다(3.5). 특히 `AnimationSystem.cpp:299` 는 출처를 읽지 않았습니다(미확인).

### 3.5 타입 없는 채로 두는 곳

- **여러 타입을 받는 핸들**: `AnimationNotifyComponent.cpp:125` 의 `_animator` 는 스켈레탈 애니메이터와 스프라이트 애니메이터를 둘 다 받고 `castTo` 를 두 번 합니다. 공통 기반이 생기기 전까지 그대로 둡니다.
  `BenchScene.h:193` 의 `_listBenchExtra` 도 라이트와 바닥을 섞어 보관합니다.
- **`Component` 로만 쓰는 곳**: `VoxelPlayerComponent.cpp:117`, `FarmDirectorComponent.cpp:462`, `ArenaDirectorComponent.cpp:334`, `GameplayAutomationSteps.cpp:487` 과 `:520` 은 `getOwner()` 만 씁니다.
  핸들 출처(`getController()`, `getPawn()`)가 타입 핸들을 반환하게 되면 이곳들은 자동으로 `T*` 를 받습니다. 동작은 같습니다.
- **엔진 내부의 일반 경로**: `ComponentRegistry` 항목, 저장소의 지연 파괴와 시작 대기열, `SceneTransformHierarchy`(`isSceneComponent` 비트로 거름), `Component.cpp:330`(자기 지연 실행), `EditorService.cpp:116`, `PrefabAsset.cpp:1115`, `SocketBindingComponent.cpp:139`(살아 있는지만 봄), `BenchScene.cpp:347`, `:353`, `:362`, `:935`(소유 오브젝트만 지움), `FractureRenderUnits.cpp:115` 와 `FractureComponentBase.cpp:1540`(호출부 미확인).

### 3.6 `ComponentHandle` 필드

`Source` 의 `ComponentHandle` 멤버 전부입니다. 대상 타입은 찾는 곳의 캐스트에서 읽었고, 캐스트를 보지 않은 것은 "미확인"입니다.
`PROPERTY()` 바로 뒤에 `ComponentHandle` 이 오는 선언을 여러 줄 검색으로 찾았고 `Source` 에는 없었습니다. 위에 주석 줄이 끼어 있는 `PROPERTY` 는 이 검색에서 빠질 수 있으므로, 구현 때 생성된 `.gen.cpp` 에서 `ComponentHandle` 을 한 번 더 찾습니다.

| 위치 | 필드 | 대상 타입 | PROPERTY |
|---|---|---|---|
| `GameFramework/Base/Actor/Control/Controller/ControllerComponent.h:104` | `_pawn` | `PawnComponent` | 아님 |
| `GameFramework/Base/Actor/Control/Pawn/PawnComponent.h:135` | `_controller` | `ControllerComponent` | 아님 |
| `GameFramework/Base/Actor/Control/Controller/IntentTrackControllerComponent.h:69` | `_returnController` | `ControllerComponent`(추정) | 아님 |
| `GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h:102` | `_warnedPawn` | `PawnComponent`(추정) | 아님 |
| `GameFramework/Base/Actor/Control/ControlSystem.h:92` | `_controller` | `ControllerComponent` | 아님 |
| `GameFramework/Base/Actor/Control/ControlSystem.h:93` | `_pawn` | `PawnComponent` | 아님 |
| `GameFramework/Base/Actor/Control/ControlSystem.h:97` | `_listAutoPossessPawn` | `PawnComponent` | 아님 |
| `GameFramework/Base/Actor/Control/Intent/ControlIntentHistory.h:30` | `_pawn` | `PawnComponent`(추정) | 아님 |
| `GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h:72` | `_occupantController` | `ControllerComponent` | 아님 |
| `GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h:73` | `_previousVehicleController` | `ControllerComponent` | 아님 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.h:96` | `_listCandidateComponent` | `InteractableComponent`(추정) | 아님 |
| `GameFramework/Base/Gameplay/Interaction/InteractorComponent.h:99` | `_focusComponent` | `InteractableComponent` | 아님 |
| `GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h:164` | `_taskUnit` | `SkeletalMeshComponent` | 아님 |
| `GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h:141` | `_component` | `FractureComponentBase` | 아님 |
| `GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h:245` | `_component` | `FractureComponentBase`(추정) | 아님 |
| `Engine/Object/SceneAnimation/MotionWarpingComponent.h:108` | `_animator` | `SkeletalAnimatorComponent` | 아님 |
| `Engine/Object/SceneAnimation/AnimationRewind.h:68` | `_target` | 미확인 | 아님 |
| `Engine/Character/AnimationNotify/AnimationNotifyComponent.h:154` | `_animator` | 두 타입, 그대로 둠 | 아님 |
| `Engine/Character/Socket/SocketBindingComponent.h:137` | `_physicsBodyComponent` | 미확인 | 아님 |
| `Engine/Destruction/FractureRenderUnits.h:50` | `_arrSkinnedUnit` | 미확인 | 아님 |
| `Engine/Destruction/FractureRenderUnits.h:51` | `_arrBakedUnit` | 미확인 | 아님 |
| `Engine/Destruction/FractureComponentBase.h:348` | `_intactMesh` | 미확인 | 아님 |
| `Engine/Destruction/FractureComponentBase.h:349` | `_intactBody` | 미확인 | 아님 |
| `Engine/Character/PoseModifier/PoseModifierComponent.h:130` | `_unit` | `SkeletalMeshComponent` | 아님 |
| `Engine/Character/PoseModifier/PoseModifierComponent.h:142` | `_unit` | `SkeletalMeshComponent`(추정) | 아님 |
| `Engine/Character/PoseModifier/PoseModifierComponent.h:184` | `_listDependency` | `SkeletalMeshComponent` | 아님 |
| `Engine/Character/PoseModifier/PoseModifierComponent.h:185` | `_listTableUnit` | `SkeletalMeshComponent` | 아님 |
| `Engine/Object/Component/3D/SkeletalMeshComponent.h:240` | `_listDependency` | `SkeletalMeshComponent`(추정) | 아님 |
| `Engine/Object/Component/3D/SkeletalMeshComponent.h:243` | `_leader` | `SkeletalMeshComponent` | 아님 |
| `Engine/Object/Component/3D/PoseRetargetComponent.h:89` | `_source` | `SkeletalMeshComponent` | 아님 |
| `Engine/Object/Component/3D/FacialAnimationComponent.h:147` | `_animator` | `SkeletalAnimatorComponent` | 아님 |
| `Engine/Scene/Scene.h:155` | `_activeGameCamera` | `CameraComponent` | 아님 |
| `Engine/Scene/Scene.h:156` | `_gameCameraOverride` | `CameraComponent` | 아님 |
| `Engine/Object/GameObject/Serialization/ObjectStateSerializer.cpp:39` | `ChildLink::_child` | `SceneComponent` | 아님 |
| `Games/Empty/BenchScene.h:172` | `_listBenchMesh` | `MeshComponent` | 아님 |
| `Games/Empty/BenchScene.h:186` | `_keyLight` | 미확인 | 아님 |
| `Games/Empty/BenchScene.h:193` | `_listBenchExtra` | 여러 타입, 그대로 둠 | 아님 |
| `Games/Empty/BenchScene.h:215` | `_listRigBody` | `SkeletalMeshComponent` | 아님 |
| `Games/Empty/BenchScene.h:217` | `_listRagdoll` | `RagdollComponent` | 아님 |
| `Games/Empty/BenchScene.h:223` | `_rigLookTarget` | `MeshComponent` | 아님 |

엔진 내부의 타입 없는 목록(`ComponentRegistry.h:49`, `SceneTransformHierarchy.h:67`, `GameObjectStore.h:289`, `:291`, `:305`, `:306`)은 그대로 둡니다.
`ControllerComponent.h:104` 와 `PawnComponent.h:135` 를 바꾸면 `getPawn()`, `getController()` 같은 접근자의 반환형도 바뀌어 호출부가 따라 바뀝니다(4절 그룹 A).

### 3.7 직렬화 (7-d-3, 사용처가 생길 때)

지금 `PROPERTY` 인 타입 핸들이 필요한 곳은 없습니다. 그래서 7-d-1 에서 리플렉션 파서가 `PROPERTY()` 의 `TComponentHandle<T>` 를 생성 오류로 막고, 메시지에 이 문서를 적습니다.

**ⓔ 7-d-1 에서 타입 없는 `ComponentHandle` 도 `PROPERTY` 에서 막습니다.** 단일 값, 컨테이너 원소, 맵 키, 중첩 컨테이너의 어느 단계든 생성 오류입니다. 리플렉션 구조체의 필드도 같은 `PROPERTY` 검사를 지나므로 함께 막힙니다.
`Source` 사용처는 0 이고, 테스트 타입 하나(`TestSampleActor.h` 의 `TestHandleHolderComponent::_targetComponent`)만 지웁니다.
그러면 저장 id 공간에서 `ComponentHandle` 을 경고와 함께 비우던 폴백(`ObjectStateSerializer.cpp:651-658`)은 지울 수 있습니다. 값이 조용히 사라지는 길이 형식으로 없어집니다.
남는 "옮길 수 없는 모양의 `GameObjectHandle`"(원소가 컨테이너인 set, 옮기는 중 메모리 할당이나 값 복사 실패)은 경고와 비우기 대신 **로드 오류**입니다.
`ObjectStateBatch::finish` 가 `[[nodiscard]] bool` 을 반환하고, 부르는 곳(씬 로드, 플레이 세션 복원, 복제, 붙여넣기, 세이브 복원, 씬 넘김, 단독 상태 로드)이 실패로 처리합니다.

**ⓕ 같은 근본 원인.** `resolveObjectReferences` 는 컴포넌트 타입의 최상위 `PROPERTY` 만 봅니다(`ObjectStateSerializer.cpp:623-643`). 리플렉션 구조체 필드 안의 `GameObjectHandle` 은 다시 매겨지지 않습니다.
`ComponentHandle` 은 위 금지로 구조체 안에도 없게 되지만, 구조체 안의 `GameObjectHandle` 은 남습니다. 그런 필드가 실제로 있는지는 확인하지 않았고, 7-d 범위 밖이라 백로그 후보로 둡니다.
근본 원인 해결은 핸들 옮기기를 최상위 필드 목록이 아니라 값 방문(구조체, 컨테이너를 재귀로 내려가는 방문)으로 하는 것입니다.
아래는 사용처가 생겼을 때의 설계입니다.

- **텍스트 형태는 그대로 `objectID:componentID` 입니다**(`SerializeContext.cpp:454-477`). 파서가 `TComponentHandle<X>` 를 내장 타입 `ComponentHandle` 로 기록하고, `PropertyInfo` 에 대상 타입의 FQN 을 더합니다.
  그러면 필드를 `ComponentHandle` 에서 `TComponentHandle<X>` 로 바꿔도 저장된 텍스트와 바이트가 그대로 읽힙니다. 바이너리가 지금 16 B 를 그대로 쓰는지는 ReflectionFormats.md 8.4 의 열린 질문이며 구현 때 확인합니다.
- **타입 확인은 필드를 읽는 순간이 아니라 배치가 끝날 때 합니다.** 배치 로드는 여러 오브젝트를 차례로 만들므로, 필드를 읽을 때 대상 컴포넌트가 아직 없을 수 있습니다.
  `ObjectStateBatch::finish` 의 핸들 처리(`ObjectStateSerializer.cpp:630-659`)에서 대상을 찾고, 대상이 있는데 `isDerivedFrom( X )` 가 아니면 **로드 오류**로 멈춥니다. 오류에는 타입, 필드, 오브젝트, 기대 타입, 실제 타입을 적습니다.
  대상이 없으면 핸들을 그대로 둡니다. 핸들은 약한 참조라서 대상이 사라진 핸들은 정상 값이고, 찾으면 nullptr 을 받습니다.
- **파일 상태의 핸들.** 저장된 컴포넌트 id 를 이 실행의 컴포넌트로 옮길 테이블이 없습니다. 그래서 7-d-1 은 `PROPERTY` 의 컴포넌트 핸들을 모두 막습니다(위 ⓔ).
  타입 핸들을 파일에 실으려면 오브젝트 저장 id 와 컴포넌트 안정 키(`타입#n`)로 쓰는 형태가 필요하고, 이는 형식 변경입니다. 7-d-3 을 시작할 때 따로 정합니다(결정 필요).

### 3.8 인스펙터

- 지금 인스펙터에는 컴포넌트 선택기가 없습니다. `ComponentHandle` 은 정수 두 개 입력이고(`InspectorPropertyManager.cpp:327-334`), 표시는 `object %# / component %#` 입니다(`InspectorBuiltinValue.cpp:60-61`).
  아무 정수나 넣을 수 있어서 다른 타입의 컴포넌트를 가리키게 만들 수 있습니다.
- 7-d-3 에서 타입 핸들 필드는 선택기(콤보)로 그립니다. 목록은 7-c 의 비템플릿 매니저 순회 `forEachComponentOfType( 대상 TypeInfo, ... )` 로 채우고, 항목은 "오브젝트 이름 / 컴포넌트 이름"입니다. 정수 입력은 두지 않습니다. 그래서 인스펙터로는 잘못된 타입을 넣을 수 없습니다.
- 타입 없는 `ComponentHandle` 위젯은 그대로 둡니다.

## 4. 단계

| 단계 | 내용 | 건드릴 파일 | 겹침 주의 | 에이전트(모델) |
|---|---|---|---|---|
| 7-c | 버킷, 갱신 규칙, 비템플릿 순회, 쿠커 정렬, 테스트 | `GameObjectStore.*`, `GameObjectManager.h`, `GameObject.cpp`, `Component.h`, `SceneNavigationCooker.cpp`, 새 테스트 | 헤더 템플릿이 바뀌어 모든 모듈을 다시 빌드 | `integrator`(opus) |
| 7-d-1 | 타입 핸들 형식, 확인 변환, 찾기, `executeOrDeferPostTickOnComponent` 와 호출부 23곳, 복원 키 FQN, 파서의 `PROPERTY` 금지(타입 없는 핸들 포함), 비우기 폴백 제거 | `ComponentHandle.h`, `GameObjectManager.h`, `GameObject.cpp`, `ObjectStateSerializer.*`, `Tools/ReflectionParser`, 호출부 파일, `finish` 호출부 | 7-c 와 같은 헤더, 7-c 뒤 | `integrator`(opus) |
| 7-d-2 | 호출부 마이그레이션(그룹 A ~ E) | 3.4 와 3.6 의 파일 | 그룹끼리 파일이 겹치지 않음 | 그룹마다 `worker`(sonnet), 병합은 `integrator`(opus) |
| 7-d-3 | (사용처가 생길 때) `PROPERTY` 타입 핸들, 배치 끝 타입 확인, 인스펙터 선택기 | `SerializeContext.cpp`, `ObjectStateSerializer.cpp`, 파서, `InspectorPropertyManager.cpp`, `InspectorBuiltinValue.*` | 파일 상태 형태 결정이 먼저 | `worker`(opus) |

**7-d-2 그룹.** 그룹은 파일을 나눠 가지며, 접근자 반환형을 바꾸면 그 호출부가 있는 파일도 같은 그룹에 넣었습니다. 7-d-1 이 병합된 뒤 시작하며, ⓖ 그룹 A(조종)를 먼저 합니다.
ⓓ 의 비교 호출부는 섞은 비교를 허용하므로(3.1) 고치지 않아도 컴파일되지만, 반환형이 바뀌는 접근자의 호출부라 A 가 함께 읽고 확인합니다.
빌드는 동시에 둘까지라서(docs/11_Workflow.md) 작업자 다섯이 빌드 차례를 나눠 씁니다.

| 그룹 | 범위 | 파일 |
|---|---|---|
| A | 조종과 탈것 | `ControllerComponent.*`, `PawnComponent.*`, `IntentTrackControllerComponent.*`, `PlayerControllerComponent.*`, `ControlSystem.*`, `ControlIntentHistory.h`, `VehicleSeatComponent.*`, `MountUtil.cpp`, `GameplayAutomationSteps.cpp`, `VoxelPlayerComponent.cpp`, `FarmDirectorComponent.cpp`, `ArenaDirectorComponent.cpp`, ⓓ `getPawn()` 비교 호출부 `VoxelDirectorComponent.cpp`, `ShooterDirectorComponent.cpp`, `ShooterPlayerComponent.cpp` |
| B | 애니메이션과 캐릭터 | `SkeletalMeshComponent.*`, `PoseModifierComponent.*`, `PoseRetargetComponent.*`, `FacialAnimationComponent.*`, `MotionWarpingComponent.*`, `AnimationSystem.cpp`, `AnimationRewind.h`, `CharacterAppearanceComponent.*` |
| C | 상호작용, 기믹, UI, 능력, 파괴 복제 | `InteractorComponent.*`, `GimmickCircuitComponent.cpp`, `PlatformerGimmicks.cpp`, `HorrorStealthGimmicks.cpp`, `HealthBarComponent.cpp`, `DamageNumberComponent.cpp`, `AbilitySystemComponentEffect.cpp`, `AbilitySystemComponentAbility.cpp`, `GameInstanceBase.h`, `DestructionReplication.*` |
| D | 엔진 씬과 직렬화 | `SceneComponent.cpp`, `Scene.*`, `ObjectStateSerializer.cpp`, `Fracture*`(대상 타입을 확인한 것만) |
| E | 벤치 | `BenchScene.*`, `BenchSceneRig.cpp`, `BenchCombatComponent.cpp` |

- 그룹 A 와 C 는 `GameplayAutomationSteps.cpp` 를 함께 볼 수 있습니다. `pAI->getPawn()` 의 반환형이 바뀌면 그 파일이 따라 바뀌므로 A 가 가집니다.
- 그룹 B 의 `CharacterAppearanceComponent.cpp:421` 은 `SkeletalMeshComponent::setLeaderPose` 에 포인터를 넘깁니다. 핸들 API 가 아니므로 B 안에서 끝납니다.
- 7-c 의 매니저 순회 호출부는 API 가 그대로라 고치지 않습니다. 7-c 와 7-d-2 는 `SceneNavigationCooker.cpp` 밖에서 파일이 겹치지 않습니다.
- 그룹마다 지시문에 이 문서의 3.4 와 3.6 의 해당 줄, AGENTS.md 의 `Naming` 과 `C++ style` 절만 읽으라고 적습니다. "구조로 푼다" 원칙과 용어 대조표도 지시문에 넣습니다.

## 5. 검증

**7-c**

1. **전후 비교 테스트(새 테스트).** 무작위로 오브젝트를 만들고 지우고, 컴포넌트를 붙이고 떼고, `clearComponents`, `processDeferredDestruction`, `clear` 를 섞습니다. 단계마다 새 매니저 순회의 결과 집합을 전수 훑기(`forEachGameObject` 와 `isDerivedFrom`)의 결과 집합과 비교합니다.
   `TestComponentRegistry.cpp` 의 `matchesScan` 과 같은 패턴입니다. 파생 타입(`MeshComponent` 와 그 파생), 삭제 대기 컴포넌트, 삭제 대기 오브젝트, 이번 프레임에 만든 오브젝트를 모두 포함합니다.
2. **묘비 테스트.** 버킷이 비면 `_pType` 이 nullptr 인지, `destroyComponentsOfModule` 뒤에 그 모듈 타입의 버킷이 모두 비었는지 확인합니다. 저장소에 진단용 질의(타입 포인터를 가진 버킷 수)를 둡니다.
3. **기존 테스트.** 2.6 의 매니저 순회 31 줄이 있는 테스트와 그 파일의 게임 테스트를 CTest 로 돌립니다. `TestGameFramework.cpp:1145` 는 이미 두 순서를 허용합니다.
4. **순서가 결과에 나오는 곳.** 내비게이션 쿠킹은 대표 씬 하나를 7-c 전후로 쿠킹해 결과 파일이 같은지 비교합니다. 정렬 때문에 다르면 정렬 뒤 결과를 새 기준으로 삼습니다.
   `ComponentStateStore` 는 세이브를 쓰고 다른 프로세스에서 읽는 기존 테스트가 있는지 확인하고(미확인), 없으면 하나 둡니다.
5. **성능.** Release 에서 `InteractionBenchTest` 와 벤치 씬의 큐브 교체를 전후로 측정하고 p50 과 p99 를 적습니다. VSync 를 끄고, 셰이더를 다시 베이크한 뒤 측정합니다.

**7-d**

1. 7-d-1 의 새 테스트: 맞는 타입의 확인 변환 성공, 다른 타입의 타입 불일치 오류, 파괴된 대상의 대상 없음 오류, 무효 핸들의 성공, 업캐스트, `resolveComponent<T>` 의 Debug 단언.
2. 파서 테스트: `PROPERTY()` 의 `TComponentHandle<T>` 가 생성 오류인지(`ReflectionParserTest` 의 기존 오류 테스트 패턴).
3. 7-d-2 는 형식 변경이라 빌드가 첫 검증입니다. 그룹마다 바뀐 파일의 기존 테스트를 돌립니다(조종, 탈것, 상호작용, 애니메이션, 벤치).
4. 씬 저장과 로드 왕복, 에디터 되돌리기의 자식 연결 복원(`ObjectStateSerializer.cpp:78` 경로)을 기존 테스트로 확인합니다.
5. 9절이 요구한 네 백엔드와 대표 셋 실행은 `gpu-verifier` 가 맡습니다. 7-d 는 렌더 경로를 바꾸지 않으므로 벤치 씬과 Shooter 시나리오를 한 번씩 돌려 단언이 없는지 봅니다.
6. 7-d-3 을 할 때: 잘못된 타입의 핸들을 담은 씬이 로드 오류로 멈추는 테스트, 텍스트 왕복, 인스펙터 선택기 캡처를 `ux-reviewer` 로 확인합니다.

## 6. 위험

1. **잠금.** `destroyComponentInstance` 가 배타 잠금을 새로 잡습니다. 저장소 `_mutex` 를 쥔 채 이 함수에 오는 경로가 있으면 멈춥니다. 읽은 범위에서는 `clear` 와 `processDeferredDestruction` 모두 잠금 밖에서 부르지만, 전 경로는 확인하지 않았습니다(미확인). 2.4 의 단언이 Debug 에서 드러냅니다.
2. **모든 모듈 다시 빌드.** `GameObjectStore` 의 헤더 템플릿이 멤버 위치를 컴파일해 넣으므로(`GameObjectStore.h:8-10`) 7-c 와 7-d-1 은 게임, 킷, 에디터, RHI 를 함께 다시 빌드해야 합니다. 부분 빌드로 확인하지 않습니다.
3. **순서 변경.** 2.7 의 세 곳 밖에서 순서에 기대는 곳을 놓쳤을 수 있습니다. "추정"으로 적은 곳(`ShooterDirectorComponent.cpp:361`, `InteractionInterruptionSystem.cpp:57`, `TestInteraction.cpp:614`)은 7-c 작업자가 읽고 확정합니다.
4. **`Component` 크기.** 8 B 가 늘 수 있습니다. 풀 블록이 커지면 큐브 20,000 개 벤치의 캐시 효율이 바뀔 수 있으므로 5절의 성능 측정에서 함께 봅니다.
5. **`castTo` 의 nullptr 이 사라짐.** 3.4 의 `castTo` 17 곳 가운데 핸들에 다른 타입이 들어갈 수 있는 곳을 바꾸면, nullptr 로 넘어가던 경우가 Debug 단언이 됩니다. 결함이 드러나는 것이지만, 의도적으로 여러 타입을 받는 곳이면 타입 없는 채로 둬야 합니다(3.5).
6. **접근자 반환형 변경의 파급.** `getController()`, `getPawn()` 의 반환형을 바꾸면 그룹 A 밖의 호출부가 있을 수 있습니다. 이 문서는 `resolveComponent` 로 찾는 곳만 셌습니다. 그룹 A 작업자가 접근자 사용처를 먼저 찾습니다.
7. **두 개의 타입별 목록.** `ComponentRegistry`(등록형, 순서 유지, 채널)와 7-c 의 버킷(자동, 순서 없음)이 함께 있습니다. 7-c 뒤 `ComponentRegistry` 의 어느 타입이 버킷으로 충분한지는 따로 판단합니다. 합치면 "활성인 첫 방향광"처럼 등록 순서에 기대는 규칙(`ComponentRegistry.h:17`)을 다른 방법으로 지켜야 합니다.

## 7. 확인하지 못한 것과 결정이 필요한 것

- **결정됨**: 7-c 의 자료구조는 ReflectionFormats.md 7.2 의 조상 목록 대신 정확한 타입 버킷이고, 버킷은 풀 항목에 합칩니다(2.2, `design-reviewer` 검토). 언리얼 `GetObjectsOfClass` 의 "정확한 클래스 버킷과 파생 열거"와 같은 계열입니다. `09_Decisions.md` 에 한 줄로 남깁니다.
- **결정 필요**: 7-d-3 에서 파일 상태의 타입 핸들을 어떤 형태로 쓸지(3.7). 그 전까지 `PROPERTY` 타입 핸들은 생성 오류입니다.
- 미확인: 씬의 실제 버킷 수 K, `Component` 패딩, `AnimationSystem.cpp:299` 와 `Fracture*` 핸들의 출처, `ComponentHandle` 의 바이너리 형태와 해시 특수화, 주석 줄이 낀 `PROPERTY` 의 `ComponentHandle`.
- 백로그 후보: `ComponentStateStore` 가 다른 실행의 세이브를 열거 순서로 맞추는 구조(2.7), 리플렉션 구조체 필드 안의 `GameObjectHandle` 이 옮겨지지 않는 것(3.7 ⓕ), 등록형 목록을 버킷으로 옮기는 기준(2.5 ⓗ).
- 측정 질문(Release, VSync 끔, 3~5판 번갈아, p50 과 p99): 오브젝트 1만과 10만 × 정확한 타입 수 K = 50, 200, 500 합성의 `forEachComponentOfType`(말단 타입 대 기반 타입), 실제 8게임의 K,
  추가와 제거 잠금(`tickCostChurn`, 비동기 씬 로드 중 조회)의 전후 비교, `sizeof( Component )` 전후와 큐브 20,000 개 `GT.Frame`, 개발 빌드의 `resolveComponent<T>` 확인 비용(`BenchScene.cpp:1341`).
- 미확인: 비동기 씬 로드 워커가 `_listComponent` 에 넣는 동안 게임 스레드 순회가 같은 목록을 읽는지.

## 8. 7-c 와 7-d 뒤의 남은 정리 (2026-10-18 설계, 백로그 C-52)

### 8.1 핸들 옮기기를 값 방문으로 (①, 끝)

`ObjectStateBatch::resolveObjectReferences` 가 리플렉션 값 방문(`Engine/Reflection/ReflectedValueWalk.h`)으로 구조체 필드 · 컨테이너의 모든 깊이를 옮깁니다. 같은 방문은 쿠킹 의존성 수집(C-42)에도 씁니다.

### 8.2 컴포넌트 상태 저장소 짝짓기 (②, 끝)

`ComponentStateStore::appendComponentIDRemap` 이 안정 키(`이름#n`)와 타입으로 잇습니다. 오브젝트 id 묶음이 키를 싣습니다(게임 상태 봉투 4 판).

### 8.3 등록형 목록을 버킷으로 (③)

`ComponentRegistry` 와 `TickBatchRegistry` 의 목록마다 세 기준을 표로 적고, 셋 다 "아니요" 인 목록만 7-c 의 타입 버킷 순회로 옮깁니다.
1. 등록 순서가 결과에 나오는가(예: 활성인 첫 방향광, `ComponentRegistry.h:17`).
2. 채널(같은 타입 안의 하위 목록)이 있는가.
3. 잠금 없이 읽어야 하는가(렌더 스레드, 워커).
표 만들기는 읽기만 하는 일이라 `scout` 에 맡깁니다. 옮기는 일은 ④ 의 측정에서 버킷 순회가 등록형 목록보다 느리지 않다는 것을 확인한 뒤에 합니다.

### 8.4 측정 (④)

Release, VSync 끔, 3 ~ 5 판 번갈아 실행, p50 과 p99 로 적습니다. 셰이더는 측정 전에 다시 베이크합니다.
- 합성 장면: 오브젝트 1만과 10만 × 정확한 타입 수 K = 50, 200, 500 의 `forEachComponentOfType`(말단 타입과 기반 타입).
  타입 500 개를 리플렉션에 등록하는 방법(템플릿 인스턴스 등록이 되는지)은 구현 때 확인합니다 [추정].
- 추가와 제거의 잠금 비용(`tickCostChurn`, 비동기 씬 로드 중 조회), `sizeof( Component )` 전후, 큐브 20,000 개 `GT.Frame`, 개발 빌드의 `resolveComponent<T>` 확인 비용(`BenchScene.cpp:1341`).

### 8.5 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| T3 | 합성 장면 벤치를 만들고 ④ 를 측정합니다 | `Test/Benchmark/`(또는 기존 벤치 실행 파일), `docs/08_Verification.md` | 없음 | 벤치가 Release 에서 돕니다 | 필요합니다(위 8.4) |
| T4 | ③ 의 표를 만들고, 기준을 통과한 목록을 버킷 순회로 옮깁니다 | `Engine/Object/GameObject/Registry/`, 옮길 목록의 호출부 | T3 | 기존 레지스트리 테스트, 전후 결과 집합 비교 테스트 | T3 의 같은 벤치로 전후 비교 |

컴포넌트 핸들은 여전히 `PROPERTY` 에서 막혀 있습니다(7-d-3 결정 대기).

### 8.6 사용자 결정

없습니다. 7절의 "7-d-3 파일 상태 형태" 는 C-52 범위 밖이고 그대로 열려 있습니다.

## 구현 전에 반영할 검토 결과

`design-reviewer` 검토 결과입니다. 결론: 고쳐서 진행합니다.
7-c 의 "정확한 타입마다 버킷 + 조회 때 버킷마다 `isDerivedFrom`"은 이 규모(오브젝트 1만~10만, 타입 수백)에서 조상 목록 원안보다 낫고, 언리얼 `GetObjectsOfClass` 의 "정확한 클래스 버킷 + 파생 열거"와 같은 계열입니다. 단 구현 전에 고칠 것이 있습니다.

- **ⓐ 버킷을 컴포넌트 풀 항목(`_mapComponentPool`, 이미 FQN 키)에 합칩니다**(`{ 풀, 버킷 목록, _pType }`, `Component::_pPool` 을 그 항목 포인터로).
  맵 하나가 줄고 추가 때 해시 조회가 없으며, `Component` 에는 위치 `uint32` 하나만 늘어 패딩에 들어갈 가능성이 큽니다(`_pPool` 사용처는 `GameObjectStore.cpp:441`, `GameObject.cpp:570-588`, `GameObject.h:468`뿐, `sizeof` 미측정).
  `Component.h` 와 `GameObjectStore.h` 가 바뀌므로 7-c 도 전체 빌드가 필요합니다.
- **ⓑ 7-d 전제 "한 핸들이 가리키는 타입은 바뀌지 않는다"에 구멍이 있습니다.**
  id 복원(`takeRestoredComponentID`)이 FQN 이 아니라 `TypeInfo::_name` 으로 맞춥니다(`GameObject.cpp:36-52`, `Component.cpp:351-355`, `ObjectStateSerializer.cpp:469`, `_name` 이 짧은 이름인지는 미확인).
  복원 키를 FQN 으로 바꾸고(7-d-1 보다 먼저 또는 함께) `resolveComponent<T>` 확인을 Debug 만이 아니라 Shipping 이 아닌 모든 구성에 둡니다(타입이 바뀌는 길인 핫 리로드는 Shipping 에 없음).
- **ⓒ 7-d-1 API.** `executeOrDeferPostTickOnComponent` 를 `( TComponent& self, func )` 로 바꾸고 `TComponent` 는 추론하게 합니다.
  호출부 23곳(약 20파일: WildWest, VoxelPlayer, VoxelChunk, ShooterPlayer, Water, Terrain, Foliage, AnimationNotify, PropScatter, Vehicle 3, WesternHorseMount, TileMapRenderer, CharacterAppearance, GameDirector, CameraDirector, CameraManager)에서 `getHandle()` 과 템플릿 인자가 함께 사라지고, 둘이 어긋나는 일이 형식으로 막힙니다.
  이 파일들이 7-d-2 그룹과 겹치므로 7-d-1 에서 한 번에 끝내고 그룹은 그 뒤에 시작합니다.
- **ⓓ 그룹 A(조종)의 파일 목록에 `getPawn()` 비교 호출부가 빠졌습니다.** `VoxelDirectorComponent.cpp:118,125`, `ShooterDirectorComponent.cpp:510,519`, `ShooterPlayerComponent.cpp:150`(그 밖에 `FarmDirectorComponent.cpp:513`, `ArenaDirectorComponent.cpp:548`).
  타입 핸들과 타입 없는 `getHandle()` 의 비교를 섞은 `==` 허용으로 할지 `untyped()` 명시로 할지 3.1 에 정합니다(비교가 주소를 쓰지 않아 섞은 비교는 안전하다고 추정).
- **ⓔ 저장 id 공간의 비우기 폴백(`ObjectStateSerializer.cpp:651-658`).** 사용처가 0 이므로 7-d-3 의 형식 결정을 기다리지 말고 7-d-1 에서 처리합니다.
  `PROPERTY` 의 타입 없는 `ComponentHandle`(컨테이너 원소 · 키 · 중첩 구조체 필드 포함)을 파서 생성 오류로 막고(`Source` 사용처 0, 테스트 `TestSampleActor.h:607` 하나), 남는 "옮길 수 없는 컨테이너 모양의 `GameObjectHandle`" 비우기는 경고 대신 로드 오류로 바꿉니다.
- **ⓕ 같은 근본 원인.** `resolveObjectReferences` 가 컴포넌트 타입의 최상위 `PROPERTY` 만 봅니다(`ObjectStateSerializer.cpp:623-643`, `holdsComponentHandle` `:210-221`).
  리플렉션 구조체 안의 핸들은 다시 매겨지지도 비워지지도 않을 수 있습니다(그런 필드가 실제로 있는지는 미확인).
- **ⓖ 7-d-2 는 API 경계를 넘는 핸들(인자 · 대기 항목 · 접근자 반환값)이 결함이 숨을 곳입니다.**
  그룹 A(조종: `ControlSystem::queuePossess( const ComponentHandle& controller, const ComponentHandle& pawn )` 가 서로 다른 타입의 핸들 둘을 같은 형식으로 받아 인자 바꿔치기가 실제 후보)부터 하고, 3.4 표에 "핸들이 `T*` 에서 만들어졌는가" 열을 더합니다.
- **ⓗ 타입별 목록이 `ComponentRegistry` · `TickBatchRegistry` · 버킷 셋이 되어** `09_Decisions.md:731` 의 근거(훑기 190 µs)가 약해집니다. 어느 등록형 목록을 버킷으로 옮길지 기준은 8.3(`C-52` ③)에서 정합니다.
  B 안(조회 타입마다 맞는 버킷 목록 캐시)은 측정 뒤로 미룹니다. 넣을 때는 타입 레지스트리 세대로 무효화합니다.
- **측정 질문**(Release, VSync 끔, 3~5판 번갈아, p50 · p99): 오브젝트 1만 · 10만 × 정확한 타입 수 K = 50 · 200 · 500 합성의 `forEachComponentOfType`(말단 타입 대 기반 타입), 실제 8게임의 K, 추가 · 제거 잠금(`tickCostChurn`, 비동기 씬 로드 중 조회) A/D 비교, `sizeof(Component)` 전후와 큐브 20,000 개 `GT.Frame`, 개발 빌드 `resolveComponent<T>` 확인 비용(`BenchScene.cpp:1341`). 8.4 와 같은 항목입니다.
- **확인하지 못한 것.** 7-b(다중 상속 금지)가 7-c 보다 먼저인지는 `reflect7ab` 병합으로 충족될 전제입니다(버킷에서 꺼낸 포인터를 `static_cast` 로 바꾸는 전제, 병합 여부는 확인 필요). 비동기 씬 로드 워커가 `_listComponent` 에 넣는 동안 게임 스레드 순회가 같은 목록을 읽는지도 미확인입니다.
