#include "pch.h"

#include "Engine/Object/Component/Audio/Physics2DAudioOcclusionQuery.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    Physics2DAudioOcclusionQuery::Physics2DAudioOcclusionQuery()
        : _pScene{ nullptr }
        , _layerMask{ MathUtil::kMaxUInt32 }
    {
    }

    void Physics2DAudioOcclusionQuery::setScene( const IPhysicsQueryScene2D* pScene, uint32 layerMask )
    {
        _pScene    = pScene;
        _layerMask = layerMask;
    }

    bool Physics2DAudioOcclusionQuery::isBlocked( const float2& from, const float2& to ) const
    {
        float2        direction = to - from;
        const float32 distance  = direction.getLength();
        if ( distance <= kListenerSkin + kEmitterSkin )
            return false;
        direction /= distance;
        PhysicsQueryFilter filter;
        filter._layerMask = _layerMask;
        // 리스너 껍질만큼 앞에서 쏘고 에미터 껍질 앞에서 멈춘다 — 서 있는 캐릭터 · 소리 내는 물체 자신은 세지 않는다.
        const float2     origin = from + direction * kListenerSkin;
        PhysicsCastHit2D hit;
        return _pScene->raycast( origin, direction, distance - kListenerSkin - kEmitterSkin, filter, hit );
    }

    AudioPropagation Physics2DAudioOcclusionQuery::computePropagation( const float3& listener, const float3& emitter ) const
    {
        if ( _pScene == nullptr )
            return AudioPropagation::makeOcclusion( 0.0f );
        const float2 from( listener._x, listener._y );
        const float2 to( emitter._x, emitter._y );
        float2       forward = to - from;
        if ( forward.getLengthSquared() <= 1e-6f )
            return AudioPropagation::makeOcclusion( 0.0f );
        forward.normalize();
        const float2 offset = float2( -forward._y, forward._x ) * kSideOffset;

        uint32 blockedCount = 0;
        blockedCount += isBlocked( from, to ) ? 1u : 0u;
        blockedCount += isBlocked( from + offset, to + offset ) ? 1u : 0u;
        blockedCount += isBlocked( from - offset, to - offset ) ? 1u : 0u;
        return AudioPropagation::makeOcclusion( static_cast<float32>( blockedCount ) / 3.0f );
    }
} // namespace sw
