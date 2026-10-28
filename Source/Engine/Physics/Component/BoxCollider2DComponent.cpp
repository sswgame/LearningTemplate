#include "pch.h"

#include "Engine/Physics/Component/BoxCollider2DComponent.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/CollisionLayers.h"
#include "Engine/Physics/Collision/ContinuousCollision.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    namespace
    {
        struct BoxCollider2DComponentInternal
        {
            /**
             * @brief 바디 상자의 가장 작은 반 크기입니다.
             * @details Box2D 는 크기 0 인 상자를 받지 않는다(단언). 크기 0 콜라이더(점)는 이 크기의 상자로 측정한다 — 점이 닿는 것만 겹친다.
             */
            static constexpr float32 kMinHalfExtent = 0.0005f;
            /** @brief 이보다 덜 움직였으면 연속 판정의 쓸기를 하지 않습니다(제자리 겹침은 센서가 측정한다). */
            static constexpr float32 kMinSweepDistance = 1.0e-4f;

            static AABB makeColliderAABB( const float2& minB, const float2& maxB )
            {
                AABB box;
                box._min = float3( minB._x, minB._y, 0.0f );
                box._max = float3( maxB._x, maxB._y, 0.0f );
                return box;
            }

            static PhysicsShapeDesc2D makeBox( const float2& halfExtents )
            {
                PhysicsShapeDesc2D shape;
                shape._type        = PhysicsShapeType2D::Box;
                shape._halfExtents = halfExtents;
                return shape;
            }

            /** @brief 상대 운동 쓸기의 상자입니다. 깊이(Z)는 두 상자에 같은 두께를 준다 — 두께 0 끼리는 슬랩 검사가 경계 비교에 걸린다. */
            static AABB makeSweepBox( const float2& center, const float2& halfExtents )
            {
                AABB box;
                box._min = float3( center._x - halfExtents._x, center._y - halfExtents._y, -1.0f );
                box._max = float3( center._x + halfExtents._x, center._y + halfExtents._y, 1.0f );
                return box;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BoxCollider2DComponent::BoxCollider2DComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _offsetPos{ 0.0f, 0.0f }
        , _offsetScale{ 0.0f, 0.0f }
        , _colliderType{ 0 }
        , _bContinuous{ false }
        , _bTrigger{ false }
        , _body{}
        , _bodyCenter{}
        , _bodyHalfExtent{}
        , _sweepOrigin{}
        , _frameFromCenter{}
        , _bodyOffsetPos{}
        , _bodyOffsetScale{}
        , _bodyLayer{ 0 }
        , _bBodyTrigger{ false }
        , _bSweepOriginValid{ false }
        , _colliderIndex{ kNotRegistered }
    {
    }

    void BoxCollider2DComponent::onBeginPlay()
    {
        PhysicsComponent::onBeginPlay();
        // 바디는 첫 물리 프레임에 생긴다. 그 전에 첫 틱이 옮긴 길(쏘자마자 얇은 적을 건너뛴 총알)도 쓸리도록 시작한 자리를 출발점으로 둔다.
        float2 minB{};
        float2 maxB{};
        getBounds( minB, maxB );
        _sweepOrigin       = float2{ ( minB._x + maxB._x ) * 0.5f, ( minB._y + maxB._y ) * 0.5f };
        _bSweepOriginValid = true;
    }

    void BoxCollider2DComponent::onRegister( GameObjectManager& manager )
    {
        PhysicsComponent::onRegister( manager );
        manager.getSubsystem<sw::ScenePhysics>().registerCollider2D( this );
    }

    void BoxCollider2DComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getSubsystem<sw::ScenePhysics>().unregisterCollider2D( this );
        PhysicsComponent::onUnregister( manager );
    }

    void BoxCollider2DComponent::getBounds( float2& outMin, float2& outMax ) const
    {
        // 상자(오프셋 · 크기)는 콜라이더의 로컬 공간에 있다 — 월드 행렬의 회전 · 스케일을 받는다(유니티 `BoxCollider2D.size` · 언리얼 박스
        // 범위). 월드 위치에 그대로 더하면 키운 콜라이더가 그려진 모습보다 작아진다. 물리는 축 정렬 상자로 판정하므로 돈 상자는 그것을
        // 덮는 축 정렬 상자다 — 축마다 회전 · 스케일 성분의 절댓값으로 반 크기를 모은다(언리얼 `FBox::TransformBy`).
        const float4x4 world   = getWorldMatrix();
        const float3   center  = float3::transform( float3{ _offsetPos._x, _offsetPos._y, 0.0f }, world );
        const float32  halfX   = _offsetScale._x * 0.5f;
        const float32  halfY   = _offsetScale._y * 0.5f;
        const float32  extentX = MathUtil::abs( world._11 ) * halfX + MathUtil::abs( world._21 ) * halfY;
        const float32  extentY = MathUtil::abs( world._12 ) * halfX + MathUtil::abs( world._22 ) * halfY;

        outMin = float2{ center._x - extentX, center._y - extentY };
        outMax = float2{ center._x + extentX, center._y + extentY };
    }

    bool BoxCollider2DComponent::getWorldBounds( float3& outCenter, float32& outRadius ) const
    {
        float2 minB{};
        float2 maxB{};
        getBounds( minB, maxB );
        outCenter = float3{ ( minB._x + maxB._x ) * 0.5f, ( minB._y + maxB._y ) * 0.5f, getWorldPosition()._z };
        outRadius = float2{ maxB._x - minB._x, maxB._y - minB._y }.getLength() * 0.5f;
        return true;
    }

    bool BoxCollider2DComponent::getWorldBox( AABB& outBox ) const
    {
        float2 minB{};
        float2 maxB{};
        getBounds( minB, maxB );
        const float32 worldZ = getWorldPosition()._z;
        outBox               = AABB{
            float3{minB._x, minB._y, worldZ},
            float3{maxB._x, maxB._y, worldZ}
        };
        return true;
    }

    bool BoxCollider2DComponent::overlapsBounds( const BoxCollider2DComponent* pOther ) const
    {
        if ( pOther == nullptr )
            return false;
        float2 bMin{}, bMax{};
        pOther->getBounds( bMin, bMax );
        return overlapsBounds( bMin, bMax );
    }

    bool BoxCollider2DComponent::isTouching( const BoxCollider2DComponent* pOther ) const
    {
        if ( pOther == nullptr )
            return false;

        // 바디(지난 프레임의 상자)가 아니라 지금 상자로 측정한다 — 등록 전후로 답이 갈리지 않는다. 레이어 행렬은 물리 씬의 것 그대로다.
        static const CollisionLayers s_defaultLayers{};
        const ScenePhysics*          pPhysics = getScenePhysics() != nullptr ? getScenePhysics() : pOther->getScenePhysics();
        const IPhysicsScene2D*       pScene   = pPhysics != nullptr ? pPhysics->findScene2D() : nullptr;
        const CollisionLayers&       layers   = pScene != nullptr ? pScene->getLayerCollision() : s_defaultLayers;

        float2 aMin{}, aMax{}, bMin{}, bMax{};
        getBounds( aMin, aMax );
        pOther->getBounds( bMin, bMax );
        return queryOverlaps( BoxCollider2DComponentInternal::makeColliderAABB( aMin, aMax ), static_cast<uint8>( _colliderType ),
                              BoxCollider2DComponentInternal::makeColliderAABB( bMin, bMax ), static_cast<uint8>( pOther->_colliderType ), layers );
    }

    bool BoxCollider2DComponent::containsPoint( const float2& point ) const
    {
        float2 aMin{}, aMax{};
        getBounds( aMin, aMax );
        return ( point._x >= aMin._x && point._x <= aMax._x &&
                 point._y >= aMin._y && point._y <= aMax._y );
    }

    bool BoxCollider2DComponent::overlapsBounds( const float2& minB, const float2& maxB ) const
    {
        float2 aMin{}, aMax{};
        getBounds( aMin, aMax );
        return ( aMin._x <= maxB._x && aMax._x >= minB._x &&
                 aMin._y <= maxB._y && aMax._y >= minB._y );
    }

    void BoxCollider2DComponent::createBody( ScenePhysics& physics, const float2& center, const float2& halfExtents )
    {
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene == nullptr )
            return;
        PhysicsBodyDesc2D desc;
        desc._listShape.push_back( BoxCollider2DComponentInternal::makeBox( halfExtents ) );
        desc._position      = center;
        desc._rotation      = 0.0f;
        desc._userData      = getOwner()->getObjectID();
        desc._gravityFactor = 0.0f;
        desc._type          = PhysicsBodyType::Kinematic;
        desc._layer         = static_cast<uint8>( _colliderType );
        desc._bTrigger      = _bTrigger;
        desc._bOverlapOnly  = true;  // 막는 콜라이더도 센서다 — 키네마틱끼리는 접촉이 없어서 센서여야 겹침이 난다
        desc._bAllowSleep   = false; // 잠든 키네마틱은 옮겨도 깨어날 일이 없다 — 센서 판정에서 빠지지 않게 깨워 둔다
        _body               = pScene->createBody( desc );
        _bodyCenter         = center;
        _bodyHalfExtent     = halfExtents;
        _bodyLayer          = _colliderType;
        _bBodyTrigger       = _bTrigger;
    }

    void BoxCollider2DComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        // 시작 전(편집 중)이거나 꺼진 콜라이더는 겹침에 들지 않는다 — 유니티도 꺼진 콜라이더를 시뮬레이션에서 뺀다.
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene2D* pScene = physics.getScene2D();
        if ( pScene == nullptr )
            return;

        // 트랜스폼(칸의 월드 변경 번호) · 상자 값 · 레이어 · 트리거가 바디를 만든 때와 같으면 상자를 다시 재지 않는다 — 서 있는 콜라이더는 이 비교로 끝난다.
        // 이번 프레임의 출발점은 지난 중심 그대로라 움직인 콜라이더로도 보지 않는다(`hasMovedThisFrame`).
        const bool bTransformChanged = consumeTransformChange();
        const bool bTeleported       = consumeTeleport();
        const bool bRebuild          = consumeRebuild();
        // 바디는 이 컴포넌트만 지운다(씬을 내릴 때는 `ScenePhysics::onSceneCleared` 가 먼저 놓게 한다) — 프레임마다 백엔드에 묻지 않고 Debug 에서만 확인한다.
        SW_ASSERT( _body.isValid() == false || pScene->isBodyValid( _body ) );
        const bool bSameInput = _body.isValid() && bTransformChanged == false && bTeleported == false && bRebuild == false && _bodyOffsetPos == _offsetPos &&
                                _bodyOffsetScale == _offsetScale && _bodyLayer == _colliderType && _bBodyTrigger == _bTrigger;
        if ( bSameInput )
        {
            _frameFromCenter   = _bodyCenter;
            _sweepOrigin       = _bodyCenter;
            _bSweepOriginValid = true;
            return;
        }

        float2 minB{};
        float2 maxB{};
        getBounds( minB, maxB );
        _bodyOffsetPos   = _offsetPos;
        _bodyOffsetScale = _offsetScale;
        const float2 center{ ( minB._x + maxB._x ) * 0.5f, ( minB._y + maxB._y ) * 0.5f };
        const float2 halfExtents{ MathUtil::max( ( maxB._x - minB._x ) * 0.5f, BoxCollider2DComponentInternal::kMinHalfExtent ),
                                  MathUtil::max( ( maxB._y - minB._y ) * 0.5f, BoxCollider2DComponentInternal::kMinHalfExtent ) };

        // 순간이동 표시는 위에서 쓰고 지웠다 — 바디가 새로 들어도 남겨 두면 다음 이동을 잘못 건너뛴다.
        // 이번 프레임의 출발점 — 시작한 자리 또는 지난 프레임의 중심이다. 순간이동했거나 출발점이 없으면 제자리다(쓸지 않고, 움직인 상대로도 보지 않는다).
        // 쓸기는 여기서 하지 않는다 — 모든 콜라이더가 이번 자리로 옮겨진 뒤 씬 물리가 부른다(`ScenePhysics::sweepContinuousColliders2D`).
        _frameFromCenter = ( _bSweepOriginValid && bTeleported == false ) ? _sweepOrigin : center;
        // 크기 · 레이어 · 트리거가 바뀌면 다시 만든다 — 세터는 표시를 남기지 않으므로 매 프레임 바디를 만든 값과 비교한다.
        const bool bShapeChanged = _body.isValid() && ( _bodyHalfExtent != halfExtents || _bodyLayer != _colliderType || _bBodyTrigger != _bTrigger );
        if ( bRebuild || bShapeChanged )
        {
            releasePhysics( physics );
            createBody( physics, center, halfExtents );
        }
        else if ( _body.isValid() == false )
        {
            createBody( physics, center, halfExtents );
        }
        else if ( _bodyCenter != center )
        {
            pScene->setBodyTransform( _body, center, 0.0f );
            _bodyCenter = center;
        }
        // 다시 만든 바디도 출발점을 잇는다 — 바디를 놓으며 지운 출발점을 이번 중심으로 다시 둔다.
        _sweepOrigin       = center;
        _bSweepOriginValid = true;
    }

    SweptOverlap2D BoxCollider2DComponent::makeSweptOverlap( PhysicsBodyHandle otherBody, uint64 otherUserData, float32 hitFraction, bool bOtherTrigger ) const
    {
        SweptOverlap2D overlap;
        overlap._selfBody      = _body;
        overlap._otherBody     = otherBody;
        overlap._selfUserData  = getOwner()->getObjectID();
        overlap._otherUserData = otherUserData;
        overlap._hitFraction   = hitFraction;
        overlap._bSelfTrigger  = _bBodyTrigger;
        overlap._bOtherTrigger = bOtherTrigger;
        return overlap;
    }

    void BoxCollider2DComponent::sweepPassedBodies( ScenePhysics& physics, const vector<BoxCollider2DComponent*>& listMoved )
    {
        using Internal                = BoxCollider2DComponentInternal;
        const IPhysicsScene2D* pScene = physics.findScene2D();
        const float2           delta  = _bodyCenter - _frameFromCenter;
        const float32          length = delta.getLength();
        if ( pScene == nullptr || length <= Internal::kMinSweepDistance )
            return;

        // 레이어 행렬이 이 콜라이더와 부딪히게 둔 레이어만, 트리거까지 본다(트리거도 겹침을 받는다). 자기 바디는 뺀다.
        const CollisionLayers& layers = pScene->getLayerCollision();
        PhysicsQueryFilter     filter;
        filter._ignoreBody       = _body;
        filter._layerMask        = layers.getLayerMask( static_cast<uint8>( _bodyLayer ) );
        filter._bIncludeTriggers = true;

        vector<SweptOverlap2D> listSwept;

        // 1) 가만히 있던 바디 — 이번 자리 그대로 Box2D 로 쓴다. 움직인 콜라이더의 바디는 2) 가 측정한다(이번 자리에 선 채로 측정하면 틀린다).
        const PhysicsShapeDesc2D box = Internal::makeBox( _bodyHalfExtent );
        vector<PhysicsCastHit2D> listHit;
        (void)pScene->shapeCastAll( box, _frameFromCenter, 0.0f, delta * ( 1.0f / length ), length, filter, listHit );
        vector<PhysicsBodyHandle> listAtEnd;
        if ( listHit.empty() == false )
            (void)pScene->overlapShape( box, _bodyCenter, 0.0f, filter, listAtEnd );
        for ( const PhysicsCastHit2D& hit : listHit )
        {
            const bool bMovedBody     = std::find_if( listMoved.begin(), listMoved.end(), [&hit]( const BoxCollider2DComponent* pOther )
                { return pOther->_body == hit._body; } ) != listMoved.end();
            const bool bStartedInside = hit._fraction <= 0.0f;
            const bool bStillInside   = VectorUtil::contains( listAtEnd, hit._body );
            // 출발점 · 도착점의 겹침은 센서가 낸다. 지나친 것만 남는다.
            const bool bSensorReports = bStartedInside || bStillInside;
            if ( bMovedBody || bSensorReports )
                continue;
            listSwept.push_back( makeSweptOverlap( hit._body, hit._userData, hit._fraction, pScene->isBodyTrigger( hit._body ) ) );
        }

        // 2) 이번 프레임에 움직인 콜라이더 — 둘 다 출발점에서, 이 이동에서 상대의 이동을 뺀 만큼 쓴다(Box2D 총알 TOI · 유니티 Continuous Dynamic).
        //    함께 움직인 상대(나란히 나는 산탄)는 차이가 없어 제자리 겹침이 전부다.
        const AABB fromBox = Internal::makeSweepBox( _frameFromCenter, _bodyHalfExtent );
        const AABB toBox   = Internal::makeSweepBox( _bodyCenter, _bodyHalfExtent );
        for ( const BoxCollider2DComponent* pOther : listMoved )
        {
            const bool bSkipOther = pOther == this || layers.shouldCollide( static_cast<uint8>( _bodyLayer ), static_cast<uint8>( pOther->_bodyLayer ) ) == false;
            if ( bSkipOther )
                continue;
            const float2 relative = delta - ( pOther->_bodyCenter - pOther->_frameFromCenter );
            if ( relative.getLength() <= Internal::kMinSweepDistance )
                continue;
            SweepHit   hit{};
            const AABB otherFromBox = Internal::makeSweepBox( pOther->_frameFromCenter, pOther->_bodyHalfExtent );
            const bool bEntered     = ContinuousCollision::sweepAABB( fromBox, float3( relative._x, relative._y, 0.0f ), otherFromBox, hit ) && hit._hitFraction > 0.0f;
            const bool bStillInside = bEntered && toBox.intersects( Internal::makeSweepBox( pOther->_bodyCenter, pOther->_bodyHalfExtent ) );
            if ( bEntered == false || bStillInside )
                continue;
            listSwept.push_back( makeSweptOverlap( pOther->_body, pOther->getOwner()->getObjectID(), hit._hitFraction, pOther->_bBodyTrigger ) );
        }

        // 먼저 닿은 것이 먼저 받는다(총알이 뒤의 적부터 맞지 않는다). 같은 시점끼리는 모은 순서 그대로다.
        std::stable_sort( listSwept.begin(), listSwept.end(), []( const SweptOverlap2D& lhs, const SweptOverlap2D& rhs )
        { return lhs._hitFraction < rhs._hitFraction; } );
        for ( const SweptOverlap2D& overlap : listSwept )
        {
            physics.queueSweptOverlap2D( overlap );
        }
    }

    void BoxCollider2DComponent::releasePhysics( ScenePhysics& physics )
    {
        _bSweepOriginValid = false; // 다시 켠 콜라이더가 꺼진 자리에서 쓸리지 않게
        if ( _body.isValid() == false )
            return;
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene != nullptr )
            pScene->destroyBody( _body );
        _body = PhysicsBodyHandle{};
    }
} // namespace sw
