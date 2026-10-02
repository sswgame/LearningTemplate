#include "pch.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/AABB.h"
#include "Engine/Physics/PhysicsWorld.h"

namespace sw
{
    namespace
    {
        struct BoxCollider2DComponentInternal
        {
            static AABB makeColliderAabb( const float2& minB, const float2& maxB )
            {
                AABB box;
                box._min = float3( minB._x, minB._y, 0.0f );
                box._max = float3( maxB._x, maxB._y, 0.0f );
                return box;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BoxCollider2DComponent::BoxCollider2DComponent()
        : _offsetPos{ 0.0f, 0.0f }
        , _offsetScale{ 0.0f, 0.0f }
        , _pPhysics{ nullptr }
        , _physicsBody{}
        , _colliderType{ 0 }
        , _colliderIndex{ kNotRegistered }
    {
    }

    void BoxCollider2DComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();

        GameObject* pGameObject = getOwner();
        if ( pGameObject != nullptr )
            pGameObject->addTag( "Collider"_tag );

        syncPhysicsBody();
    }

    void BoxCollider2DComponent::onEndPlay()
    {
        unregisterPhysicsBody();
        SceneComponent::onEndPlay();
    }

    void BoxCollider2DComponent::onDestroy()
    {
        unregisterPhysicsBody();
        SceneComponent::onDestroy();
    }

    void BoxCollider2DComponent::getBounds( float2& outMin, float2& outMax ) const
    {
        // 상자(오프셋 · 크기)는 콜라이더의 로컬 공간에 있다 — 월드 행렬의 회전 · 스케일을 받는다(유니티 `BoxCollider2D.size` · 언리얼 박스
        // 범위). 예전에는 월드 위치에 그대로 더해, 키운 콜라이더가 그려진 모습보다 작았다. 물리는 축 정렬 상자로 판정하므로 돈 상자는 그것을
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

    void BoxCollider2DComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pPhysics = &manager.getPhysicsWorld();
        manager.registerCollider( this );
    }

    void BoxCollider2DComponent::onUnregister( GameObjectManager& manager )
    {
        unregisterPhysicsBody();
        manager.unregisterCollider( this );
        _pPhysics = nullptr;
        SceneComponent::onUnregister( manager );
    }

    bool BoxCollider2DComponent::intersects( const BoxCollider2DComponent* pOther ) const
    {
        if ( pOther == nullptr )
            return false;
        if ( _pPhysics != nullptr && _physicsBody.isValid() && pOther->_physicsBody.isValid() )
            return _pPhysics->overlaps( _physicsBody, pOther->_physicsBody );

        float2 aMin{}, aMax{}, bMin{}, bMax{};
        getBounds( aMin, aMax );
        pOther->getBounds( bMin, bMax );
        return ( aMin._x <= bMax._x && aMax._x >= bMin._x &&
                 aMin._y <= bMax._y && aMax._y >= bMin._y );
    }

    bool BoxCollider2DComponent::intersects( const float2& point ) const
    {
        float2 aMin{}, aMax{};
        getBounds( aMin, aMax );
        return ( point._x >= aMin._x && point._x <= aMax._x &&
                 point._y >= aMin._y && point._y <= aMax._y );
    }

    bool BoxCollider2DComponent::intersects( const float2& minB, const float2& maxB ) const
    {
        float2 aMin{}, aMax{};
        getBounds( aMin, aMax );
        return ( aMin._x <= maxB._x && aMax._x >= minB._x &&
                 aMin._y <= maxB._y && aMax._y >= minB._y );
    }

    void BoxCollider2DComponent::unregisterPhysicsBody()
    {
        if ( _pPhysics == nullptr || _physicsBody.isValid() == false )
            return;
        _pPhysics->removeBody( _physicsBody );
        _physicsBody = SlotHandle{};
    }

    void BoxCollider2DComponent::syncPhysicsBody()
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || _pPhysics == nullptr )
            return;
        // 시작 전(편집 중)이거나 꺼진 콜라이더는 겹침에 들지 않는다 — 유니티도 꺼진 콜라이더를 시뮬레이션에서 뺀다.
        if ( hasBegunPlay() == false || isActive() == false || isPendingDestroy() )
        {
            unregisterPhysicsBody();
            return;
        }

        float2 minB{};
        float2 maxB{};
        getBounds( minB, maxB );

        const AABB  box   = BoxCollider2DComponentInternal::makeColliderAabb( minB, maxB );
        const uint8 layer = static_cast<uint8>( _colliderType );

        if ( _physicsBody.isValid() )
        {
            _pPhysics->setAabb( _physicsBody, box );
            return;
        }
        _physicsBody = _pPhysics->addBody( box, layer, pOwner->getObjectId() );
    }

} // namespace sw
