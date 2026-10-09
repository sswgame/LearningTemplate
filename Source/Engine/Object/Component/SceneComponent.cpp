#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    SW_LOG_CALLER( "SceneComponent" );

    SceneComponent::SceneComponent()
        : _attachOwner{}
        , _attachOwnerId{ 0 }
        , _attachComponent{}
        , _pTransformPage{ nullptr }
        , _transformSlot{ SceneTransformStorage::kInvalidSlot }
        , _dirtyRootIndex{ kNotInList }
        , _pManager{ nullptr }
        , _pParent{ nullptr }
        , _listChild{}
        , _unresolvedAttach{}
        , _bIsTransformDirty{ SW_TRUE }
        , _bHasDirtyDescendant{ SW_FALSE }
        , _bQueuedDirtyRoot{ SW_FALSE }
    {
        // 값(로컬 TRS · 월드 행렬 · LWC)은 저장소의 칸에 있다. 칸은 항등 로컬 · 항등 월드로 채워져 나온다.
        _transformSlot     = SceneTransformStorage::get().allocateSlot( this, _pTransformPage );
        _bIsSceneComponent = SW_TRUE;
    }

    // **SceneComponent 는 이동하지 않는다.** 이 클래스는 부모 포인터 · 자식 목록 · 계층의 더티
    // 루트 목록에 **자기 주소로** 얽혀 있는 계층의 노드다. 옮기려면 자식들의 `_pParent`, 부모의
    // `_listChild` 항목, 더티 루트 목록이 들고 있는 포인터를 모두 새 주소로
    // 고쳐야 한다. 컴포넌트는 풀 안의 제자리에서 만들고 없애므로 실제로 옮겨지는 일이 없고,
    // 그래서 **막는다.** 파생 타입도 이동 연산을 선언하지 않는다.
    // 트랜스폼 칸도 같은 이유로 옮길 수 없다(칸의 소유자 포인터가 이 주소다).

    SceneComponent::~SceneComponent()
    {
        // 소멸할 때 자식 컴포넌트들을 떼어 낸다(힙 복사 없이 뒤에서부터).
        //
        // **미루는 쪽(`detachFromComponent`)을 쓰면 안 된다.** 그쪽은 틱 중이면 일을 큐에 넣고
        // 그냥 돌아오므로 `_listChild` 가 줄지 않는다. 아래 루프가 끝나지 않고 미룬 일만 무한히
        // 쌓인다. 게다가 그 일이 나중에 실행될 때 핸들로 되찾을 자기 자신은 이미 없다. 파괴는
        // 지금 틱 창 밖에서만 일어나므로 실제로 닿지는 않지만, 닿았을 때의 모습이 "멈춘다" 인
        // 것을 남겨 둘 이유가 없다.
        while ( _listChild.empty() == false )
        {
            SceneComponent* pChild = _listChild.back();
            if ( pChild != nullptr )
                pChild->detachFromParentImmediate();
            else
                _listChild.pop_back();
        }
        detachFromParentImmediate();
        SceneTransformStorage::get().freeSlot( _transformSlot );
        _pTransformPage = nullptr;
        _transformSlot  = SceneTransformStorage::kInvalidSlot;
    }

    void SceneComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        markTransformDirty();
    }

    void SceneComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _pManager = &manager;
        if ( getParent() == nullptr )
            manager.registerRootSceneComponent( this );
    }

    void SceneComponent::onUnregister( GameObjectManager& manager )
    {
        manager.unregisterRootSceneComponent( this );
        _pManager = nullptr;
        Component::onUnregister( manager );
    }

    void SceneComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        // 이름은 한 번만 만든다(알림마다 셋을 intern 하던 것).
        static const hashed_string s_positionName( "_localPosition" );
        static const hashed_string s_rotationName( "_localRotation" );
        static const hashed_string s_scaleName( "_localScale" );
        if ( propertyName == s_positionName || propertyName == s_rotationName || propertyName == s_scaleName )
            markTransformDirty();
    }

    bool SceneComponent::isInParallelTick() const
    {
        return _pManager != nullptr && _pManager->isStructuralMutationFrozen();
    }

    void SceneComponent::queueTickWrite( SceneTransformWrite& write )
    {
        // 병렬 틱 중이다. 자기 스레드 슬롯의 쓰기 큐에 올리고, 틱이 끝나면 배치로 적용된다(잠금도 할당도 없다).
        write._handle  = getHandle();
        write._pTarget = this;
        _pManager->queueTransformWrite( write );
    }

    void SceneComponent::writeTickTransform( uint8 bit, const float3& value )
    {
        // **자기 오브젝트를 틱하는 스레드면 칸에 바로 쓴다.** 한 오브젝트의 항목은 한 워커가 돌므로 이 칸의 대기 자리를 쓰는 스레드는
        // 이 스레드 하나다. 틱 뒤 적용은 칸 번호 목록을 따라 배열만 읽는다.
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && GameObjectManager::getTickingObject() == pOwner )
        {
            const uint32 pageIndex   = getPageIndex();
            uint8&       pendingMask = _pTransformPage->_arrPendingMask[pageIndex];
            // 칸이 처음 대기에 들 때만 번호를 올린다. 스레드가 대기 목록 칸을 못 받았으면(도우미 칸이 다 찬 드문 경우) 아래 큐로 간다.
            if ( pendingMask != 0 || _pManager->getTransformHierarchy().queuePendingSlot( _transformSlot ) )
            {
                pendingMask |= bit;
                _pTransformPage->getPendingValueRef( pageIndex, bit ) = value;
                return;
            }
        }

        // 다른 오브젝트의 컴포넌트에 쓰는 것은 두 스레드가 한 칸에 쓸 수 있으므로 쓰기 큐로 간다.
        SceneTransformWrite write{};
        write.setValue( bit, value );
        queueTickWrite( write );
    }

    void SceneComponent::setLocalValue( uint8 bit, const float3& value )
    {
        if ( isInParallelTick() )
        {
            writeTickTransform( bit, value );
            return;
        }
        // 거의 같은 값이면 쓰지 않는다(허용치 규칙은 `SceneTransformPage::writeLocalValue` 한 곳).
        if ( _pTransformPage->writeLocalValue( getPageIndex(), bit, value ) )
            markTransformDirty();
    }

    void SceneComponent::setLocalPosition( const float3& pos )
    {
        setLocalValue( SceneTransformPage::kLocalPosition, pos );
    }

    float3 SceneComponent::getLocalPosition() const
    {
        return _pTransformPage->_arrLocalPosition[getPageIndex()];
    }

    void SceneComponent::setLocalRotation( const float3& rot )
    {
        setLocalValue( SceneTransformPage::kLocalRotation, rot );
    }

    float3 SceneComponent::getLocalRotation() const
    {
        return _pTransformPage->_arrLocalRotation[getPageIndex()];
    }

    void SceneComponent::setLocalScale( const float3& scale )
    {
        setLocalValue( SceneTransformPage::kLocalScale, scale );
    }

    float3 SceneComponent::getLocalScale() const
    {
        return _pTransformPage->_arrLocalScale[getPageIndex()];
    }

    float3 SceneComponent::getWorldPosition() const
    {
        // float32 월드 위치는 LWC 를 내린 값이다. 따로 들지 않는다.
        ensureWorldCache();
        const double3& worldLwc = _pTransformPage->_arrWorldPositionLwc[getPageIndex()];
        return float3( static_cast<float32>( worldLwc._x ), static_cast<float32>( worldLwc._y ), static_cast<float32>( worldLwc._z ) );
    }

    void SceneComponent::setWorldPosition( const float3& worldPosition )
    {
        if ( _pParent == nullptr )
        {
            setLocalPosition( worldPosition );
            return;
        }
        // 로컬 → 월드는 `로컬 TRS × 부모 월드`(행 벡터)다. 점 하나를 거꾸로 옮긴다.
        setLocalPosition( float3::transform( worldPosition, _pParent->getWorldMatrix().invert() ) );
    }

    void SceneComponent::teleportTo( const float3& worldPosition )
    {
        setWorldPosition( worldPosition );
        // 이 아래에 붙은 모두가 함께 옮겨졌다 — 재귀 없이 스택으로 돈다(깊은 계층에서도 스택이 자라지 않는다).
        vector<SceneComponent*, InlineAllocator<SceneComponent*, 16>> listPending;
        listPending.push_back( this );
        while ( listPending.empty() == false )
        {
            SceneComponent* pNode = listPending.back();
            listPending.pop_back();
            pNode->onTeleported();
            for ( SceneComponent* pChild : pNode->_listChild )
            {
                if ( pChild != nullptr )
                    listPending.push_back( pChild );
            }
        }
    }

    void SceneComponent::setWorldTransform( const float4x4& worldMatrix )
    {
        const float4x4 localMatrix = ( _pParent != nullptr ) ? worldMatrix * _pParent->getWorldMatrix().invert() : worldMatrix;
        float3         scale{};
        quaternion     rotation{};
        float3         translation{};
        localMatrix.decompose( scale, rotation, translation );
        setLocalPosition( translation );
        setLocalRotation( rotation.getEulerAngles() );
        setLocalScale( scale );
    }

    double3 SceneComponent::getWorldPositionLwc() const
    {
        ensureWorldCache();
        return _pTransformPage->_arrWorldPositionLwc[getPageIndex()];
    }

    float4x4 SceneComponent::getWorldMatrix() const
    {
        ensureWorldCache();
        return _pTransformPage->_arrWorldMatrix[getPageIndex()];
    }

    void SceneComponent::ensureWorldCache() const
    {
        // 병렬 틱 중에는 캐시만 읽는다(워커가 계층을 고쳐 쓰면 안 된다).
        if ( _bIsTransformDirty.load( std::memory_order_relaxed ) == SW_FALSE || isInParallelTick() )
            return;

        // **합성은 `updateWorldTransformFromParent` 한 곳에서만 한다.** 주의: 여기서 따로 합성 · 해제하면 갱신 훅(`onWorldTransformUpdated`)이
        // 빠진다. 여기서 깨끗해진 노드는 플러시가 건너뛰므로(더티가 아니다) 훅이 영영 안 불린다 — 인스펙터에서 메시 위치를 끌면 세터 직후
        // 월드 위치를 읽어, 화면의 메시가 제자리에 멈춰 있게 된다.
        //
        // 더티인 조상 사슬을 위에서부터 합성한다. 깨끗한 노드의 조상은 모두 깨끗하다(더티는 자손 전부에 세우고, 해제는 위에서 아래로).
        // 재귀하지 않는다 — 깊은 계층에서도 스택이 자라지 않는다.
        vector<SceneComponent*, InlineAllocator<SceneComponent*, 16>> listChain;
        for ( SceneComponent* pNode = const_cast<SceneComponent*>( this ); pNode != nullptr && pNode->_bIsTransformDirty.load( std::memory_order_relaxed ) == SW_TRUE;
              pNode                 = pNode->_pParent )
        {
            listChain.push_back( pNode );
        }
        for ( auto it = listChain.rbegin(); it != listChain.rend(); ++it )
        {
            ( *it )->updateWorldTransformFromParent();
        }
    }

    float4x4 SceneComponent::getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const
    {
        ensureWorldCache();
        const uint32  pageIndex     = getPageIndex();
        const double3 relativePos64 = _pTransformPage->_arrWorldPositionLwc[pageIndex] - cameraWorldPos;
        const float3  relativePos32( static_cast<float32>( relativePos64._x ),
                                     static_cast<float32>( relativePos64._y ),
                                     static_cast<float32>( relativePos64._z ) );
        float4x4      cameraRel = _pTransformPage->_arrWorldMatrix[pageIndex];
        cameraRel.setTranslation( relativePos32 );
        return cameraRel;
    }

    void SceneComponent::updateWorldTransformFromParent()
    {
        if ( _pParent != nullptr )
        {
            const SceneTransformPage& parentPage      = *_pParent->_pTransformPage;
            const uint32              parentPageIndex = _pParent->getPageIndex();
            SceneTransformStorage::composeWorld( *_pTransformPage, getPageIndex(), &parentPage._arrWorldMatrix[parentPageIndex],
                                                 &parentPage._arrWorldPositionLwc[parentPageIndex] );
        }
        else
            SceneTransformStorage::composeWorld( *_pTransformPage, getPageIndex(), nullptr, nullptr );
        _bIsTransformDirty.store( SW_FALSE, std::memory_order_relaxed );
        notifyWorldTransformUpdated();
    }

    void SceneComponent::notifyWorldTransformUpdated()
    {
        // 칸의 합성(틱 뒤 적용)과 같은 알림 한 곳을 쓴다 — 프리미티브는 칸의 번호로, 그 밖의 파생은 알림 비트로 훅을 받는다.
        SceneTransformHierarchy::notifyWorldUpdated( *_pTransformPage, getPageIndex(), _pManager != nullptr ? &_pManager->getPrimitiveRegistry() : nullptr );
    }

    void SceneComponent::setWorldTransformNotify( bool bNotify )
    {
        setTransformFlag( SceneTransformPage::kNotifyOwner, bNotify );
    }

    void SceneComponent::setTransformPrimitiveIndex( uint32 primitiveIndex )
    {
        _pTransformPage->_arrPrimitiveIndex[getPageIndex()] = primitiveIndex;
    }

    void SceneComponent::setTransformFlag( uint8 flag, bool bOn )
    {
        uint8& slotFlag = _pTransformPage->_arrFlag[getPageIndex()];
        slotFlag        = bOn ? static_cast<uint8>( slotFlag | flag ) : static_cast<uint8>( slotFlag & ~flag );
    }

    void SceneComponent::deferSelfCall( void ( SceneComponent::*pMethod )() )
    {
        GameObjectManager*        pManager = _pManager;
        const sw::ComponentHandle handle   = getHandle();
        pManager->deferStructuralChange( [pManager, handle, pMethod]()
        {
            SceneComponent* pSelf = static_cast<SceneComponent*>( pManager->resolveComponent( handle ) );
            if ( pSelf != nullptr )
                ( pSelf->*pMethod )();
        } );
    }

    bool SceneComponent::getWorldBounds( float3& outCenter, float32& outRadius ) const
    {
        (void)outCenter;
        (void)outRadius;
        return false;
    }

    bool SceneComponent::getWorldBox( AABB& outBox ) const
    {
        (void)outBox;
        return false;
    }

    bool SceneComponent::canAttachTo( const SceneComponent* pParent ) const
    {
        if ( pParent == nullptr || pParent == this )
            return false;

        // 다른 매니저의 부모는 거절한다. 붙이면 **부모 쪽 루트**가 자기 매니저의 더티 루트 목록에 오르는데, 부모 매니저가 그 루트를 파괴해도
        // 이쪽 목록은 모르니 다음 플러시가 해제된 컴포넌트를 읽는다.
        if ( pParent->_pManager != _pManager )
        {
            SW_LOG_WARNING( "attachToComponent: the parent belongs to another object manager (scene) - a hierarchy cannot span two scenes" );
            return false;
        }
        const GameObject* pParentOwner = pParent->getOwner();
        if ( pParent->isPendingDestroy() || ( pParentOwner != nullptr && pParentOwner->isPendingDestroy() ) )
            return false;

        // 컴포넌트 사슬의 순환.
        for ( const SceneComponent* pAncestor = pParent; pAncestor != nullptr; pAncestor = pAncestor->_pParent )
        {
            if ( pAncestor == this )
                return false;
        }

        // 오브젝트 사슬의 순환. 오브젝트의 부모는 "primary 가 붙은 컴포넌트의 소유자" 라, 붙는 것이 소유자의 primary 일 때만 오브젝트의
        // 부모가 바뀐다. 소켓을 거치면 위의 컴포넌트 검사는 통과한다(A 의 primary → B 의 소켓, B 의 primary → A 의 소켓).
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && pParentOwner != nullptr && pParentOwner != pOwner && pOwner->getPrimarySceneComponent() == this &&
             pParentOwner->isDescendantOf( pOwner ) )
        {
            SW_LOG_WARNING( "attachToComponent: '%#' would become a descendant of its own child '%#' - rejected", pOwner->getName().c_str(),
                            pParentOwner->getName().c_str() );
            return false;
        }
        return true;
    }

    bool SceneComponent::attachToComponent( SceneComponent* pParent, AttachRule rule )
    {
        // 이미 그 부모면 할 일이 없다(성공). 나머지는 틱 중이든 아니든 **여기서** 거른다 — 미룬 붙이기가 그때 실패하면 부른 쪽은 모른다.
        if ( pParent != nullptr && _pParent == pParent )
            return true;
        if ( canAttachTo( pParent ) == false )
            return false;

        if ( isInParallelTick() )
        {
            GameObjectManager*        pManager     = _pManager;
            const sw::ComponentHandle selfHandle   = getHandle();
            const sw::ComponentHandle parentHandle = ( pParent != nullptr ) ? pParent->getHandle() : sw::ComponentHandle{};
            pManager->deferHierarchyChange( [pManager, selfHandle, parentHandle, rule]()
            {
                SceneComponent* pSelf           = static_cast<SceneComponent*>( pManager->resolveComponent( selfHandle ) );
                SceneComponent* pResolvedParent = parentHandle.isValid() ? static_cast<SceneComponent*>( pManager->resolveComponent( parentHandle ) ) : nullptr;
                // 미루기 전에 붙일 수 있는지 봤다(`canAttachTo`). 그사이 부모가 죽어 가면 붙지 않는다.
                if ( pSelf != nullptr )
                    (void)pSelf->attachToComponent( pResolvedParent, rule ); // 거부는 미루기 전에 걸렀다 — 남는 실패는 부모가 사라진 경우뿐이다
            } );
            return true;
        }

        // 월드를 지키려면 붙이기 **전의** 월드를 찍어 두고, 붙인 뒤 새 부모 기준으로 다시 적는다(`setWorldTransform` 이 분해한다).
        const bool     bKeepWorld  = ( rule == AttachRule::KeepWorld );
        const float4x4 worldBefore = bKeepWorld ? getWorldMatrix() : float4x4{};
        detachFromComponent();

        _unresolvedAttach.reset(); // 부모가 정해졌다 — 남겨 둔 참조는 이제 뜻이 없다
        _pParent = pParent;
        pParent->_listChild.push_back( this );
        // 칸의 계층 비트는 틱 뒤 적용이 컴포넌트를 거치지 않고 "잎 루트인가" 를 묻는 데 쓴다.
        setTransformFlag( SceneTransformPage::kHasParent, true );
        pParent->setTransformFlag( SceneTransformPage::kHasChildren, true );

        if ( _pManager != nullptr )
            _pManager->unregisterRootSceneComponent( this );

        markTransformDirty();
        refreshOwnerActiveInHierarchy();
        if ( bKeepWorld )
            setWorldTransform( worldBefore );
        return true;
    }

    void SceneComponent::detachFromComponent( AttachRule rule )
    {
        if ( isInParallelTick() )
        {
            GameObjectManager*        pManager   = _pManager;
            const sw::ComponentHandle selfHandle = getHandle();
            pManager->deferHierarchyChange( [pManager, selfHandle, rule]()
            {
                SceneComponent* pSelf = static_cast<SceneComponent*>( pManager->resolveComponent( selfHandle ) );
                if ( pSelf != nullptr )
                    pSelf->detachFromComponent( rule );
            } );
            return;
        }

        const bool     bKeepWorld  = ( rule == AttachRule::KeepWorld && _pParent != nullptr );
        const float4x4 worldBefore = bKeepWorld ? getWorldMatrix() : float4x4{};
        // 일부러 뗐다 — 찾지 못해 남겨 둔 참조도 버린다(그대로 두면 다음 로드가 그 부모에 다시 붙인다).
        _unresolvedAttach.reset();
        detachFromParentImmediate();
        if ( bKeepWorld )
            setWorldTransform( worldBefore );
    }

    void SceneComponent::detachFromParentImmediate()
    {
        if ( _pParent == nullptr )
            return;

        vector<SceneComponent*>& listSibling = _pParent->_listChild;
        (void)VectorUtil::removeSingleSwap( listSibling, this ); // 붙인 쪽이 넣어 두었다 — 없으면 뺄 것이 없다
        if ( listSibling.empty() )
            _pParent->setTransformFlag( SceneTransformPage::kHasChildren, false );
        setTransformFlag( SceneTransformPage::kHasParent, false );
        _pParent = nullptr;

        if ( _pManager != nullptr )
            _pManager->registerRootSceneComponent( this );

        markTransformDirty();
        refreshOwnerActiveInHierarchy();
    }

    void SceneComponent::refreshOwnerActiveInHierarchy()
    {
        // 부모가 바뀌는 곳은 모두 여기(붙이기 · 떼기)를 지난다 — 컴포넌트를 직접 붙이기 · 상태를 되돌리는 로드 · 부모 컴포넌트의 소멸자가
        // 자식을 떼기까지. 그래서 계층 활성은 값이 그대로면 멈출 수 있다. primary 가 아닌 컴포넌트면 오브젝트의 부모가 그대로라 O(1) 로 끝난다.
        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && pOwner->isPendingDestroy() == false )
            pOwner->refreshActiveInHierarchy();
    }

    void SceneComponent::markHierarchyDirtyParallel()
    {
        // 루트를 올리고 플러시가 내려간다. 표시는 `markTransformDirty` 와 같은 두 함수다(세대 올리기와 지연 경로만 없다). 모두 같은
        // 값을 쓰는 바이트 저장이라 워커 여럿이 겹쳐 써도 무해하다. 루트는 워커 스크래치에 올린다 — 같은 루트를 두 워커가 올리려 해도
        // 원자 플래그가 한 번만 통과시킨다.
        SceneComponent* pRoot = markSelfAndAncestorsDirty();
        if ( pRoot != nullptr && _pManager != nullptr )
            _pManager->getTransformHierarchy().queueDirtyRootParallel( pRoot );
        markDescendantsDirty();
    }

    SceneComponent* SceneComponent::markSelfAndAncestorsDirty()
    {
        _bIsTransformDirty.store( SW_TRUE, std::memory_order_relaxed );

        // 부모 사슬을 올라가며 "자손 더티" 를 세우고, 루트에 닿으면 그것을 돌려준다. 이미 서 있는 조상을 만나면 그 루트는 이미 올라
        // 있다(불변식) — 거기서 멈추고 nullptr. 내가 루트면 나다.
        SceneComponent* pRoot = ( _pParent == nullptr ) ? this : nullptr;
        for ( SceneComponent* pParentComp = _pParent; pParentComp != nullptr; pParentComp = pParentComp->_pParent )
        {
            if ( pParentComp->_bHasDirtyDescendant.load( std::memory_order_relaxed ) == SW_TRUE )
                break;
            pParentComp->_bHasDirtyDescendant.store( SW_TRUE, std::memory_order_relaxed );
            if ( pParentComp->_pParent == nullptr )
                pRoot = pParentComp;
        }
        return pRoot;
    }

    void SceneComponent::markDescendantsDirty()
    {
        // 자손 전부를 더티로, 자식이 있는 노드는 "자손 더티" 도 세운다. 둘째가 필요한 이유: 플러시 전에 누가 자손 하나의 월드 위치를
        // 읽으면(`ensureWorldCache`) 그 사슬은 깨끗해진다. 플러시는 깨끗한 노드 아래로 "자손 더티" 가 있을 때만 내려가므로, 그게 없으면
        // 사슬 옆의 형제가 갱신되지 않는다.
        //
        // 이미 더티인 자식은 그 아래도 이미 더티다 — 건너뛴다(부모와 자식을 같이 움직이면 제곱으로 느는 것을 막는다).
        // 재귀하지 않고, 자손마다 `markTransformDirty` 를 다시 부르지 않는다(지연 검사 · 세대 올리기(공유 원자) · 부모 걷기가 자손 수만큼 돈다).
        vector<SceneComponent*, InlineAllocator<SceneComponent*, 32>> listStack;
        listStack.push_back( this );
        while ( listStack.empty() == false )
        {
            SceneComponent* pNode = listStack.back();
            listStack.pop_back();
            if ( pNode->_listChild.empty() )
                continue;
            pNode->_bHasDirtyDescendant.store( SW_TRUE, std::memory_order_relaxed );
            for ( SceneComponent* pChild : pNode->_listChild )
            {
                if ( pChild == nullptr || pChild->_bIsTransformDirty.load( std::memory_order_relaxed ) == SW_TRUE )
                    continue;
                pChild->_bIsTransformDirty.store( SW_TRUE, std::memory_order_relaxed );
                listStack.push_back( pChild );
            }
        }
    }

    void SceneComponent::markTransformDirty()
    {
        if ( isInParallelTick() )
        {
            deferSelfCall( &SceneComponent::markTransformDirty );
            return;
        }

        if ( _pManager != nullptr )
            _pManager->notifyTransformDirtied();

        SceneComponent* pRoot = markSelfAndAncestorsDirty();
        if ( pRoot != nullptr && _pManager != nullptr )
            _pManager->getTransformHierarchy().queueDirtyRoot( pRoot );
        markDescendantsDirty();
    }

    void SceneComponent::syncAttachSerializeFields( const ObjectSaveOptions& options ) const
    {
        _attachOwner     = {};
        _attachOwnerId   = 0;
        _attachComponent = {};
        if ( _pParent == nullptr )
        {
            // 찾지 못한 참조는 지우지 않는다 — 여기서 비우면 부모가 아직 없는(쿠커는 엔티티를 하나씩 읽는다) · 프리팹이 없는 부모의
            // 자식이 저장할 때마다 연결을 잃는다. 다른 공간으로 옮겨 적을 때는 id 가 엉뚱한 오브젝트를 가리킬 수 있으니 이름만 남긴다.
            if ( _unresolvedAttach == nullptr )
                return;
            const SceneAttachReference& kept           = *_unresolvedAttach;
            const ObjectIdSpace         writtenIdSpace = ( options._pSavedIdMap != nullptr ) ? ObjectIdSpace::Saved : ObjectIdSpace::Live;
            const bool                  bExternal      = kept._ownerName.empty() == false || kept._ownerId != 0;
            if ( bExternal && options._bOmitExternalParent )
                return;
            _attachOwner     = kept._ownerName;
            _attachOwnerId   = ( kept._idSpace == writtenIdSpace ) ? kept._ownerId : 0;
            _attachComponent = kept._componentKey;
            return;
        }

        GameObject* pParentOwner = _pParent->getOwner();
        if ( pParentOwner == nullptr )
            return;

        const string parentKey = ComponentStableKey::makeKey( _pParent );
        if ( parentKey.empty() )
            return;

        // 같은 오브젝트 안의 부착은 소유자 칸을 비운다(= 자기). 자기 이름을 적으면 읽을 때 이름이 유일하게 바뀐(`Rig` → `Rig_2`)
        // 복제본 · 두 번째 프리팹 인스턴스가 이름으로 **원본**을 찾아 그 컴포넌트에 붙는다.
        if ( pParentOwner == getOwner() )
        {
            _attachComponent = hashed_string( parentKey.c_str() );
            return;
        }
        if ( options._bOmitExternalParent )
            return;

        // 부모 id 를 저장할 id 로 옮긴다 — 핸들 PROPERTY 와 같은 규칙 하나(`ObjectSaveOptions::getSavedObjectId`).
        _attachOwner     = pParentOwner->getName();
        _attachOwnerId   = options.getSavedObjectId( pParentOwner->getObjectId() );
        _attachComponent = hashed_string( parentKey.c_str() );
    }

    SceneAttachReference SceneComponent::getLoadedAttachReference() const
    {
        SceneAttachReference reference{};
        reference._ownerName    = _attachOwner;
        reference._ownerId      = _attachOwnerId;
        reference._componentKey = _attachComponent;
        return reference;
    }

    void SceneComponent::keepUnresolvedAttach( const SceneAttachReference& reference )
    {
        _unresolvedAttach = make_unique<SceneAttachReference>( reference );
    }

} // namespace sw
