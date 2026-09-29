#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    SceneComponent::SceneComponent()
        : _attachOwner{}
        , _attachComponent{}
        , _pTransformPage{ nullptr }
        , _pManager{ nullptr }
        , _pParent{ nullptr }
        , _listChild{}
        , _transformSlot{ SceneTransformStorage::kInvalidSlot }
        , _bIsTransformDirty{ SW_TRUE }
        , _bHasDirtyDescendant{ SW_FALSE }
        , _bQueuedDirtyRoot{ SW_FALSE }
        , _dirtyRootIndex{ kNotInList }
    {
        // 값(로컬 TRS · 월드 행렬 · LWC)은 저장소의 칸에 있다. 칸은 항등 로컬 · 항등 월드로 채워져 나온다.
        _transformSlot     = SceneTransformStorage::get().allocateSlot( this, _pTransformPage );
        _bCanEverTick      = SW_FALSE;
        _bIsSceneComponent = SW_TRUE;
    }

    // **SceneComponent 는 이동하지 않는다.** 이 클래스는 부모 포인터 · 자식 목록 · 계층의 더티
    // 루트 목록에 **자기 주소로** 얽혀 있는 계층의 노드다. 옮기려면 자식들의 `_pParent`, 부모의
    // `_listChild` 항목, 더티 루트 목록이 들고 있는 포인터를 모두 새 주소로
    // 고쳐야 하는데, 예전 이동 연산은 그중 하나도 하지 않았다(이동 대입은 심지어 방금 옮겨
    // 온 `_listChild` 를 그 자리에서 비웠다). 컴포넌트는 풀 안의 제자리에서 만들고 없애므로 실제로
    // 옮겨지는 일이 없었고(삭제로 바꿔도 저장소 전체에서 두 정의 말고는 아무것도 깨지지
    // 않았다), 그래서 고치는 대신 **막는다.** 파생 7종의 `= default` 선언도 같이 걷었다.
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

    void SceneComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
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

    void SceneComponent::writeTickTransform( uint8 pendingBit, const float3& value )
    {
        // **자기 오브젝트를 틱하는 스레드면 칸에 바로 쓴다.** 한 오브젝트의 항목은 한 워커가 돌므로 이 칸의 대기 자리를 쓰는 스레드는
        // 이 스레드 하나다. 예전에는 세터마다 64 바이트짜리 쓰기 건을 스레드 큐에 올렸고(같은 컴포넌트면 앞 건과 합치는 비교 포함), 틱 뒤
        // 적용이 그 건의 대상 컴포넌트를 하나씩 찾아가 값을 옮겼다. 이제 틱 뒤 적용은 칸 번호 목록을 따라 배열만 읽는다.
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && GameObjectManager::getTickingObject() == pOwner )
        {
            const uint32 pageIndex   = getPageIndex();
            uint8&       pendingMask = _pTransformPage->_arrPendingMask[pageIndex];
            // 칸이 처음 대기에 들 때만 번호를 올린다. 스레드가 대기 목록 칸을 못 받았으면(도우미 칸이 다 찬 드문 경우) 아래 큐로 간다.
            if ( pendingMask != 0 || _pManager->getTransformHierarchy().queuePendingSlot( _transformSlot ) )
            {
                pendingMask |= pendingBit;
                if ( pendingBit == SceneTransformPage::kPendingPosition )
                    _pTransformPage->_arrPendingPosition[pageIndex] = value;
                else if ( pendingBit == SceneTransformPage::kPendingRotation )
                    _pTransformPage->_arrPendingRotation[pageIndex] = value;
                else
                    _pTransformPage->_arrPendingScale[pageIndex] = value;
                return;
            }
        }

        // 다른 오브젝트의 컴포넌트에 쓰는 것은 두 스레드가 한 칸에 쓸 수 있으므로 예전처럼 쓰기 큐로 간다.
        SceneTransformWrite write{};
        if ( pendingBit == SceneTransformPage::kPendingPosition )
        {
            write._localPosition = value;
            write._bSetPosition  = SW_TRUE;
        }
        else if ( pendingBit == SceneTransformPage::kPendingRotation )
        {
            write._localRotation = value;
            write._bSetRotation  = SW_TRUE;
        }
        else
        {
            write._localScale = value;
            write._bSetScale  = SW_TRUE;
        }
        queueTickWrite( write );
    }

    void SceneComponent::setLocalPosition( const float3& pos )
    {
        if ( isInParallelTick() )
        {
            writeTickTransform( SceneTransformPage::kPendingPosition, pos );
            return;
        }
        // **제곱 거리에는 제곱한 허용치를 쓴다.** `Epsilon` 을 그대로 대면 실제 거리 1e-3 까지가
        // "안 움직였다" 가 되는데, 비교 기준이 매번 **현재 값**이라 그 아래 움직임은 쌓이지도
        // 않는다. 한 프레임에 1e-3 보다 조금씩 가는 물체는 영원히 제자리에 있었다.
        float3& localPosition = _pTransformPage->_arrLocalPosition[getPageIndex()];
        if ( float3::getDistanceSquared( localPosition, pos ) <= MathUtil::EpsilonSquared )
            return;
        localPosition = pos;
        markTransformDirty();
    }

    float3 SceneComponent::getLocalPosition() const
    {
        return _pTransformPage->_arrLocalPosition[getPageIndex()];
    }

    void SceneComponent::setLocalRotation( const float3& rot )
    {
        if ( isInParallelTick() )
        {
            writeTickTransform( SceneTransformPage::kPendingRotation, rot );
            return;
        }
        float3& localRotation = _pTransformPage->_arrLocalRotation[getPageIndex()];
        if ( float3::getDistanceSquared( localRotation, rot ) <= MathUtil::EpsilonSquared )
            return;
        localRotation = rot;
        markTransformDirty();
    }

    float3 SceneComponent::getLocalRotation() const
    {
        return _pTransformPage->_arrLocalRotation[getPageIndex()];
    }

    void SceneComponent::setLocalScale( const float3& scale )
    {
        if ( isInParallelTick() )
        {
            writeTickTransform( SceneTransformPage::kPendingScale, scale );
            return;
        }
        float3& localScale = _pTransformPage->_arrLocalScale[getPageIndex()];
        if ( float3::getDistanceSquared( localScale, scale ) <= MathUtil::EpsilonSquared )
            return;
        localScale = scale;
        markTransformDirty();
    }

    float3 SceneComponent::getLocalScale() const
    {
        return _pTransformPage->_arrLocalScale[getPageIndex()];
    }

    float3 SceneComponent::getWorldPosition() const
    {
        // float32 월드 위치는 LWC 를 내린 값이다. 따로 들지 않는다(예전에는 캐시가 하나 더 있었다).
        ensureWorldCache();
        const double3& worldLwc = _pTransformPage->_arrWorldPositionLwc[getPageIndex()];
        return float3( static_cast<float32>( worldLwc._x ), static_cast<float32>( worldLwc._y ), static_cast<float32>( worldLwc._z ) );
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
        if ( _bIsTransformDirty == SW_FALSE || isInParallelTick() )
            return;

        // **합성은 `updateWorldTransformFromParent` 한 곳에서만 한다.** 예전에는 여기에 같은 합성 · 해제가 한 벌 더 있었고, 그쪽만
        // 갱신 훅(`onWorldTransformUpdated`)을 부르지 않았다. 여기서 깨끗해진 노드는 플러시가 건너뛰므로(더티가 아니다) 훅이 영영
        // 안 불렸다 — 인스펙터에서 메시 위치를 끌면 세터 직후 월드 위치를 읽어, 화면의 메시가 제자리에 멈춰 있었다.
        //
        // 더티인 조상 사슬을 위에서부터 합성한다. 깨끗한 노드의 조상은 모두 깨끗하다(더티는 자손 전부에 세우고, 해제는 위에서 아래로).
        // 재귀하지 않는다 — 깊은 계층에서도 스택이 자라지 않는다.
        vector<SceneComponent*, InlineAllocator<SceneComponent*, 16>> listChain;
        for ( SceneComponent* pNode = const_cast<SceneComponent*>( this ); pNode != nullptr && pNode->_bIsTransformDirty == SW_TRUE;
              pNode                 = pNode->_pParent )
            listChain.push_back( pNode );
        for ( auto it = listChain.rbegin(); it != listChain.rend(); ++it )
            ( *it )->updateWorldTransformFromParent();
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
        _bIsTransformDirty = SW_FALSE;
        notifyWorldTransformUpdated();
    }

    void SceneComponent::notifyWorldTransformUpdated()
    {
        // 렌더 프리미티브는 칸에 적힌 번호로 등록부에 바로 찍는다(메시 컴포넌트는 훅을 끈다). 그 밖의 파생은 예전처럼 훅을 받는다.
        const uint32 pageIndex      = getPageIndex();
        const uint32 primitiveIndex = _pTransformPage->_arrPrimitiveIndex[pageIndex];
        if ( primitiveIndex != SceneTransformStorage::kNoPrimitive && _pManager != nullptr )
            _pManager->getPrimitiveRegistry().markTransformDirty( primitiveIndex );
        if ( ( _pTransformPage->_arrFlag[pageIndex] & SceneTransformPage::kNotifyOwner ) != 0 )
            onWorldTransformUpdated();
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
        pManager->deferTransformUpdate( [pManager, handle, pMethod]()
        {
            SceneComponent* pSelf = static_cast<SceneComponent*>( pManager->resolveComponent( handle ) );
            if ( pSelf != nullptr )
                ( pSelf->*pMethod )();
        } );
    }

    bool SceneComponent::attachToComponent( SceneComponent* pParent )
    {
        if ( isInParallelTick() )
        {
            GameObjectManager*        pManager     = _pManager;
            const sw::ComponentHandle selfHandle   = getHandle();
            const sw::ComponentHandle parentHandle = ( pParent != nullptr ) ? pParent->getHandle() : sw::ComponentHandle{};
            pManager->deferTransformUpdate( [pManager, selfHandle, parentHandle]()
            {
                SceneComponent* pSelf           = static_cast<SceneComponent*>( pManager->resolveComponent( selfHandle ) );
                SceneComponent* pResolvedParent = parentHandle.isValid() ? static_cast<SceneComponent*>( pManager->resolveComponent( parentHandle ) ) : nullptr;
                if ( pSelf != nullptr )
                    pSelf->attachToComponent( pResolvedParent );
            } );
            return true;
        }

        if ( pParent == this )
            return false;

        if ( _pParent == pParent )
            return true;

        if ( pParent == nullptr )
            return false;

        SceneComponent* pAncestor = pParent;
        while ( pAncestor != nullptr )
        {
            if ( pAncestor == this )
                return false;
            pAncestor = pAncestor->_pParent;
        }

        detachFromComponent();

        _pParent = pParent;
        pParent->_listChild.push_back( this );
        // 칸의 계층 비트는 틱 뒤 적용이 컴포넌트를 거치지 않고 "잎 루트인가" 를 묻는 데 쓴다.
        setTransformFlag( SceneTransformPage::kHasParent, true );
        pParent->setTransformFlag( SceneTransformPage::kHasChildren, true );

        if ( _pManager != nullptr )
            _pManager->unregisterRootSceneComponent( this );

        markTransformDirty();
        refreshOwnerActiveInHierarchy();
        return true;
    }

    void SceneComponent::detachFromComponent()
    {
        if ( isInParallelTick() )
        {
            deferSelfCall( &SceneComponent::detachFromComponent );
            return;
        }

        detachFromParentImmediate();
    }

    void SceneComponent::detachFromParentImmediate()
    {
        if ( _pParent == nullptr )
            return;

        vector<SceneComponent*>& listSibling = _pParent->_listChild;
        for ( size_t childIndex = 0; childIndex < listSibling.size(); ++childIndex )
        {
            if ( listSibling[childIndex] == this )
            {
                listSibling[childIndex] = listSibling.back();
                listSibling.pop_back();
                break;
            }
        }
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
        // 부모가 바뀌는 곳은 모두 여기(붙이기 · 떼기)를 지난다 — 그래서 계층 활성은 값이 그대로면 멈출 수 있다. 예전에는
        // `GameObject::attachToParent` 만 맞췄고, 컴포넌트를 직접 붙이거나 상태를 되돌리는 로드(`applyLoadedHierarchy`)나 부모
        // 컴포넌트의 소멸자가 자식을 뗀 경우는 옛 값이 남았다. primary 가 아닌 컴포넌트면 오브젝트의 부모가 그대로라 O(1) 로 끝난다.
        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && pOwner->isPendingDestroy() == false )
            pOwner->refreshActiveInHierarchy();
    }

    bool SceneComponent::applyTransformWrite( const SceneTransformWrite& write )
    {
        const uint32 pageIndex     = getPageIndex();
        float3&      localPosition = _pTransformPage->_arrLocalPosition[pageIndex];
        float3&      localRotation = _pTransformPage->_arrLocalRotation[pageIndex];
        float3&      localScale    = _pTransformPage->_arrLocalScale[pageIndex];
        bool         bChanged      = false;
        if ( write._bSetPosition != SW_FALSE && float3::getDistanceSquared( localPosition, write._localPosition ) > MathUtil::EpsilonSquared )
        {
            localPosition = write._localPosition;
            bChanged      = true;
        }
        if ( write._bSetRotation != SW_FALSE && float3::getDistanceSquared( localRotation, write._localRotation ) > MathUtil::EpsilonSquared )
        {
            localRotation = write._localRotation;
            bChanged      = true;
        }
        if ( write._bSetScale != SW_FALSE && float3::getDistanceSquared( localScale, write._localScale ) > MathUtil::EpsilonSquared )
        {
            localScale = write._localScale;
            bChanged   = true;
        }
        if ( bChanged == false )
            return false;

        // 잎 루트(부모도 자식도 없다)는 **여기서 곧장** 월드를 만든다. 방금 쓴 캐시 라인이 뜨거운 채로, 같은 워커가 만든다. 자손이 없으니
        // 순서를 기다릴 것이 없고 플러시 패스가 이 루트를 만질 일도 없다(더티 목록에 오르지 않는다). 큐브 8000 개가 모두 움직이는
        // 프레임에서 사후 플러시 114 us 가 통째로 사라진 자리다. 계층이 있는 것은 예전처럼 루트를 올리고 플러시가 내려간다.
        if ( _pParent == nullptr && _listChild.empty() )
        {
            _bIsTransformDirty = SW_TRUE;
            updateWorldTransformFromParent();
            return true;
        }

        markHierarchyDirtyParallel();
        return true;
    }

    void SceneComponent::markHierarchyDirtyParallel()
    {
        // 계층이 있는 것은 루트를 올리고 플러시가 내려간다. 표시는 `markTransformDirty` 와 같은 두 함수다(세대 올리기와 지연 경로만
        // 없다). 모두 같은 값을 쓰는 바이트 저장이라 워커 여럿이 겹쳐 써도 무해하다. 루트는 워커 스크래치에 올린다 — 같은 루트를
        // 두 워커가 올리려 해도 원자 플래그가 한 번만 통과시킨다.
        SceneComponent* pRoot = markSelfAndAncestorsDirty();
        if ( pRoot != nullptr && _pManager != nullptr )
            _pManager->getTransformHierarchy().queueDirtyRootParallel( pRoot );
        markDescendantsDirty();
    }

    SceneComponent* SceneComponent::markSelfAndAncestorsDirty()
    {
        _bIsTransformDirty = SW_TRUE;

        // 부모 사슬을 올라가며 "자손 더티" 를 세우고, 루트에 닿으면 그것을 돌려준다. 이미 서 있는 조상을 만나면 그 루트는 이미 올라
        // 있다(불변식) — 거기서 멈추고 nullptr. 내가 루트면 나다.
        SceneComponent* pRoot = ( _pParent == nullptr ) ? this : nullptr;
        for ( SceneComponent* pParentComp = _pParent; pParentComp != nullptr; pParentComp = pParentComp->_pParent )
        {
            if ( pParentComp->_bHasDirtyDescendant == SW_TRUE )
                break;
            pParentComp->_bHasDirtyDescendant = SW_TRUE;
            if ( pParentComp->_pParent == nullptr )
                pRoot = pParentComp;
        }
        return pRoot;
    }

    void SceneComponent::markDescendantsDirty()
    {
        // 자손 전부를 더티로, 자식이 있는 노드는 "자손 더티" 도 세운다. 둘째가 필요한 이유: 플러시 전에 누가 자손 하나의 월드 위치를
        // 읽으면(`ensureWorldCache`) 그 사슬은 깨끗해진다. 플러시는 깨끗한 노드 아래로 "자손 더티" 가 있을 때만 내려가므로, 그게 없으면
        // 사슬 옆의 형제가 갱신되지 않는다. 예전 워커 쪽 복사본(`markDirtySubtree`)은 더티만 세워 바로 그 형제를 놓쳤다.
        //
        // 이미 더티인 자식은 그 아래도 이미 더티다 — 건너뛴다(부모와 자식을 같이 움직이면 제곱으로 느는 것을 막는다).
        // 재귀하지 않는다. 예전 직렬 쪽은 자손마다 `markTransformDirty` 를 다시 불러 지연 검사 · 세대 올리기(공유 원자) · 부모 걷기를
        // 자손 수만큼 했다.
        vector<SceneComponent*, InlineAllocator<SceneComponent*, 32>> listStack;
        listStack.push_back( this );
        while ( listStack.empty() == false )
        {
            SceneComponent* pNode = listStack.back();
            listStack.pop_back();
            if ( pNode->_listChild.empty() )
                continue;
            pNode->_bHasDirtyDescendant = SW_TRUE;
            for ( SceneComponent* pChild : pNode->_listChild )
            {
                if ( pChild == nullptr || pChild->_bIsTransformDirty == SW_TRUE )
                    continue;
                pChild->_bIsTransformDirty = SW_TRUE;
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

    void SceneComponent::syncAttachSerializeFields() const
    {
        _attachOwner     = {};
        _attachComponent = {};
        if ( _pParent == nullptr )
            return;

        GameObject* pParentOwner = _pParent->getOwner();
        if ( pParentOwner == nullptr )
            return;

        const string parentKey = ComponentStableKey::makeKey( _pParent );
        if ( parentKey.empty() )
            return;
        _attachOwner     = pParentOwner->getName();
        _attachComponent = hashed_string( parentKey.c_str() );
    }

    void SceneComponent::applyAttachSerializeFields()
    {
        if ( _attachComponent.empty() )
            return;

        GameObject* pSelfOwner = getOwner();
        if ( pSelfOwner == nullptr )
            return;

        GameObject* pParentOwner = pSelfOwner;
        if ( _attachOwner.empty() == false && _attachOwner != pSelfOwner->getName() )
        {
            if ( _pManager == nullptr )
                return;
            pParentOwner = _pManager->findGameObjectByName( _attachOwner );
            if ( pParentOwner == nullptr )
                return;
        }

        Component*      pParentComp = ComponentStableKey::findComponent( pParentOwner, _attachComponent.c_str() );
        SceneComponent* pParent     = pParentComp != nullptr ? castTo<SceneComponent>( pParentComp ) : nullptr;
        if ( pParent == nullptr || pParent == this )
            return;
        attachToComponent( pParent );
    }

} // namespace sw
