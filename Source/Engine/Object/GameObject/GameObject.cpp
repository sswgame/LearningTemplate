#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Physics/Collision/AABB.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    namespace
    {
        const TagContainer s_emptyTags{};

        /** @brief 이 스레드에서 진행 중인 컴포넌트 ID 복원입니다. `GameObject::ComponentIDRestoreScope` 가 채우고 되돌립니다. */
        struct ComponentIDRestoreState
        {
            const GameObject*     _pTarget{ nullptr };
            const ObjectIdentity* _pIdentity{ nullptr };
            size_t                _cursor{ 0 }; ///< 목록에서 다음에 볼 자리
        };
        thread_local ComponentIDRestoreState t_componentIDRestore{};

        struct GameObjectInternal
        {
            /**
             * @brief @p pOwner 에 붙는 @p typeName 컴포넌트가 되살릴 원래 ID 를 가져갑니다. 복원 중이 아니거나 목록에 없으면 false.
             * @details 커서부터 앞으로 찾아 타입이 같은 첫 항목을 가져갑니다. 목록과 다른 컴포넌트가 끼어들어도(생성 중에 다른
             *          컴포넌트를 스스로 붙이는 타입 등) 그 하나만 새 ID 를 받고 나머지 순서는 어긋나지 않습니다.
             */
            static bool takeRestoredComponentID( const GameObject* pOwner, hashed_string typeName, uint64& outComponentID )
            {
                ComponentIDRestoreState& state = t_componentIDRestore;
                if ( state._pIdentity == nullptr || state._pTarget != pOwner )
                    return false;

                const vector<ObjectIdentity::ComponentEntry>& listEntry = state._pIdentity->_listComponent;
                for ( size_t entryIndex = state._cursor; entryIndex < listEntry.size(); ++entryIndex )
                {
                    if ( listEntry[entryIndex]._typeName != typeName )
                        continue;
                    state._cursor  = entryIndex + 1;
                    outComponentID = listEntry[entryIndex]._componentID;
                    return outComponentID != 0;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObject" );

    // 오브젝트는 수천 개가 풀(16 B 단위 칸)에서 나온다. 필드 크기 합을 정렬로 올린 값을 넘으면 필드 사이에 구멍이 생긴 것이다(칸이 한 단계 커진다).
    static_assert( sizeof( GameObject ) <=
                       ( sizeof( void* ) + sizeof( uint64 ) + sizeof( hashed_string ) + sizeof( GameObjectManager* ) + sizeof( atomic<bool> ) * 3 + sizeof( uint32 ) +
                         sizeof( vector<Component*, InlineAllocator<Component*, 4>> ) + sizeof( atomic<Component*> ) + sizeof( TickItemList ) +
                         sizeof( uint32 ) * TickRegistry::kGroupCount + sizeof( uint32 ) + sizeof( atomic<uint8> ) + alignof( GameObject ) - 1 ) /
                           alignof( GameObject ) * alignof( GameObject ),
                   "GameObject has padding between fields (or a field was added without adding its size here)" );

    GameObject::GameObject()
        : GameObject( hashed_string( GameObject::kDefaultName ) )
    {
    }

    GameObject::GameObject( hashed_string name )
        : _objectID{ 0 }
        , _name{ name }
        , _pOwnerManager{ nullptr }
        , _bActive{ true }
        , _bIsActiveInHierarchy{ true }
        , _bIsPendingDestroy{ false }
        , _bHiddenInEditor{ false }
        , _managerIndex{ invalid_index::kUint32 }
        , _listComponent{}
        , _pPrimaryScene{ nullptr }
        , _listTickItem{}
        , _arrTickIndex{ TickRegistry::kNotInList, TickRegistry::kNotInList, TickRegistry::kNotInList, TickRegistry::kNotInList }
        , _tickPrerequisiteCount{ 0 }
        , _bTickDirty{ SW_FALSE }
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
     * @brief 게임플레이가 시작될 때 소유한 컴포넌트마다 onBeginPlay 를 한 번 부릅니다(이미 시작한 것 · 삭제 대기는 건너뜁니다).
     */
    void GameObject::beginPlay()
    {
        // **자리로 돈다.** onBeginPlay 가 컴포넌트를 붙이면(`addTag` 가 TagComponent 를) 목록이 자라고, 인라인 네 칸을 넘으면 힙으로
        // 옮겨 가 범위 for 의 반복자가 풀린 칸을 읽는다. 크기는 매번 다시 읽는다 — 새로 붙은 것도 돈다. 활성 여부와 무관하다(짝은 비트가 맞춘다).
        for ( size_t compIndex = 0; compIndex < _listComponent.size(); ++compIndex )
        {
            Component* pComp = _listComponent[compIndex];
            if ( pComp != nullptr )
                pComp->dispatchBeginPlay();
        }
    }

    /**
     * @brief 게임플레이가 끝날 때 시작했던 컴포넌트마다 onEndPlay 를 한 번 부릅니다.
     */
    void GameObject::endPlay()
    {
        for ( size_t compIndex = 0; compIndex < _listComponent.size(); ++compIndex )
        {
            Component* pComp = _listComponent[compIndex];
            if ( pComp != nullptr )
                pComp->dispatchEndPlay();
        }
    }

    void GameObject::onPropertyChanged( hashed_string propertyName )
    {
        (void)propertyName;
        // 인스펙터는 `_bActive` 를 세터가 아니라 멤버에 직접 쓴다. setActive 가 해 주던 계층 전파를
        // 여기서 해 줘야 자식들의 isActiveInHierarchy 가 따라온다. 렌더 더티도 그 안에서 찍힌다.
        refreshActiveInHierarchy();
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

    void GameObject::markPendingDestroy()
    {
        (void)tryMarkPendingDestroy(); // 이미 표시돼 있어도 된다
    }

    bool GameObject::tryMarkPendingDestroy()
    {
        return _bIsPendingDestroy.exchange( true, std::memory_order_acq_rel ) == false;
    }

    /**
     * @brief 게임 오브젝트 이름을 바꾸고 GameObjectManager 의 이름 검색 인덱스를 갱신합니다.
     */
    void GameObject::setName( hashed_string name )
    {
        // 철자까지 같을 때만 할 일이 없다 — `==` 는 대소문자를 무시하므로(FName) 그것으로 보면 `hero` → `Hero` 가 아무 일도 하지 않았다.
        if ( name.isEqual( _name, NameCase::CaseSensitive ) )
            return;
        // 틱 중이면 틱 뒤로 미룬다 — 다른 워커가 이 이름을 읽고(`getName`), 이름 표는 매니저의 것이다. addTag · setActive 와 같다.
        if ( isComponentMutationFrozen() )
        {
            deferOnSelfStructural( Delegate<void( GameObject& )>( [name]( GameObject& self )
            { self.setName( name ); } ) );
            return;
        }

        const hashed_string oldName = _name;
        _name                       = name;
        if ( _pOwnerManager != nullptr )
            _pOwnerManager->notifyNameChanged( this, oldName, name );
    }

    /**
     * @brief 로컬 활성 상태를 바꾸고 계층 활성을 자손까지 다시 맞춥니다. 틱 중이면 틱 뒤로 미룹니다.
     */
    void GameObject::setActive( bool bActive )
    {
        // 틱 중(병렬 onTick — 시퀀서가 대상을 켜고 끈다)이면 다른 오브젝트의 계층 상태를 워커가 쓰지 않게 틱 뒤로 미룬다. addTag 와 같다.
        if ( isComponentMutationFrozen() )
        {
            deferOnSelfStructural( Delegate<void( GameObject& )>( [bActive]( GameObject& self )
            { self.setActive( bActive ); } ) );
            return;
        }

        // **컴포넌트의 자기 비트는 건드리지 않는다.** 이 비트를 소유 컴포넌트마다 복사하면 꺼 둔 컴포넌트가 오브젝트를 껐다 켤 때
        // 다시 켜진다(로드 · 되돌리기 · 시퀀서 트랙 · 에디터 계층 토글마다). `Component::isActive` 가 이미 소유 오브젝트의 계층 활성을
        // 함께 본다 — 유니티의 SetActive / enabled 와 같은 나눔이다. 같은 값이어도 계층은 다시 맞춘다(로드가 그것에 기댄다).
        static const hashed_string s_activeName( "_bActive" );
        _bActive.store( bActive, std::memory_order_relaxed );
        onPropertyChanged( s_activeName );
    }

    bool GameObject::getWorldBox( AABB& outBox ) const
    {
        bool bAny = false;
        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->isSceneComponent() == false || pComp->isSelfActive() == false )
                continue;
            AABB componentBox{};
            if ( static_cast<const SceneComponent*>( pComp )->getWorldBox( componentBox ) == false )
                continue;
            outBox = bAny ? outBox.unionWith( componentBox ) : componentBox;
            bAny   = true;
        }
        return bAny;
    }

    bool GameObject::attachToParent( GameObject* pParent, AttachRule rule )
    {
        if ( pParent == nullptr )
            return false;
        if ( getParent() == pParent )
            return true;

        // 미루기 **전에** 거른다. 미룬 일은 자기 매니저로 부모를 다시 찾으므로 다른 매니저의 부모는 그때 조용히 사라지고, 순환은 그때야
        // 실패하는데 이 호출은 이미 true 를 돌려준 뒤다. 붙는 자리의 나머지 규칙은 `SceneComponent::canAttachTo` 가 본다.
        GameObjectManager* pManager = getManager();
        if ( pParent->getManager() != pManager || pParent->isDescendantOf( this ) )
        {
            SW_LOG_WARNING( "attachToParent: '%#' cannot become the parent of '%#' (another scene, or its own descendant)", pParent->getName().c_str(),
                            getName().c_str() );
            return false;
        }
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            const uint64 childID  = _objectID;
            const uint64 parentID = pParent->getObjectID();
            pManager->deferHierarchyChange( [pManager, childID, parentID, rule]()
            {
                GameObject* pChildObj  = pManager->findGameObjectByID( childID );
                GameObject* pParentObj = pManager->findGameObjectByID( parentID );
                // 미루기 전에 붙일 수 있는지 봤다(`canAttachTo`). 그사이 부모가 죽어 가면 붙지 않는다.
                if ( pChildObj != nullptr && pParentObj != nullptr )
                    (void)pChildObj->attachToParent( pParentObj, rule ); // 거부는 미루기 전에 걸렀다 — 남는 실패는 부모가 사라진 경우뿐이다
            } );
            return true;
        }

        SceneComponent* pChildSc = getPrimarySceneComponent();
        if ( pChildSc == nullptr )
            return false;

        SceneComponent* pParentSc = pParent->getPrimarySceneComponent();
        if ( pParentSc == nullptr )
            return false;

        // 계층 활성은 `attachToComponent` 가 그 자리에서 맞춘다.
        return pChildSc->attachToComponent( pParentSc, rule );
    }

    void GameObject::detachFromParent( AttachRule rule )
    {
        GameObjectManager* pManager = getManager();
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            const uint64 childID = _objectID;
            pManager->deferHierarchyChange( [pManager, childID, rule]()
            {
                GameObject* pChildObj = pManager->findGameObjectByID( childID );
                if ( pChildObj != nullptr )
                    pChildObj->detachFromParent( rule );
            } );
            return;
        }

        // 계층 활성은 떼는 자리(`detachFromParentImmediate`)가 맞춘다.
        SceneComponent* pChildSc = getPrimarySceneComponent();
        if ( pChildSc != nullptr )
            pChildSc->detachFromComponent( rule );
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

        // **값이 그대로면 여기서 멈춘다.** 자식의 값은 부모의 값과 자기 비트로만 정해지므로 자손도 그대로다. 늘 서브트리 전체를 걸면
        // 병합 · 파괴 · 해체처럼 오브젝트마다 부르는 자리에서 깊은 계층이 O(N · 깊이)가 된다. 부모가 바뀌는 자리는 모두 그 자리에서
        // 이것을 부른다(`SceneComponent` 의 붙이기 · 떼기 · primary 교체).
        if ( bWasActive == bActiveInHierarchy )
            return;

        // 틱 목록은 켜진 오브젝트만 든다(`TickRegistry` — 언리얼 · 유니티처럼 꺼진 것은 목록에서 뺀다). 다음 틱 전에 칸을 넣거나 뺀다.
        // 틱 항목이 없는 오브젝트는 목록에 칸이 없으니 알릴 것도 없다(깊은 계층을 켜고 끌 때 오브젝트마다 잠금을 잡지 않는다). 항목이 막
        // 생겨 아직 지어지지 않았다면 그 오브젝트는 이미 표시되어 있고, 다시 지을 때 지금의 활성을 읽는다.
        if ( _listTickItem.empty() == false )
            markTickOrderDirty();

        // 이 값이 곧 렌더 스냅샷의 포함 여부다. 자기 컴포넌트에만 알린다 — 메시가 제 칸을 더티로 찍는다. 프리미티브 집합 세대를
        // 올리면 메시 하나 없는 오브젝트(빛 · 트리거)를 켜고 꺼도 GPUScene 이 전체를 다시 모은다.
        for ( Component* pComp : _listComponent )
        {
            if ( pComp != nullptr )
                pComp->onOwnerActiveInHierarchyChanged();
        }

        // 자식 오브젝트는 primary 가 아닌 씬 컴포넌트(소켓)에도 붙는다 — 자식의 부모는 "제 primary 가 붙은 컴포넌트의 소유자" 다. 그래서
        // primary 만이 아니라 **자기 씬 컴포넌트 전부**의 자식을 본다 — primary 의 자식만 보면 소켓에 붙은 무기가 캐릭터를 꺼도
        // 켜진 채다(틱하고 그려진다). 자식 목록을 만들지 않는다. 재귀는 계층을 바꾸지 않으므로 그대로 돈다.
        for ( Component* pOwnComp : _listComponent )
        {
            if ( pOwnComp == nullptr || pOwnComp->isPendingDestroy() || pOwnComp->isSceneComponent() == false )
                continue;
            for ( SceneComponent* pChildComp : static_cast<SceneComponent*>( pOwnComp )->getChildren() )
            {
                GameObject* pChildObj = ( pChildComp != nullptr ) ? pChildComp->getOwner() : nullptr;
                if ( pChildObj != nullptr && pChildObj != this )
                    pChildObj->refreshActiveInHierarchy();
            }
        }
    }

    GameObject* GameObject::findChildObjectAttachedTo( const SceneComponent* pChildComp ) const
    {
        if ( pChildComp == nullptr )
            return nullptr;
        GameObject* pChildObj = pChildComp->getOwner();
        // **같은 오브젝트 안의 부착은 자식 오브젝트가 아니다.** 한 GameObject 의 SceneComponent 를
        // 다른 SceneComponent 에 붙이는 것은 정상적인 구성인데(`ObjectStateBatch::finish` 가
        // 복원까지 한다), 그러면 그 자식 컴포넌트의 owner 는 자기 자신이라 거르지 않으면 **자신이
        // 자기 자식으로** 나와 `refreshActiveInHierarchy` 가 무한 재귀해 스택을 넘긴다.
        if ( pChildObj == nullptr || pChildObj == this )
            return nullptr;
        // 자식 오브젝트는 **primary 가 우리에게 붙은** 오브젝트다(`getParent` 의 정의). 다른 씬 컴포넌트만 붙인 오브젝트는 자식이 아니고,
        // 이렇게 가려야 한 오브젝트가 두 번 나오지 않는다.
        return ( pChildObj->getPrimarySceneComponent() == pChildComp ) ? pChildObj : nullptr;
    }

    void GameObject::getChildren( vector<GameObject*>& outListChild ) const
    {
        outListChild.clear();
        // primary 만이 아니라 **자기 씬 컴포넌트 전부**의 자식을 본다 — 소켓(primary 가 아닌 씬 컴포넌트)에 붙은 오브젝트도 자식이다
        // (`getParent` · `refreshActiveInHierarchy` 도 그렇게 본다). primary 의 자식만 돌려주면 `destroyObject( 캐릭터, true )` 가
        // 소켓에 붙은 무기를 남기고, 그 무기는 루트가 되어 계속 틱하고 그려진다.
        for ( Component* pOwnComp : _listComponent )
        {
            if ( pOwnComp == nullptr || pOwnComp->isSceneComponent() == false )
                continue;
            for ( SceneComponent* pChildComp : static_cast<SceneComponent*>( pOwnComp )->getChildren() )
            {
                GameObject* pChildObj = findChildObjectAttachedTo( pChildComp );
                if ( pChildObj != nullptr )
                    outListChild.push_back( pChildObj );
            }
        }
    }

    bool GameObject::hasChildren() const
    {
        for ( Component* pOwnComp : _listComponent )
        {
            if ( pOwnComp == nullptr || pOwnComp->isSceneComponent() == false )
                continue;
            for ( SceneComponent* pChildComp : static_cast<SceneComponent*>( pOwnComp )->getChildren() )
            {
                if ( findChildObjectAttachedTo( pChildComp ) != nullptr )
                    return true;
            }
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
        if ( pCached != nullptr && pCached->isPendingDestroy() == false )
            return static_cast<SceneComponent*>( pCached );

        // 캐시가 비었거나 죽었다. 살아 있는 첫 씬 컴포넌트를 목록에서 찾아 적는다(리플렉션 캐스트 없이 플래그 비트로).
        Component* pFound = nullptr;
        for ( Component* pComp : _listComponent )
        {
            if ( pComp != nullptr && pComp->isPendingDestroy() == false && pComp->isSceneComponent() )
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
            deferOnSelfStructural( Delegate<void( GameObject& )>( [tagCopy]( GameObject& self )
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
            deferOnSelfStructural( Delegate<void( GameObject& )>( [tagCopy]( GameObject& self )
            { self.removeTag( tagCopy ); } ) );
            return;
        }

        TagComponent* pTagComp = getComponent<TagComponent>();
        if ( pTagComp != nullptr )
            pTagComp->removeTag( tag );
    }

    void GameObject::clearTags()
    {
        // `removeTag` 와 같이 틱 중이면 틱 뒤로 미룬다 — 바로 지우면 병렬 틱의 다른 워커가 `hasTag` 로 같은 컨테이너를 읽는 동안 비운다
        // (유니티 DOTS `EntityCommandBuffer` · 언리얼 Mass 명령 버퍼처럼 병렬 구간의 변경은 동기점에서 한다).
        if ( isComponentMutationFrozen() )
        {
            deferOnSelfStructural( Delegate<void( GameObject& )>( []( GameObject& self )
            { self.clearTags(); } ) );
            return;
        }

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

    size_t GameObject::getComponentCount() const
    {
        size_t count = 0;
        for ( Component* pComp : _listComponent )
        {
            if ( pComp != nullptr && pComp->isPendingDestroy() == false )
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
            if ( pComp == nullptr || pComp->isPendingDestroy() )
                continue;
            // 타입이 먼저다. 이름표는 타입이 아니다 — 같은 이름표가 없을 때만 보조로 본다(아래).
            const TypeInfo* pTypeInfo = pComp->getTypeInfo();
            if ( pTypeInfo != nullptr && pTypeInfo->_name == typeName )
                return pComp;
        }
        for ( Component* pComp : _listComponent )
        {
            if ( pComp != nullptr && pComp->isPendingDestroy() == false && pComp->getComponentName() == typeName )
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

    void GameObject::attachCreatedComponent( Component* pComp, const TypeInfo* pTypeInfo, PoolAllocator* pPool, bool bOverridesTick )
    {
        // 주 틱의 기본은 "onTick 을 오버라이드했는가" 다(유니티 Update). 오버라이드한 타입은 생성자의 값(기본 켜짐, 끄면 꺼짐)을 그대로 둔다.
        if ( bOverridesTick == false )
            pComp->_bCanEverTick = SW_FALSE;
        pComp->setOwner( this );
        pComp->_pPool     = pPool;
        pComp->_pTypeInfo = pTypeInfo; // 타입은 여기서 한 번 정해진다. 이름표(아래)는 타입과 무관하다

        const hashed_string typeKey = pTypeInfo != nullptr ? pTypeInfo->_name : hashed_string{};
        pComp->setComponentName( typeKey );
        pComp->applyTypeDefaults( pTypeInfo );

        // 상태를 되돌리는 로드 중이면 원래 ID 를 되살린다. 등록보다 먼저여야 서브틱 핸들도 원래 ID 로 잡힌다.
        // 같은 프로세스에서 발급된 ID 라 카운터는 보통 이미 그 뒤지만, 아니면 뒤로 밀어 앞으로의 발급과 겹치지 않게 한다.
        uint64 restoredComponentID = 0;
        if ( GameObjectInternal::takeRestoredComponentID( this, typeKey, restoredComponentID ) )
        {
            pComp->_componentID = restoredComponentID;
            Component::_s_nextComponentID.fetch_max( restoredComponentID + 1, std::memory_order_relaxed );
        }

        _listComponent.push_back( pComp );
        if ( pComp->isSceneComponent() && _pPrimaryScene.load( std::memory_order_relaxed ) == nullptr )
            _pPrimaryScene.store( pComp, std::memory_order_relaxed );
        // 어느 등록부에 들어갈지는 컴포넌트가 안다. GameObject 는 타입을 몰라도 된다.
        pComp->onRegister( *_pOwnerManager );
        // 둘째 씬 컴포넌트부터는 primary 에 붙인다 — 오브젝트의 루트는 하나다(언리얼 RootComponent). 루트로 두면 **월드 원점**에 놓여
        // 오브젝트를 옮겨도 콜라이더 · 메시가 따라오지 않는다.
        // 상태 읽기는 이 뒤에 저장된 부착(소켓 · 다른 오브젝트)으로 다시 붙이고, 부착이 비어 있으면 이대로 둔다.
        Component* pPrimary = _pPrimaryScene.load( std::memory_order_relaxed );
        if ( pComp->isSceneComponent() && pPrimary != nullptr && pPrimary != pComp )
        {
            if ( static_cast<SceneComponent*>( pComp )->attachToComponent( static_cast<SceneComponent*>( pPrimary ) ) == false )
                SW_LOG_WARNING( "'%#': a second scene component could not be put under the primary - it stays a root", getName().c_str() );
        }
        // 월드가 플레이 중이면 다음 틱 단계에서 onBeginPlay 를 부른다. **여기서 부르지 않는다** — 부르는 쪽(`addComponent` 뒤의 세팅,
        // 이름으로 붙이는 역직렬화의 PROPERTY 채우기)이 아직 끝나지 않았다.
        if ( _pOwnerManager->hasBegunPlay() )
            _pOwnerManager->queueBeginPlay( pComp->getHandle() );
        // 틱에 참여하는 컴포넌트만 이 오브젝트의 틱 항목을 다시 짓게 한다. 메시 · 태그 같은 것은 틱과 무관하다.
        if ( pComp->hasTickWork() )
            markTickOrderDirty();
    }

    GameObject::ComponentIDRestoreScope::ComponentIDRestoreScope( const GameObject* pTarget, const ObjectIdentity* pIdentity )
        : _pPreviousTarget{ t_componentIDRestore._pTarget }
        , _pPreviousIdentity{ t_componentIDRestore._pIdentity }
        , _previousCursor{ t_componentIDRestore._cursor }
    {
        t_componentIDRestore._pTarget   = ( pIdentity != nullptr ) ? pTarget : nullptr;
        t_componentIDRestore._pIdentity = pIdentity;
        t_componentIDRestore._cursor    = 0;
    }

    GameObject::ComponentIDRestoreScope::~ComponentIDRestoreScope()
    {
        t_componentIDRestore._pTarget   = _pPreviousTarget;
        t_componentIDRestore._pIdentity = _pPreviousIdentity;
        t_componentIDRestore._cursor    = _previousCursor;
    }

    void GameObject::deferOnSelfStructural( Delegate<void( GameObject& )> func )
    {
        if ( _pOwnerManager == nullptr || func.isBound() == false )
            return;
        const uint64       objectID = _objectID;
        GameObjectManager* pManager = _pOwnerManager;
        pManager->deferStructuralChange( [pManager, objectID, deferred = std::move( func )]()
        {
            GameObject* pObj = pManager->findGameObjectByID( objectID );
            if ( pObj != nullptr )
                deferred( *pObj );
        } );
    }

    void GameObject::clearComponents()
    {
        // 컴포넌트 틱 중에는 형제(`removeComponent`)와 같이 미룬다 — 다른 워커가 지금 이 컴포넌트들을 틱하고 있을 수 있다(틱 안에서 부르는
        // 곳: 제자리 상태 읽기).
        if ( isComponentMutationFrozen() )
        {
            for ( Component* pComp : _listComponent )
            {
                if ( pComp != nullptr && pComp->isPendingDestroy() == false )
                    _pOwnerManager->destroyComponent( pComp );
            }
            return;
        }

        // 인라인 네 칸을 그대로 복사한다. 힙을 만지지 않는다(다섯 개 이상일 때만 만진다).
        ComponentList listOwned( _listComponent.begin(), _listComponent.end() );
        _listComponent.clear();
        _pPrimaryScene.store( nullptr, std::memory_order_relaxed );
        // 파괴 뒤에는 물을 수 없으니 지금 본다. 틱에 참여하던 것이 하나라도 있었을 때만 틱 항목을 다시 짓는다.
        bool bTickWork = false;
        for ( Component* pComp : listOwned )
        {
            bTickWork = bTickWork || ( pComp != nullptr && pComp->hasTickWork() );
        }
        for ( Component* pComp : listOwned )
        {
            if ( pComp != nullptr )
                destroyOwnedComponent( pComp );
        }
        if ( bTickWork )
            markTickOrderDirty();
        // primary 가 없어졌으니 부모도 없다(자식들은 부모 컴포넌트의 소멸자가 떼며 스스로 맞췄다).
        if ( isPendingDestroy() == false )
            refreshActiveInHierarchy();
    }

    void GameObject::destroyOwnedComponent( Component* pComp )
    {
        if ( _pOwnerManager != nullptr )
        {
            _pOwnerManager->destroyComponentInstance( pComp );
            return;
        }
        // 매니저 없이 만든 오브젝트다(등록부에 들어간 적이 없다). 해제 콜백만 부르고 힙으로 돌려준다.
        pComp->onDestroy();
        pComp->setOwner( nullptr );
        sw_delete( pComp );
    }

    Component* GameObject::findComponentByID( uint64 componentID, bool bIncludePendingDestroy ) const
    {
        for ( Component* pComp : _listComponent )
        {
            if ( pComp == nullptr || pComp->getComponentID() != componentID )
                continue;
            if ( bIncludePendingDestroy == false && pComp->isPendingDestroy() )
                return nullptr;
            return pComp;
        }
        return nullptr;
    }

    bool GameObject::moveComponent( Component* pComp, int32 direction )
    {
        if ( pComp == nullptr || pComp->getOwner() != this || direction == 0 || isComponentMutationFrozen() )
            return false;
        const auto it = std::find( _listComponent.begin(), _listComponent.end(), pComp );
        if ( it == _listComponent.end() )
            return false;
        const size_t index  = static_cast<size_t>( it - _listComponent.begin() );
        const size_t target = direction < 0 ? index - 1 : index + 1;
        if ( ( direction < 0 && index == 0 ) || target >= _listComponent.size() )
            return false;
        const Component* pPrimary = getPrimarySceneComponent();
        if ( _listComponent[index] == pPrimary || _listComponent[target] == pPrimary )
            return false;
        std::swap( _listComponent[index], _listComponent[target] );
        return true;
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

        // **순서를 지키며 뺀다**(swap-remove 금지). 목록 순서는 뜻이 있다 — primary 는 "살아 있는 첫 SceneComponent", `getComponent<T>` 는
        // 첫 일치, 안정 키(`ComponentStableKey`)는 같은 타입 안의 순번이다. 살아 있는 동안은 primary 캐시가 가려 주지만 저장했다 다시 읽으면
        // 바뀐 순서가 그대로 굳는다. 목록은 대개 네 칸 이하다.
        const auto it = std::find( _listComponent.begin(), _listComponent.end(), pComp );
        if ( it == _listComponent.end() )
        {
            SW_LOG_ERROR( "Failed to remove component '%#' from actor list.", pComp->getComponentName().c_str() );
            return false;
        }
        _listComponent.erase( it );
        // 씬 컴포넌트면 primary 였을 수 있다 — 캐시로 가리지 않는다. 지연 제거(틱 중)는 컴포넌트를 삭제 대기로 남기고, 그 사이
        // `getPrimarySceneComponent` 가 캐시를 다음 씬 컴포넌트로 옮겨 둘 수 있다(그러면 캐시로는 primary 였는지 알 수 없다).
        const bool bWasScene = pComp->isSceneComponent();
        if ( _pPrimaryScene.load( std::memory_order_relaxed ) == pComp )
            _pPrimaryScene.store( nullptr, std::memory_order_relaxed );

        destroyOwnedComponent( pComp );
        if ( bTickWork )
            markTickOrderDirty();
        // primary 가 바뀌면 부모(= primary 의 부모)도 바뀐다. 값이 그대로면 재계산은 O(1) 로 끝난다.
        if ( bWasScene && isPendingDestroy() == false )
            refreshActiveInHierarchy();
        return true;
    }

    void GameObject::markTickOrderDirty()
    {
        // 죽어 가는 오브젝트는 파괴 때 등록부에서 빠진다. 표시할 것이 없다.
        if ( _pOwnerManager != nullptr && isPendingDestroy() == false )
            _pOwnerManager->getTickRegistry().markObjectDirty( this );
    }

    void GameObject::prepareSerialize( const ObjectSaveOptions& options ) const
    {
        for ( Component* pComp : _listComponent )
        {
            SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
            if ( pSceneComp != nullptr )
                pSceneComp->syncAttachSerializeFields( options );
        }
    }
} // namespace sw
