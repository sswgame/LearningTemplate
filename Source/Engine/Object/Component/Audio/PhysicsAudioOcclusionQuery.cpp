#include "pch.h"

#include "Engine/Object/Component/Audio/PhysicsAudioOcclusionQuery.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    PhysicsAudioOcclusionQuery::PhysicsAudioOcclusionQuery()
        : _pScene{ nullptr }
        , _layerMask{ MathUtil::kMaxUInt32 }
    {
    }

    void PhysicsAudioOcclusionQuery::setScene( const IPhysicsScene3D* pScene, uint32 layerMask )
    {
        _pScene    = pScene;
        _layerMask = layerMask;
    }

    bool PhysicsAudioOcclusionQuery::isBlocked( const float3& from, const float3& to ) const
    {
        float3        direction = to - from;
        const float32 distance  = direction.getLength();
        if ( distance <= kListenerSkin + kEmitterSkin )
            return false;
        direction /= distance;
        PhysicsQueryFilter filter;
        filter._layerMask = _layerMask;
        // 리스너 껍질만큼 앞에서 쏘고 에미터 껍질 앞에서 멈춘다 — 서 있는 캡슐 · 소리 내는 물체 자신은 세지 않는다.
        const float3     origin = from + direction * kListenerSkin;
        PhysicsCastHit3D hit;
        return _pScene->raycast( origin, direction, distance - kListenerSkin - kEmitterSkin, filter, hit );
    }

    float32 PhysicsAudioOcclusionQuery::computeOcclusion( const float3& listener, const float3& emitter ) const
    {
        if ( _pScene == nullptr )
            return 0.0f;
        float3 forward = emitter - listener;
        if ( forward.getLengthSquared() <= 1e-6f )
            return 0.0f;
        forward.normalize();
        // 수평으로 옆 방향(위 × 앞). 똑바로 위아래면 X 축을 옆으로 쓴다.
        float3 side = float3( 0.0f, 1.0f, 0.0f ).cross( forward );
        if ( side.getLengthSquared() <= 1e-6f )
            side = float3( 1.0f, 0.0f, 0.0f );
        side.normalize();
        const float3 offset = side * kSideOffset;

        uint32 blockedCount = 0;
        blockedCount += isBlocked( listener, emitter ) ? 1u : 0u;
        blockedCount += isBlocked( listener + offset, emitter + offset ) ? 1u : 0u;
        blockedCount += isBlocked( listener - offset, emitter - offset ) ? 1u : 0u;
        return static_cast<float32>( blockedCount ) / 3.0f;
    }
} // namespace sw
