#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    namespace
    {
        const TagContainer s_emptyTags{};
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObject" );

    GameObject::GameObject()
        : _objectId{ _s_nextObjectId.fetch_add( 1, std::memory_order_relaxed ) }
        , _name{ "GameObject" }
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

        // 컴포넌트 파괴 전 부모-자식 계층 링크 분리 (자식 오브젝트들은 루트로 승격되어 생존)
        detachFromParent();

        // 자식 목록을 복사하지 않는다 — 떼면 그 자리가 swap-remove 되므로 뒤에서 앞으로 돈다(뒤에서 온 원소는 이미 본 것).
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
     * @brief 게임플레이 시작(Play Mode) 시 소유한 모든 활성 컴포넌트의 onBeginPlay를 호출합니다.
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
     * @brief 게임플레이 종료 시 소유한 모든 컴포넌트의 onEndPlay를 호출합니다.
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
        // 인스펙터는 `_bActive` 를 세터가 아니라 멤버에 직접 쓴다. setActive 가 해주던 계층 전파를
        // 여기서 해줘야 자식들의 isActiveInHierarchy 가 따라온다 — 프리미티브 집합 무효화도
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
     * @brief 프레임 종료 시점에 게임 오브젝트를 안전하게 파괴하도록 지연 등록합니다.
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
     * @brief 게임 오브젝트 이름을 변경하고 GameObjectManager의 이름 검색 인덱스를 갱신합니다.
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
     * @brief 로컬 활성 상태를 변경하고 부모 계층 상태를 반영하여 자식들에게 활성화 이벤트를 전파합니다.
     */
    void GameObject::setActive( bool bActive )
    {
        _bActive.store( bActive, std::memory_order_relaxed );
        refreshActiveInHierarchy();

        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingKill() )
                continue;
            pComp->setActive( bActive );
        }
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

        // 자식 목록을 만들지 않는다 — 재귀는 계층을 바꾸지 않으므로 그대로 돈다. 병합되는 오브젝트마다 불리는 자리다.
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

        // 캐시가 비었거나 죽었다 — 살아 있는 첫 씬 컴포넌트를 목록에서 찾아 적는다(리플렉션 캐스트 없이 플래그 비트).
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
        if ( _pOwnerManager != nullptr && _pOwnerManager->isStructuralMutationFrozen() )
        {
            const uint64       objectId = _objectId;
            GameObjectManager* pMgr     = _pOwnerManager;
            const TagID        tagCopy  = tag;
            pMgr->deferPostTick( [pMgr, objectId, tagCopy]()
            {
                GameObject* pObj = pMgr->findGameObjectById( objectId );
                if ( pObj != nullptr )
                    pObj->addTag( tagCopy );
            } );
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
        if ( _pOwnerManager != nullptr && _pOwnerManager->isStructuralMutationFrozen() )
        {
            const uint64       objectId = _objectId;
            GameObjectManager* pMgr     = _pOwnerManager;
            const TagID        tagCopy  = tag;
            pMgr->deferPostTick( [pMgr, objectId, tagCopy]()
            {
                GameObject* pObj = pMgr->findGameObjectById( objectId );
                if ( pObj != nullptr )
                    pObj->removeTag( tagCopy );
            } );
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

        // 틱 중이라 `addComponent` 가 미뤄져 nullptr 을 준 경우다. 서명은 참조를 요구하는데
        // 돌려줄 컨테이너가 없다 — 예전에는 **공용 상수** `s_emptyTags` 를 `const_cast` 해서
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

    void GameObject::clearComponents()
    {
        // 옮긴다 — 복사하면 파괴마다 할당 하나다.
        vector<Component*> listOwned = std::move( _listComponent );
        _listComponent.clear();
        _pPrimaryScene.store( nullptr, std::memory_order_relaxed );
        // 파괴 뒤에는 물을 수 없으니 지금 본다 — 틱에 참여하던 것이 하나라도 있었을 때만 웨이브를 다시 만든다.
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

        if ( _pOwnerManager != nullptr && _pOwnerManager->isStructuralMutationFrozen() )
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
        // 죽어 가는 오브젝트는 파괴 때 등록부에서 빠진다 — 표시할 것이 없다.
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
