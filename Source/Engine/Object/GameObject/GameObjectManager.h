/**
 * @file GameObjectManager.h
 * @brief 씬 내 GameObject 생성·조회·지연 삭제 관리
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/ComponentHandle.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
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

    /// @brief GameObject 등록, 지연 삭제, 씬 컴포넌트 루트
    class SW_API GameObjectManager
    {
        friend class GameObject;
        friend class SceneComponent;
        friend class MeshComponent;

    public:
        /** @brief GameObject를 생성하고 이름 맵을 비운 채 시작합니다. */
        GameObjectManager();
        /** @brief 등록된 오브젝트를 모두 파괴합니다. */
        ~GameObjectManager();

        /** @brief 새 GameObject를 생성하고 등록합니다. */
        GameObject* createGameObject( hashed_string name = hashed_string( "GameObject" ) );

        /**
         * @brief 등록된 GameObject의 이름을 바꾸고 이름 맵을 갱신합니다.
         * @details GameObject::setName이 내부적으로 호출합니다.
         */
        void notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName );

        /** @brief rename API — setName과 동일하게 이름 맵을 유지합니다. */
        bool renameGameObject( GameObject* pObj, hashed_string newName );

        /** @brief 이름으로 GameObject를 찾습니다. */
        GameObject* findGameObjectByName( hashed_string name ) const;

        /** @brief 오브젝트 ID로 GameObject를 찾습니다. */
        GameObject* findGameObjectById( uint64 objectId ) const;

        /** @brief 등록된 모든 GameObject 목록의 스냅샷을 반환합니다. pending-add를 포함하며 목록을 바꾸지 않습니다. */
        vector<GameObject*> getAllGameObjects() const;

        /** @brief 힙 할당 없이 등록된 모든 유효한 GameObject를 순회합니다. */
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

        /** @brief 힙 할당 없이 씬의 모든 유효한 Component를 순회합니다. */
        template <typename Func>
        void forEachComponent( Func&& func ) const
        {
            forEachGameObject( [&func]( GameObject* pObj )
            {
                pObj->forEachComponent( func );
            } );
        }

        /** @brief 힙 할당 없이 씬의 특정 타입 TComponent 파생 컴포넌트들을 순회합니다. */
        template <typename TComponent, typename Func>
        void forEachComponentOfType( Func&& func ) const
        {
            forEachGameObject( [&func]( GameObject* pObj )
            {
                pObj->forEachComponentOfType<TComponent>( func );
            } );
        }

        /** @brief 태그를 가진 첫 GameObject를 반환합니다. 없으면 nullptr. */
        GameObject* findGameObjectByTag( TagID tag ) const;

        /** @brief 태그를 가진 GameObject를 outListGameObject에 넣습니다. */
        void findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const;

        /** @brief 등록된 GameObject의 beginPlay를 호출합니다. */
        void beginPlay();

        /** @brief 등록된 GameObject의 endPlay를 호출합니다. */
        void endPlay();

        /**
         * @brief 계층-안전 병렬 tick
         * @details 1) SceneComponent 월드 캐시 flush (루트→자식, dirty subtree만)
         *          2) 병렬 Component tick (트랜스폼 캐시 읽기 전용, 구조 변경 지연)
         *          3) 지연 트랜스폼 적용 후 지연 큐(deferPostTick) 실행 및 지연 삭제 처리, dirty면 다시 flush
         */
        void tick( float32 deltaTime );

        /** @brief 모든 루트 SceneComponent 월드 캐시를 계층 순으로 갱신합니다 (dirty만). */
        void flushSceneTransforms();

        /** @brief 어떤 루트라도 dirty/descendant dirty가 있으면 true. */
        bool hasDirtySceneTransforms() const;

        /** @brief 현재 매니저가 병렬 틱(읽기 전용 트랜스폼) 구간인지 확인합니다. */
        bool isParallelTransformReadOnly() const { return _bParallelTransformReadOnly.load( std::memory_order_relaxed ); }

        /** @brief beginTick/병렬 tick 구간이라 GO 생성·addComponent 등 구조 변경을 미뤄야 하면 true. */
        bool isStructuralMutationFrozen() const
        {
            return isParallelTransformReadOnly() || _bTicking.load( std::memory_order_acquire );
        }

        using TransformUpdateDelegate = Delegate<void()>;
        using PostTickDelegate        = Delegate<void()>;

        /** @brief 트랜스폼/계층 구조 변경을 지연 큐에 넣습니다. */
        void deferTransformUpdate( TransformUpdateDelegate func );

        /**
         * @brief 병렬 tick 이후(finishTick 뒤) 메인 스레드에서 실행할 작업을 넣습니다.
         * @details GO 생성·addComponent·데미지·태그 변경 등 구조/공유 상태 변경에 사용합니다.
         */
        void deferPostTick( PostTickDelegate func );

        /**
         * @brief 구조 동결 중이면 deferPostTick, 아니면 즉시 실행합니다.
         * @details createGameObject + addComponent + 초기화를 한 람다로 묶을 때 사용합니다.
         */
        void executeOrDeferPostTick( PostTickDelegate func );

        /** @brief 루트 계층이 된 SceneComponent를 캐시에 등록합니다. */
        void registerRootSceneComponent( class SceneComponent* pComp );

        /** @brief 부모가 생기거나 파괴된 SceneComponent를 루트 캐시에서 제거합니다. */
        void unregisterRootSceneComponent( class SceneComponent* pComp );

        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. pending-kill이면 nullptr. */
        Component* resolveComponent( sw::ComponentHandle handle );

        /** @brief 이 씬의 AABB 질의 월드입니다. */
        PhysicsWorld& getPhysicsWorld() { return _physicsWorld; }
        /** @brief 이 씬의 AABB 질의 월드입니다. */
        const PhysicsWorld& getPhysicsWorld() const { return _physicsWorld; }

        /**
         * @brief GameObject를 지연 삭제 큐에 넣습니다.
         * @param pObj 삭제할 게임 오브젝트
         * @param bDestroyChildren 자식 계층 오브젝트들을 함께 연쇄 삭제할지 여부
         */
        void destroyObject( GameObject* pObj, bool bDestroyChildren = true );

        /** @brief Component를 지연 삭제 큐에 넣습니다. */
        void destroyComponent( Component* pComp );

        /** @brief 풀 또는 힙에서 할당된 컴포넌트 인스턴스를 파괴하고 메모리를 반환합니다. */
        void destroyComponentInstance( Component* pComp );

        /** @brief 지연 삭제 큐의 오브젝트·컴포넌트를 실제로 해제합니다. */
        void processDeferredDestruction();

        /** @brief 등록·대기 목록을 모두 비웁니다. */
        void clear();

        /**
         * @brief 컴포넌트 이름 키로 TypeRegistry에서 기본값을 다시 주입합니다.
         * @details registerPendingTypes 직후에 호출합니다.
         */
        void rebindAllCachedTypeInfo();

        /** @brief 이번 프레임에 추가된 GO를 활성 목록에 합칩니다. */
        void mergePendingAdds();

        /** @brief 모듈 컴포넌트 팩토리를 이 매니저에 등록합니다. */
        void registerPendingFactories( string_view moduleName, sw::ComponentFactoryRegistrar* pHead );

        /** @brief 해당 모듈이 등록한 컴포넌트 팩토리를 제거합니다. */
        void unregisterFactoriesByModule( string_view moduleName );

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

        /** @brief 등록된 이름으로 컴포넌트를 추가합니다. 에디터·직렬화 전용. 게임은 addComponent<T>를 씁니다. */
        Component* addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning = true );

        /** @brief 타입 T의 컴포넌트를 전용 풀 또는 힙에서 할당하고 생성합니다. */
        template <typename T, typename... Args>
        T* createComponent( GameObject* pOwner, Args&&... args )
        {
            const TypeInfo* pTypeInfo = nullptr;
            if constexpr ( HasStaticType_v<T> )
                pTypeInfo = T::StaticType();
            else if constexpr ( HasReflectStaticType_v<T> )
                pTypeInfo = ReflectTypeTraits<T>::StaticType();

            void* pMem = nullptr;
            if ( pTypeInfo != nullptr )
            {
                PoolAllocator* pPool = getOrCreateComponentPool( pTypeInfo, sizeof( T ) );
                if ( pPool != nullptr )
                    pMem = pPool->allocate();
            }

            if ( pMem == nullptr )
            {
                pMem = Memory::allocMemory( sizeof( T ) );
                if ( pMem == nullptr )
                    return nullptr;
            }

            T* pComp = sw_placement_new( pMem ) T( std::forward<Args>( args )... );
            pComp->setOwner( pOwner );
            return pComp;
        }

        /** @brief Tick 웨이브를 다음 beginTick에서 다시 만듭니다. */
        void markTickWavesDirty() { _bIsTickWavesDirty.store( true, std::memory_order_release ); }

        /** @brief 에디터 등에서 추가 가능한 컴포넌트 타입 이름 목록입니다. */
        vector<hashed_string> getRegisteredComponentTypeNames() const;

        /** @brief 트랜스폼이 변경되었음을 매니저에 알려 세대 카운터를 원자적으로 갱신합니다. */
        void notifyTransformDirtied() { _dirtyTransformGeneration.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 현재 트랜스폼 더티 세대 번호를 반환합니다. */
        uint64 getTransformGeneration() const { return _dirtyTransformGeneration.load( std::memory_order_relaxed ); }

        /** @brief 이미 생성된 GameObject를 매니저에 등록하고 소유권을 가져갑니다. */
        void registerGameObject( GameObject* pObj );

        /** @brief 틱 디스패치 웨이브의 개별 틱 실행 항목 */
        struct TickExecutionItem
        {
            Component*      _pComponent{ nullptr };
            ComponentHandle _handle;
            uint32          _subTickId{ 0 };
        };

    private:
        /** @brief 소유 컴포넌트를 TickGroup 순으로 틱합니다. */
        void tickComponents( float32 deltaTime );
        /** @brief 서브트리 월드 행렬을 플러시합니다. */
        void flushSceneComponentSubtree( SceneComponent* pRoot, bool bParentChanged );
        /** @brief 새 ObjectId를 발급합니다. */
        uint64 generateNewId();
        /** @brief 잠금 없이 고유 이름을 만듭니다. */
        hashed_string makeUniqueNameUnlocked( hashed_string requested ) const;

        PoolAllocator* getOrCreateComponentPool( const TypeInfo* pTypeInfo, size_t typeSize )
        {
            if ( pTypeInfo == nullptr )
                return nullptr;

            std::unique_lock<std::shared_mutex> lock{ _mutex };
            auto                                iter = _mapComponentPool.find( pTypeInfo );
            if ( iter != _mapComponentPool.end() )
                return iter->second.get();

            auto           pNewPool      = make_unique<PoolAllocator>( typeSize, 64u, true );
            PoolAllocator* pRaw          = pNewPool.get();
            _mapComponentPool[pTypeInfo] = std::move( pNewPool );
            return pRaw;
        }

    private:
        TypedPoolAllocator<GameObject>                            _poolGameObject;
        unordered_map<const TypeInfo*, unique_ptr<PoolAllocator>> _mapComponentPool;

        vector<GameObject*>                       _listGameObject;
        unordered_map<hashed_string, GameObject*> _mapNameToObject;
        unordered_map<uint64, GameObject*>        _mapIdToObject;
        vector<GameObject*>                       _listPendingAdd;
        vector<GameObject*>                       _listPendingDestroyObject;
        vector<Component*>                        _listPendingDestroyComponent;

        vector<GameObject*> _listProcessingDestroyObject;
        vector<Component*>  _listProcessingDestroyComponent;

        vector<SceneComponent*>   _listRootSceneComponent;
        mutable std::shared_mutex _mutex;
        atomic<uint64>            _nextId;

        PhysicsWorld _physicsWorld;

        atomic<bool>                      _bParallelTransformReadOnly;
        atomic<bool>                      _bTicking;
        atomic<bool>                      _bIsTickWavesDirty;
        vector<vector<TickExecutionItem>> _listCachedTickWave;
        mutex                             _deferredTransformMutex;
        vector<TransformUpdateDelegate>   _listDeferredTransformUpdate;
        vector<TransformUpdateDelegate>   _listProcessingTransform;
        mutex                             _deferredPostTickMutex;
        vector<PostTickDelegate>          _listDeferredPostTickUpdate;
        vector<PostTickDelegate>          _listProcessingPostTick;

        unordered_map<hashed_string, ComponentFactoryDelegate> _mapFactory;
        unordered_map<hashed_string, hashed_string>            _mapFactoryModule;
        hashed_string                                          _activeModuleName;

        atomic<uint64> _dirtyTransformGeneration;
        uint64         _lastFlushedTransformGeneration;
    };
} // namespace sw
