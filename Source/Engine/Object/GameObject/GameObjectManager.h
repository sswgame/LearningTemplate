/**
 * @file GameObjectManager.h
 * @brief 씬 안의 GameObject 생성 · 조회 · 지연 삭제를 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/PagedArray.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/DeferredDelegateQueue.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/LightRegistry.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Object/GameObject/TickRegistry.h"
#include "Engine/Physics/PhysicsWorld.h"

namespace sw
{
    class BoxCollider2DComponent;
    class Component;
    class GameObjectManager;
    class MeshComponent;
    class SceneComponent;

    /// @brief GameObject 등록 · 지연 삭제와 씬의 등록부(트랜스폼 계층 · 프리미티브 · 빛 · 틱)를 소유합니다.
    class SW_API GameObjectManager
    {
        friend class GameObject;
        friend class SceneComponent;
        friend class MeshComponent;

    public:
        /** @brief 엔진과 모듈의 컴포넌트 팩토리를 등록하며 만듭니다. 오브젝트는 없는 채로 시작합니다. */
        GameObjectManager();
        /** @brief 등록된 오브젝트를 모두 파괴합니다. */
        ~GameObjectManager();

        /** @brief 새 GameObject 를 만들고 등록합니다. */
        GameObject* createGameObject( hashed_string name = hashed_string( "GameObject" ) );

        /**
         * @brief 앞서 발급한 objectId 를 그대로 써서 오브젝트를 다시 만듭니다. 되돌리기 · 플레이 세션 복원 · 핫 리로드가 씁니다.
         * @details 핸들(`GameObjectHandle` · `ComponentHandle`)은 objectId 로 대상을 찾으므로, 되살린 오브젝트가 같은 id 를 받아야
         *          그 너머로도 핸들이 이어집니다. 그 id 로 등록된 오브젝트가 아직 있으면(삭제 대기 포함) 새 id 를 쓰고 경고를 남깁니다.
         *          옛 오브젝트의 지연 파괴가 나중에 id 로 정리하는 항목(슬롯 표 · 에디터 GUID 맵 등)이 새 오브젝트 몫까지 지우지
         *          않게 하기 위해서입니다. 발급 카운터는 그 id 뒤로 밀어 앞으로의 발급과 겹치지 않게 합니다.
         * @return 만든 오브젝트입니다. 실제로 받은 id 는 `getObjectId()` 로 확인합니다.
         */
        GameObject* createGameObjectWithId( hashed_string name, uint64 objectId );

        /**
         * @brief 등록된 GameObject 의 이름이 바뀐 것을 이름 맵에 반영합니다.
         * @details GameObject::setName 이 부릅니다.
         */
        void notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName );

        /** @brief 이름으로 GameObject 를 찾습니다. */
        GameObject* findGameObjectByName( hashed_string name ) const;

        /** @brief 오브젝트 ID 로 GameObject 를 찾습니다. 락이 없습니다(칸이 그 id 를 들고 있으면). */
        GameObject* findGameObjectById( uint64 objectId ) const;

        /**
         * @brief 락 없는 id 표의 칸 수입니다. 칸은 id 의 아래 비트(`id % kObjectSlotCount`)입니다.
         * @details 이만큼 떨어진 id 둘이 함께 살아 있을 때만 뒤에 온 것이 맵(잠금 + 해시)으로 갑니다 — `getOverflowObjectCount`.
         */
        static constexpr uint64 kObjectSlotCount = 1ull << 22;
        /** @brief 표의 칸을 다른 오브젝트가 써서 맵에 든 오브젝트 수입니다. 진단 · 회귀 테스트용입니다(보통 0). */
        uint32 getOverflowObjectCount() const { return _overflowObjectCount.load( std::memory_order_relaxed ); }

        /**
         * @brief 살아 있는 오브젝트를 outList 에 채웁니다(부르는 쪽 버퍼 재사용).
         * @details 같은 클래스의 findGameObjectsByTag 와 같은 규약입니다. 값 반환형은 호출마다 씬
         *          전체를 새로 할당 · 복사하므로, 매 프레임 도는 곳이나 두 번 이상 쓰는 곳은 이쪽을
         *          씁니다. 순회만 하면 되는 곳은 forEachGameObject 가 복사조차 하지 않습니다.
         */
        void getAllGameObjects( vector<GameObject*>& outListGameObject ) const;

        /** @brief 살아 있는 오브젝트 목록을 새 벡터로 반환합니다(한 번만 쓰는 곳 전용). */
        vector<GameObject*> getAllGameObjects() const;

        /**
         * @struct WalkScope
         * @brief `forEachGameObject` 가 공유 잠금을 쥔 동안을 표시합니다(스레드별 깊이). 그 안에서 구조를 바꾸는 호출은 Debug 에서 단언합니다.
         * @details `_mutex` 는 재진입하지 않습니다. 순회 콜백이 오브젝트를 만들거나(배타 잠금) 컴포넌트를 붙이면(풀 맵의 배타 잠금) 같은
         *          스레드가 제 공유 잠금을 기다리며 **멈춥니다** — 에디터 Play 가 그렇게 멈췄습니다(beginPlay → onBeginPlay → addTag →
         *          addComponent). 멈추는 대신 단언으로 알립니다. 깊이는 엔진 쪽 한 칸이라 모듈이 순회를 인스턴스화해도 같은 칸을 셉니다.
         */
        struct SW_API WalkScope
        {
            WalkScope();
            ~WalkScope();
            WalkScope( const WalkScope& )            = delete;
            WalkScope& operator=( const WalkScope& ) = delete;
            /** @brief 지금 스레드가 `forEachGameObject` 안에 있으면 true 입니다. */
            static bool isInsideWalk();
        };

        /**
         * @brief 힙 할당 없이 등록된 모든 유효한 GameObject 를 순회합니다.
         * @note 공유 잠금을 쥔 채 콜백을 부릅니다. 콜백 안에서 오브젝트를 만들거나 지우거나 컴포넌트를 붙이면 안 됩니다(`WalkScope`) —
         *       그런 일을 하는 순회는 `getAllGameObjects( out )` 로 목록을 받아 잠금 없이 돕니다(`beginPlay` 처럼).
         */
        template <typename Func>
        void forEachGameObject( Func&& func ) const
        {
            std::shared_lock<std::shared_mutex> lock{ _mutex };
            const WalkScope                     walkScope{};
            for ( GameObject* pObj : _listGameObject )
            {
                if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                    func( pObj );
            }
            for ( GameObject* pObj : _listPendingAdd )
            {
                if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                    func( pObj );
            }
        }

        /** @brief 힙 할당 없이 씬의 모든 유효한 Component 를 순회합니다. */
        template <typename Func>
        void forEachComponent( Func&& func ) const
        {
            forEachGameObject( [&func]( GameObject* pObj )
            {
                pObj->forEachComponent( func );
            } );
        }

        /** @brief 힙 할당 없이 씬에서 TComponent 와 그 파생 컴포넌트를 순회합니다. TypeInfo 는 씬 전체에 한 번만 구합니다. */
        template <typename TComponent, typename Func>
        void forEachComponentOfType( Func&& func ) const
        {
            const TypeInfo* pToType = findStaticType<TComponent>();
            forEachGameObject( [&func, pToType]( GameObject* pObj )
            {
                pObj->forEachComponentOfType<TComponent>( pToType, func );
            } );
        }

        /** @brief 태그를 가진 첫 GameObject 를 반환합니다. 없으면 nullptr 입니다. */
        GameObject* findGameObjectByTag( TagID tag ) const;

        /** @brief 태그를 가진 GameObject 를 outListGameObject 에 넣습니다. */
        void findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const;

        /**
         * @brief 플레이를 시작합니다 — 살아 있는 오브젝트의 컴포넌트마다 onBeginPlay 를 한 번 부르고, 이후 붙는 컴포넌트는 다음 틱 단계에서 시작합니다.
         * @details 보통은 직접 부르지 않고 `SceneManager::setWorldPlaying` · 활성 씬 교체가 부릅니다. 두 번 불러도 컴포넌트마다 한 번입니다.
         */
        void beginPlay();

        /** @brief 플레이를 끝냅니다 — 시작했던 컴포넌트마다 onEndPlay 를 한 번 부르고, 시작을 기다리던 줄을 비웁니다. */
        void endPlay();

        /** @brief 플레이 중(`beginPlay` 뒤, `endPlay` 전)이면 true 입니다. 이때 붙는 컴포넌트는 다음 틱 단계에서 onBeginPlay 를 받습니다. */
        bool hasBegunPlay() const { return _bHasBegunPlay.load( std::memory_order_acquire ); }

        /**
         * @brief 계층을 지키는 병렬 틱입니다.
         * @details 0) 지연 파괴 처리, 새로 만든 오브젝트 병합
         *          1) SceneComponent 월드 캐시 플러시(루트 → 자식, 더티 서브트리만)
         *          2) 병렬 Component 틱(트랜스폼 캐시는 읽기 전용, 구조 변경은 지연)
         *          3) 지연된 attach · detach → 틱 중 쌓인 트랜스폼 쓰기 → 지연 큐(deferPostTick) 실행 → 병합,
         *             더티면 다시 플러시, 마지막으로 지연 파괴 처리
         *          1) · 2) 는 오브젝트가 있을 때만 돌고, 3) 은 늘 돕니다. 단계마다 `GT.Scene.tick.*` 프로파일 스코프가 있습니다.
         */
        void tick( float32 deltaTime );

        /**
         * @brief 트랜스폼 계층입니다(루트 목록 · 더티 세대 · 플러시 알고리즘). 매니저는 소유하고 tick 의 단계만 정합니다.
         * @details `PhysicsWorld` · `PrimitiveRegistry` 와 같은 자리입니다. 능력은 별도 타입이 갖고, 매니저는 순서만 정합니다. 병렬 시스템을
         *          하나 더하려면 이런 타입 하나와 tick 의 한 줄이면 됩니다. 매니저가 그 알고리즘을 알 필요가 없습니다.
         */
        SceneTransformHierarchy&       getTransformHierarchy() { return _transformHierarchy; }
        const SceneTransformHierarchy& getTransformHierarchy() const { return _transformHierarchy; }

        /** @brief 더티 루트의 SceneComponent 월드 캐시를 계층 순으로 갱신합니다. `getTransformHierarchy().flush()` 와 같습니다. */
        void flushSceneTransforms() { _transformHierarchy.flush(); }

        /** @brief 플러시를 기다리는 더티 루트가 하나라도 있으면 true 입니다. */
        bool hasDirtySceneTransforms() const { return _transformHierarchy.hasDirty(); }

        /**
         * @brief 트랜스폼 쓰기 여러 건을 한 번에 적용합니다. 건수가 많으면 워커에 나눕니다.
         * @details 세터를 컴포넌트마다 부르는 것과 결과가 같습니다(값이 같으면 건너뛰고, 바뀌면 더티 표시). 다른 것은
         *          (1) 핸들 해석 · 필드 쓰기 · 더티 표시가 워커에서 나란히 돌고 (2) 세대는 배치 끝에 한 번 오른다는 것입니다.
         *          핸들이 씬 컴포넌트가 아니거나 죽었으면 그 건은 건너뜁니다.
         *          **틱 중에는 부를 수 없습니다.** 워커가 트랜스폼을 읽는 구간이라 쓰면 안 되고, 구조 변경이 미뤄지는 구간이라
         *          부모 사슬이 흔들립니다. 그때는 건마다 세터로 돌립니다(세터가 틱 중 쓰기 길을 탑니다). 알고리즘은
         *          `SceneTransformHierarchy::applyBatch` 에 있습니다.
         * @return 실제로 값이 바뀐 건수입니다.
         */
        uint32 applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count );

        /**
         * @brief 컴포넌트 틱 중이면 true 입니다. 이때 구조 변경(GameObject 생성 · addComponent · attach · detach)은 미뤄지고,
         *        트랜스폼은 읽기 전용이라 세터가 쓰기 큐로 갑니다.
         * @details 구조 동결과 트랜스폼 읽기 전용은 같은 구간이라 플래그 하나(`_bTicking`)가 둘 다 답합니다.
         */
        bool isStructuralMutationFrozen() const { return _bTicking.load( std::memory_order_acquire ); }

        using StructuralChangeDelegate = DeferredDelegateQueue::Callback;
        using PostTickDelegate         = DeferredDelegateQueue::Callback;

        /**
         * @brief 틱 중의 구조 변경(컴포넌트 추가 · 부착 · 떼기 · 태그 · 활성)을 **부른 순서대로** 지연 큐에 넣습니다. 틱 직후 가장 먼저 돕니다.
         * @details 구조 변경은 이 큐 하나다. 주의: 종류마다 큐를 나누면 틱 안에서 씬 컴포넌트를 붙이고(미뤄짐) 이어 부모에 붙일 때 부착이
         *          먼저 돌아 붙일 씬 컴포넌트가 없고, 오브젝트는 루트로 남는다.
         */
        void deferStructuralChange( StructuralChangeDelegate func );

        /**
         * @brief 병렬 틱 중의 트랜스폼 쓰기 한 건을 슬롯 큐에 올립니다. 세터가 `isStructuralMutationFrozen()` 일 때 부릅니다.
         * @details 큐는 `SceneTransformHierarchy` 의 것이고, 틱 뒤 `SceneTransformHierarchy::applyTickWrites` 가 `applyTransformBatch` 와 같은
         *          병렬 적용을 돕니다. 스크래치 슬롯을 받지 못한 스레드(도우미 칸이 다 찬 드문 경우)만 지연 델리게이트로 갑니다.
         */
        void queueTransformWrite( const SceneTransformWrite& write );

        /**
         * @brief 이 스레드가 지금 틱하고 있는 오브젝트입니다. 오브젝트 그룹 틱(보통 경로)이 항목을 도는 동안만 채워지고, 그 밖에서는 nullptr 입니다.
         * @details 씬 컴포넌트의 세터가 "내 오브젝트를 틱하는 스레드인가" 를 묻습니다. 그렇다면 한 오브젝트의 항목은 한 워커가 도므로
         *          칸의 대기 자리에 잠금 없이 바로 쓰고, 아니면(다른 오브젝트의 컴포넌트 · 선행 조건 스테이지 경로) 쓰기 큐로 갑니다.
         */
        static const GameObject* getTickingObject();

        /**
         * @brief 병렬 틱이 끝난 뒤 메인 스레드에서 실행할 작업을 넣습니다.
         * @details GameObject 생성 · addComponent · 데미지 · 태그 변경 같은 구조 · 공유 상태 변경에 씁니다.
         */
        void deferPostTick( PostTickDelegate func );

        /**
         * @brief 구조 변경이 얼어 있으면 deferPostTick 으로 미루고, 아니면 바로 실행합니다.
         * @details createGameObject + addComponent + 초기화를 한 람다로 묶을 때 씁니다.
         */
        void executeOrDeferPostTick( PostTickDelegate func );

        /** @brief SceneComponent 가 루트가 됐음을 트랜스폼 계층에 알립니다(더티면 플러시 목록에 오릅니다). */
        void registerRootSceneComponent( SceneComponent* pComp );

        /** @brief 부모가 생기거나 파괴된 SceneComponent 를 트랜스폼 계층의 플러시 목록에서 뺍니다. */
        void unregisterRootSceneComponent( SceneComponent* pComp );

        /**
         * @brief 그릴 수 있는 컴포넌트의 등록부입니다.
         * @details PhysicsWorld 와 같은 자리입니다. 능력은 별도 타입으로 두고 매니저는 그것을 소유만
         *          합니다. 컴포넌트는 등록 시점에 이것을 받아 들고 있으므로, 매니저 전체를 알 필요가 없습니다.
         */
        PrimitiveRegistry& getPrimitiveRegistry() { return _primitiveRegistry; }
        /** @brief 그릴 수 있는 컴포넌트의 등록부입니다. */
        const PrimitiveRegistry& getPrimitiveRegistry() const { return _primitiveRegistry; }

        /**
         * @brief 애니메이션 유닛(`SkeletalMeshComponent`)을 평가하는 시스템입니다.
         * @details PrimitiveRegistry 와 같은 자리입니다 — 유닛은 등록 때 이것을 받고, 매니저는 tick 의 단계(틱 뒤 · 트랜스폼 플러시 앞)만 정합니다.
         */
        AnimationSystem&       getAnimationSystem() { return _animationSystem; }
        const AnimationSystem& getAnimationSystem() const { return _animationSystem; }

        /**
         * @brief 빛 컴포넌트의 등록부입니다.
         * @details 프리미티브와 같은 이유로 있습니다. 매 프레임 씬을 뒤져 빛을 **찾지** 않고, 빛이 붙을
         *          때 **등록받습니다**. 자세한 사연은 LightRegistry.h 에 있습니다.
         */
        LightRegistry& getLightRegistry() { return _lightRegistry; }
        /** @brief 빛 컴포넌트의 등록부입니다. */
        const LightRegistry& getLightRegistry() const { return _lightRegistry; }

        /**
         * @brief 카메라 컴포넌트의 등록부입니다. 역할 · 우선순위로 고르는 규칙(`selectCamera`)도 여기 하나입니다.
         * @details 빛과 같은 이유로 있습니다 — 게임 · 에디터 카메라 선택이 씬 전체를 훑지 않습니다. 자세한 사연은 CameraRegistry.h 에 있습니다.
         */
        CameraRegistry& getCameraRegistry() { return _cameraRegistry; }
        /** @brief 카메라 컴포넌트의 등록부입니다. */
        const CameraRegistry& getCameraRegistry() const { return _cameraRegistry; }

        /**
         * @brief 틱에 참여하는 오브젝트의 등록부입니다(언리얼 `FTickTaskManager` 의 자리). 자세한 사연은 TickRegistry.h 에 있습니다.
         * @details 같은 규칙으로 소유만 합니다. 컴포넌트가 틱을 켜고 끄면 소유 오브젝트가 여기에 표시하고, `tick` 이 디스패치 전에
         *          표시된 오브젝트만 다시 훑습니다. 씬 전체를 훑어 스테이지를 다시 짓던 0.5~1 ms 가 사라진 자리입니다.
         */
        TickRegistry& getTickRegistry() { return _tickRegistry; }
        /** @brief 틱에 참여하는 오브젝트의 등록부입니다. */
        const TickRegistry& getTickRegistry() const { return _tickRegistry; }

        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. 삭제 예정이면 nullptr 입니다. */
        Component* resolveComponent( ComponentHandle handle );

        /** @brief 핸들이 가리키는 오브젝트를 찾습니다. 파괴됐거나 삭제 대기면 nullptr 입니다. 락을 잡지 않습니다(`findGameObjectById`). */
        GameObject* resolveGameObject( GameObjectHandle handle ) const { return findGameObjectById( handle.objectId() ); }

        /**
         * @brief 이 씬의 강체 물리입니다(3D · 2D 물리 씬 · 고정 스텝 · 물리 컴포넌트 · 접촉 이벤트). 처음 쓸 때 씬을 만듭니다.
         * @details 겹침 월드(`getPhysicsWorld`)와 따로 돕니다 — 둘 다 `stepPhysics` 에서 이 순서(겹침 → 강체)로 한 번씩 진행합니다.
         */
        ScenePhysics& getScenePhysics() { return _scenePhysics; }
        /** @brief 이 씬의 강체 물리입니다. */
        const ScenePhysics& getScenePhysics() const { return _scenePhysics; }

        /** @brief 이 씬의 AABB 질의 월드입니다. */
        PhysicsWorld& getPhysicsWorld() { return _physicsWorld; }
        /** @brief 이 씬의 AABB 질의 월드입니다. */
        const PhysicsWorld& getPhysicsWorld() const { return _physicsWorld; }

        /**
         * @brief GameObject 를 지연 삭제 큐에 넣습니다.
         * @param pObj 삭제할 게임 오브젝트
         * @param bDestroyChildren 자식 오브젝트도 함께 지울지 여부
         */
        void destroyObject( GameObject* pObj, bool bDestroyChildren = true );

        /** @brief Component 를 지연 삭제 큐에 넣습니다. 처리 때 핸들로 다시 찾으므로, 그 사이 다른 경로가 먼저 해제해도 안전합니다. */
        void destroyComponent( Component* pComp );

        /**
         * @brief 목록에서 이미 뺀 컴포넌트를 해체합니다: `onUnregister`(정확히 한 번) → `onDestroy` → 소유자 끊기 → 소멸 → 풀 · 힙 반납.
         * @details 컴포넌트 해체는 이 함수 하나를 지납니다. 목록에서 빼는 일은 `GameObject::removeComponent` · `clearComponents` 가 합니다.
         */
        void destroyComponentInstance( Component* pComp );

        /** @brief 지연 삭제 큐의 오브젝트 · 컴포넌트를 실제로 해제합니다. */
        void processDeferredDestruction();

        /** @brief 등록·대기 목록을 모두 비웁니다. */
        void clear();

        /** @brief 이번 프레임에 추가된 GameObject 를 활성 목록에 합칩니다. */
        void mergePendingAdds();

#if !defined( SW_SHIPPING )
        /**
         * @brief 모듈이 내려가기 전에, 그 모듈이 정의한 컴포넌트 타입의 **살아 있는 인스턴스**를 모두 지웁니다.
         * @details 팩토리 · 타입 · 전역 변수는 등록 해제되지만 씬은 엔진이 소유해 모듈보다 오래 삽니다. 인스턴스가 남으면
         *          vtable 이 사라진 객체가 씬에 남아 다음 틱 · 소멸에서 없는 코드로 뛰어듭니다. 지연 파괴 목록에 남은 것도
         *          그 소멸이 모듈 코드이므로 먼저 지금 처리합니다. 틱 밖에서만 부릅니다(구조 변경이 얼려 있으면 미뤄질 뿐입니다).
         * @return 지운 컴포넌트 수입니다.
         */
        uint32 destroyComponentsOfModule( string_view moduleName );
#endif

        /**
         * @brief 타입 이름(짧은 이름 · FQN · 옛 이름 별칭, `이름#번호` 꼬리는 무시)으로 컴포넌트를 만들어 붙입니다. 에디터 · 직렬화 전용이고, 게임은 addComponent<T> 를 씁니다.
         * @details 생성 함수는 리플렉션 표 하나에서 찾습니다(`TypeRegistry::findType` → `TypeInfo::_addComponent`). 매니저가 따로 든 팩토리 표는 없으므로
         *          모듈이 올라오거나 내려가면 모든 매니저가 그 즉시 같은 답을 냅니다.
         */
        Component* addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning = true );

        /** @brief 모든 오브젝트의 틱 항목을 다음 틱 전에 다시 짓게 합니다(타입 재바인딩 · 씬 초기화). */
        void markTickStagesDirty() { _tickRegistry.markAllDirty(); }
        /**
         * @brief 틱 등록부가 오브젝트 항목을 다시 지은 틱의 수입니다. 진단 · 회귀 테스트용입니다.
         * @details 틱에 참여하는 컴포넌트(`Component::hasTickWork`)가 생기거나 없어지거나 순서가 바뀔 때만 올라야 합니다.
         *          틱하지 않는 MeshComponent 를 붙였다 떼는 것으로는 오르지 않습니다. 다시 지을 때도 바뀐 오브젝트의 컴포넌트 몇 개만 훑습니다.
         */
        uint32 getTickStageBuildCount() const { return _tickStageBuildCount.load( std::memory_order_relaxed ); }

        /** @brief 이름으로 만들 수 있는 컴포넌트 타입(리플렉션 표에서 `_addComponent` 가 있는 타입)의 짧은 이름 목록입니다. 에디터의 "Add Component" 가 씁니다. */
        static vector<hashed_string> getRegisteredComponentTypeNames();

        /**
         * @brief 물리 바디를 맞출 콜라이더를 등록합니다(`BoxCollider2DComponent::onRegister`). 매니저가 step 직전에 한 번에 맞춥니다.
         * @details 콜라이더가 병렬 틱에서 제 바디를 맞추면 같은 그룹에서 겹침을 묻는 쪽이 스케줄에 따라 옛 · 새 자리를 본다.
         */
        void registerCollider( BoxCollider2DComponent* pCollider );
        /** @brief 콜라이더 등록을 풉니다. 멱등입니다. */
        void unregisterCollider( BoxCollider2DComponent* pCollider );
        /** @brief 등록된 콜라이더 목록입니다(순서 없음). 틱 밖에서 읽습니다 — 에디터 시각화가 씬 전체를 훑지 않고 이것을 봅니다. */
        const vector<BoxCollider2DComponent*>& getColliders() const { return _listCollider; }

        /** @brief 트랜스폼이 바뀌었음을 알려 세대를 올립니다(`getTransformHierarchy().notifyDirtied()`). */
        void notifyTransformDirtied() { _transformHierarchy.notifyDirtied(); }
        /** @brief 현재 트랜스폼 더티 세대 번호를 반환합니다. */
        uint64 getTransformGeneration() const { return _transformHierarchy.getGeneration(); }

    private:
        /**
         * @struct NameEntry
         * @brief 이름 표의 값 — 오브젝트와, 번호를 붙여 만든 이름이면 그 밑 이름과 번호입니다.
         */
        struct NameEntry
        {
            GameObject*   _pObject{ nullptr };
            hashed_string _baseName{};  ///< 번호를 붙인 밑 이름(`Bullet_7` 이면 `Bullet`). 번호를 붙이지 않았으면 비어 있습니다
            uint32        _suffix{ 0 }; ///< 붙인 번호입니다. 0 이면 번호를 붙이지 않은 이름입니다
        };

        /**
         * @struct NameSuffixState
         * @brief 밑 이름 하나의 번호 상태 — 다음 새 번호와, 지운 오브젝트가 돌려준 번호들입니다.
         * @details 주의: 번호를 오르기만 하게 두면 안 된다. 번호마다 `hashed_string` 을 **새로 인턴**하는데 인턴 풀은 전역 65,536 칸이고
         *          한번 들어간 문자열은 나가지 않아, 같은 이름으로 스폰 · 파괴를 거듭하는 게임(총알)이 결국 풀을 채우고 그 뒤로 엔진의
         *          **모든** 새 `hashed_string`(리소스 경로 · 태그 · 프로퍼티 이름)이 None 이 된다. 되쓰면 번호 수는 같은 이름으로
         *          동시에 살아 있던 오브젝트 수를 넘지 않습니다. 되쓰기도 O(1) 입니다(맨 뒤에서 꺼낸다).
         */
        struct NameSuffixState
        {
            uint32         _nextSuffix{ 2 };  ///< 빈 번호가 없을 때 쓸 다음 새 번호입니다. 첫 중복이 `_2` 입니다
            vector<uint32> _listFreeSuffix{}; ///< 지운 오브젝트가 돌려준 번호들입니다
        };

        /**
         * @brief 등록부의 오브젝트를 TickGroup 순으로 틱합니다.
         * @details 보통은 그룹마다 오브젝트 목록을 한 번의 포크-조인으로 나눕니다(한 오브젝트의 항목은 한 워커가 순서대로).
         *          서브틱 선행 조건이 하나라도 있으면 등록부가 지은 DAG 스테이지를 차례로 돕니다. 그 캐시는 등록부 세대로 무효화됩니다.
         */
        void tickComponents( float32 deltaTime );
        /** @brief 플레이 중에 붙어 줄을 선 컴포넌트의 onBeginPlay 를 부릅니다(게임 스레드, 틱 밖). 도는 중에 선 것은 다음 번에 돕니다. */
        void dispatchPendingBeginPlay();
        /** @brief 플레이 중에 붙은 컴포넌트를 시작 줄에 세웁니다(`GameObject::attachCreatedComponent`). 핸들로 들어 그새 해체돼도 안전합니다. */
        void queueBeginPlay( ComponentHandle handle );
        /** @brief `tick` 의 컴포넌트 단계입니다 — 플러시 → 쓰기 큐 준비 → 틱 중 표시 → `tickComponents` → 표시 해제. 오브젝트가 있을 때만 돕니다. */
        void tickComponentsPhase( float32 deltaTime );
        /** @brief 새 ObjectId 를 발급합니다. */
        uint64 generateNewId();
        /** @brief `_mutex` 를 쥔 채 @p objectId 로 오브젝트를 만들어 이름 맵 · id 표 · 병합 대기 목록에 올립니다. */
        GameObject* createGameObjectUnlocked( hashed_string name, uint64 objectId );
        /**
         * @brief 잠금 없이 고유 이름을 만듭니다. 번호를 붙였으면 그 밑 이름과 번호를 @p outEntry 에 적습니다(`_pObject` 는 건드리지 않습니다).
         * @details 번호는 밑 이름마다 **지운 것부터 되씁니다**(`NameSuffixState`). 그래서 인턴되는 이름 수는 같은 이름으로 동시에 살아 있던
         *          오브젝트 수의 최댓값으로 묶입니다.
         */
        hashed_string makeUniqueNameUnlocked( hashed_string requested, NameEntry& outEntry );
        /** @brief 잠금 없이, 지울 이름 표 항목이 번호를 붙여 만든 이름이면 그 번호를 밑 이름의 빈 번호로 돌려줍니다. 항목은 부르는 쪽이 지웁니다. */
        void releaseNameSuffixUnlocked( const NameEntry& nameEntry );
        /**
         * @brief 잠금 없이 id 로 등록된 오브젝트(삭제 대기 포함)를 찾습니다. 슬롯 표를 보고, 범위 밖이면 맵을 봅니다.
         * @details `findGameObjectById` 와 달리 삭제 대기 오브젝트도 반환하고 잠그지 않습니다. 이미 `_mutex` 를 쥔 자리에서 씁니다.
         */
        GameObject* findRegisteredUnlocked( uint64 objectId ) const;
        /**
         * @brief 잠금 없이 이름이 **살아 있는** 오브젝트에 쓰이고 있는지 봅니다.
         * @details 지연 파괴 대기(pending destroy) 오브젝트는 이름 맵에 남아 있지만 이름으로 찾을 수 없습니다. 그 이름은 비어 있는
         *          것으로 봅니다 — 모듈 리로드 · RHI 교체 때 새 인스턴스가 옛 오브젝트가 아직 사라지기 전에 같은 이름을 만들기
         *          때문입니다. 대신 파괴 쪽은 맵 항목이 **자기 것**일 때만 지웁니다.
         */
        bool isNameTakenUnlocked( hashed_string name ) const;

        /**
         * @brief 타입별 컴포넌트 풀을 얻거나 만듭니다.
         * @details 키는 **FQN 이고 TypeInfo 포인터가 아닙니다.** 재등록은 같은 객체에 덮어써 주소가 고정이지만 레지스트리 밖 사본도
         *          있을 수 있어, 재등록에도 변하지 않는 FQN 을 씁니다. 주의: 포인터로 키를 잡아 한 클래스에 `TypeInfo` 가 둘이 되면
         *          **생성 때와 해제 때가 서로 다른 풀**을 가리켜 `PoolAllocator::free` 의 "Pointer does not belong to any allocated chunk"
         *          단정이 걸린다.
         *          해제는 이 표를 보지 않습니다. 컴포넌트가 `_pPool` 로 자기 풀을 들고, 그리로 돌아갑니다.
         */
        PoolAllocator* getOrCreateComponentPool( const TypeInfo* pTypeInfo, size_t typeSize )
        {
            if ( pTypeInfo == nullptr || pTypeInfo->_fullyQualifiedName.empty() )
                return nullptr;

            SW_ASSERT( WalkScope::isInsideWalk() == false );
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            auto                                iter = _mapComponentPool.find( pTypeInfo->_fullyQualifiedName );
            if ( iter != _mapComponentPool.end() )
                return iter->second.get();

            auto           pNewPool                           = make_unique<PoolAllocator>( typeSize, 64u, true );
            PoolAllocator* pRaw                               = pNewPool.get();
            _mapComponentPool[pTypeInfo->_fullyQualifiedName] = std::move( pNewPool );
            return pRaw;
        }

    private:
        /**
         * @struct ObjectSlotTable
         * @brief `objectId → GameObject*` 를 **락 없이** 읽는 밀집 표입니다.
         *
         * @details 핸들 해석(`resolveComponent`)이 프레임당 오브젝트 수만큼 일어납니다. 매번 매니저 `_mutex` 를 공유 잠금하고
         *          해시 맵을 조회하면 큐브 20,000 개 벤치에서 **호출당 110ns, 프레임당 2.2ms** 다.
         *
         *          id 는 단조 증가 카운터라 **밀집**하므로 배열이면 됩니다. 다만 배열을 늘리면 주소가
         *          옮겨져 읽는 쪽과 부딪히므로, 절대 재배치되지 않는 청크 배열(`PagedArray`)에 둡니다.
         *          쓰기는 모두 매니저 락 안에서 일어나고, 읽기는 청크 포인터 하나와 슬롯 하나의 원자적 로드입니다.
         *
         * @note **칸은 id 의 아래 비트입니다**(`kObjectSlotCount` 로 나눈 나머지) — id 를 그대로 칸 번호로 쓰면 약 420 만을 넘는 id 가 모두
         *       맵(잠금 + 해시, 호출당 110 ns)으로 가서, 스폰이 잦은 게임은 몇 시간 뒤 모든 핸들 해석이 그 길이 된다. 칸을 **다른 살아 있는
         *       오브젝트**가 쓸 때만(이만큼 떨어진 id 둘이
         *       함께 살아 있을 때 — 오래 사는 오브젝트와 420 만 뒤의 스폰) 뒤에 온 것이 맵으로 갑니다. 칸의 오브젝트가 다른 id 면 "여기 없음" 이라
         *       읽는 쪽이 id 를 견줍니다 — 묻는 id 가 `_compareFromId` 이상일 때만. 그 값은 칸 수이고, 칸 수를 넘는 id 가 한 번이라도 들어오면
         *       0 이 됩니다. 그 전에는 칸 번호가 곧 id 라 작은 id 는 견줄 것이 없습니다(늘 견주면 오브젝트의 `_objectId` 를 한 번 더 읽어
         *       조회가 0.8 ns 느리다. Release · FindById 번갈아 5 회 5.6 → 6.4 ns).
         *       청크는 늘 1024 개 이하(32 MB 상한)입니다 — id 범위를
         *       넓히면(2 단 디렉터리) 지난 id 범위마다 청크가 남아 메모리가 스폰 수에 비례해 자랍니다. 언리얼 `FUObjectArray` 는 칸을
         *       재사용하고 약한 포인터가 일련번호로 견줍니다. 여기서는 id 자체가 일련번호입니다.
         */
        struct ObjectSlotTable
        {
            /** @brief 청크 하나가 담는 슬롯 수입니다. */
            static constexpr uint32 kChunkSize = 4096;
            /** @brief 청크 표의 칸 수입니다. `kChunkSize` 와 곱하면 칸 수(`kObjectSlotCount`)입니다. */
            static constexpr uint32 kMaxChunk = 1024;
            static_assert( static_cast<uint64>( kChunkSize ) * kMaxChunk == kObjectSlotCount, "ObjectSlotTable must cover kObjectSlotCount slots" );

            using SlotArray = PagedArray<atomic<GameObject*>, kChunkSize, kMaxChunk>;

            /** @brief id 의 칸 번호입니다. */
            static constexpr uint64 getSlotIndex( uint64 objectId ) { return objectId & ( kObjectSlotCount - 1 ); }
            /** @brief 칸이 비었으면 씁니다. 다른 오브젝트가 쓰고 있으면 false — 부르는 쪽이 맵에 넣습니다. 매니저 락을 쥔 채 부르십시오. */
            [[nodiscard]] bool tryStore( uint64 objectId, GameObject* pObject );
            /** @brief 칸이 이 오브젝트를 들고 있으면 비웁니다. 아니면 false — 맵에 든 것입니다. 매니저 락을 쥔 채 부르십시오. */
            [[nodiscard]] bool tryRemove( uint64 objectId, const GameObject* pObject );
            /** @brief 칸이 **그 id 의** 오브젝트를 들고 있으면 반환합니다. **락이 필요 없습니다.** 비었거나 다른 id 면 nullptr 입니다. */
            GameObject* load( uint64 objectId ) const;
            /** @brief 모든 슬롯을 비웁니다. 락 없이 읽는 쪽이 있을 수 있어 청크는 그대로 둡니다. */
            void clear();

        private:
            SlotArray      _listSlot;
            atomic<uint64> _compareFromId{ kObjectSlotCount }; ///< 이 이상의 id 를 물으면 칸의 오브젝트 id 와 견준다. 감긴 id 를 넣으면 0(`clear` 가 되돌린다)
        };

        TypedPoolAllocator<GameObject>                          _poolGameObject;
        unordered_map<hashed_string, unique_ptr<PoolAllocator>> _mapComponentPool; ///< 키는 타입 FQN(`getOrCreateComponentPool` 설명 참고)

        vector<GameObject*>                           _listGameObject;
        unordered_map<hashed_string, NameEntry>       _mapNameToObject;
        unordered_map<hashed_string, NameSuffixState> _mapNameSuffix; ///< 밑 이름마다 번호 상태. 중복 이름 만들기가 O(1) 입니다(언리얼 MakeUniqueObjectName 의 자리)
        /**
         * @brief id → 오브젝트 맵입니다. **슬롯 표의 칸을 다른 살아 있는 오브젝트가 쓰는 id 만** 듭니다(보통 비어 있습니다).
         * @details 모든 오브젝트를 넣지 않습니다 — 표와 같은 답을 두 번 들고, 스폰마다 노드 할당 하나와 파괴마다 해제 하나가 붙는다.
         */
        unordered_map<uint64, GameObject*> _mapIdToObject;
        /** @brief id → 오브젝트의 **빠른 읽기 길**입니다. 칸이 막힌 id 만 위 맵으로 갑니다. */
        ObjectSlotTable         _objectSlotTable;
        atomic<uint32>          _overflowObjectCount; ///< `_mapIdToObject` 의 크기. 0 이면 읽는 쪽이 표에서 못 찾은 id 로 잠그지 않습니다
        vector<GameObject*>     _listPendingAdd;
        vector<GameObject*>     _listPendingDestroyObject;
        vector<ComponentHandle> _listPendingDestroyComponent; ///< 핸들로 든다(`destroyComponent` 설명 참고)

        vector<GameObject*>     _listProcessingDestroyObject;
        vector<ComponentHandle> _listProcessingDestroyComponent;

        mutable std::shared_mutex _mutex;
        /**
         * @brief 오브젝트 id 발급 카운터입니다. **프로세스 전체에서 하나**입니다(컴포넌트 id `Component::_s_nextComponentId` 와 같은 규칙).
         * @details 씬을 넘어 옮긴 오브젝트(`SceneManager::markPersistent`)가 같은 id 를 지키려면 다른 매니저의 발급과 겹치지 않아야 한다 —
         *          겹치면 새 id 를 받고 그 오브젝트를 가리키던 핸들이 끊긴다. 유니티의 인스턴스 id 도 프로세스 전체다.
         */
        static atomic<uint64> _s_nextObjectId;

        PhysicsWorld _physicsWorld;
        /** @brief 강체 물리입니다. 컴포넌트보다 늦게 사라지도록 등록부들과 함께 둔다(컴포넌트의 해제가 바디를 놓는다). */
        ScenePhysics _scenePhysics;
        /**
         * @brief 콜라이더 바디를 맞추고 물리를 step 한 뒤 겹침 이벤트를 두 오브젝트의 켜진 컴포넌트에 나눠 줍니다. 틱 · 트랜스폼 적용 뒤, 게임 스레드에서.
         * @details 유니티는 물리 갱신 뒤 OnTrigger 를, 언리얼은 움직임이 끝난 뒤 Begin/EndOverlap 을 부른다. 여기서는 그 프레임에 적용된 월드 자리로 잰다.
         */
        void stepPhysics( float32 deltaTime );
        /** @brief 겹침 월드(`PhysicsWorld`)만 진행하고 겹침 이벤트를 나눠 줍니다(`stepPhysics` 의 앞 절반). */
        void                            stepOverlapWorld( float32 deltaTime );
        vector<BoxCollider2DComponent*> _listCollider; ///< `registerCollider` 한 콜라이더. 콜라이더가 자기 자리(`_colliderIndex`)를 든다

        atomic<bool>            _bTicking;                ///< 컴포넌트 틱 중(`isStructuralMutationFrozen`)
        bool                    _bProcessingDestruction;  ///< 지연 파괴를 처리하는 중 — 소멸자에서 다시 들어오면 단언한다
        uint64                  _lastStageGeneration;     ///< DAG 스테이지 캐시(`_listCachedTickStage`)를 지은 등록부 세대
        atomic<uint32>          _tickStageBuildCount;     ///< 등록부가 항목을 다시 지은 틱의 수(진단)
        vector<TickStage>       _listCachedTickStage;     ///< 선행 조건이 있을 때만 쓰는 DAG 스테이지(등록부가 짓습니다)
        vector<GameObject*>     _listPlayWalk;            ///< beginPlay · endPlay 가 잠금 없이 돌 오브젝트 목록(할당 재사용)
        atomic<bool>            _bHasBegunPlay;           ///< 플레이 중(`hasBegunPlay`)
        mutex                   _beginPlayMutex;          ///< 시작 줄을 지킵니다(비동기 씬 로드는 워커에서 붙입니다)
        vector<ComponentHandle> _listPendingBeginPlay;    ///< 플레이 중에 붙어 onBeginPlay 를 기다리는 컴포넌트
        vector<ComponentHandle> _listProcessingBeginPlay; ///< 도는 중인 시작 줄(할당 재사용)
        DeferredDelegateQueue   _deferredStructuralQueue; ///< 틱이 미룬 구조 변경(컴포넌트 추가 · attach · detach · 태그 · 활성), 부른 순서. 틱 직후 가장 먼저 돈다
        DeferredDelegateQueue   _deferredPostTickQueue;   ///< 틱이 미룬 스폰 · 데미지 · 태그(`deferPostTick`)

        /** @brief 트랜스폼 계층입니다. PhysicsWorld 처럼 매니저가 소유만 합니다. */
        SceneTransformHierarchy _transformHierarchy;
        /** @brief 그릴 수 있는 컴포넌트의 등록부입니다. PhysicsWorld 처럼 매니저가 소유만 합니다. */
        PrimitiveRegistry _primitiveRegistry;
        /** @brief 빛 컴포넌트의 등록부입니다. 같은 규칙으로 소유만 합니다. */
        LightRegistry _lightRegistry;
        /** @brief 카메라 컴포넌트의 등록부입니다. 같은 규칙으로 소유만 합니다. */
        CameraRegistry _cameraRegistry;
        /** @brief 애니메이션 시스템입니다. 같은 규칙으로 소유만 합니다. */
        AnimationSystem _animationSystem;
        /** @brief 틱에 참여하는 오브젝트의 등록부입니다. 같은 규칙으로 소유만 합니다. */
        TickRegistry _tickRegistry;
    };
} // namespace sw
