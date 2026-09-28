/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 의 수명 관리입니다(생성 · 이름 · id 표 · 조회 · 파괴 · 팩토리). 프레임 경로(tick · 트랜스폼 배치)는 `GameObjectManagerTick.cpp` 에 있습니다.
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
            /** @brief 이 스레드가 들어가 있는 `forEachGameObject` 의 깊이입니다(`WalkScope`). */
            inline static thread_local uint32 s_walkDepth = 0;

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
     * @brief 엔진과 모듈의 컴포넌트 팩토리를 등록하며 만듭니다.
     */
    GameObjectManager::GameObjectManager()
        : _poolGameObject{ 256, true }
        , _mapComponentPool{}
        , _listGameObject{}
        , _mapNameToObject{}
        , _mapNameSuffix{}
        , _mapIdToObject{}
        , _listPendingAdd{}
        , _listPendingDestroyObject{}
        , _listPendingDestroyComponent{}
        , _listProcessingDestroyObject{}
        , _listProcessingDestroyComponent{}
        , _mutex{}
        , _nextId{ 1 }
        , _physicsWorld{}
        , _bTicking{ false }
        , _bProcessingDestruction{ false }
        , _lastWaveGeneration{ 0 }
        , _tickWaveBuildCount{ 0 }
        , _listCachedTickWave{}
        , _listActiveWriteSlot{}
        , _listPlayWalk{}
        , _bHasBegunPlay{ false }
        , _beginPlayMutex{}
        , _listPendingBeginPlay{}
        , _listProcessingBeginPlay{}
        , _deferredTransformQueue{}
        , _deferredPostTickQueue{}
        , _mapFactory{}
        , _mapFactoryModule{}
        , _activeModuleName{}
        , _transformHierarchy{}
        , _primitiveRegistry{}
        , _lightRegistry{}
        , _cameraRegistry{}
        , _tickRegistry{}
    {
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
    }

    /**
     * @brief 새 게임 오브젝트를 만들고 고유 이름을 붙여 인덱스에 등록합니다.
     */
    GameObjectManager::WalkScope::WalkScope()
    {
        ++GameObjectManagerInternal::s_walkDepth;
    }

    GameObjectManager::WalkScope::~WalkScope()
    {
        --GameObjectManagerInternal::s_walkDepth;
    }

    bool GameObjectManager::WalkScope::isInsideWalk()
    {
        return GameObjectManagerInternal::s_walkDepth != 0;
    }

    GameObject* GameObjectManager::createGameObject( hashed_string name )
    {
        SW_ASSERT( WalkScope::isInsideWalk() == false );
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        return createGameObjectUnlocked( name, generateNewId() );
    }

    GameObject* GameObjectManager::createGameObjectWithId( hashed_string name, uint64 objectId )
    {
        SW_ASSERT( WalkScope::isInsideWalk() == false );
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        if ( objectId == 0 )
            return createGameObjectUnlocked( name, generateNewId() );

        // 그 id 로 등록된 것이 아직 있으면(삭제 대기 포함) 쓰지 않는다. 옛 것의 지연 파괴가 id 로 정리하는 항목을 새 것 몫까지 지운다.
        if ( findRegisteredUnlocked( objectId ) != nullptr )
        {
            SW_LOG_WARNING( "createGameObjectWithId: id %# is still registered, so '%#' gets a new id. Handles to the old object will not follow it.",
                            objectId, name.c_str() );
            return createGameObjectUnlocked( name, generateNewId() );
        }

        _nextId.fetch_max( objectId + 1, std::memory_order_relaxed );
        return createGameObjectUnlocked( name, objectId );
    }

    GameObject* GameObjectManager::createGameObjectUnlocked( hashed_string name, uint64 objectId )
    {
        NameEntry           nameEntry{};
        const hashed_string uniqueName = makeUniqueNameUnlocked( name, nameEntry );
        GameObject*         pObj       = _poolGameObject.create( uniqueName );
        pObj->_objectId                = objectId;
        pObj->_pOwnerManager           = this;

        nameEntry._pObject = pObj;
        _mapNameToObject.insert_or_assign( uniqueName, nameEntry );
        if ( _objectSlotTable.store( objectId, pObj ) == false )
            _mapIdToObject.insert_or_assign( objectId, pObj );

        _listPendingAdd.push_back( pObj );
        return pObj;
    }

    /**
     * @brief 게임 오브젝트 이름이 바뀌면 이름 검색 맵을 갱신합니다.
     */
    void GameObjectManager::notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName )
    {
        if ( pObj == nullptr )
            return;
        SW_ASSERT( WalkScope::isInsideWalk() == false );

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        if ( findRegisteredUnlocked( pObj->getObjectId() ) != pObj )
            return;

        const auto oldIt         = _mapNameToObject.find( oldName );
        const bool bOwnsOldEntry = oldIt != _mapNameToObject.end() && oldIt->second._pObject == pObj;

        // 번호를 붙여 받은 이름(`Bullet_3`)을 그 밑 이름(`Bullet`)으로 되돌리는데 밑 이름을 다른 오브젝트가 쓰고 있으면, 다시 유일화해도
        // 번호만 바뀐다. 그대로 둔다. 프리팹 스폰이 그 길이다 — 만들 때 `Bullet_3`, 상태를 읽으며 저장된 이름 `Bullet` 으로 한 번,
        // 인스턴스 이름을 다시 세팅하며 또 한 번. 예전에는 스폰 하나가 이름을 세 번 유일화해 번호 셋(인턴 셋)을 썼다.
        if ( bOwnsOldEntry && oldIt->second._suffix != 0 && oldIt->second._baseName == newName && isNameTakenUnlocked( newName ) )
        {
            pObj->_name = oldName;
            return;
        }
        if ( bOwnsOldEntry )
        {
            releaseNameSuffixUnlocked( oldIt->second );
            _mapNameToObject.erase( oldIt );
        }

        NameEntry           nameEntry{};
        const hashed_string uniqueName = makeUniqueNameUnlocked( newName, nameEntry );
        if ( uniqueName != newName )
            pObj->_name = uniqueName;
        nameEntry._pObject = pObj;
        _mapNameToObject.insert_or_assign( uniqueName, nameEntry );
    }

    GameObject* GameObjectManager::findGameObjectByName( hashed_string name ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapNameToObject.find( name );
        if ( it == _mapNameToObject.end() )
            return nullptr;
        GameObject* pObj = it->second._pObject;
        return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
    }

    // ======================================================================
    // ObjectSlotTable: id → GameObject* 를 락 없이 읽는 밀집 표
    // ======================================================================

    bool GameObjectManager::ObjectSlotTable::store( uint64 objectId, GameObject* pObject )
    {
        if ( isInRange( objectId ) == false )
            return false;

        // 지우는 길이라면 청크를 새로 만들 이유가 없다. 쓰기는 모두 매니저 락 안이라 여기서 두 스레드가 겹치지 않는다.
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

        // id 가 표의 범위를 넘어선 경우에만 맵으로 간다. 범위 안의 id 는 표가 유일한 기준이다(맵에는 넣지 않는다).
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
        // 표시를 먼저 세운다 — 도는 동안 onBeginPlay 가 붙이는 컴포넌트도 줄을 선다(이미 시작했으면 비트가 거른다).
        _bHasBegunPlay.store( true, std::memory_order_release );
        mergePendingAdds();
        // **잠금을 쥔 채 컴포넌트 코드를 부르지 않는다.** 예전에는 `forEachGameObject` 의 공유 잠금 안에서 onBeginPlay 를 불렀다.
        // onBeginPlay 가 태그를 붙이면(`addTag` → `addComponent<TagComponent>` → 풀 맵의 배타 잠금) 같은 스레드가 제 공유 잠금을 기다려
        // 에디터 Play 가 멈췄다 — 트리의 열두 컴포넌트가 onBeginPlay 에서 태그를 붙인다. 목록을 받아 잠금 없이 돈다. 활성 여부와 무관하게
        // 시작한다(언리얼과 같다. 짝은 컴포넌트의 "시작됨" 비트가 맞춘다). 도는 동안 생긴 오브젝트는 아래 줄 처리가 시작한다.
        getAllGameObjects( _listPlayWalk );
        for ( GameObject* pObj : _listPlayWalk )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false )
                pObj->beginPlay();
        }
        _listPlayWalk.clear();
        dispatchPendingBeginPlay();
    }

    void GameObjectManager::endPlay()
    {
        _bHasBegunPlay.store( false, std::memory_order_release );
        {
            std::scoped_lock<mutex> lock{ _beginPlayMutex };
            _listPendingBeginPlay.clear();
        }
        getAllGameObjects( _listPlayWalk );
        for ( GameObject* pObj : _listPlayWalk )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false )
                pObj->endPlay();
        }
        _listPlayWalk.clear();
    }

    void GameObjectManager::queueBeginPlay( ComponentHandle handle )
    {
        std::scoped_lock<mutex> lock{ _beginPlayMutex };
        _listPendingBeginPlay.push_back( handle );
    }

    void GameObjectManager::dispatchPendingBeginPlay()
    {
        {
            std::scoped_lock<mutex> lock{ _beginPlayMutex };
            if ( _listPendingBeginPlay.empty() )
                return;
            _listProcessingBeginPlay.swap( _listPendingBeginPlay );
        }
        // 핸들로 다시 푼다 — 줄을 선 뒤 떼였거나 오브젝트째 지워졌으면 건너뛴다. 삭제 대기면 dispatchBeginPlay 가 거른다.
        for ( const ComponentHandle handle : _listProcessingBeginPlay )
        {
            Component* pComp = resolveComponent( handle );
            if ( pComp != nullptr )
                pComp->dispatchBeginPlay();
        }
        _listProcessingBeginPlay.clear();
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
        SW_ASSERT( WalkScope::isInsideWalk() == false );

        // **표시를 먼저, 원자적으로 자리를 잡는다.** 예전에는 `isPendingKill()` 로 보고 나서
        // `markPendingKill()` 을 했다. 둘 사이가 벌어져 있어, 같은 오브젝트를 같은 프레임에
        // 없애는 두 스레드가 나란히 통과하면 파괴 목록에 같은 포인터가 두 번 들어가고
        // `_poolGameObject.destroy` 가 같은 블록을 두 번 반납한다. `onTick` 은 병렬로 돌고
        // (총알 둘이 같은 적을 맞히는) 그 경우는 흔하다. 자리를 잡은 스레드만 진행한다.
        if ( pObj->tryMarkPendingKill() == false )
            return;

        vector<GameObject*> listChildren;
        if ( bDestroyChildren )
            pObj->getChildren( listChildren );

        // 계층 활성은 다시 맞추지 않는다 — 삭제 대기는 그 값의 입력이 아니다(예전에는 여기서 서브트리 전체를 걸었다).
        pObj->forEachComponent( []( Component* pComp )
        { pComp->markPendingKill(); } );

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
        // 틱 등록부에는 아무것도 알리지 않는다. 삭제 대기 오브젝트는 디스패치가 건너뛰고, 실제 파괴가 등록부에서 뺀다.
    }

    void GameObjectManager::destroyComponent( Component* pComp )
    {
        SW_ASSERT( WalkScope::isInsideWalk() == false );
        // destroyObject 와 같은 이유로 자리부터 잡는다. 여기 목록에 두 번 들어가면
        // `removeComponent` 가 두 번 불리고 컴포넌트 풀이 같은 블록을 두 번 받는다.
        if ( pComp == nullptr || pComp->tryMarkPendingKill() == false )
            return;

        // **포인터가 아니라 핸들로 적는다.** 줄을 선 뒤에도 즉시 경로(틱 밖의 `removeComponent` · 상태를 되돌리는 로드의
        // `clearComponents`)가 먼저 해제할 수 있다. 날 포인터로 들면 처리할 때 풀려난 블록을 — 그새 같은 자리에 새
        // 컴포넌트가 들었으면 **엉뚱한 컴포넌트를** — 지웠다. 핸들은 처리 때 다시 풀어, 이미 없으면 건너뛴다.
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingDestroyComponent.push_back( pComp->getHandle() );
        // 틱에 참여하던 컴포넌트면 소유 오브젝트의 항목을 다시 짓게 한다. 나머지는 등록부와 무관하다.
        if ( pComp->hasTickWork() )
            _tickRegistry.markObjectDirty( pComp->getOwner() );
    }

    void GameObjectManager::destroyComponentInstance( Component* pComp )
    {
        if ( pComp == nullptr )
            return;

        // **컴포넌트 해체는 여기 하나다** — 등록 해제 → 파괴 콜백 → 소유자 끊기 → 소멸 → 반납.
        // 등록부는 raw 포인터를 들고 있다(렌더 경로가 프레임마다 전부 훑으므로 핸들은 비싸다). 그래서 메모리를 실제로 놓는
        // 이 지점에서 등록을 해제해 "등록된 채로 해제" 가 구조적으로 없게 한다. 예전에는 목록에서 빼는 쪽이 한 번, 여기가
        // 또 한 번 `onUnregister` 를 불러 구현마다 멱등이어야 했고 파괴마다 등록부 잠금을 두 번 잡았다. 이제 정확히 한 번이다.
        // 소멸자 호출 전이어야 가상 디스패치가 유효하다. 시작했던 컴포넌트는 여기서 끝낸다 — 떼기 · 비우기 · 지연 파괴 · 모듈 내리기가
        // 모두 이 한 곳을 지나므로 onEndPlay 가 빠지는 길이 없다(모듈 코드가 아직 올라와 있는 시점이다).
        pComp->dispatchEndPlay();
        pComp->onUnregister( *this );
        pComp->onDestroy();
        pComp->setOwner( nullptr );

        // 풀은 컴포넌트가 든다(`_pPool`, 생성이 적는다). 이름 · 타입 표로 **다시 찾지 않는다.** 이름은 바뀔 수 있고
        // 타입은 그새 해제될 수 있어, 찾지 못하면 풀 블록을 힙으로 반납해 힙이 깨졌다(Shipping 0xc0000374). 풀은 한 번
        // 만들어지면 매니저가 죽을 때까지 그 자리에 있고 자체 잠금을 들고 있으므로 _mutex 없이 반납한다.
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
        // 처리 목록을 돌며 컴포넌트 · 오브젝트 소멸자를 부른다. 그 안에서 다시 들어오면 도는 목록을 맞바꿔 버린다.
        SW_ASSERT( _bProcessingDestruction == false );
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            if ( _listPendingDestroyObject.empty() && _listPendingDestroyComponent.empty() )
                return;

            _listProcessingDestroyObject.swap( _listPendingDestroyObject );
            _listProcessingDestroyComponent.swap( _listPendingDestroyComponent );
        }
        _bProcessingDestruction = true;

        for ( const ComponentHandle handle : _listProcessingDestroyComponent )
        {
            // 소유 오브젝트가 삭제 대기면 건너뛴다 — 오브젝트를 없앨 때 컴포넌트도 함께 없앤다(아래). 즉시 경로가 이미 해제했으면
            // 목록에 없다. 둘 다 아니고 여전히 삭제 표시된 것만 뺀다.
            GameObject* pOwner = findGameObjectById( handle.objectId() );
            Component*  pComp  = ( pOwner != nullptr ) ? pOwner->findComponentById( handle.componentId(), true ) : nullptr;
            if ( pComp != nullptr && pComp->isPendingKill() )
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
                        // 본 목록에 없으면 이번 프레임에 만들어져 아직 병합되지 않은 것이다. 그때만 대기 목록을 훑는다.
                        // (예전에는 무조건 훑어, 프레임에 100 개를 만들고 100 개를 지우면 만 번 비교였다.)
                        auto pendingIt = std::find( _listPendingAdd.begin(), _listPendingAdd.end(), pObj );
                        if ( pendingIt != _listPendingAdd.end() )
                        {
                            *pendingIt = _listPendingAdd.back();
                            _listPendingAdd.pop_back();
                        }
                    }
                    const auto nameIt = _mapNameToObject.find( pObj->getName() );
                    if ( nameIt != _mapNameToObject.end() && nameIt->second._pObject == pObj )
                    {
                        releaseNameSuffixUnlocked( nameIt->second );
                        _mapNameToObject.erase( nameIt );
                    }
                    if ( _objectSlotTable.store( pObj->getObjectId(), nullptr ) == false )
                        _mapIdToObject.erase( pObj->getObjectId() );
                }
            }
        }

        // 처리 목록은 이 함수만 만진다(대기 목록과 맞바꾼 것이다). 예전에는 그것을 다시 잠금 아래 지역 벡터로 베껴 돌았다 — 잠금 한 번과
        // 파괴가 있는 프레임마다 할당 하나. 목록은 용량을 들고 있다.
        for ( GameObject* pObj : _listProcessingDestroyObject )
        {
            if ( pObj == nullptr )
                continue;
            // 메모리를 놓기 전에 등록부의 그룹 목록에서 뺀다. 지운 컴포넌트 쪽은 소유 오브젝트가 표시되어 틱 전에 다시 지어진다.
            _tickRegistry.unregisterObject( pObj );
            _poolGameObject.destroy( pObj );
        }
        _listProcessingDestroyObject.clear();
        _listProcessingDestroyComponent.clear();
        _bProcessingDestruction = false;
    }

    void GameObjectManager::clear()
    {
        // 지연 파괴가 도는 목록을 여기서 비우면 그 루프가 풀린 메모리를 읽는다.
        SW_ASSERT( _bProcessingDestruction == false );
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
                std::scoped_lock<mutex> lockBeginPlay{ _beginPlayMutex };
                _listPendingBeginPlay.clear();
            }

            _deferredTransformQueue.clear();
            _deferredPostTickQueue.clear();

            _listGameObject.clear();
            _listPendingAdd.clear();
            _mapNameToObject.clear();
            _mapNameSuffix.clear();
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

    void GameObjectManager::mergePendingAdds()
    {
        // 잠금 한 번에 옮긴다. 예전에는 대기 목록을 지역 벡터로 **옮겨 가며**(그래서 대기 목록이 매번 용량을 잃고 다음 스폰이 다시 할당했다)
        // 잠금을 두 번 잡았다. 이름 맵 · id 표는 만들 때(`createGameObjectUnlocked`) 이미 넣었다. 여기서 다시 넣지 않는다.
        size_t firstNewIndex = 0;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            if ( _listPendingAdd.empty() )
                return;
            firstNewIndex = _listGameObject.size();
            _listGameObject.reserve( firstNewIndex + _listPendingAdd.size() );
            for ( GameObject* pObj : _listPendingAdd )
            {
                if ( pObj != nullptr )
                {
                    pObj->_managerIndex = static_cast<uint32>( _listGameObject.size() );
                    _listGameObject.push_back( pObj );
                }
            }
            _listPendingAdd.clear();
        }

        // 계층 활성은 잠금 밖에서 맞춘다(컴포넌트 콜백이 돈다). 본 목록을 바꾸는 것은 게임 스레드의 병합 · 파괴뿐이라 자리로 읽어도 된다.
        for ( size_t objectIndex = firstNewIndex; objectIndex < _listGameObject.size(); ++objectIndex )
            _listGameObject[objectIndex]->refreshActiveInHierarchy();
        // 틱 항목은 addComponent 가 틱에 참여하는 컴포넌트를 붙일 때 이미 더럽혔다. 병합 자체는 멤버십을 바꾸지 않는다.
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
        // 지연 파괴 목록부터 비운다. 거기 남은 컴포넌트의 소멸자도 모듈 코드다.
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
        // removeComponent 는 얼려 있으면 미룬다. 위에서 단언했지만, 미뤄졌더라도 여기서 끝낸다.
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

    uint64 GameObjectManager::generateNewId()
    {
        return _nextId.fetch_add( 1, std::memory_order_relaxed );
    }

    bool GameObjectManager::isNameTakenUnlocked( hashed_string name ) const
    {
        const auto it = _mapNameToObject.find( name );
        return it != _mapNameToObject.end() && it->second._pObject != nullptr && it->second._pObject->isPendingKill() == false;
    }

    void GameObjectManager::releaseNameSuffixUnlocked( const NameEntry& nameEntry )
    {
        if ( nameEntry._suffix == 0 )
            return;
        const auto stateIt = _mapNameSuffix.find( nameEntry._baseName );
        if ( stateIt != _mapNameSuffix.end() )
            stateIt->second._listFreeSuffix.push_back( nameEntry._suffix );
    }

    hashed_string GameObjectManager::makeUniqueNameUnlocked( hashed_string requested, NameEntry& outEntry )
    {
        if ( isNameTakenUnlocked( requested ) == false )
            return requested;

        const utf8* pBase = requested.c_str();
        if ( StringUtil::isNullOrEmpty( pBase ) )
            pBase = "GameObject";

        string_view baseView{ pBase };
        if ( baseView.size() > 96 )
            baseView = baseView.substr( 0, 96 );

        // 이름마다 **번호 상태를 기억한다**(언리얼 MakeUniqueObjectName 의 자리). 예전에는 매번 _2 부터 다시 물어, 같은
        // 이름 N 개면 생성 하나가 N 번 조회였다. 8000 개에 2.6 초(개당 326 µs). **지운 오브젝트의 번호부터 되쓴다** — 번호마다
        // 이름을 인턴하므로, 오르기만 하면 스폰 · 파괴를 거듭하는 이름이 전역 인턴 풀을 채운다(`NameSuffixState` 설명).
        StringBuilder<constant::kMaxBuffer128> sb;
        const hashed_string                    baseKey( baseView.data(), static_cast<uint32>( baseView.size() ) );
        NameSuffixState&                       suffixState     = _mapNameSuffix[baseKey];
        const bool                             bFirstDuplicate = suffixState._nextSuffix == 2 && suffixState._listFreeSuffix.empty();
        for ( uint32 probeCount = 0; probeCount < 10000; ++probeCount )
        {
            uint32 nameSuffix = 0;
            if ( suffixState._listFreeSuffix.empty() == false )
            {
                nameSuffix = suffixState._listFreeSuffix.back();
                suffixState._listFreeSuffix.pop_back();
            }
            else
                nameSuffix = suffixState._nextSuffix++;
            sb.clear();
            sb.append( baseView ).append( '_' ).append( nameSuffix );
            const hashed_string candidate( sb.c_str(), sb.size() );
            if ( isNameTakenUnlocked( candidate ) == false )
            {
                // 이름당 첫 중복만 경고한다. 총알처럼 같은 이름으로 수천 개를 스폰하는 게임에서 줄마다 경고는 로그 스팸이고,
                // 그 로그 쓰기(개당 약 20 µs)가 생성 자체보다 비쌌다. 그 뒤로는 조용히 번호를 붙인다.
                if ( bFirstDuplicate )
                    SW_LOG_WARNING( "Duplicate name '%#' — using '%#' (further duplicates of this name are numbered silently)", requested.c_str(), candidate.c_str() );
                outEntry._baseName = baseKey;
                outEntry._suffix   = nameSuffix;
                return candidate;
            }
            // 그 번호의 이름을 다른 오브젝트가 직접 받아 쓰고 있다(누가 `Bullet_7` 로 만들었다). 번호를 버린다 — 그 오브젝트는
            // 번호를 붙여 만든 이름이 아니라 사라져도 번호로 돌아오지 않는다.
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
