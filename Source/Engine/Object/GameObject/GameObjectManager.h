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

#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/LightRegistry.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/TickRegistry.h"
#include "Engine/Physics/PhysicsWorld.h"

namespace sw
{
    class Component;
    class GameObjectManager;
    class MeshComponent;
    class SceneComponent;

    /**
     * @struct ComponentFactoryRegistrar
     * @brief 정적 초기화로 컴포넌트 팩토리를 체인에 연결합니다.
     */
    struct SW_API ComponentFactoryRegistrar
    {
        void ( *_registerFunc )( GameObjectManager& );
        ComponentFactoryRegistrar* _pNext;

        static ComponentFactoryRegistrar*& getHead();
        ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ) );
        ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ), ComponentFactoryRegistrar*& moduleHead );
    };

#ifndef SW_COMPONENT_FACTORY_MODULE_HEAD
    #define SW_COMPONENT_FACTORY_MODULE_HEAD() ( ::sw::ComponentFactoryRegistrar::getHead() )
#endif

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
         * @brief 예전에 발급한 objectId 를 그대로 써서 오브젝트를 다시 만듭니다. 되돌리기 · 플레이 세션 복원 · 핫 리로드가 씁니다.
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

        /** @brief 오브젝트 ID 로 GameObject 를 찾습니다. */
        GameObject* findGameObjectById( uint64 objectId ) const;

        /**
         * @brief 살아 있는 오브젝트를 outList 에 채웁니다(부르는 쪽 버퍼 재사용).
         * @details 같은 클래스의 findGameObjectsByTag 와 같은 규약입니다. 값 반환형은 호출마다 씬
         *          전체를 새로 할당 · 복사하므로, 매 프레임 도는 곳이나 두 번 이상 쓰는 곳은 이쪽을
         *          씁니다. 순회만 하면 되는 곳은 forEachGameObject 가 복사조차 하지 않습니다.
         */
        void getAllGameObjects( vector<GameObject*>& outListGameObject ) const;

        /** @brief 살아 있는 오브젝트 목록을 새 벡터로 반환합니다(한 번만 쓰는 곳 전용). */
        vector<GameObject*> getAllGameObjects() const;

        /** @brief 힙 할당 없이 등록된 모든 유효한 GameObject 를 순회합니다. */
        template <typename Func>
        void forEachGameObject( Func&& func ) const
        {
            std::shared_lock<std::shared_mutex> lock{ _mutex };
            for ( GameObject* pObj : _listGameObject )
            {
                if ( pObj != nullptr && pObj->isPendingKill() == false )
                    func( pObj );
            }
            for ( GameObject* pObj : _listPendingAdd )
            {
                if ( pObj != nullptr && pObj->isPendingKill() == false )
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

        /** @brief 등록된 GameObject 의 beginPlay 를 부릅니다. */
        void beginPlay();

        /** @brief 등록된 GameObject 의 endPlay 를 부릅니다. */
        void endPlay();

        /**
         * @brief 계층을 지키는 병렬 틱입니다.
         * @details 0) 지연 파괴 처리, 새로 만든 오브젝트 병합
         *          1) SceneComponent 월드 캐시 플러시(루트 → 자식, 더티 서브트리만)
         *          2) 병렬 Component 틱(트랜스폼 캐시는 읽기 전용, 구조 변경은 지연)
         *          3) 지연된 attach · detach → 틱 중 쌓인 트랜스폼 쓰기 → 지연 큐(deferPostTick) 실행 → 병합,
         *             더티면 다시 플러시, 마지막으로 지연 파괴 처리
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
         *          부모 사슬이 흔들립니다. 그때는 건마다 세터로 돌립니다(세터가 지연 경로를 탑니다).
         * @return 실제로 값이 바뀐 건수입니다.
         */
        uint32 applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count );

        /** @brief 현재 매니저가 병렬 틱(읽기 전용 트랜스폼) 구간인지 확인합니다. */
        bool isParallelTransformReadOnly() const { return _bParallelTransformReadOnly.load( std::memory_order_relaxed ); }

        /** @brief 틱 중이거나 병렬 트랜스폼 읽기 구간이라 GameObject 생성 · addComponent 같은 구조 변경을 미뤄야 하면 true 입니다. */
        bool isStructuralMutationFrozen() const
        {
            return isParallelTransformReadOnly() || _bTicking.load( std::memory_order_acquire );
        }

        using TransformUpdateDelegate = Delegate<void()>;
        using PostTickDelegate        = Delegate<void()>;

        /** @brief 트랜스폼/계층 구조 변경을 지연 큐에 넣습니다. */
        void deferTransformUpdate( TransformUpdateDelegate func );

        /**
         * @brief 병렬 틱 중의 트랜스폼 쓰기 한 건을 슬롯 큐에 올립니다. 세터가 `isParallelTransformReadOnly()` 일 때 부릅니다.
         * @details 큐는 `SceneTransformHierarchy` 의 것이고, 틱 뒤 `applyQueuedTransformWrites` 가 `applyTransformBatch` 와 같은
         *          병렬 적용을 돕니다. 슬롯이 준비되지 않은 드문 경우(틱 밖에서 읽기 전용 구간을 흉내 낼 때)만 지연 델리게이트로 갑니다.
         */
        void queueTransformWrite( const SceneTransformWrite& write );

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

        /** @brief 루트가 된 SceneComponent 를 트랜스폼 계층에 등록합니다. */
        void registerRootSceneComponent( SceneComponent* pComp );

        /** @brief 부모가 생기거나 파괴된 SceneComponent 를 트랜스폼 계층의 루트 목록에서 뺍니다. */
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
         * @brief 빛 컴포넌트의 등록부입니다.
         * @details 프리미티브와 같은 이유로 있습니다. 매 프레임 씬을 뒤져 빛을 **찾지** 않고, 빛이 붙을
         *          때 **등록받습니다**. 자세한 사연은 LightRegistry.h 에 있습니다.
         */
        LightRegistry& getLightRegistry() { return _lightRegistry; }
        /** @brief 빛 컴포넌트의 등록부입니다. */
        const LightRegistry& getLightRegistry() const { return _lightRegistry; }

        /**
         * @brief 틱에 참여하는 오브젝트의 등록부입니다(언리얼 `FTickTaskManager` 의 자리). 자세한 사연은 TickRegistry.h 에 있습니다.
         * @details 같은 규칙으로 소유만 합니다. 컴포넌트가 틱을 켜고 끄면 소유 오브젝트가 여기에 표시하고, `tick` 이 디스패치 전에
         *          표시된 오브젝트만 다시 훑습니다. 씬 전체를 훑어 웨이브를 다시 짓던 0.5~1 ms 가 사라진 자리입니다.
         */
        TickRegistry& getTickRegistry() { return _tickRegistry; }
        /** @brief 틱에 참여하는 오브젝트의 등록부입니다. */
        const TickRegistry& getTickRegistry() const { return _tickRegistry; }

        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. 삭제 예정이면 nullptr 입니다. */
        Component* resolveComponent( ComponentHandle handle );

        /** @brief 핸들이 가리키는 오브젝트를 찾습니다. 파괴됐거나 삭제 대기면 nullptr 입니다. 락을 잡지 않습니다(`findGameObjectById`). */
        GameObject* resolveGameObject( GameObjectHandle handle ) const { return findGameObjectById( handle.objectId() ); }

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

        /** @brief Component 를 지연 삭제 큐에 넣습니다. */
        void destroyComponent( Component* pComp );

        /** @brief 풀 또는 힙에서 할당된 컴포넌트 인스턴스를 파괴하고 메모리를 반환합니다. */
        void destroyComponentInstance( Component* pComp );

        /** @brief 지연 삭제 큐의 오브젝트 · 컴포넌트를 실제로 해제합니다. */
        void processDeferredDestruction();

        /** @brief 등록·대기 목록을 모두 비웁니다. */
        void clear();

        /**
         * @brief 컴포넌트 이름으로 TypeRegistry 에서 타입을 다시 찾아 기본값을 다시 주입하고, 틱 항목을 다시 짓게 합니다.
         * @details registerPendingTypes 직후에 부릅니다.
         */
        void rebindAllCachedTypeInfo();

        /** @brief 이번 프레임에 추가된 GameObject 를 활성 목록에 합칩니다. */
        void mergePendingAdds();

        /** @brief 모듈 컴포넌트 팩토리를 이 매니저에 등록합니다. */
        void registerPendingFactories( string_view moduleName, sw::ComponentFactoryRegistrar* pHead );

#if !defined( SW_SHIPPING )
        /** @brief 해당 모듈이 등록한 컴포넌트 팩토리를 제거합니다. */
        void unregisterFactoriesByModule( string_view moduleName );
#endif

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

        /** @brief 전역 모듈 팩토리 헤드를 등록합니다. */
        static void registerModuleFactoryHead( string_view moduleName, sw::ComponentFactoryRegistrar* pHead );
        /** @brief 전역 모듈 팩토리 헤드를 해제합니다. */
        static void unregisterModuleFactoryHead( string_view moduleName );

        using ComponentFactoryDelegate = Delegate<Component*( GameObject* )>;

        template <typename T>
        /** @brief 타입 이름과 모듈 이름으로 T 팩토리를 등록합니다. */
        void registerComponentType( hashed_string typeName, hashed_string moduleName = hashed_string() )
        {
            static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
            _mapFactory[typeName] = []( GameObject* pGameObject ) -> Component*
            {
                if ( pGameObject == nullptr )
                    return nullptr;
                return pGameObject->addComponent<T>();
            };

            if ( _mapFactoryModule.find( typeName ) == _mapFactoryModule.end() )
            {
                if ( moduleName.getHash() != 0 )
                    _mapFactoryModule[typeName] = moduleName;
                else if ( _activeModuleName.getHash() != 0 )
                    _mapFactoryModule[typeName] = _activeModuleName;
                else
                    _mapFactoryModule[typeName] = hashed_string( "Engine" );
            }
        }

        /** @brief 등록된 이름으로 컴포넌트를 추가합니다. 에디터 · 직렬화 전용입니다. 게임은 addComponent<T> 를 씁니다. */
        Component* addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning = true );

        /** @brief 모든 오브젝트의 틱 항목을 다음 틱 전에 다시 짓게 합니다(타입 재바인딩 · 씬 초기화). */
        void markTickWavesDirty() { _tickRegistry.markAllDirty(); }
        /**
         * @brief 틱 등록부가 오브젝트 항목을 다시 지은 틱의 수입니다. 진단 · 회귀 테스트용입니다.
         * @details 틱에 참여하는 컴포넌트(`Component::hasTickWork`)가 생기거나 없어지거나 순서가 바뀔 때만 올라야 합니다.
         *          예전에는 아무 구조 변경에나 씬 전체 웨이브를 다시 만들었습니다. 틱하지 않는 MeshComponent 를 붙였다 떼도 다음 틱이
         *          8000 컴포넌트를 모두 훑었습니다(2 ms). 지금은 바뀐 오브젝트의 컴포넌트 몇 개를 훑는 값입니다.
         */
        uint32 getTickWaveBuildCount() const { return _tickWaveBuildCount.load( std::memory_order_relaxed ); }

        /** @brief 에디터 등에서 추가 가능한 컴포넌트 타입 이름 목록입니다. */
        vector<hashed_string> getRegisteredComponentTypeNames() const;

        /** @brief 트랜스폼이 바뀌었음을 알려 세대를 올립니다(`getTransformHierarchy().notifyDirtied()`). */
        void notifyTransformDirtied() { _transformHierarchy.notifyDirtied(); }
        /** @brief 현재 트랜스폼 더티 세대 번호를 반환합니다. */
        uint64 getTransformGeneration() const { return _transformHierarchy.getGeneration(); }

        /** @brief 이미 만든 GameObject 를 매니저에 등록하고 소유권을 가져갑니다. */
        void registerGameObject( GameObject* pObj );

    private:
        /**
         * @brief 등록부의 오브젝트를 TickGroup 순으로 틱합니다.
         * @details 보통은 그룹마다 오브젝트 목록을 한 번의 포크-조인으로 나눕니다(한 오브젝트의 항목은 한 워커가 순서대로).
         *          서브틱 선행 조건이 하나라도 있으면 등록부가 지은 DAG 웨이브를 차례로 돕니다. 그 캐시는 등록부 세대로 무효화됩니다.
         */
        void tickComponents( float32 deltaTime );
        /**
         * @brief 틱 중 슬롯 큐에 쌓인 트랜스폼 쓰기를 슬롯 단위로 나눠 적용하고 큐를 비웁니다(틱 뒤, 게임 스레드).
         * @details 같은 슬롯의 건은 한 워커가 순서대로 적용하므로 한 스레드가 잇따라 쓴 값은 마지막이 이깁니다. 다른 슬롯이
         *          같은 컴포넌트를 쓴 경우는 예전(뮤텍스 순서)과 같이 순서가 없습니다.
         * @return 실제로 값이 바뀐 건수입니다.
         */
        uint32 applyQueuedTransformWrites();
        /** @brief 새 ObjectId 를 발급합니다. */
        uint64 generateNewId();
        /** @brief `_mutex` 를 쥔 채 @p objectId 로 오브젝트를 만들어 이름 맵 · id 표 · 병합 대기 목록에 올립니다. */
        GameObject* createGameObjectUnlocked( hashed_string name, uint64 objectId );
        /** @brief 잠금 없이 고유 이름을 만듭니다. */
        hashed_string makeUniqueNameUnlocked( hashed_string requested );
        /**
         * @brief 잠금 없이 id 로 등록된 오브젝트(삭제 대기 포함)를 찾습니다. 슬롯 표를 보고, 범위 밖이면 맵을 봅니다.
         * @details `findGameObjectById` 와 달리 삭제 대기 오브젝트도 반환하고 잠그지 않습니다. 이미 `_mutex` 를 쥔 자리에서 씁니다.
         */
        GameObject* findRegisteredUnlocked( uint64 objectId ) const;
        /**
         * @brief 잠금 없이 이름이 **살아 있는** 오브젝트에 쓰이고 있는지 봅니다.
         * @details 지연 파괴 대기(pending kill) 오브젝트는 이름 맵에 남아 있지만 이름으로 찾을 수 없습니다. 그 이름은 비어 있는
         *          것으로 봅니다. 모듈 리로드 · RHI 교체 때 새 인스턴스가 옛 오브젝트가 아직 사라지기 전에 같은 이름을 만들며
         *          `Duplicate name` 경고를 내던 원인입니다. 대신 파괴 쪽은 맵 항목이 **자기 것**일 때만 지웁니다.
         */
        bool isNameTakenUnlocked( hashed_string name ) const;

        /**
         * @brief 타입별 컴포넌트 풀을 얻거나 만듭니다.
         * @details 키는 **FQN 이고 TypeInfo 포인터가 아닙니다.** 예전에는 한 클래스에 `TypeInfo` 인스턴스가 둘 이상
         *          생길 수 있었습니다(모듈이 로드되며 리플렉션을 다시 등록하면 새 인스턴스가 생겼습니다). 포인터로 키를 잡으면
         *          **생성 때와 해제 때가 서로 다른 풀**을 가리켜서, `destroyComponentInstance` 가 엉뚱한 풀에 블록을 반납하고
         *          `PoolAllocator::free` 의 "Pointer does not belong to any allocated chunk" 단정이 걸렸습니다(테스트 씬을 로드한 뒤
         *          종료할 때 실제로 그랬습니다). 지금은 재등록이 같은 객체에 덮어써 주소가 고정이지만, 레지스트리 밖 사본도 있을 수
         *          있어 재등록에도 변하지 않는 FQN 을 그대로 씁니다.
         *          해제는 이 표를 보지 않습니다. 컴포넌트가 `_pPool` 로 자기 풀을 들고, 그리로 돌아갑니다.
         */
        PoolAllocator* getOrCreateComponentPool( const TypeInfo* pTypeInfo, size_t typeSize )
        {
            if ( pTypeInfo == nullptr || pTypeInfo->_fullyQualifiedName.empty() )
                return nullptr;

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
         * @details 핸들 해석(`resolveComponent`)이 프레임당 오브젝트 수만큼 일어납니다. 예전에는 그 한
         *          번마다 매니저 `_mutex` 를 공유 잠금하고 해시 맵을 조회했습니다. 큐브 20,000 개 벤치에서
         *          **호출당 110ns, 프레임당 2.2ms** 였습니다(핸들 대신 미리 푼 포인터를 쓰게 바꿔 실측).
         *
         *          id 는 단조 증가 카운터라 **밀집**하므로 배열이면 됩니다. 다만 배열을 늘리면 주소가
         *          옮겨져 읽는 쪽과 부딪히므로, 절대 재배치되지 않는 청크 배열(`PagedArray`)에 둡니다.
         *          쓰기는 모두 매니저 락 안에서 일어나고, 읽기는 청크 포인터 하나와 슬롯 하나의 원자적 로드입니다.
         *          예전에는 이 표가 청크 관리를 따로 구현했습니다. `SlotHandleTable` 이 쓰는 `PagedArray` 와 같은 일이었습니다.
         *
         * @note 범위 안의 id 는 이 표가 **유일한 기준**입니다(맵에 넣지 않습니다). 표가 답하지 못하는 id(범위 밖)만
         *       부르는 쪽이 맵으로 갑니다.
         */
        struct ObjectSlotTable
        {
            /** @brief 청크 하나가 담는 슬롯 수입니다. */
            static constexpr uint32 kChunkSize = 4096;
            /** @brief 청크 표의 칸 수입니다. `kChunkSize` 와 곱하면 다룰 수 있는 id 범위가 됩니다(약 420만). */
            static constexpr uint32 kMaxChunk = 1024;

            using SlotArray = PagedArray<atomic<GameObject*>, kChunkSize, kMaxChunk>;

            /** @brief 슬롯에 포인터를 씁니다. 매니저 락을 쥔 채 부르십시오. 범위 밖이면 false 입니다. */
            bool store( uint64 objectId, GameObject* pObject );
            /** @brief 슬롯을 읽습니다. **락이 필요 없습니다.** 범위 밖이거나 비었으면 nullptr 입니다. */
            GameObject* load( uint64 objectId ) const;
            /** @brief 그 id 가 표가 다룰 수 있는 범위인지 반환합니다. 범위 밖이면 부르는 쪽이 맵으로 갑니다. */
            static bool isInRange( uint64 objectId ) { return SlotArray::isInRange( objectId ); }
            /** @brief 모든 슬롯을 비웁니다. 락 없이 읽는 쪽이 있을 수 있어 청크는 그대로 둡니다. */
            void clear();

        private:
            SlotArray _listSlot;
        };

        TypedPoolAllocator<GameObject>                          _poolGameObject;
        unordered_map<hashed_string, unique_ptr<PoolAllocator>> _mapComponentPool; ///< 키는 타입 FQN(`getOrCreateComponentPool` 설명 참고)

        vector<GameObject*>                       _listGameObject;
        unordered_map<hashed_string, GameObject*> _mapNameToObject;
        unordered_map<hashed_string, uint32>      _mapNameNextSuffix; ///< 이름마다 다음 번호. 중복 이름 만들기가 O(1) 입니다(언리얼 MakeUniqueObjectName 의 자리)
        /**
         * @brief id → 오브젝트 맵입니다. **슬롯 표가 다루지 못하는 id(범위 밖)만** 듭니다.
         * @details 예전에는 모든 오브젝트를 여기에도 넣었습니다. 표와 같은 답을 두 번 들고, 스폰마다 노드 할당 하나와 파괴마다
         *          해제 하나였습니다(총알처럼 스폰이 잦은 게임의 비용). 표가 답하는 범위(약 420만 id)에서는 비어 있습니다.
         */
        unordered_map<uint64, GameObject*> _mapIdToObject;
        /** @brief id → 오브젝트의 **빠른 읽기 길**이자 기준입니다. 범위 밖 id 만 위 맵으로 갑니다. */
        ObjectSlotTable     _objectSlotTable;
        vector<GameObject*> _listPendingAdd;
        vector<GameObject*> _listPendingDestroyObject;
        vector<Component*>  _listPendingDestroyComponent;

        vector<GameObject*> _listProcessingDestroyObject;
        vector<Component*>  _listProcessingDestroyComponent;

        mutable std::shared_mutex _mutex;
        atomic<uint64>            _nextId;

        PhysicsWorld _physicsWorld;

        atomic<bool>                    _bParallelTransformReadOnly;
        atomic<bool>                    _bTicking;
        uint64                          _lastWaveGeneration;  ///< DAG 웨이브 캐시(`_listCachedTickWave`)를 지은 등록부 세대
        atomic<uint32>                  _tickWaveBuildCount;  ///< 등록부가 항목을 다시 지은 틱의 수(진단)
        vector<TickWave>                _listCachedTickWave;  ///< 선행 조건이 있을 때만 쓰는 DAG 웨이브(등록부가 짓습니다)
        vector<uint32>                  _listActiveWriteSlot; ///< 이번 적용에서 비어 있지 않은 쓰기 큐 슬롯(할당 재사용)
        mutex                           _deferredTransformMutex;
        vector<TransformUpdateDelegate> _listDeferredTransformUpdate;
        vector<TransformUpdateDelegate> _listProcessingTransform;
        mutex                           _deferredPostTickMutex;
        vector<PostTickDelegate>        _listDeferredPostTickUpdate;
        vector<PostTickDelegate>        _listProcessingPostTick;

        unordered_map<hashed_string, ComponentFactoryDelegate> _mapFactory;
        unordered_map<hashed_string, hashed_string>            _mapFactoryModule;
        hashed_string                                          _activeModuleName;

        /** @brief 트랜스폼 계층입니다. PhysicsWorld 처럼 매니저가 소유만 합니다. */
        SceneTransformHierarchy _transformHierarchy;
        /** @brief 그릴 수 있는 컴포넌트의 등록부입니다. PhysicsWorld 처럼 매니저가 소유만 합니다. */
        PrimitiveRegistry _primitiveRegistry;
        /** @brief 빛 컴포넌트의 등록부입니다. 같은 규칙으로 소유만 합니다. */
        LightRegistry _lightRegistry;
        /** @brief 틱에 참여하는 오브젝트의 등록부입니다. 같은 규칙으로 소유만 합니다. */
        TickRegistry _tickRegistry;
    };
} // namespace sw
