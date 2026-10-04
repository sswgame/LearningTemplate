/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 의 수명 관리입니다(생성 · 이름 · id 표 · 조회 · 파괴 · 이름으로 컴포넌트 만들기). 프레임 경로(tick · 트랜스폼 배치)는 `GameObjectManagerTick.cpp` 에 있습니다.
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
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct GameObjectManagerInternal
        {
            /** @brief 이 스레드가 들어가 있는 `forEachGameObject` 의 깊이입니다(`WalkScope`). */
            inline static thread_local uint32 s_walkDepth = 0;

            /**
             * @brief 이름으로 컴포넌트를 만들 타입을 리플렉션 표에서 찾습니다. 없으면 nullptr 입니다.
             * @details 짧은 이름 · FQN · 옛 이름(`REFLECT( Alias = Old )`)을 `findType` 이 모두 받는다. 못 찾으면 `이름#번호` 꼬리(같은 타입 둘째 컴포넌트의
             *          저장 이름)를 떼고 한 번 더 찾는다.
             */
            static const TypeInfo* findComponentType( hashed_string typeName )
            {
                const TypeRegistry& registry = engine::getTypeRegistry();
                const TypeInfo*     pType    = registry.findType( typeName );
                if ( pType != nullptr )
                    return pType;

                const utf8* pRawName = typeName.c_str();
                if ( pRawName == nullptr )
                    return nullptr;
                const utf8* pHash = nullptr;
                for ( const utf8* pCursor = pRawName; *pCursor != '\0'; ++pCursor )
                {
                    if ( *pCursor == '#' )
                        pHash = pCursor;
                }
                if ( pHash == nullptr || pHash == pRawName )
                    return nullptr;
                return registry.findType( hashed_string( pRawName, static_cast<uint32>( pHash - pRawName ) ) );
            }

            /**
             * @brief 스레드마다 둔 이름 → 타입 조회 캐시입니다. 타입 표의 **캐시**일 뿐 등록부가 아닙니다 — 표의 세대가 바뀌면 통째로 버립니다.
             * @details 씬 · 프리팹 로드는 컴포넌트마다 이름으로 찾는다. 표 조회는 공유 잠금 + 해시 두 번(짧은 이름 → FQN → 줄)이라
             *          컴포넌트 하나에 10 ns 남짓을 더했다(Release `GameObjectBenchTest.AddComponentByName`). `TypeInfo` 주소는 고정이고
             *          해제는 세대를 올리므로 포인터를 들고 있어도 된다. 생성 함수(`_addComponent`)는 부를 때마다 다시 읽는다.
             */
            struct ComponentTypeCache
            {
                static constexpr uint32 kSlotCount = 64;
                struct Slot
                {
                    uint32          _nameIndex; ///< `hashed_string::getIndex()`(대소문자 무시 비교 키)
                    const TypeInfo* _pType;     ///< 비었으면 nullptr
                };
                uint32 _generation; ///< 채울 때의 `TypeRegistry::getGeneration`
                Slot   _arrSlot[kSlotCount];
            };
            inline static thread_local ComponentTypeCache s_typeCache{};

            /** @brief `findComponentType` 의 캐시 앞단입니다. 못 찾은 이름은 적지 않습니다(다음 등록이 세대를 올리기 전이라도 매번 다시 찾는다). */
            static const TypeInfo* findComponentTypeCached( hashed_string typeName )
            {
                ComponentTypeCache& cache      = s_typeCache;
                const uint32        generation = engine::getTypeRegistry().getGeneration();
                if ( cache._generation != generation )
                {
                    for ( ComponentTypeCache::Slot& slot : cache._arrSlot )
                        slot = ComponentTypeCache::Slot{ 0, nullptr };
                    cache._generation = generation;
                }
                const uint32              nameIndex = typeName.getIndex();
                ComponentTypeCache::Slot& slot      = cache._arrSlot[nameIndex % ComponentTypeCache::kSlotCount];
                if ( slot._pType != nullptr && slot._nameIndex == nameIndex )
                    return slot._pType;
                const TypeInfo* pType = findComponentType( typeName );
                if ( pType != nullptr )
                    slot = ComponentTypeCache::Slot{ nameIndex, pType };
                return pType;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    atomic<uint64> GameObjectManager::_s_nextObjectId = 1;

    GameObjectManager::GameObjectManager()
        : _poolGameObject{ 256, true }
        , _mapComponentPool{}
        , _listGameObject{}
        , _mapNameToObject{}
        , _mapNameSuffix{}
        , _mapIdToObject{}
        , _overflowObjectCount{ 0 }
        , _listPendingAdd{}
        , _listPendingDestroyObject{}
        , _listPendingDestroyComponent{}
        , _listProcessingDestroyObject{}
        , _listProcessingDestroyComponent{}
        , _mutex{}
        , _overlapWorld2D{}
        , _bTicking{ false }
        , _bProcessingDestruction{ false }
        , _bDeferredHierarchyChange{ SW_FALSE }
        , _stageTransformApplyCount{ 0 }
        , _tickStageBuildCount{ 0 }
        , _listPlayWalk{}
        , _bHasBegunPlay{ false }
        , _beginPlayMutex{}
        , _listPendingBeginPlay{}
        , _listProcessingBeginPlay{}
        , _deferredStructuralQueue{}
        , _deferredPostTickQueue{}
        , _transformHierarchy{}
        , _primitiveRegistry{}
        , _lightRegistry{}
        , _cameraRegistry{}
        , _animationSystem{}
        , _tickRegistry{}
    {
        _animationSystem.setObjectManager( this );
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

        _s_nextObjectId.fetch_max( objectId + 1, std::memory_order_relaxed );
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
        if ( _objectSlotTable.tryStore( objectId, pObj ) == false )
        {
            _mapIdToObject.insert_or_assign( objectId, pObj );
            _overflowObjectCount.store( static_cast<uint32>( _mapIdToObject.size() ), std::memory_order_release );
        }

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

    GameObject* GameObjectManager::findGameObjectByName( hashed_string name ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapNameToObject.find( name );
        if ( it == _mapNameToObject.end() )
            return nullptr;
        GameObject* pObj = it->second._pObject;
        return ( pObj != nullptr && pObj->isPendingDestroy() == false ) ? pObj : nullptr;
    }

    // ======================================================================
    // ObjectSlotTable: id → GameObject* 를 락 없이 읽는 밀집 표
    // ======================================================================

    bool GameObjectManager::ObjectSlotTable::tryStore( uint64 objectId, GameObject* pObject )
    {
        // 쓰기는 모두 매니저 락 안이라 여기서 두 스레드가 겹치지 않는다.
        atomic<GameObject*>* pSlot = _listSlot.ensure( getSlotIndex( objectId ) );
        if ( pSlot == nullptr )
            return false;
        const GameObject* pOccupant = pSlot->load( std::memory_order_relaxed );
        if ( pOccupant != nullptr && pOccupant != pObject )
            return false;
        // 칸을 발행하기 **전에** 적는다. 읽는 쪽은 칸을 acquire 로 읽은 뒤 이것을 보므로, 감긴 id 의 오브젝트를 보는 쪽은 반드시 true 를 본다.
        if ( objectId >= kObjectSlotCount )
            _compareFromId.store( 0, std::memory_order_relaxed );
        pSlot->store( pObject, std::memory_order_release );
        return true;
    }

    bool GameObjectManager::ObjectSlotTable::tryRemove( uint64 objectId, const GameObject* pObject )
    {
        // 지우는 길이라 청크를 새로 만들 이유가 없다.
        atomic<GameObject*>* pSlot = _listSlot.find( getSlotIndex( objectId ) );
        if ( pSlot == nullptr || pSlot->load( std::memory_order_relaxed ) != pObject )
            return false;
        pSlot->store( nullptr, std::memory_order_release );
        return true;
    }

    GameObject* GameObjectManager::ObjectSlotTable::load( uint64 objectId ) const
    {
        const atomic<GameObject*>* pSlot = _listSlot.find( getSlotIndex( objectId ) );
        if ( pSlot == nullptr )
            return nullptr;
        GameObject* pObject = pSlot->load( std::memory_order_acquire );
        if ( pObject == nullptr )
            return nullptr;
        // 칸은 id 의 아래 비트라 같은 칸의 다른 id 가 들어 있을 수 있다 — 묻는 id 가 감긴 것이거나, 감긴 id 가 들어온 적이 있을 때
        // (`_compareFromId` 가 0). 그때만 오브젝트의 id 로 견준다. 둘 다 아니면 이 칸에는 칸 번호와 같은 id 만 들어 있을 수 있다.
        if ( objectId >= _compareFromId.load( std::memory_order_relaxed ) && pObject->getObjectId() != objectId )
            return nullptr;
        return pObject;
    }

    void GameObjectManager::ObjectSlotTable::clear()
    {
        _listSlot.forEachElement( []( atomic<GameObject*>& slot )
        {
            slot.store( nullptr, std::memory_order_release );
        } );
        _compareFromId.store( kObjectSlotCount, std::memory_order_relaxed );
    }

    GameObject* GameObjectManager::findRegisteredUnlocked( uint64 objectId ) const
    {
        if ( GameObject* pSlotObject = _objectSlotTable.load( objectId ); pSlotObject != nullptr )
            return pSlotObject;
        const auto it = _mapIdToObject.find( objectId );
        return ( it != _mapIdToObject.end() ) ? it->second : nullptr;
    }

    GameObject* GameObjectManager::findGameObjectById( uint64 objectId ) const
    {
        // **빠른 길: 락도 해시도 없다.** 핸들 해석이 프레임당 오브젝트 수만큼 도는 자리라
        // 공유 잠금 하나가 곧 밀리초가 된다(벤치 실측: 호출당 110ns → 프레임당 2.2ms).
        if ( GameObject* pSlotObject = _objectSlotTable.load( objectId ); pSlotObject != nullptr )
            return ( pSlotObject->isPendingDestroy() == false ) ? pSlotObject : nullptr;

        // 칸이 막혀 맵에 든 오브젝트가 있을 때만 맵으로 간다(보통 없다). 없는 id 를 묻는 핸들(파괴된 대상)도 잠그지 않는다.
        if ( _overflowObjectCount.load( std::memory_order_acquire ) == 0 )
            return nullptr;
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapIdToObject.find( objectId );
        if ( it == _mapIdToObject.end() )
            return nullptr;
        GameObject* pObj = it->second;
        return ( pObj != nullptr && pObj->isPendingDestroy() == false ) ? pObj : nullptr;
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

    void GameObjectManager::findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const
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

    void GameObjectManager::beginPlay()
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
            if ( pObj != nullptr && pObj->isPendingDestroy() == false )
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

    void GameObjectManager::destroyComponent( Component* pComp )
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
            _tickRegistry.markObjectDirty( pComp->getOwner() );
    }

    void GameObjectManager::destroyComponentInstance( Component* pComp )
    {
        if ( pComp == nullptr )
            return;
        // 해체는 틱 밖에서만 한다 — 틱 중의 제거 · 비우기는 미뤄져 틱 뒤의 지연 파괴가 여기로 온다.
        SW_ASSERT( isStructuralMutationFrozen() == false );

        // **컴포넌트 해체는 여기 하나다** — 등록 해제 → 파괴 콜백 → 소유자 끊기 → 소멸 → 반납.
        // 등록부는 raw 포인터를 들고 있다(렌더 경로가 프레임마다 전부 훑으므로 핸들은 비싸다). 그래서 메모리를 실제로 놓는
        // 이 지점에서 등록을 해제해 "등록된 채로 해제" 가 구조적으로 없게 한다. `onUnregister` 는 여기서 정확히 한 번 불린다.
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
                    if ( _objectSlotTable.tryRemove( pObj->getObjectId(), pObj ) == false )
                    {
                        _mapIdToObject.erase( pObj->getObjectId() );
                        _overflowObjectCount.store( static_cast<uint32>( _mapIdToObject.size() ), std::memory_order_release );
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

            _deferredStructuralQueue.clear();
            _deferredPostTickQueue.clear();

            _listGameObject.clear();
            _listPendingAdd.clear();
            _mapNameToObject.clear();
            _mapNameSuffix.clear();
            _mapIdToObject.clear();
            _overflowObjectCount.store( 0, std::memory_order_release );
            _objectSlotTable.clear();
            _transformHierarchy.clear();
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

        _tickRegistry.clear();
        markTickStagesDirty();
        // 컴포넌트가 바디를 놓았다 — 빈 물리 씬과 쌓인 시간을 버린다(다음 씬은 처음 쓸 때 새로 만든다).
        _scenePhysics.shutdown();
    }

    void GameObjectManager::mergePendingAdds()
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
            _listGameObject[objectIndex]->refreshActiveInHierarchy();
        // 틱 항목은 addComponent 가 틱에 참여하는 컴포넌트를 붙일 때 이미 더럽혔다. 병합 자체는 멤버십을 바꾸지 않는다.
    }

#if !defined( SW_SHIPPING )
    uint32 GameObjectManager::destroyComponentsOfModule( string_view moduleName )
    {
        SW_ASSERT( isStructuralMutationFrozen() == false );
        // 지연 파괴 목록부터 비운다. 거기 남은 컴포넌트의 소멸자도 모듈 코드다.
        processDeferredDestruction();

        const hashed_string     hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        vector<ComponentHandle> listToDestroy;
        forEachComponent( [&]( Component* pComp )
        {
            const TypeInfo* pTypeInfo = ( pComp != nullptr ) ? pComp->getTypeInfo() : nullptr;
            if ( pTypeInfo != nullptr && pTypeInfo->_moduleName == hashModule )
                listToDestroy.push_back( pComp->getHandle() );
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

    Component* GameObjectManager::addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning )
    {
        if ( pGameObject == nullptr )
            return nullptr;
        if ( engine::areEngineServicesBound() == false )
        {
            if ( bLogWarning )
                SW_LOG_WARNING( "Cannot create component '%#' by name - engine services (TypeRegistry) are not bound", typeName.c_str() );
            return nullptr;
        }

        const TypeInfo* pType = GameObjectManagerInternal::findComponentTypeCached( typeName );
        if ( pType == nullptr )
        {
            if ( bLogWarning )
                SW_LOG_WARNING( "Component type '%#' is not registered (no REFLECT, its module is not loaded, or a typo)", typeName.c_str() );
            return nullptr;
        }
        if ( pType->_addComponent == nullptr )
        {
            if ( bLogWarning )
                SW_LOG_WARNING( "Type '%#' cannot be created as a component (abstract, not a component, or its module was unloaded)", typeName.c_str() );
            return nullptr;
        }
        return pType->_addComponent( pGameObject );
    }

    vector<hashed_string> GameObjectManager::getRegisteredComponentTypeNames()
    {
        vector<hashed_string> listName;
        if ( engine::areEngineServicesBound() == false )
            return listName;
        engine::getTypeRegistry().forEachType( [&listName]( const TypeInfo& info )
        {
            if ( info._addComponent != nullptr )
                listName.push_back( info._name );
        } );
        return listName;
    }

    uint64 GameObjectManager::generateNewId()
    {
        return _s_nextObjectId.fetch_add( 1, std::memory_order_relaxed );
    }

    bool GameObjectManager::isNameTakenUnlocked( hashed_string name ) const
    {
        const auto it = _mapNameToObject.find( name );
        return it != _mapNameToObject.end() && it->second._pObject != nullptr && it->second._pObject->isPendingDestroy() == false;
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
} // namespace sw
