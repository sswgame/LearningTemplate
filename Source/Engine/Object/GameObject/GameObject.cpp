#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    namespace
    {
        const TagContainer s_emptyTags{};

        /** @brief 이 스레드에서 진행 중인 컴포넌트 ID 복원입니다. `GameObject::ComponentIdRestoreScope` 가 채우고 되돌립니다. */
        struct ComponentIdRestoreState
        {
            const GameObject*     _pTarget{ nullptr };
            const ObjectIdentity* _pIdentity{ nullptr };
            size_t                _cursor{ 0 }; ///< 목록에서 다음에 볼 자리
        };
        thread_local ComponentIdRestoreState t_componentIdRestore{};

        struct GameObjectInternal
        {
            /**
             * @brief @p pOwner 에 붙는 @p typeName 컴포넌트가 되살릴 원래 ID 를 가져갑니다. 복원 중이 아니거나 목록에 없으면 false.
             * @details 커서부터 앞으로 찾아 타입이 같은 첫 항목을 가져갑니다. 목록과 다른 컴포넌트가 끼어들어도(생성 중에 다른
             *          컴포넌트를 스스로 붙이는 타입 등) 그 하나만 새 ID 를 받고 나머지 순서는 어긋나지 않습니다.
             */
            static bool takeRestoredComponentId( const GameObject* pOwner, hashed_string typeName, uint64& outComponentId )
            {
                ComponentIdRestoreState& state = t_componentIdRestore;
                if ( state._pIdentity == nullptr || state._pTarget != pOwner )
                    return false;

                const vector<ObjectIdentity::ComponentEntry>& listEntry = state._pIdentity->_listComponent;
                for ( size_t entryIndex = state._cursor; entryIndex < listEntry.size(); ++entryIndex )
                {
                    if ( listEntry[entryIndex]._typeName != typeName )
                        continue;
                    state._cursor  = entryIndex + 1;
                    outComponentId = listEntry[entryIndex]._componentId;
                    return outComponentId != 0;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObject" );

    GameObject::GameObject()
        : GameObject( hashed_string( "GameObject" ) )
    {
    }

    GameObject::GameObject( hashed_string name )
        : _objectId{ _s_nextObjectId.fetch_add( 1, std::memory_order_relaxed ) }
        , _name{ name }
        , _pOwnerManager{ nullptr }
        , _bActive{ true }
        , _bIsActiveInHierarchy{ true }
        , _bIsPendingKill{ false }
        , _listComponent{}
        , _pPrimaryScene{ nullptr }
        , _listTickItem{}
        , _arrTickGroupBegin{}
        , _arrTickIndex{ TickRegistry::kNotInList, TickRegistry::kNotInList, TickRegistry::kNotInList, TickRegistry::kNotInList }
        , _tickPrerequisiteCount{ 0 }
        , _bTickDirty{ SW_FALSE }
        , _managerIndex{ invalid_index::kUint32 }
    {
    }

    /**
     * @brief 게임 오브젝트 소멸자: 부모 계층 연결을 해제하고 소유 컴포넌트를 파괴합니다.
     */
    GameObject::~GameObject()
    {
        // 컴포넌트를 파괴하기 전에 부모-자식 계층 연결을 끊는다(자식 오브젝트들은 루트가 되어 살아남는다).
        detachFromParent();

        // 자식 목록을 복사하지 않는다. 떼면 그 자리가 swap-remove 되므로 뒤에서 앞으로 돈다(뒤에서 온 원소는 이미 본 것).
        if ( SceneComponent* pSceneComp = getPrimarySceneComponent() )
        {
            const vector<SceneComponent*>& listChildComp = pSceneComp->getChildren();
            for ( size_t childIndex = listChildComp.size(); childIndex-- > 0; )
            {
                if ( childIndex >= listChildComp.size() )
                    continue;
                SceneComponent* pChildComp = listChildComp[childIndex];
                GameObject*     pChildObj  = ( pChildComp != nullptr ) ? pChildComp->getOwner() : nullptr;
                if ( pChildObj != nullptr && pChildObj != this )
                    pChildObj->detachFromParent();
            }
        }

        clearComponents();
    }

    const TypeInfo* GameObject::getTypeInfo() const
    {
        return StaticType();
    }

    /**
     * @brief 게임플레이가 시작될 때(Play Mode) 소유한 모든 활성 컴포넌트의 onBeginPlay 를 부릅니다.
     */
    void GameObject::beginPlay()
    {
        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingKill() || pComp->isActive() == false )
                continue;
            pComp->onBeginPlay();
        }
    }

    /**
     * @brief 게임플레이가 끝날 때 소유한 모든 컴포넌트의 onEndPlay 를 부릅니다.
     */
    void GameObject::endPlay()
    {
        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingKill() || pComp->isActive() == false )
                continue;
            pComp->onEndPlay();
        }
    }

    void GameObject::onPropertyChanged( hashed_string propertyName )
    {
        (void)propertyName;
        // 인스펙터는 `_bActive` 를 세터가 아니라 멤버에 직접 쓴다. setActive 가 해 주던 계층 전파를
        // 여기서 해 줘야 자식들의 isActiveInHierarchy 가 따라온다. 프리미티브 집합 무효화도
        // 그 안에서 함께 일어난다.
        refreshActiveInHierarchy();
    }

    void GameObject::markPrimitiveSetDirtyOnManager()
    {
        GameObjectManager* pManager = getManager();
        if ( pManager != nullptr )
            pManager->getPrimitiveRegistry().markSetDirty();
    }

    /**
     * @brief 프레임이 끝날 때 안전하게 파괴되도록 이 게임 오브젝트를 파괴 대기에 올립니다.
     */
    void GameObject::destroy()
    {
        if ( _pOwnerManager == nullptr )
            return;
        _pOwnerManager->destroyObject( this );
    }

    void GameObject::markPendingKill()
    {
        tryMarkPendingKill();
    }

    bool GameObject::tryMarkPendingKill()
    {
        return _bIsPendingKill.exchange( true, std::memory_order_acq_rel ) == false;
    }

    /**
     * @brief 게임 오브젝트 이름을 바꾸고 GameObjectManager 의 이름 검색 인덱스를 갱신합니다.
     */
    void GameObject::setName( hashed_string name )
    {
        if ( name == _name )
            return;

        const hashed_string oldName = _name;
        _name                       = name;
        if ( _pOwnerManager != nullptr )
            _pOwnerManager->notifyNameChanged( this, oldName, name );
    }

    /**
     * @brief 로컬 활성 상태를 변경하고 소유 컴포넌트에 전파한 뒤, 계층 활성을 자손까지 한 번 재계산합니다.
     */
    void GameObject::setActive( bool bActive )
    {
        _bActive.store( bActive, std::memory_order_relaxed );

        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingKill() )
                continue;
            pComp->setActive( bActive );
        }
        // 계층 재계산은 이 안에서 한 번만 한다. 예전에는 위에서 한 번 더 돌아 자손 전체를 두 번 걸었다.
        onPropertyChanged( hashed_string( "_bActive" ) );
    }

    bool GameObject::attachToParent( GameObject* pParent )
    {
        if ( pParent == nullptr )
            return false;
        if ( getParent() == pParent )
            return true;

        GameObjectManager* pMgr = getManager();
        if ( pMgr != nullptr && pMgr->isParallelTransformReadOnly() )
        {
            const uint64 childId  = _objectId;
            const uint64 parentId = pParent->getObjectId();
            pMgr->deferTransformUpdate( [pMgr, childId, parentId]()
            {
                GameObject* pChildObj  = pMgr->findGameObjectById( childId );
                GameObject* pParentObj = pMgr->findGameObjectById( parentId );
                if ( pChildObj != nullptr && pParentObj != nullptr )
                    pChildObj->attachToParent( pParentObj );
            } );
            return true;
        }

        SceneComponent* pChildSc = getPrimarySceneComponent();
        if ( pChildSc == nullptr )
            return false;

        SceneComponent* pParentSc = pParent->getPrimarySceneComponent();
        if ( pParentSc == nullptr )
            return false;

        bool bAttached = pChildSc->attachToComponent( pParentSc );
        if ( bAttached )
            refreshActiveInHierarchy();

        return bAttached;
    }

    void GameObject::detachFromParent()
    {
        GameObjectManager* pMgr = getManager();
        if ( pMgr != nullptr && pMgr->isParallelTransformReadOnly() )
        {
            const uint64 childId = _objectId;
            pMgr->deferTransformUpdate( [pMgr, childId]()
            {
                GameObject* pChildObj = pMgr->findGameObjectById( childId );
                if ( pChildObj != nullptr )
                    pChildObj->detachFromParent();
            } );
            return;
        }

        SceneComponent* pChildSc = getPrimarySceneComponent();
        if ( pChildSc != nullptr )
        {
            pChildSc->detachFromComponent();
            refreshActiveInHierarchy();
        }
    }

    GameObject* GameObject::getParent() const
    {
        SceneComponent* pSceneComp = getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return nullptr;
        SceneComponent* pParentComp = pSceneComp->getParent();
        if ( pParentComp == nullptr )
            return nullptr;
        return pParentComp->getOwner();
    }

    void GameObject::refreshActiveInHierarchy()
    {
        bool        bParentActive = true;
        GameObject* pParent       = getParent();
        if ( pParent != nullptr )
            bParentActive = pParent->isActiveInHierarchy();

        const bool bActiveInHierarchy = bParentActive && isActive();
        const bool bWasActive         = _bIsActiveInHierarchy.exchange( bActiveInHierarchy, std::memory_order_relaxed );

        // 이 값이 곧 렌더 스냅샷의 포함 여부다. 바뀌면 프리미티브 집합이 통째로 달라진다.
        if ( bWasActive != bActiveInHierarchy )
            markPrimitiveSetDirtyOnManager();

        // 자식 목록을 만들지 않는다. 재귀는 계층을 바꾸지 않으므로 그대로 돈다. 병합되는 오브젝트마다 불리는 자리다.
        if ( SceneComponent* pSceneComp = getPrimarySceneComponent() )
        {
            for ( SceneComponent* pChildComp : pSceneComp->getChildren() )
            {
                GameObject* pChildObj = ( pChildComp != nullptr ) ? pChildComp->getOwner() : nullptr;
                if ( pChildObj != nullptr && pChildObj != this )
                    pChildObj->refreshActiveInHierarchy();
            }
        }
    }

    void GameObject::getChildren( vector<GameObject*>& outListChild ) const
    {
        outListChild.clear();
        SceneComponent* pSceneComp = getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        const vector<SceneComponent*>& listChildComp = pSceneComp->getChildren();
        outListChild.reserve( listChildComp.size() );
        for ( SceneComponent* pChildComp : listChildComp )
        {
            if ( pChildComp == nullptr )
                continue;
            GameObject* pChildObj = pChildComp->getOwner();
            // **같은 오브젝트 안의 부착은 자식 오브젝트가 아니다.** 한 GameObject 의 SceneComponent 를
            // 다른 SceneComponent 에 붙이는 것은 정상적인 구성인데(`applyAttachSerializeFields` 가
            // 복원까지 한다), 그러면 그 자식 컴포넌트의 owner 는 자기 자신이라 여기서 **자신이
            // 자기 자식으로** 나왔다. `refreshActiveInHierarchy` 가 그대로 무한 재귀해 스택을 넘겼다.
            if ( pChildObj == nullptr || pChildObj == this )
                continue;
            outListChild.push_back( pChildObj );
        }
    }

    bool GameObject::hasChildren() const
    {
        SceneComponent* pSceneComp = getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return false;
        for ( SceneComponent* pChildComp : pSceneComp->getChildren() )
        {
            if ( pChildComp == nullptr )
                continue;
            GameObject* pChildObj = pChildComp->getOwner();
            if ( pChildObj != nullptr && pChildObj != this )
                return true;
        }
        return false;
    }

    bool GameObject::isDescendantOf( const GameObject* pAncestor ) const
    {
        if ( pAncestor == nullptr )
            return false;
        const GameObject* pCurrent = this;
        while ( pCurrent != nullptr )
        {
            if ( pCurrent == pAncestor )
                return true;
            pCurrent = pCurrent->getParent();
        }
        return false;
    }

    SceneComponent* GameObject::getPrimarySceneComponent() const
    {
        Component* pCached = _pPrimaryScene.load( std::memory_order_relaxed );
        if ( pCached != nullptr && pCached->isPendingKill() == false )
            return static_cast<SceneComponent*>( pCached );

        // 캐시가 비었거나 죽었다. 살아 있는 첫 씬 컴포넌트를 목록에서 찾아 적는다(리플렉션 캐스트 없이 플래그 비트로).
        Component* pFound = nullptr;
        for ( Component* pComp : _listComponent )
        {
            if ( pComp != nullptr && pComp->isPendingKill() == false && pComp->isSceneComponent() )
            {
                pFound = pComp;
                break;
            }
        }
        _pPrimaryScene.store( pFound, std::memory_order_relaxed );
        return static_cast<SceneComponent*>( pFound );
    }

    void GameObject::addTag( TagID tag )
    {
        if ( isComponentMutationFrozen() )
        {
            const TagID tagCopy = tag;
            deferOnSelfPostTick( Delegate<void( GameObject& )>( [tagCopy]( GameObject& self )
            { self.addTag( tagCopy ); } ) );
            return;
        }

        TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp == nullptr )
            pTagComp = addComponent<TagComponent>();
        if ( pTagComp != nullptr )
            pTagComp->addTag( tag );
    }

    void GameObject::removeTag( TagID tag )
    {
        if ( isComponentMutationFrozen() )
        {
            const TagID tagCopy = tag;
            deferOnSelfPostTick( Delegate<void( GameObject& )>( [tagCopy]( GameObject& self )
            { self.removeTag( tagCopy ); } ) );
            return;
        }

        TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp != nullptr )
            pTagComp->removeTag( tag );
    }

    void GameObject::clearTags()
    {
        TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp != nullptr )
            pTagComp->clearTags();
    }

    bool GameObject::hasTag( TagID tag, bool bExactMatch ) const
    {
        const TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp == nullptr )
            return false;
        return pTagComp->hasTag( tag, bExactMatch );
    }

    bool GameObject::matchesTagQuery( const TagQuery& query ) const
    {
        const TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp == nullptr )
            return query.matches( s_emptyTags );
        return pTagComp->matchesQuery( query );
    }

    const TagContainer& GameObject::getTags() const
    {
        const TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp == nullptr )
            return s_emptyTags;
        return pTagComp->getTags();
    }

    TagContainer& GameObject::getOrCreateTags()
    {
        TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp == nullptr )
            pTagComp = addComponent<TagComponent>();
        if ( pTagComp != nullptr )
            return pTagComp->getTags();

        // 틱 중이라 `addComponent` 가 미뤄져 nullptr 을 준 경우다. 시그니처는 참조를 요구하는데
        // 반환할 컨테이너가 없다. 예전에는 **공용 상수** `s_emptyTags` 를 `const_cast` 해서
        // 줬다. 그쪽에 한 번이라도 쓰면 태그가 없는 **모든** 오브젝트의 `getTags() const` ·
        // `hasTag` · `matchesTagQuery` 가 그 값을 보게 된다. 버리는 통을 따로 둬서 쓰기가
        // 아무에게도 새지 않게 한다.
        SW_LOG_ERROR( "getOrCreateTags(): cannot attach a TagComponent while structural mutation is frozen — writes are discarded." );
        thread_local TagContainer t_discardedTags;
        t_discardedTags.clear();
        return t_discardedTags;
    }

    size_t GameObject::getComponentCount() const
    {
        size_t count = 0;
        for ( Component* pComp : _listComponent )
        {
            if ( pComp != nullptr && pComp->isPendingKill() == false )
                ++count;
        }
        return count;
    }

    Component* GameObject::findComponentByTypeName( hashed_string typeName ) const
    {
        if ( typeName.empty() )
            return nullptr;
        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingKill() )
                continue;
            if ( pComp->getComponentName() == typeName )
                return pComp;
            const TypeInfo* pTypeInfo = pComp->getTypeInfo();
            if ( pTypeInfo != nullptr && pTypeInfo->_name == typeName )
                return pComp;
        }
        return nullptr;
    }

    bool GameObject::isComponentMutationFrozen() const
    {
        return _pOwnerManager != nullptr && _pOwnerManager->isStructuralMutationFrozen();
    }

    GameObject::ComponentStorage GameObject::allocateComponentStorage( const TypeInfo* pTypeInfo, size_t typeSize )
    {
        ComponentStorage storage{};
        if ( _pOwnerManager == nullptr )
            return storage;

        // 타입마다 풀 하나다. 파괴가 그 풀로 돌아간다(`Component::_pPool`). 풀을 만들 수 없는 타입(리플렉션 없음)은 힙에서 잡는다.
        if ( pTypeInfo != nullptr )
        {
            storage._pPool = _pOwnerManager->getOrCreateComponentPool( pTypeInfo, typeSize );
            if ( storage._pPool != nullptr )
                storage._pMemory = storage._pPool->allocate();
        }
        if ( storage._pMemory == nullptr )
        {
            storage._pPool   = nullptr;
            storage._pMemory = Memory::allocate( typeSize );
        }
        return storage;
    }

    void GameObject::attachCreatedComponent( Component* pComp, const TypeInfo* pTypeInfo, PoolAllocator* pPool )
    {
        pComp->setOwner( this );
        pComp->_pPool = pPool;

        const hashed_string typeKey = pTypeInfo != nullptr ? pTypeInfo->_name : hashed_string{};
        pComp->setComponentName( typeKey );
        pComp->applyTypeDefaults( pTypeInfo );

        // 상태를 되돌리는 로드 중이면 원래 ID 를 되살린다. 등록보다 먼저여야 서브틱 핸들도 원래 ID 로 잡힌다.
        // 같은 프로세스에서 발급된 ID 라 카운터는 보통 이미 그 뒤지만, 아니면 뒤로 밀어 앞으로의 발급과 겹치지 않게 한다.
        uint64 restoredComponentId = 0;
        if ( GameObjectInternal::takeRestoredComponentId( this, typeKey, restoredComponentId ) )
        {
            pComp->_componentId = restoredComponentId;
            Component::_s_nextComponentId.fetch_max( restoredComponentId + 1, std::memory_order_relaxed );
        }

        _listComponent.push_back( pComp );
        if ( pComp->isSceneComponent() && _pPrimaryScene.load( std::memory_order_relaxed ) == nullptr )
            _pPrimaryScene.store( pComp, std::memory_order_relaxed );
        // 어느 등록부에 들어갈지는 컴포넌트가 안다. GameObject 는 타입을 몰라도 된다.
        pComp->onRegister( *_pOwnerManager );
        // 틱에 참여하는 컴포넌트만 이 오브젝트의 틱 항목을 다시 짓게 한다. 메시 · 태그 같은 것은 틱과 무관하다.
        if ( pComp->hasTickWork() )
            markTickOrderDirty();
    }

    GameObject::ComponentIdRestoreScope::ComponentIdRestoreScope( const GameObject* pTarget, const ObjectIdentity* pIdentity )
        : _pPreviousTarget{ t_componentIdRestore._pTarget }
        , _pPreviousIdentity{ t_componentIdRestore._pIdentity }
        , _previousCursor{ t_componentIdRestore._cursor }
    {
        t_componentIdRestore._pTarget   = ( pIdentity != nullptr ) ? pTarget : nullptr;
        t_componentIdRestore._pIdentity = pIdentity;
        t_componentIdRestore._cursor    = 0;
    }

    GameObject::ComponentIdRestoreScope::~ComponentIdRestoreScope()
    {
        t_componentIdRestore._pTarget   = _pPreviousTarget;
        t_componentIdRestore._pIdentity = _pPreviousIdentity;
        t_componentIdRestore._cursor    = _previousCursor;
    }

    void GameObject::deferOnSelfPostTick( Delegate<void( GameObject& )> func )
    {
        if ( _pOwnerManager == nullptr || func.isBound() == false )
            return;
        const uint64       objectId = _objectId;
        GameObjectManager* pMgr     = _pOwnerManager;
        pMgr->deferPostTick( [pMgr, objectId, deferred = std::move( func )]()
        {
            GameObject* pObj = pMgr->findGameObjectById( objectId );
            if ( pObj != nullptr )
                deferred( *pObj );
        } );
    }

    void GameObject::clearComponents()
    {
        // 인라인 네 칸을 그대로 복사한다. 힙을 만지지 않는다(다섯 개 이상일 때만 만진다).
        ComponentList listOwned( _listComponent.begin(), _listComponent.end() );
        _listComponent.clear();
        _pPrimaryScene.store( nullptr, std::memory_order_relaxed );
        // 파괴 뒤에는 물을 수 없으니 지금 본다. 틱에 참여하던 것이 하나라도 있었을 때만 틱 항목을 다시 짓는다.
        bool bTickWork = false;
        for ( Component* pComp : listOwned )
            bTickWork = bTickWork || ( pComp != nullptr && pComp->hasTickWork() );
        for ( Component* pComp : listOwned )
        {
            if ( pComp == nullptr )
                continue;
            if ( _pOwnerManager != nullptr )
                pComp->onUnregister( *_pOwnerManager );
            pComp->onDestroy();
            pComp->setOwner( nullptr );
            if ( _pOwnerManager != nullptr )
                _pOwnerManager->destroyComponentInstance( pComp );
            else
                sw_delete( pComp );
        }
        if ( bTickWork )
            markTickOrderDirty();
    }

    Component* GameObject::findComponentById( uint64 componentId, bool bIncludePendingKill ) const
    {
        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->getComponentId() != componentId )
                continue;
            if ( bIncludePendingKill == false && pComp->isPendingKill() )
                return nullptr;
            return pComp;
        }
        return nullptr;
    }

    bool GameObject::removeComponent( Component* pComp )
    {
        if ( pComp == nullptr || pComp->getOwner() != this )
            return false;
        // 파괴 뒤에는 물을 수 없으니 지금 본다.
        const bool bTickWork = pComp->hasTickWork();

        if ( isComponentMutationFrozen() )
        {
            _pOwnerManager->destroyComponent( pComp );
            return true;
        }

        if ( _pOwnerManager != nullptr )
            pComp->onUnregister( *_pOwnerManager );
        pComp->onDestroy();
        pComp->setOwner( nullptr );

        bool bRemoved = false;
        for ( size_t compIndex = 0; compIndex < _listComponent.size(); ++compIndex )
        {
            if ( _listComponent[compIndex] == pComp )
            {
                _listComponent[compIndex] = _listComponent.back();
                _listComponent.pop_back();
                bRemoved = true;
                break;
            }
        }
        if ( _pPrimaryScene.load( std::memory_order_relaxed ) == pComp )
            _pPrimaryScene.store( nullptr, std::memory_order_relaxed );
        if ( bRemoved == false )
            SW_LOG_ERROR( "Failed to remove component '%#' from actor list.", pComp->getComponentName().c_str() );
        else
        {
            if ( _pOwnerManager != nullptr )
                _pOwnerManager->destroyComponentInstance( pComp );
            else
                sw_delete( pComp );
        }
        if ( bRemoved && bTickWork )
            markTickOrderDirty();
        return bRemoved;
    }

    void GameObject::markTickOrderDirty()
    {
        // 죽어 가는 오브젝트는 파괴 때 등록부에서 빠진다. 표시할 것이 없다.
        if ( _pOwnerManager != nullptr && isPendingKill() == false )
            _pOwnerManager->getTickRegistry().markObjectDirty( this );
    }

    void GameObject::prepareSerialize() const
    {
        for ( Component* pComp : _listComponent )
        {
            SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
            if ( pSceneComp != nullptr )
                pSceneComp->syncAttachSerializeFields();
        }
    }

    void GameObject::applyLoadedHierarchy()
    {
        for ( Component* pComp : _listComponent )
        {
            SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
            if ( pSceneComp != nullptr )
                pSceneComp->applyAttachSerializeFields();
        }
    }

    atomic<uint64> GameObject::_s_nextObjectId{ 1 };
} // namespace sw
