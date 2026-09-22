#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"

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
                // 행렬 셋을 곱하지 않고 결과를 바로 적는다 — 값은 같고 곱 두 번이 사라진다
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

            static string_view sceneComponentTypeBaseName( const Component* pComp )
            {
                if ( pComp == nullptr )
                    return "Component";
                if ( pComp->getComponentName().empty() == false )
                    return pComp->getComponentName().c_str();
                const TypeInfo* pTypeInfo = pComp->getTypeInfo();
                if ( pTypeInfo != nullptr )
                {
                    if ( pTypeInfo->_name.empty() == false )
                        return pTypeInfo->_name.c_str();
                    if ( pTypeInfo->_fullyQualifiedName.empty() == false )
                        return pTypeInfo->_fullyQualifiedName.c_str();
                }
                return "Component";
            }

            static string makeStableSceneComponentKey( const Component* pComp, int32 occurrenceIndex )
            {
                const string_view base = sceneComponentTypeBaseName( pComp );
                string            key;
                key.reserve( base.size() + 12 );
                key.append( base.data(), base.size() );
                key += '#';
                key += to_string( occurrenceIndex );
                return key;
            }

            static string findStableSceneComponentKey( const Component* pComp )
            {
                if ( pComp == nullptr || pComp->getOwner() == nullptr )
                    return {};

                const string_view targetBase = sceneComponentTypeBaseName( pComp );
                int32             occ        = 0;
                string            resultKey;

                pComp->getOwner()->forEachComponent( [&]( const Component* pOther )
                {
                    if ( resultKey.empty() == false || pOther == nullptr )
                        return;

                    const string_view otherBase = sceneComponentTypeBaseName( pOther );
                    if ( otherBase == targetBase )
                    {
                        if ( pOther == pComp )
                            resultKey = makeStableSceneComponentKey( pComp, occ );
                        else
                            ++occ;
                    }
                } );

                return resultKey;
            }

            static SceneComponent* findSceneComponentByAttachKey( GameObject* pOwner, string_view attachKey )
            {
                if ( pOwner == nullptr || attachKey.empty() )
                    return nullptr;

                const size_t hashPos = attachKey.rfind( '#' );
                if ( hashPos == string_view::npos )
                    return nullptr;

                const string_view reqBase = attachKey.substr( 0, hashPos );
                int32             reqOcc  = 0;
                for ( size_t index = hashPos + 1; index < attachKey.size(); ++index )
                {
                    if ( attachKey[index] < '0' || attachKey[index] > '9' )
                        return nullptr;
                    reqOcc = reqOcc * 10 + ( attachKey[index] - '0' );
                }

                int32           occ    = 0;
                SceneComponent* pFound = nullptr;

                pOwner->forEachComponent( [&]( Component* pComp )
                {
                    if ( pFound != nullptr || pComp == nullptr )
                        return;

                    const string_view base = sceneComponentTypeBaseName( pComp );
                    if ( base == reqBase )
                    {
                        if ( occ == reqOcc )
                            pFound = castTo<SceneComponent>( pComp );
                        else
                            ++occ;
                    }
                } );

                return pFound;
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

    // **SceneComponent 는 이동하지 않는다.** 이 클래스는 부모 포인터·자식 목록·매니저의 루트
    // 등록부에 **자기 주소로** 얽혀 있는 계층의 노드다. 옮기려면 자식들의 `_pParent`, 부모의
    // `_listChild` 항목, `registerRootSceneComponent` 가 들고 있는 포인터를 전부 새 주소로
    // 고쳐야 하는데, 예전 이동 연산은 그중 하나도 하지 않았다(이동 대입은 심지어 방금 옮겨
    // 온 `_listChild` 를 그 자리에서 비웠다). 컴포넌트는 풀에서 제자리 생성·소멸하므로 실제로
    // 옮겨지는 일이 없었고 — 삭제로 바꿔도 저장소 전체에서 두 정의 말고는 아무것도 깨지지
    // 않았다 — 그래서 고치는 대신 **막는다.** 파생 7종의 `= default` 선언도 같이 걷었다.

    SceneComponent::~SceneComponent()
    {
        // 소멸 시 자식 컴포넌트들을 부모로부터 분리 (힙 복사 없이 역순 분리)
        //
        // **미루는 쪽(`detachFromComponent`)을 쓰면 안 된다.** 그쪽은 틱 중이면 일을 큐에 넣고
        // 그냥 돌아오므로 `_listChild` 가 줄지 않는다 — 아래 루프가 끝나지 않고 미룬 일만 무한히
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

    void SceneComponent::setLocalPosition( const float3& pos )
    {
        if ( _pManager != nullptr && _pManager->isParallelTransformReadOnly() )
        {
            GameObjectManager*        pMgr   = _pManager;
            const sw::ComponentHandle handle = getHandle();
            pMgr->deferTransformUpdate( [pMgr, handle, pos]()
            {
                SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
                if ( pSelf != nullptr )
                    pSelf->setLocalPosition( pos );
            } );
            return;
        }
        // **제곱 거리에는 제곱한 허용치를 쓴다.** `Epsilon` 을 그대로 대면 실제 거리 1e-3 까지가
        // "안 움직였다" 가 되는데, 비교 기준이 매번 **현재 값**이라 그 아래 움직임은 쌓이지도
        // 않는다 — 한 프레임에 1e-3 보다 조금씩 가는 물체는 영원히 제자리에 있었다.
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
        if ( _pManager != nullptr && _pManager->isParallelTransformReadOnly() )
        {
            GameObjectManager*        pMgr   = _pManager;
            const sw::ComponentHandle handle = getHandle();
            pMgr->deferTransformUpdate( [pMgr, handle, rot]()
            {
                SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
                if ( pSelf != nullptr )
                    pSelf->setLocalRotation( rot );
            } );
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
        if ( _pManager != nullptr && _pManager->isParallelTransformReadOnly() )
        {
            GameObjectManager*        pMgr   = _pManager;
            const sw::ComponentHandle handle = getHandle();
            pMgr->deferTransformUpdate( [pMgr, handle, scale]()
            {
                SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
                if ( pSelf != nullptr )
                    pSelf->setLocalScale( scale );
            } );
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
        if ( _pManager == nullptr || _pManager->isParallelTransformReadOnly() == false )
            getWorldMatrix();
        return _cachedWorldPosition;
    }

    double3 SceneComponent::getWorldPositionLwc() const
    {
        if ( _pManager == nullptr || _pManager->isParallelTransformReadOnly() == false )
            getWorldMatrix();
        return _cachedWorldPositionLWC;
    }

    float4x4 SceneComponent::getWorldMatrix() const
    {
        if ( _pManager != nullptr && _pManager->isParallelTransformReadOnly() )
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

    bool SceneComponent::attachToComponent( SceneComponent* pParent )
    {
        if ( _pManager != nullptr && _pManager->isParallelTransformReadOnly() )
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
        if ( _pManager != nullptr && _pManager->isParallelTransformReadOnly() )
        {
            GameObjectManager*        pMgr   = _pManager;
            const sw::ComponentHandle handle = getHandle();
            pMgr->deferTransformUpdate( [pMgr, handle]()
            {
                SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
                if ( pSelf != nullptr )
                    pSelf->detachFromComponent();
            } );
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

        // markTransformDirty 와 같은 표시 — 세대 올리기와 지연 경로만 뺐다. 전부 바이트 저장이라 워커에서 안전하다.
        // 루트는 워커 스크래치에 올린다 — 같은 루트를 두 워커가 올리려 해도 원자 플래그가 한 번만 통과시킨다.
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
        if ( _pManager != nullptr && _pManager->isParallelTransformReadOnly() )
        {
            GameObjectManager*        pMgr   = _pManager;
            const sw::ComponentHandle handle = getHandle();
            pMgr->deferTransformUpdate( [pMgr, handle]()
            {
                SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
                if ( pSelf != nullptr )
                    pSelf->markTransformDirty();
            } );
            return;
        }

        _bIsTransformDirty = SW_TRUE;

        if ( _pManager != nullptr )
            _pManager->notifyTransformDirtied();

        // 부모 사슬을 올라가며 "자손 더티" 를 세우고, 루트에 닿으면 플러시 목록에 올린다. 이미 서 있는 조상을 만나면
        // 그 루트는 이미 올라 있다(불변식) — 거기서 멈춘다. 내가 루트면 나를 올린다.
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

        const string parentKey = SceneComponentInternal::findStableSceneComponentKey( _pParent );
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

        SceneComponent* pParent = SceneComponentInternal::findSceneComponentByAttachKey( pParentOwner, _attachComponent.c_str() );
        if ( pParent == nullptr || pParent == this )
            return;
        attachToComponent( pParent );
    }

} // namespace sw
