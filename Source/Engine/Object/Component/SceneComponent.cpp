#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    namespace
    {
        struct SceneComponentInternal
        {
            /**
             * @brief 로컬 위치, 오일러 회전(Yaw/Pitch/Roll), 스케일 벡터로부터 TRS 로컬 변환 행렬을 생성합니다.
             */
            static float4x4 makeLocalTRS( const float3& position, const float3& rotation, const float3& scale )
            {
                // DirectX 행-벡터 규격: Scale * Rotation * Translation.
                // 행렬 셋을 곱하지 않고 결과를 바로 적는다. 값은 같고 곱 두 번이 사라진다
                // (`float4x4::createTrs` 주석). 움직이는 컴포넌트마다 매 프레임 지나는 자리다.
                return float4x4::createTrs( position, rotation, scale );
            }

            /**
             * @brief 부모의 월드 행렬과 결합하여 현재 컴포넌트의 월드 행렬 및 64비트 LWC 월드 좌표를 합성합니다.
             */
            static void composeWorldFromParent( const float3&   localPosition,
                                                const float3&   localRotation,
                                                const float3&   localScale,
                                                const float4x4* pParentWorldMatrix,
                                                const double3*  pParentWorldLWC,
                                                float4x4&       outWorldMatrix,
                                                double3&        outWorldLWC,
                                                float3&         outWorldPos )
            {
                const float4x4 localTRS = makeLocalTRS( localPosition, localRotation, localScale );

                if ( pParentWorldMatrix != nullptr && pParentWorldLWC != nullptr )
                {
                    outWorldMatrix      = localTRS * ( *pParentWorldMatrix );
                    const float3 offset = float3::transformNormal( localPosition, *pParentWorldMatrix );
                    outWorldLWC         = *pParentWorldLWC + double3( static_cast<float64>( offset._x ),
                                                                      static_cast<float64>( offset._y ),
                                                                      static_cast<float64>( offset._z ) );
                }
                else
                {
                    outWorldMatrix = localTRS;
                    outWorldLWC    = double3( static_cast<float64>( localPosition._x ),
                                              static_cast<float64>( localPosition._y ),
                                              static_cast<float64>( localPosition._z ) );
                }

                outWorldPos = float3( static_cast<float32>( outWorldLWC._x ),
                                      static_cast<float32>( outWorldLWC._y ),
                                      static_cast<float32>( outWorldLWC._z ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SceneComponent::SceneComponent()
        : _localPosition{ 0.0f, 0.0f, 0.0f }
        , _localRotation{ 0.0f, 0.0f, 0.0f }
        , _localScale{ 1.0f, 1.0f, 1.0f }
        , _attachOwner{}
        , _attachComponent{}
        , _cachedWorldPosition{ 0.0f, 0.0f, 0.0f }
        , _cachedWorldMatrix{ float4x4::Identity }
        , _cachedWorldPositionLWC{ 0.0, 0.0, 0.0 }
        , _pManager{ nullptr }
        , _pParent{ nullptr }
        , _listChild{}
        , _bIsTransformDirty{ SW_TRUE }
        , _bHasDirtyDescendant{ SW_FALSE }
        , _bQueuedDirtyRoot{ SW_FALSE }
        , _rootIndex{ kNotInList }
        , _dirtyRootIndex{ kNotInList }
    {
        _bCanEverTick      = SW_FALSE;
        _bIsSceneComponent = SW_TRUE;
    }

    // **SceneComponent 는 이동하지 않는다.** 이 클래스는 부모 포인터 · 자식 목록 · 매니저의 루트
    // 등록부에 **자기 주소로** 얽혀 있는 계층의 노드다. 옮기려면 자식들의 `_pParent`, 부모의
    // `_listChild` 항목, `registerRootSceneComponent` 가 들고 있는 포인터를 모두 새 주소로
    // 고쳐야 하는데, 예전 이동 연산은 그중 하나도 하지 않았다(이동 대입은 심지어 방금 옮겨
    // 온 `_listChild` 를 그 자리에서 비웠다). 컴포넌트는 풀 안의 제자리에서 만들고 없애므로 실제로
    // 옮겨지는 일이 없었고(삭제로 바꿔도 저장소 전체에서 두 정의 말고는 아무것도 깨지지
    // 않았다), 그래서 고치는 대신 **막는다.** 파생 7종의 `= default` 선언도 같이 걷었다.

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
        if ( propertyName == hashed_string( "_localPosition" ) ||
             propertyName == hashed_string( "_localRotation" ) ||
             propertyName == hashed_string( "_localScale" ) )
            markTransformDirty();
    }

    bool SceneComponent::isInParallelTick() const
    {
        return _pManager != nullptr && _pManager->isParallelTransformReadOnly();
    }

    void SceneComponent::queueTickWrite( SceneTransformWrite& write )
    {
        // 병렬 틱 중이다. 자기 스레드 슬롯의 쓰기 큐에 올리고, 틱이 끝나면 배치로 적용된다(잠금도 할당도 없다).
        write._handle  = getHandle();
        write._pTarget = this;
        _pManager->queueTransformWrite( write );
    }

    void SceneComponent::setLocalPosition( const float3& pos )
    {
        if ( isInParallelTick() )
        {
            SceneTransformWrite write{};
            write._localPosition = pos;
            write._bSetPosition  = SW_TRUE;
            queueTickWrite( write );
            return;
        }
        // **제곱 거리에는 제곱한 허용치를 쓴다.** `Epsilon` 을 그대로 대면 실제 거리 1e-3 까지가
        // "안 움직였다" 가 되는데, 비교 기준이 매번 **현재 값**이라 그 아래 움직임은 쌓이지도
        // 않는다. 한 프레임에 1e-3 보다 조금씩 가는 물체는 영원히 제자리에 있었다.
        if ( float3::getDistanceSquared( _localPosition, pos ) <= MathUtil::EpsilonSquared )
            return;
        _localPosition = pos;
        markTransformDirty();
    }

    float3 SceneComponent::getLocalPosition() const
    {
        return _localPosition;
    }

    void SceneComponent::setLocalRotation( const float3& rot )
    {
        if ( isInParallelTick() )
        {
            SceneTransformWrite write{};
            write._localRotation = rot;
            write._bSetRotation  = SW_TRUE;
            queueTickWrite( write );
            return;
        }
        if ( float3::getDistanceSquared( _localRotation, rot ) <= MathUtil::EpsilonSquared )
            return;
        _localRotation = rot;
        markTransformDirty();
    }

    float3 SceneComponent::getLocalRotation() const
    {
        return _localRotation;
    }

    void SceneComponent::setLocalScale( const float3& scale )
    {
        if ( isInParallelTick() )
        {
            SceneTransformWrite write{};
            write._localScale = scale;
            write._bSetScale  = SW_TRUE;
            queueTickWrite( write );
            return;
        }
        if ( float3::getDistanceSquared( _localScale, scale ) <= MathUtil::EpsilonSquared )
            return;
        _localScale = scale;
        markTransformDirty();
    }

    float3 SceneComponent::getLocalScale() const
    {
        return _localScale;
    }

    float3 SceneComponent::getWorldPosition() const
    {
        if ( isInParallelTick() == false )
            getWorldMatrix();
        return _cachedWorldPosition;
    }

    double3 SceneComponent::getWorldPositionLwc() const
    {
        if ( isInParallelTick() == false )
            getWorldMatrix();
        return _cachedWorldPositionLWC;
    }

    float4x4 SceneComponent::getWorldMatrix() const
    {
        if ( isInParallelTick() )
            return _cachedWorldMatrix;

        if ( _bIsTransformDirty == SW_TRUE )
        {
            SceneComponent* pParent = _pParent;
            if ( pParent != nullptr )
                pParent->getWorldMatrix();
            const float4x4* pParentWorld = pParent != nullptr ? &pParent->_cachedWorldMatrix : nullptr;
            const double3*  pParentLwc   = pParent != nullptr ? &pParent->_cachedWorldPositionLWC : nullptr;
            SceneComponent* pMutable     = const_cast<SceneComponent*>( this );
            SceneComponentInternal::composeWorldFromParent( _localPosition, _localRotation, _localScale, pParentWorld, pParentLwc,
                                                            pMutable->_cachedWorldMatrix, pMutable->_cachedWorldPositionLWC, pMutable->_cachedWorldPosition );
            pMutable->_bIsTransformDirty = SW_FALSE;
        }
        return _cachedWorldMatrix;
    }

    float4x4 SceneComponent::getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const
    {
        const float4x4 worldMat      = getWorldMatrix();
        const double3  relativePos64 = getWorldPositionLwc() - cameraWorldPos;
        const float3   relativePos32( static_cast<float32>( relativePos64._x ),
                                      static_cast<float32>( relativePos64._y ),
                                      static_cast<float32>( relativePos64._z ) );

        float4x4 cameraRel = worldMat;
        cameraRel.setTranslation( relativePos32 );
        return cameraRel;
    }

    void SceneComponent::updateWorldTransformFromParent()
    {
        const float4x4* pParentWorld = _pParent != nullptr ? &_pParent->_cachedWorldMatrix : nullptr;
        const double3*  pParentLwc   = _pParent != nullptr ? &_pParent->_cachedWorldPositionLWC : nullptr;
        SceneComponentInternal::composeWorldFromParent( _localPosition, _localRotation, _localScale, pParentWorld, pParentLwc,
                                                        _cachedWorldMatrix, _cachedWorldPositionLWC, _cachedWorldPosition );
        _bIsTransformDirty = SW_FALSE;
        onWorldTransformUpdated();
    }

    void SceneComponent::deferSelfCall( void ( SceneComponent::*pMethod )() )
    {
        GameObjectManager*        pMgr   = _pManager;
        const sw::ComponentHandle handle = getHandle();
        pMgr->deferTransformUpdate( [pMgr, handle, pMethod]()
        {
            SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
            if ( pSelf != nullptr )
                ( pSelf->*pMethod )();
        } );
    }

    bool SceneComponent::attachToComponent( SceneComponent* pParent )
    {
        if ( isInParallelTick() )
        {
            GameObjectManager*        pMgr         = _pManager;
            const sw::ComponentHandle selfHandle   = getHandle();
            const sw::ComponentHandle parentHandle = ( pParent != nullptr ) ? pParent->getHandle() : sw::ComponentHandle{};
            pMgr->deferTransformUpdate( [pMgr, selfHandle, parentHandle]()
            {
                SceneComponent* pSelf           = static_cast<SceneComponent*>( pMgr->resolveComponent( selfHandle ) );
                SceneComponent* pResolvedParent = parentHandle.isValid() ? static_cast<SceneComponent*>( pMgr->resolveComponent( parentHandle ) ) : nullptr;
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

        if ( _pManager != nullptr )
            _pManager->unregisterRootSceneComponent( this );

        markTransformDirty();
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
        _pParent = nullptr;

        if ( _pManager != nullptr )
            _pManager->registerRootSceneComponent( this );

        markTransformDirty();
    }

    bool SceneComponent::applyTransformWrite( const SceneTransformWrite& write )
    {
        bool bChanged = false;
        if ( write._bSetPosition != SW_FALSE && float3::getDistanceSquared( _localPosition, write._localPosition ) > MathUtil::EpsilonSquared )
        {
            _localPosition = write._localPosition;
            bChanged       = true;
        }
        if ( write._bSetRotation != SW_FALSE && float3::getDistanceSquared( _localRotation, write._localRotation ) > MathUtil::EpsilonSquared )
        {
            _localRotation = write._localRotation;
            bChanged       = true;
        }
        if ( write._bSetScale != SW_FALSE && float3::getDistanceSquared( _localScale, write._localScale ) > MathUtil::EpsilonSquared )
        {
            _localScale = write._localScale;
            bChanged    = true;
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

        // markTransformDirty 와 같은 표시다. 세대 올리기와 지연 경로만 뺐다. 모두 바이트 저장이라 워커에서 안전하다.
        // 루트는 워커 스크래치에 올린다. 같은 루트를 두 워커가 올리려 해도 원자 플래그가 한 번만 통과시킨다.
        _bIsTransformDirty    = SW_TRUE;
        SceneComponent* pRoot = ( _pParent == nullptr ) ? this : nullptr;
        for ( SceneComponent* pParentComp = _pParent; pParentComp != nullptr; pParentComp = pParentComp->_pParent )
        {
            if ( pParentComp->_bHasDirtyDescendant == SW_TRUE )
                break;
            pParentComp->_bHasDirtyDescendant = SW_TRUE;
            if ( pParentComp->_pParent == nullptr )
                pRoot = pParentComp;
        }
        if ( pRoot != nullptr && _pManager != nullptr )
            _pManager->getTransformHierarchy().queueDirtyRootParallel( pRoot );
        for ( SceneComponent* pChild : _listChild )
        {
            if ( pChild != nullptr && pChild->_bIsTransformDirty == SW_FALSE )
                pChild->markDirtySubtree();
        }
        return true;
    }

    void SceneComponent::markDirtySubtree()
    {
        _bIsTransformDirty = SW_TRUE;
        for ( SceneComponent* pChild : _listChild )
        {
            if ( pChild != nullptr && pChild->_bIsTransformDirty == SW_FALSE )
                pChild->markDirtySubtree();
        }
    }

    void SceneComponent::markTransformDirty()
    {
        if ( isInParallelTick() )
        {
            deferSelfCall( &SceneComponent::markTransformDirty );
            return;
        }

        _bIsTransformDirty = SW_TRUE;

        if ( _pManager != nullptr )
            _pManager->notifyTransformDirtied();

        // 부모 사슬을 올라가며 "자손 더티" 를 세우고, 루트에 닿으면 플러시 목록에 올린다. 이미 서 있는 조상을 만나면
        // 그 루트는 이미 올라 있다(불변식). 거기서 멈춘다. 내가 루트면 나를 올린다.
        SceneComponent* pParentComp = _pParent;
        SceneComponent* pRoot       = ( pParentComp == nullptr ) ? this : nullptr;
        while ( pParentComp != nullptr )
        {
            if ( pParentComp->_bHasDirtyDescendant == SW_TRUE )
                break;
            pParentComp->_bHasDirtyDescendant = SW_TRUE;
            if ( pParentComp->_pParent == nullptr )
                pRoot = pParentComp;
            pParentComp = pParentComp->_pParent;
        }
        if ( pRoot != nullptr && _pManager != nullptr )
            _pManager->getTransformHierarchy().queueDirtyRoot( pRoot );

        for ( SceneComponent* pChild : _listChild )
        {
            if ( pChild != nullptr && pChild->_bIsTransformDirty == SW_FALSE )
                pChild->markTransformDirty();
        }
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
