/**
 * @file GameObjectStore.cpp
 * @brief 씬 하나의 오브젝트 저장소 — 생성 · 이름 · id 표 · 조회 · 지연 파괴 · 컴포넌트 풀 · 플레이 수명입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectStore.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneTickScheduler.h"
#include "Engine/Object/GameObject/StructuralChangeBuffer.h"
#include "Engine/Object/GameObject/TickRegistry.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    namespace
    {
        struct GameObjectStoreInternal
        {
            /** @brief 이 스레드가 들어가 있는 `forEachGameObject` 의 깊이입니다(`WalkScope`). Engine 의 이 TU 하나에만 있습니다. */
            inline static thread_local uint32 s_walkDepth = 0;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObjectStore" );

    atomic<uint64> GameObjectStore::_s_nextObjectID = 1;

    GameObjectStore::GameObjectStore( GameObjectManager& manager, SceneTickScheduler& tickScheduler, const StructuralChangeBuffer& structuralChangeBuffer )
        : _pManager{ &manager }
        , _pTickRegistry{ &tickScheduler.getTickRegistry() }
        , _pStructuralChangeBuffer{ &structuralChangeBuffer }
        , _poolGameObject{ 256, true }
        , _mapComponentPool{}
        , _listGameObject{}
        , _mapNameToObject{}
        , _mapNameSuffix{}
        , _mapIDToObject{}
        , _objectSlotTable{}
        , _overflowObjectCount{ 0 }
        , _listPendingAdd{}
        , _listPendingDestroyObject{}
        , _listPendingDestroyComponent{}
        , _listProcessingDestroyObject{}
        , _listProcessingDestroyComponent{}
        , _mutex{}
        , _bProcessingDestruction{ false }
        , _listPlayWalk{}
        , _bHasBegunPlay{ false }
        , _beginPlayMutex{}
        , _listPendingBeginPlay{}
        , _listProcessingBeginPlay{}
    {
    }

    GameObjectStore::WalkScope::WalkScope()
    {
        ++GameObjectStoreInternal::s_walkDepth;
    }

    GameObjectStore::WalkScope::~WalkScope()
    {
        --GameObjectStoreInternal::s_walkDepth;
    }

    bool GameObjectStore::WalkScope::isInsideWalk()
    {
        return GameObjectStoreInternal::s_walkDepth != 0;
    }

    GameObject* GameObjectStore::createGameObject( hashed_string name )
    {
        SW_ASSERT( WalkScope::isInsideWalk() == false );
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        return createGameObjectUnlocked( name, generateNewID() );
    }

    GameObject* GameObjectStore::createGameObjectWithID( hashed_string name, uint64 objectID )
    {
        SW_ASSERT( WalkScope::isInsideWalk() == false );
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        if ( objectID == 0 )
            return createGameObjectUnlocked( name, generateNewID() );

        // 그 id 로 등록된 것이 아직 있으면(삭제 대기 포함) 쓰지 않는다. 옛 것의 지연 파괴가 id 로 정리하는 항목을 새 것 몫까지 지운다.
        if ( findRegisteredUnlocked( objectID ) != nullptr )
        {
            SW_LOG_WARNING( "createGameObjectWithID: id %# is still registered, so '%#' gets a new id. Handles to the old object will not follow it.",
                            objectID, name.c_str() );
            return createGameObjectUnlocked( name, generateNewID() );
        }

        _s_nextObjectID.fetch_max( objectID + 1, std::memory_order_relaxed );
        return createGameObjectUnlocked( name, objectID );
    }

    GameObject* GameObjectStore::createGameObjectUnlocked( hashed_string name, uint64 objectID )
    {
        NameEntry           nameEntry{};
        const hashed_string uniqueName = makeUniqueNameUnlocked( name, nameEntry );
        GameObject*         pObj       = _poolGameObject.create( uniqueName );
        pObj->_objectID                = objectID;
        pObj->_pOwnerManager           = _pManager;

        nameEntry._pObject = pObj;
        _mapNameToObject.insert_or_assign( uniqueName, nameEntry );
        if ( _objectSlotTable.tryStore( objectID, pObj ) == false )
        {
            _mapIDToObject.insert_or_assign( objectID, pObj );
            _overflowObjectCount.store( static_cast<uint32>( _mapIDToObject.size() ), std::memory_order_release );
        }

        _listPendingAdd.push_back( pObj );
        return pObj;
    }

    /**
     * @brief 게임 오브젝트 이름이 바뀌면 이름 검색 맵을 갱신합니다.
     */
    void GameObjectStore::notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName )
    {
        if ( pObj == nullptr )
            return;
        SW_ASSERT( WalkScope::isInsideWalk() == false );

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        if ( findRegisteredUnlocked( pObj->getObjectID() ) != pObj )
            return;

        const auto oldIt         = _mapNameToObject.find( oldName );
        const bool bOwnsOldEntry = oldIt != _mapNameToObject.end() && oldIt->second._pObject == pObj;

        // 번호를 붙여 받은 이름(`Bullet_3`)을 그 밑 이름(`Bullet`)으로 되돌리는데 밑 이름을 다른 오브젝트가 쓰고 있으면, 다시 유일화해도
        // 번호만 바뀐다. 그대로 둔다. 프리팹 스폰이 그 길이다 — 만들 때 `Bullet_3`, 상태를 읽으며 저장된 이름 `Bullet` 으로 한 번,
        // 인스턴스 이름을 다시 세팅하며 또 한 번. 그때마다 유일화하면 스폰 하나가 번호 셋(인턴 셋)을 쓴다.
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

    GameObject* GameObjectStore::findGameObjectByName( hashed_string name ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapNameToObject.find( name );
        if ( it == _mapNameToObject.end() )
            return nullptr;
        GameObject* pObj = it->second._pObject;
        return ( pObj != nullptr && pObj->isPendingDestroy() == false ) ? pObj : nullptr;
    }

    bool GameObjectStore::ObjectSlotTable::tryStore( uint64 objectID, GameObject* pObject )
    {
        // 쓰기는 모두 매니저 락 안이라 여기서 두 스레드가 겹치지 않는다.
        atomic<GameObject*>* pSlot = _listSlot.ensure( getSlotIndex( objectID ) );
        if ( pSlot == nullptr )
            return false;
        const GameObject* pOccupant = pSlot->load( std::memory_order_relaxed );
        if ( pOccupant != nullptr && pOccupant != pObject )
            return false;
        // 칸을 발행하기 **전에** 적는다. 읽는 쪽은 칸을 acquire 로 읽은 뒤 이것을 보므로, 감긴 id 의 오브젝트를 보는 쪽은 반드시 true 를 본다.
        if ( objectID >= kObjectSlotCount )
            _compareFromID.store( 0, std::memory_order_relaxed );
        pSlot->store( pObject, std::memory_order_release );
        return true;
    }

    bool GameObjectStore::ObjectSlotTable::tryRemove( uint64 objectID, const GameObject* pObject )
    {
        // 지우는 길이라 청크를 새로 만들 이유가 없다.
        atomic<GameObject*>* pSlot = _listSlot.find( getSlotIndex( objectID ) );
        if ( pSlot == nullptr || pSlot->load( std::memory_order_relaxed ) != pObject )
            return false;
        pSlot->store( nullptr, std::memory_order_release );
        return true;
    }

    GameObject* GameObjectStore::ObjectSlotTable::load( uint64 objectID ) const
    {
        const atomic<GameObject*>* pSlot = _listSlot.find( getSlotIndex( objectID ) );
        if ( pSlot == nullptr )
            return nullptr;
        GameObject* pObject = pSlot->load( std::memory_order_acquire );
        if ( pObject == nullptr )
            return nullptr;
        // 칸은 id 의 아래 비트라 같은 칸의 다른 id 가 들어 있을 수 있다 — 묻는 id 가 감긴 것이거나, 감긴 id 가 들어온 적이 있을 때
        // (`_compareFromID` 가 0). 그때만 오브젝트의 id 로 견준다. 둘 다 아니면 이 칸에는 칸 번호와 같은 id 만 들어 있을 수 있다.
        if ( objectID >= _compareFromID.load( std::memory_order_relaxed ) && pObject->getObjectID() != objectID )
            return nullptr;
        return pObject;
    }

    void GameObjectStore::ObjectSlotTable::clear()
    {
        _listSlot.forEachElement( []( atomic<GameObject*>& slot )
        {
            slot.store( nullptr, std::memory_order_release );
        } );
        _compareFromID.store( kObjectSlotCount, std::memory_order_relaxed );
    }

    GameObject* GameObjectStore::findRegisteredUnlocked( uint64 objectID ) const
    {
        if ( GameObject* pSlotObject = _objectSlotTable.load( objectID ); pSlotObject != nullptr )
            return pSlotObject;
        const auto it = _mapIDToObject.find( objectID );
        return ( it != _mapIDToObject.end() ) ? it->second : nullptr;
    }

    GameObject* GameObjectStore::findGameObjectByID( uint64 objectID ) const
    {
        // **빠른 길: 락도 해시도 없다.** 핸들 해석이 프레임당 오브젝트 수만큼 도는 자리라
        // 공유 잠금 하나가 곧 밀리초가 된다(벤치 실측: 호출당 110ns → 프레임당 2.2ms).
        if ( GameObject* pSlotObject = _objectSlotTable.load( objectID ); pSlotObject != nullptr )
            return ( pSlotObject->isPendingDestroy() == false ) ? pSlotObject : nullptr;

        // 칸이 막혀 맵에 든 오브젝트가 있을 때만 맵으로 간다(보통 없다). 없는 id 를 묻는 핸들(파괴된 대상)도 잠그지 않는다.
        if ( _overflowObjectCount.load( std::memory_order_acquire ) == 0 )
            return nullptr;
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapIDToObject.find( objectID );
        if ( it == _mapIDToObject.end() )
            return nullptr;
        GameObject* pObj = it->second;
        return ( pObj != nullptr && pObj->isPendingDestroy() == false ) ? pObj : nullptr;
    }

    void GameObjectStore::getAllGameObjects( vector<GameObject*>& outListGameObject ) const
    {
        outListGameObject.clear();
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        outListGameObject.reserve( _listGameObject.size() + _listPendingAdd.size() );
        outListGameObject.insert( outListGameObject.end(), _listGameObject.begin(), _listGameObject.end() );
        outListGameObject.insert( outListGameObject.end(), _listPendingAdd.begin(), _listPendingAdd.end() );
    }

    GameObject* GameObjectStore::findGameObjectByTag( TagID tag ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( GameObject* pObj : _listGameObject )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false && pObj->hasTag( tag ) )
                return pObj;
        }
        for ( GameObject* pObj : _listPendingAdd )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false && pObj->hasTag( tag ) )
                return pObj;
        }
        return nullptr;
    }

    void GameObjectStore::findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const
    {
        outListGameObject.clear();
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( GameObject* pObj : _listGameObject )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false && pObj->hasTag( tag ) )
                outListGameObject.push_back( pObj );
        }
        for ( GameObject* pObj : _listPendingAdd )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false && pObj->hasTag( tag ) )
                outListGameObject.push_back( pObj );
        }
    }

    void GameObjectStore::beginPlay()
    {
        // 표시를 먼저 세운다 — 도는 동안 onBeginPlay 가 붙이는 컴포넌트도 줄을 선다(이미 시작했으면 비트가 거른다).
        _bHasBegunPlay.store( true, std::memory_order_release );
        mergePendingAdds();
        // **잠금을 쥔 채 컴포넌트 코드를 부르지 않는다.** `forEachGameObject` 의 공유 잠금 안에서 onBeginPlay 를 부르면, onBeginPlay 가
        // 태그를 붙일 때(`addTag` → `addComponent<TagComponent>` → 풀 맵의 배타 잠금) 같은 스레드가 제 공유 잠금을 기다려
        // 에디터 Play 가 멈춘다. 목록을 받아 잠금 없이 돈다. 활성 여부와 무관하게
        // 시작한다(언리얼과 같다. 짝은 컴포넌트의 "시작됨" 비트가 맞춘다). 도는 동안 생긴 오브젝트는 아래 줄 처리가 시작한다.
        getAllGameObjects( _listPlayWalk );
        for ( GameObject* pObj : _listPlayWalk )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                pObj->beginPlay();
        }
        _listPlayWalk.clear();
        dispatchPendingBeginPlay();
    }

    void GameObjectStore::endPlay()
    {
        _bHasBegunPlay.store( false, std::memory_order_release );
        {
            std::scoped_lock<mutex> lock{ _beginPlayMutex };
            _listPendingBeginPlay.clear();
        }
        getAllGameObjects( _listPlayWalk );
        for ( GameObject* pObj : _listPlayWalk )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                pObj->endPlay();
        }
        _listPlayWalk.clear();
    }

    void GameObjectStore::queueBeginPlay( ComponentHandle handle )
    {
        std::scoped_lock<mutex> lock{ _beginPlayMutex };
        _listPendingBeginPlay.push_back( handle );
    }

    void GameObjectStore::dispatchPendingBeginPlay()
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

    Component* GameObjectStore::resolveComponent( ComponentHandle handle ) const
    {
        if ( handle.isValid() == false )
            return nullptr;
        GameObject* pObj = findGameObjectByID( handle.objectID() );
        if ( pObj == nullptr )
            return nullptr;
        return pObj->findComponentByID( handle.componentID(), true );
    }

    void GameObjectStore::destroyObject( GameObject* pObj, bool bDestroyChildren )
    {
        if ( pObj == nullptr )
            return;
        SW_ASSERT( WalkScope::isInsideWalk() == false );

        // **표시를 먼저, 원자적으로 자리를 잡는다.** 보고 나서 표시하면(check-then-set) 같은 오브젝트를 같은 프레임에
        // 없애는 두 스레드가 나란히 통과해 파괴 목록에 같은 포인터가 두 번 들어가고
        // `_poolGameObject.destroy` 가 같은 블록을 두 번 반납한다. `onTick` 은 병렬로 돌고
        // (총알 둘이 같은 적을 맞히는) 그 경우는 흔하다. 자리를 잡은 스레드만 진행한다.
        if ( pObj->tryMarkPendingDestroy() == false )
            return;

        vector<GameObject*> listChildren;
        if ( bDestroyChildren )
            pObj->getChildren( listChildren );

        // 계층 활성은 다시 맞추지 않는다 — 삭제 대기는 그 값의 입력이 아니다.
        pObj->forEachComponent( []( Component* pComp )
        { pComp->markPendingDestroy(); } );

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

    void GameObjectStore::destroyComponent( Component* pComp )
    {
        SW_ASSERT( WalkScope::isInsideWalk() == false );
        // destroyObject 와 같은 이유로 자리부터 잡는다. 여기 목록에 두 번 들어가면
        // `removeComponent` 가 두 번 불리고 컴포넌트 풀이 같은 블록을 두 번 받는다.
        if ( pComp == nullptr || pComp->tryMarkPendingDestroy() == false )
            return;

        // **포인터가 아니라 핸들로 적는다.** 줄을 선 뒤에도 즉시 경로(틱 밖의 `removeComponent` · 상태를 되돌리는 로드의
        // `clearComponents`)가 먼저 해제할 수 있다. 날 포인터로 들면 처리할 때 풀려난 블록을 — 그새 같은 자리에 새
        // 컴포넌트가 들었으면 **엉뚱한 컴포넌트를** — 지운다. 핸들은 처리 때 다시 풀어, 이미 없으면 건너뛴다.
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingDestroyComponent.push_back( pComp->getHandle() );
        // 틱에 참여하던 컴포넌트면 소유 오브젝트의 항목을 다시 짓게 한다. 나머지는 등록부와 무관하다.
        if ( pComp->hasTickWork() )
            _pTickRegistry->markObjectDirty( pComp->getOwner() );
    }

    void GameObjectStore::destroyComponentInstance( Component* pComp )
    {
        if ( pComp == nullptr )
            return;
        // 해체는 틱 밖에서만 한다 — 틱 중의 제거 · 비우기는 미뤄져 틱 뒤의 지연 파괴가 여기로 온다.
        SW_ASSERT( _pStructuralChangeBuffer->isFrozen() == false );

        // **컴포넌트 해체는 여기 하나다** — 등록 해제 → 파괴 콜백 → 소유자 끊기 → 소멸 → 반납.
        // 등록부는 raw 포인터를 들고 있다(렌더 경로가 프레임마다 전부 훑으므로 핸들은 비싸다). 그래서 메모리를 실제로 놓는
        // 이 지점에서 등록을 해제해 "등록된 채로 해제" 가 구조적으로 없게 한다. `onUnregister` 는 여기서 정확히 한 번 불린다.
        // 소멸자 호출 전이어야 가상 디스패치가 유효하다. 시작했던 컴포넌트는 여기서 끝낸다 — 떼기 · 비우기 · 지연 파괴 · 모듈 내리기가
        // 모두 이 한 곳을 지나므로 onEndPlay 가 빠지는 길이 없다(모듈 코드가 아직 올라와 있는 시점이다).
        pComp->dispatchEndPlay();
        pComp->onUnregister( *_pManager );
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

    void GameObjectStore::processDeferredDestruction()
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
            GameObject* pOwner = findGameObjectByID( handle.objectID() );
            Component*  pComp  = ( pOwner != nullptr ) ? pOwner->findComponentByID( handle.componentID(), true ) : nullptr;
            if ( pComp != nullptr && pComp->isPendingDestroy() )
                (void)pOwner->removeComponent( pComp ); // 실패(목록에 없음)는 removeComponent 가 알린다
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
                        // 본 목록에 없으면 이번 프레임에 만들어져 아직 병합되지 않은 것이다. 그때만 대기 목록을 훑는다
                        // (늘 훑으면 프레임에 100 개를 만들고 100 개를 지울 때 만 번 비교다).
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
                    if ( _objectSlotTable.tryRemove( pObj->getObjectID(), pObj ) == false )
                    {
                        _mapIDToObject.erase( pObj->getObjectID() );
                        _overflowObjectCount.store( static_cast<uint32>( _mapIDToObject.size() ), std::memory_order_release );
                    }
                }
            }
        }

        // 처리 목록은 이 함수만 만진다(대기 목록과 맞바꾼 것이다). 지역 벡터로 베끼지 않는다 — 목록은 용량을 들고 있다.
        for ( GameObject* pObj : _listProcessingDestroyObject )
        {
            if ( pObj == nullptr )
                continue;
            // 메모리를 놓기 전에 등록부의 그룹 목록에서 뺀다. 지운 컴포넌트 쪽은 소유 오브젝트가 표시되어 틱 전에 다시 지어진다.
            _pTickRegistry->unregisterObject( pObj );
            _poolGameObject.destroy( pObj );
        }
        _listProcessingDestroyObject.clear();
        _listProcessingDestroyComponent.clear();
        _bProcessingDestruction = false;
    }

    void GameObjectStore::clear()
    {
        // 지연 파괴가 도는 목록을 여기서 비우면 그 루프가 풀린 메모리를 읽는다.
        SW_ASSERT( _bProcessingDestruction == false );
        vector<GameObject*> listToDestroy;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };

            listToDestroy.reserve( _listGameObject.size() + _listPendingAdd.size() );
            listToDestroy.insert( listToDestroy.end(), _listGameObject.begin(), _listGameObject.end() );
            listToDestroy.insert( listToDestroy.end(), _listPendingAdd.begin(), _listPendingAdd.end() );

            _listPendingDestroyObject.clear();
            _listPendingDestroyComponent.clear();
            _listProcessingDestroyObject.clear();
            _listProcessingDestroyComponent.clear();
            {
                std::scoped_lock<mutex> lockBeginPlay{ _beginPlayMutex };
                _listPendingBeginPlay.clear();
            }

            _listGameObject.clear();
            _listPendingAdd.clear();
            _mapNameToObject.clear();
            _mapNameSuffix.clear();
            _mapIDToObject.clear();
            _overflowObjectCount.store( 0, std::memory_order_release );
            _objectSlotTable.clear();
        }

        for ( GameObject* pObj : listToDestroy )
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
    }

    void GameObjectStore::mergePendingAdds()
    {
        // 잠금 한 번에 옮긴다. 대기 목록을 지역 벡터로 **옮기지** 않는다(그러면 대기 목록이 매번 용량을 잃고 다음 스폰이 다시 할당한다).
        // 이름 맵 · id 표는 만들 때(`createGameObjectUnlocked`) 이미 넣었다. 여기서 다시 넣지 않는다.
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
        {
            _listGameObject[objectIndex]->refreshActiveInHierarchy();
        }
        // 틱 항목은 addComponent 가 틱에 참여하는 컴포넌트를 붙일 때 이미 더럽혔다. 병합 자체는 멤버십을 바꾸지 않는다.
    }

#if !defined( SW_SHIPPING )
    uint32 GameObjectStore::destroyComponentsOfModule( string_view moduleName )
    {
        SW_ASSERT( _pStructuralChangeBuffer->isFrozen() == false );
        // 지연 파괴 목록부터 비운다. 거기 남은 컴포넌트의 소멸자도 모듈 코드다.
        processDeferredDestruction();

        const hashed_string     hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        vector<ComponentHandle> listToDestroy;
        forEachGameObject( [&]( GameObject* pObj )
        {
            pObj->forEachComponent( [&]( Component* pComp )
            {
                const TypeInfo* pTypeInfo = ( pComp != nullptr ) ? pComp->getTypeInfo() : nullptr;
                if ( pTypeInfo != nullptr && pTypeInfo->_moduleName == hashModule )
                    listToDestroy.push_back( pComp->getHandle() );
            } );
        } );
        // **핸들로 모으고 매번 다시 푼다.** 지우는 동안 모듈 콜백(onEndPlay · onUnregister · onDestroy)이 돌고, 그 콜백이 형제 컴포넌트를
        // 곧바로 지울 수 있다. 생포인터를 들고 돌면 다음 차례가 풀에 반납된 자리(다른 컴포넌트가 다시 받았으면 엉뚱한 것)를
        // 지운다. 지연 파괴(`processDeferredDestruction`)가 핸들로 다시 푸는 것과 같은 이유다.
        uint32 count{ 0 };
        for ( const ComponentHandle handle : listToDestroy )
        {
            Component* pComp = resolveComponent( handle );
            if ( pComp == nullptr )
                continue;
            GameObject* pOwner = pComp->getOwner();
            if ( pOwner == nullptr )
                continue;
            if ( pOwner->removeComponent( pComp ) )
                ++count;
        }
        // removeComponent 는 얼려 있으면 미룬다. 위에서 단언했지만, 미뤄졌더라도 여기서 끝낸다.
        processDeferredDestruction();

        if ( count > 0 )
            SW_LOG_INFO( "Destroyed %# live component(s) of module '%#' before unload.", count, moduleName );
        return count;
    }
#endif

    uint64 GameObjectStore::generateNewID()
    {
        return _s_nextObjectID.fetch_add( 1, std::memory_order_relaxed );
    }

    bool GameObjectStore::isNameTakenUnlocked( hashed_string name ) const
    {
        const auto it = _mapNameToObject.find( name );
        return it != _mapNameToObject.end() && it->second._pObject != nullptr && it->second._pObject->isPendingDestroy() == false;
    }

    void GameObjectStore::releaseNameSuffixUnlocked( const NameEntry& nameEntry )
    {
        if ( nameEntry._suffix == 0 )
            return;
        const auto stateIt = _mapNameSuffix.find( nameEntry._baseName );
        if ( stateIt != _mapNameSuffix.end() )
            stateIt->second._listFreeSuffix.push_back( nameEntry._suffix );
    }

    hashed_string GameObjectStore::makeUniqueNameUnlocked( hashed_string requested, NameEntry& outEntry )
    {
        if ( isNameTakenUnlocked( requested ) == false )
            return requested;

        const utf8* pBase = requested.c_str();
        if ( StringUtil::isNullOrEmpty( pBase ) )
            pBase = "GameObject";

        string_view baseView{ pBase };
        if ( baseView.size() > 96 )
            baseView = baseView.substr( 0, 96 );

        // 이름마다 **번호 상태를 기억한다**(언리얼 MakeUniqueObjectName 의 자리). 매번 _2 부터 다시 물으면 같은
        // 이름 N 개일 때 생성 하나가 N 번 조회다(8000 개에 2.6 초). **지운 오브젝트의 번호부터 되쓴다** — 번호마다
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

    PoolAllocator* GameObjectStore::getOrCreateComponentPool( const TypeInfo* pTypeInfo, size_t typeSize )
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
} // namespace sw
