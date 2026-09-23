/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 의 수명 — 생성 · 이름 · id 표 · 조회 · 파괴 · 팩토리. 프레임 경로(tick · 트랜스폼 배치)는 `GameObjectManagerTick.cpp`.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    namespace
    {
        struct GameObjectManagerInternal
        {
            static mutex& getModuleFactoryHeadsMutex()
            {
                static mutex s_mutex;
                return s_mutex;
            }

            static unordered_map<string, ComponentFactoryRegistrar*>& getModuleFactoryHeads()
            {
                static unordered_map<string, ComponentFactoryRegistrar*> s_mapFactoryHeads;
                return s_mapFactoryHeads;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    static ComponentFactoryRegistrar* _s_engineHead{ nullptr };
    static bool                       _s_engineHeadSealed{ false };

    ComponentFactoryRegistrar*& ComponentFactoryRegistrar::getHead()
    {
        static ComponentFactoryRegistrar* s_pHead{ nullptr };
        return s_pHead;
    }

    ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ) )
        : _registerFunc{ registerFunc }
        , _pNext{ getHead() }
    {
        getHead() = this;
        if ( _s_engineHeadSealed == false )
            _s_engineHead = this;
    }

    ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ), ComponentFactoryRegistrar*& moduleHead )
        : _registerFunc{ registerFunc }
        , _pNext{ moduleHead }
    {
        moduleHead = this;
        if ( _s_engineHeadSealed == false && &moduleHead == &getHead() )
            _s_engineHead = this;
    }

    /**
     * @brief GameObjectManager 생성자: 기본 엔진 및 모듈 컴포넌트 팩토리들을 등록합니다.
     */
    GameObjectManager::GameObjectManager()
        : _poolGameObject{ 256, true }
        , _mapComponentPool{}
        , _listGameObject{}
        , _mapNameToObject{}
        , _mapNameNextSuffix{}
        , _mapIdToObject{}
        , _listPendingAdd{}
        , _listPendingDestroyObject{}
        , _listPendingDestroyComponent{}
        , _listProcessingDestroyObject{}
        , _listProcessingDestroyComponent{}
        , _mutex{}
        , _nextId{ 1 }
        , _physicsWorld{}
        , _bParallelTransformReadOnly{ false }
        , _bTicking{ false }
        , _lastWaveGeneration{ 0 }
        , _tickWaveBuildCount{ 0 }
        , _listCachedTickWave{}
        , _listActiveWriteSlot{}
        , _deferredTransformMutex{}
        , _listDeferredTransformUpdate{}
        , _listProcessingTransform{}
        , _deferredPostTickMutex{}
        , _listDeferredPostTickUpdate{}
        , _listProcessingPostTick{}
        , _mapFactory{}
        , _mapFactoryModule{}
        , _activeModuleName{}
        , _transformHierarchy{}
        , _primitiveRegistry{}
        , _lightRegistry{}
        , _tickRegistry{}
    {
        _listDeferredTransformUpdate.reserve( 128 );
        _listProcessingTransform.reserve( 128 );
        _listDeferredPostTickUpdate.reserve( 128 );
        _listProcessingPostTick.reserve( 128 );

        ComponentFactoryRegistrar* pEngineHead = ComponentFactoryRegistrar::getHead();
        if ( pEngineHead == nullptr )
            pEngineHead = _s_engineHead;
        registerPendingFactories( "Engine", pEngineHead );

        vector<pair<string, ComponentFactoryRegistrar*>> listModuleHead;
        {
            std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
            for ( const auto& [mod, head] : GameObjectManagerInternal::getModuleFactoryHeads() )
                listModuleHead.push_back( { mod, head } );
        }
        for ( const auto& [mod, head] : listModuleHead )
        {
            if ( head == nullptr )
                continue;
            if ( mod == "Engine" && pEngineHead != nullptr )
                continue;
            registerPendingFactories( mod.c_str(), head );
        }
    }

    GameObjectManager::~GameObjectManager()
    {
        clear();
        // 활성 슬롯이 나를 가리키고 있었다면 비운다 — 씬 층이 잊어도 죽은 포인터는 남지 않는다.
        if ( _s_pActive == this )
            _s_pActive = nullptr;
    }

    GameObjectManager* GameObjectManager::_s_pActive = nullptr;

    void GameObjectManager::setActiveManager( GameObjectManager* pManager )
    {
        _s_pActive = pManager;
    }

    GameObjectManager* GameObjectManager::getActiveManager()
    {
        return _s_pActive;
    }

    /**
     * @brief 새로운 게임 오브젝트를 생성하고 고유 이름을 부여하며 인덱스 사전에 등록합니다.
     */
    GameObject* GameObjectManager::createGameObject( hashed_string name )
    {
        GameObject* pObj = nullptr;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            const hashed_string                 uniqueName = makeUniqueNameUnlocked( name );
            pObj                                           = _poolGameObject.create( uniqueName );

            const uint64 newObjectId = generateNewId();
            pObj->_objectId          = newObjectId;
            pObj->_pOwnerManager     = this;

            _mapNameToObject.insert_or_assign( uniqueName, pObj );
            if ( _objectSlotTable.store( newObjectId, pObj ) == false )
                _mapIdToObject.insert_or_assign( newObjectId, pObj );

            _listPendingAdd.push_back( pObj );
        }
        return pObj;
    }

    /**
     * @brief 게임 오브젝트 이름 변경 시 이름 검색 맵을 갱신합니다.
     */
    void GameObjectManager::notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName )
    {
        if ( pObj == nullptr )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        if ( findRegisteredUnlocked( pObj->getObjectId() ) != pObj )
            return;

        const auto oldIt = _mapNameToObject.find( oldName );
        if ( oldIt != _mapNameToObject.end() && oldIt->second == pObj )
            _mapNameToObject.erase( oldIt );

        const hashed_string uniqueName = makeUniqueNameUnlocked( newName );
        if ( uniqueName != newName )
            pObj->_name = uniqueName;
        _mapNameToObject.insert_or_assign( uniqueName, pObj );
    }

    GameObjectManager* GameObjectManager::resolveOwningManager( GameObjectManager* pPreferred )
    {
        if ( pPreferred != nullptr )
            return pPreferred;

        // 붙잡아 둔 매니저가 없으면 활성 씬의 것을 쓴다. 씬 층이 슬롯을 채우기 전(테스트·초기화 중)에는
        // nullptr 이다 — 호출부는 그때 해석을 그냥 미룬다. 씬 층에 직접 묻지 않는다(setActiveManager 참고).
        return _s_pActive;
    }

    GameObject* GameObjectManager::findGameObjectByName( hashed_string name ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapNameToObject.find( name );
        if ( it == _mapNameToObject.end() )
            return nullptr;
        GameObject* pObj = it->second;
        return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
    }

    // ======================================================================
    // ObjectSlotTable — id → GameObject* 를 락 없이 읽는 밀집 표
    // ======================================================================

    bool GameObjectManager::ObjectSlotTable::store( uint64 objectId, GameObject* pObject )
    {
        if ( isInRange( objectId ) == false )
            return false;

        // 지우는 길이라면 청크를 새로 만들 이유가 없다. 쓰기는 전부 매니저 락 안이라 여기서 두 스레드가 겹치지 않는다.
        atomic<GameObject*>* pSlot = ( pObject != nullptr ) ? _listSlot.ensure( objectId ) : _listSlot.find( objectId );
        if ( pSlot != nullptr )
            pSlot->store( pObject, std::memory_order_release );
        return true;
    }

    GameObject* GameObjectManager::ObjectSlotTable::load( uint64 objectId ) const
    {
        const atomic<GameObject*>* pSlot = _listSlot.find( objectId );
        return ( pSlot != nullptr ) ? pSlot->load( std::memory_order_acquire ) : nullptr;
    }

    void GameObjectManager::ObjectSlotTable::clear()
    {
        _listSlot.forEachElement( []( atomic<GameObject*>& slot )
        {
            slot.store( nullptr, std::memory_order_release );
        } );
    }

    GameObject* GameObjectManager::findRegisteredUnlocked( uint64 objectId ) const
    {
        if ( ObjectSlotTable::isInRange( objectId ) )
            return _objectSlotTable.load( objectId );
        const auto it = _mapIdToObject.find( objectId );
        return ( it != _mapIdToObject.end() ) ? it->second : nullptr;
    }

    GameObject* GameObjectManager::findGameObjectById( uint64 objectId ) const
    {
        // **빠른 길: 락도 해시도 없다.** 핸들 해석이 프레임당 오브젝트 수만큼 도는 자리라
        // 공유 잠금 하나가 곧 밀리초가 된다(벤치 실측: 호출당 110ns → 프레임당 2.2ms).
        if ( ObjectSlotTable::isInRange( objectId ) )
        {
            GameObject* pSlotObject = _objectSlotTable.load( objectId );
            return ( pSlotObject != nullptr && pSlotObject->isPendingKill() == false ) ? pSlotObject : nullptr;
        }

        // id 가 표의 범위를 넘어선 경우에만 맵으로 간다 — 표는 빠른 길이지 유일한 진실이 아니다.
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapIdToObject.find( objectId );
        if ( it == _mapIdToObject.end() )
            return nullptr;
        GameObject* pObj = it->second;
        return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
    }

    void GameObjectManager::getAllGameObjects( vector<GameObject*>& outListGameObject ) const
    {
        outListGameObject.clear();
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        outListGameObject.reserve( _listGameObject.size() + _listPendingAdd.size() );
        outListGameObject.insert( outListGameObject.end(), _listGameObject.begin(), _listGameObject.end() );
        outListGameObject.insert( outListGameObject.end(), _listPendingAdd.begin(), _listPendingAdd.end() );
    }

    vector<GameObject*> GameObjectManager::getAllGameObjects() const
    {
        vector<GameObject*> listAllGameObject;
        getAllGameObjects( listAllGameObject );
        return listAllGameObject;
    }

    GameObject* GameObjectManager::findGameObjectByTag( TagID tag ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( GameObject* pObj : _listGameObject )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                return pObj;
        }
        for ( GameObject* pObj : _listPendingAdd )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                return pObj;
        }
        return nullptr;
    }

    void GameObjectManager::findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const
    {
        outListGameObject.clear();
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( GameObject* pObj : _listGameObject )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                outListGameObject.push_back( pObj );
        }
        for ( GameObject* pObj : _listPendingAdd )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                outListGameObject.push_back( pObj );
        }
    }

    void GameObjectManager::beginPlay()
    {
        mergePendingAdds();
        forEachGameObject( []( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->isActiveInHierarchy() )
                pObj->beginPlay();
        } );
    }

    void GameObjectManager::endPlay()
    {
        forEachGameObject( []( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->isActiveInHierarchy() )
                pObj->endPlay();
        } );
    }

    Component* GameObjectManager::resolveComponent( sw::ComponentHandle handle )
    {
        if ( handle.isValid() == false )
            return nullptr;
        GameObject* pObj = findGameObjectById( handle.objectId() );
        if ( pObj == nullptr )
            return nullptr;
        return pObj->findComponentById( handle.componentId(), true );
    }

    void GameObjectManager::destroyObject( GameObject* pObj, bool bDestroyChildren )
    {
        if ( pObj == nullptr )
            return;

        // **표시를 먼저, 원자적으로 자리를 잡는다.** 예전에는 `isPendingKill()` 로 보고 나서
        // `markPendingKill()` 을 했다 — 둘 사이가 벌어져 있어, 같은 오브젝트를 같은 프레임에
        // 없애는 두 스레드가 나란히 통과하면 파괴 목록에 같은 포인터가 두 번 들어가고
        // `_poolGameObject.destroy` 가 같은 블록을 두 번 반납한다. `onTick` 은 병렬로 돌고
        // (총알 둘이 같은 적을 맞히는) 그 경우는 흔하다. 자리를 잡은 스레드만 진행한다.
        if ( pObj->tryMarkPendingKill() == false )
            return;

        vector<GameObject*> listChildren;
        if ( bDestroyChildren )
            pObj->getChildren( listChildren );

        pObj->forEachComponent( []( Component* pComp )
        { pComp->markPendingKill(); } );
        pObj->refreshActiveInHierarchy();

        if ( bDestroyChildren )
        {
            for ( GameObject* pChild : listChildren )
            {
                if ( pChild != nullptr )
                    destroyObject( pChild, true );
            }
        }

        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _listPendingDestroyObject.push_back( pObj );
        }
        // 틱 등록부에는 아무것도 알리지 않는다 — 삭제 대기 오브젝트는 디스패치가 건너뛰고, 실제 파괴가 등록부에서 뺀다.
    }

    void GameObjectManager::destroyComponent( Component* pComp )
    {
        // destroyObject 와 같은 이유로 자리부터 잡는다 — 여기 목록에 두 번 들어가면
        // `removeComponent` 가 두 번 불리고 컴포넌트 풀이 같은 블록을 두 번 받는다.
        if ( pComp == nullptr || pComp->tryMarkPendingKill() == false )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingDestroyComponent.push_back( pComp );
        // 틱에 참여하던 컴포넌트면 소유 오브젝트의 항목을 다시 짓게 한다 — 나머지는 등록부와 무관하다.
        if ( pComp->hasTickWork() )
            _tickRegistry.markObjectDirty( pComp->getOwner() );
    }

    void GameObjectManager::destroyComponentInstance( Component* pComp )
    {
        if ( pComp == nullptr )
            return;

        // 등록부는 raw 포인터를 들고 있다. ComponentPtr/ComponentHandle 은 접근할 때마다 이름·타입으로
        // 다시 찾기 때문에, 프레임마다 전부 훑는 렌더 경로에 쓰면 지금 걷어내려는 순회보다 비싸진다.
        // 대신 **메모리를 실제로 반납하는 이 한 지점**에서 등록을 해제해, 등록된 채로 해제되는 경우가
        // 구조적으로 없게 만든다. 목록에서 빼는 쪽(removeComponent, clearComponents)에만 걸어두면
        // 지연 파괴 경로가 그걸 우회한다. onUnregister 는 멱등이라 두 번 불려도 된다.
        // 소멸자 호출 전이어야 가상 디스패치가 유효하다.
        pComp->onUnregister( *this );

        // 풀은 컴포넌트가 든다(`_pPool`, 생성이 적는다). 이름·타입 표로 **다시 찾지 않는다** — 이름은 바뀔 수 있고
        // 타입은 그새 해제될 수 있어, 못 찾으면 풀 블록을 힙으로 반납해 힙이 깨졌다(Shipping 0xc0000374). 풀은 한 번
        // 만들어지면 매니저가 죽을 때까지 그 자리에 있고 자체 잠금을 들고 있으므로 _mutex 없이 반납한다 — ~SceneComponent
        // 는 detachFromComponent 를 타고 (un)registerRootSceneComponent 로 다시 들어와 같은 _mutex 를 잡는다.
        PoolAllocator* pPool = pComp->_pPool;
        if ( pPool != nullptr )
        {
            pComp->~Component();
            pPool->free( pComp );
            return;
        }

        sw_delete( pComp );
    }

    void GameObjectManager::processDeferredDestruction()
    {
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            if ( _listPendingDestroyObject.empty() && _listPendingDestroyComponent.empty() )
                return;

            _listProcessingDestroyObject.swap( _listPendingDestroyObject );
            _listProcessingDestroyComponent.swap( _listPendingDestroyComponent );
        }

        for ( Component* pComp : _listProcessingDestroyComponent )
        {
            if ( pComp == nullptr )
                continue;
            GameObject* pOwner = pComp->getOwner();
            if ( pOwner != nullptr )
                pOwner->removeComponent( pComp );
        }

        if ( _listProcessingDestroyObject.empty() == false )
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            for ( GameObject* pObj : _listProcessingDestroyObject )
            {
                if ( pObj != nullptr )
                {
                    const uint32 index = pObj->_managerIndex;
                    if ( index < _listGameObject.size() && _listGameObject[index] == pObj )
                    {
                        GameObject* pBackObj    = _listGameObject.back();
                        _listGameObject[index]  = pBackObj;
                        pBackObj->_managerIndex = index;
                        _listGameObject.pop_back();
                    }
                    else
                    {
                        // 본 목록에 없으면 이번 프레임에 만들어져 아직 병합되지 않은 것이다 — 그때만 대기 목록을 훑는다.
                        // (예전엔 무조건 훑어, 프레임에 100 개를 만들고 100 개를 지우면 만 번 비교였다.)
                        auto pendingIt = std::find( _listPendingAdd.begin(), _listPendingAdd.end(), pObj );
                        if ( pendingIt != _listPendingAdd.end() )
                        {
                            *pendingIt = _listPendingAdd.back();
                            _listPendingAdd.pop_back();
                        }
                    }
                    const auto nameIt = _mapNameToObject.find( pObj->getName() );
                    if ( nameIt != _mapNameToObject.end() && nameIt->second == pObj )
                        _mapNameToObject.erase( nameIt );
                    if ( _objectSlotTable.store( pObj->getObjectId(), nullptr ) == false )
                        _mapIdToObject.erase( pObj->getObjectId() );
                }
            }
        }

        vector<GameObject*> listDying;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            for ( GameObject* pObj : _listProcessingDestroyObject )
            {
                if ( pObj == nullptr )
                    continue;
                listDying.push_back( pObj );
            }
            _listProcessingDestroyObject.clear();
            _listProcessingDestroyComponent.clear();
        }

        for ( GameObject* pObj : listDying )
        {
            if ( pObj == nullptr )
                continue;
            // 등록부의 그룹 목록에서 뺀다 — 메모리를 놓기 전에. 지운 컴포넌트 쪽은 소유 오브젝트가 표시되어 틱 전에 다시 지어진다.
            _tickRegistry.unregisterObject( pObj );
            _poolGameObject.destroy( pObj );
        }
    }

    void GameObjectManager::clear()
    {
        vector<GameObject*> listDying;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };

            listDying.reserve( _listGameObject.size() + _listPendingAdd.size() );
            listDying.insert( listDying.end(), _listGameObject.begin(), _listGameObject.end() );
            listDying.insert( listDying.end(), _listPendingAdd.begin(), _listPendingAdd.end() );

            _listPendingDestroyObject.clear();
            _listPendingDestroyComponent.clear();
            _listProcessingDestroyObject.clear();
            _listProcessingDestroyComponent.clear();

            {
                std::scoped_lock<mutex> lockT{ _deferredTransformMutex };
                _listDeferredTransformUpdate.clear();
                _listProcessingTransform.clear();
            }
            {
                std::scoped_lock<mutex> lockP{ _deferredPostTickMutex };
                _listDeferredPostTickUpdate.clear();
                _listProcessingPostTick.clear();
            }

            _listGameObject.clear();
            _listPendingAdd.clear();
            _mapNameToObject.clear();
            _mapIdToObject.clear();
            _objectSlotTable.clear();
            _transformHierarchy.clear();
            _listCachedTickWave.clear();
        }

        for ( GameObject* pObj : listDying )
        {
            if ( pObj != nullptr )
                _poolGameObject.destroy( pObj );
        }

        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _poolGameObject.clear();
            for ( auto& [typeFqn, pPool] : _mapComponentPool )
            {
                if ( pPool != nullptr )
                    pPool->clear();
            }
        }

        _listCachedTickWave.clear();
        _tickRegistry.clear();
        markTickWavesDirty();
    }

    void GameObjectManager::rebindAllCachedTypeInfo()
    {
        TypeRegistry& typeRegistry = engine::getTypeRegistry();
        forEachGameObject( [&typeRegistry]( GameObject* pObj )
        {
            if ( pObj == nullptr )
                return;
            pObj->forEachComponent( [&typeRegistry]( Component* pComp )
            {
                if ( pComp == nullptr )
                    return;
                const hashed_string typeKey = pComp->getComponentName();
                if ( typeKey.empty() )
                    return;
                pComp->applyTypeDefaults( typeRegistry.findType( typeKey ) );
            } );
        } );
        markTickWavesDirty();
    }

    void GameObjectManager::mergePendingAdds()
    {
        vector<GameObject*> listLocalPending;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            if ( _listPendingAdd.empty() )
                return;
            listLocalPending = std::move( _listPendingAdd );
            _listPendingAdd.clear();
        }

        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _listGameObject.reserve( _listGameObject.size() + listLocalPending.size() );
            // 이름 맵·id 표는 만들 때(createGameObject · registerGameObject) 이미 넣었다 — 여기서 다시 넣지 않는다.
            for ( GameObject* pObj : listLocalPending )
            {
                if ( pObj != nullptr )
                {
                    pObj->_managerIndex = static_cast<uint32>( _listGameObject.size() );
                    _listGameObject.push_back( pObj );
                }
            }
        }

        for ( GameObject* pObj : listLocalPending )
        {
            if ( pObj != nullptr )
                pObj->refreshActiveInHierarchy();
        }
        // 웨이브는 addComponent 가 틱에 참여하는 컴포넌트를 붙일 때 더럽혔다 — 병합 자체는 멤버십을 바꾸지 않는다.
    }

    void GameObjectManager::registerPendingFactories( string_view moduleName, sw::ComponentFactoryRegistrar* pHead )
    {
        if ( pHead == nullptr )
            return;

        {
            std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
            GameObjectManagerInternal::getModuleFactoryHeads()[string( moduleName )] = pHead;
        }
        _activeModuleName                   = hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        ComponentFactoryRegistrar* pCurrent = pHead;
        while ( pCurrent != nullptr )
        {
            if ( pCurrent->_registerFunc != nullptr )
                pCurrent->_registerFunc( *this );
            pCurrent = pCurrent->_pNext;
        }
        _activeModuleName = hashed_string();
    }

#if !defined( SW_SHIPPING )
    void GameObjectManager::unregisterFactoriesByModule( string_view moduleName )
    {
        const hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        for ( auto it = _mapFactoryModule.begin(); it != _mapFactoryModule.end(); )
        {
            if ( it->second == hashModule )
            {
                _mapFactory.erase( it->first );
                it = _mapFactoryModule.erase( it );
            }
            else
                ++it;
        }
    }
#endif

#if !defined( SW_SHIPPING )
    uint32 GameObjectManager::destroyComponentsOfModule( string_view moduleName )
    {
        SW_ASSERT( isStructuralMutationFrozen() == false );
        // 지연 파괴 목록부터 — 거기 남은 컴포넌트의 소멸자도 모듈 코드다.
        processDeferredDestruction();

        const hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        vector<Component*>  listDoomed;
        forEachComponent( [&]( Component* pComp )
        {
            const TypeInfo* pTypeInfo = ( pComp != nullptr ) ? pComp->getTypeInfo() : nullptr;
            if ( pTypeInfo != nullptr && pTypeInfo->_moduleName == hashModule )
                listDoomed.push_back( pComp );
        } );
        for ( Component* pComp : listDoomed )
        {
            GameObject* pOwner = pComp->getOwner();
            if ( pOwner != nullptr )
                pOwner->removeComponent( pComp );
        }
        // removeComponent 는 얼려 있으면 미룬다 — 위에서 단언했지만, 미뤄졌더라도 여기서 끝낸다.
        processDeferredDestruction();

        const uint32 count = static_cast<uint32>( listDoomed.size() );
        if ( count > 0 )
            SW_LOG_INFO( "Destroyed %# live component(s) of module '%#' before unload.", count, moduleName );
        return count;
    }
#endif

    void GameObjectManager::registerModuleFactoryHead( string_view moduleName, sw::ComponentFactoryRegistrar* pHead )
    {
        _s_engineHeadSealed = true;
        std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
        if ( pHead == nullptr )
            GameObjectManagerInternal::getModuleFactoryHeads().erase( string( moduleName ) );
        else
            GameObjectManagerInternal::getModuleFactoryHeads()[string( moduleName )] = pHead;
    }

    void GameObjectManager::unregisterModuleFactoryHead( string_view moduleName )
    {
        std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
        GameObjectManagerInternal::getModuleFactoryHeads().erase( string( moduleName ) );
    }

    Component* GameObjectManager::addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning )
    {
        if ( pGameObject == nullptr )
            return nullptr;

        hashed_string factoryName = typeName;
        auto          it          = _mapFactory.find( factoryName );
        if ( it == _mapFactory.end() )
        {
            const utf8* pRawName = typeName.c_str();
            if ( pRawName != nullptr )
            {
                const utf8* pHash = nullptr;
                for ( const utf8* pCursor = pRawName; *pCursor != '\0'; ++pCursor )
                {
                    if ( *pCursor == '#' )
                        pHash = pCursor;
                }
                if ( pHash != nullptr && pHash != pRawName )
                    factoryName = hashed_string( pRawName, static_cast<uint32>( pHash - pRawName ) );
            }
            it = _mapFactory.find( factoryName );
        }
        if ( it != _mapFactory.end() )
        {
            if ( it->second.isBound() == false )
            {
                if ( bLogWarning )
                    SW_LOG_WARNING( "Component factory for type '%#' is unbound", typeName.c_str() );
                return nullptr;
            }
            return it->second( pGameObject );
        }
        if ( bLogWarning )
            SW_LOG_WARNING( "Component factory for type '%#' not found (%# factories registered)", typeName.c_str(), static_cast<uint32>( _mapFactory.size() ) );
        return nullptr;
    }

    vector<hashed_string> GameObjectManager::getRegisteredComponentTypeNames() const
    {
        vector<hashed_string> listName;
        listName.reserve( _mapFactory.size() );
        for ( const auto& [name, factory] : _mapFactory )
        {
            (void)factory;
            listName.push_back( name );
        }
        return listName;
    }

    void GameObjectManager::registerGameObject( GameObject* pObj )
    {
        if ( pObj == nullptr )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        const uint64                        newObjectId = generateNewId();
        pObj->_objectId                                 = newObjectId;
        pObj->_pOwnerManager                            = this;

        if ( isNameTakenUnlocked( pObj->getName() ) )
            pObj->_name = makeUniqueNameUnlocked( pObj->getName() );

        _mapNameToObject.insert_or_assign( pObj->getName(), pObj );
        if ( _objectSlotTable.store( newObjectId, pObj ) == false )
            _mapIdToObject.insert_or_assign( newObjectId, pObj );

        _listPendingAdd.push_back( pObj );
    }

    uint64 GameObjectManager::generateNewId()
    {
        return _nextId.fetch_add( 1, std::memory_order_relaxed );
    }

    bool GameObjectManager::isNameTakenUnlocked( hashed_string name ) const
    {
        const auto it = _mapNameToObject.find( name );
        return it != _mapNameToObject.end() && it->second != nullptr && it->second->isPendingKill() == false;
    }

    hashed_string GameObjectManager::makeUniqueNameUnlocked( hashed_string requested )
    {
        if ( isNameTakenUnlocked( requested ) == false )
            return requested;

        const utf8* pBase = requested.c_str();
        if ( StringUtil::isNullOrEmpty( pBase ) )
            pBase = "GameObject";

        string_view baseView{ pBase };
        if ( baseView.size() > 96 )
            baseView = baseView.substr( 0, 96 );

        // 이름마다 **다음 번호를 기억한다**(언리얼 MakeUniqueObjectName 의 자리). 예전엔 매번 _2 부터 다시 물어, 같은
        // 이름 N 개면 생성 하나가 N 번 조회였다 — 8000 개에 2.6 초(개당 326 µs). 지운 이름의 번호는 되쓰지 않는다(오르기만 한다).
        StringBuilder<constant::kMaxBuffer128> sb;
        const hashed_string                    baseKey( baseView.data(), static_cast<uint32>( baseView.size() ) );
        uint32&                                nextSuffix      = _mapNameNextSuffix[baseKey];
        const bool                             bFirstDuplicate = nextSuffix < 2;
        if ( bFirstDuplicate )
            nextSuffix = 2;
        for ( uint32 probeCount = 0; probeCount < 10000; ++probeCount )
        {
            const uint32 nameSuffix = nextSuffix++;
            sb.clear();
            sb.append( baseView ).append( '_' ).append( nameSuffix );
            const hashed_string candidate( sb.c_str(), sb.size() );
            if ( isNameTakenUnlocked( candidate ) == false )
            {
                // 이름당 첫 중복만 경고한다 — 총알처럼 같은 이름으로 수천 개를 스폰하는 게임에서 줄마다 경고는 로그 스팸이고,
                // 그 로그 쓰기(개당 약 20 µs)가 생성 자체보다 비쌌다. 그 뒤로는 조용히 번호를 붙인다.
                if ( bFirstDuplicate )
                    SW_LOG_WARNING( "Duplicate name '%#' — using '%#' (further duplicates of this name are numbered silently)", requested.c_str(), candidate.c_str() );
                return candidate;
            }
        }

        static atomic<uint32> s_fallback{ 0 };
        for ( uint32 fallbackIndex = 0; fallbackIndex < 1024; ++fallbackIndex )
        {
            sb.clear();
            sb.append( baseView ).append( "_x" ).append( s_fallback.fetch_add( 1 ) );
            const hashed_string fallback( sb.c_str(), sb.size() );
            if ( isNameTakenUnlocked( fallback ) == false )
            {
                SW_LOG_ERROR( "Exhausted numeric suffixes for '%#' — using '%#'",
                              requested.c_str(), fallback.c_str() );
                return fallback;
            }
        }

        SW_LOG_ERROR( "Failed to uniquify '%#' — creating unnamed object", requested.c_str() );
        sb.clear();
        sb.append( "GameObject_anon_" ).append( s_fallback.fetch_add( 1 ) );
        return hashed_string( sb.c_str(), sb.size() );
    }
} // namespace sw
