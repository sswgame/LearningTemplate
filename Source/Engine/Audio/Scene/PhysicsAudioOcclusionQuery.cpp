#include "pch.h"

#include "Engine/Audio/Scene/PhysicsAudioOcclusionQuery.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct PhysicsAudioOcclusionQueryInternal
        {
            /** @brief 레이 셋 가운데 막힌 수를 가림 비율로 바꿉니다. */
            static AudioPropagation makeOcclusion( uint32 blockedCount ) { return AudioPropagation::makeOcclusion( static_cast<float32>( blockedCount ) / 3.0f ); }
        };
    } // namespace

    PhysicsAudioOcclusionQuery::PhysicsAudioOcclusionQuery()
        : _pScene3D{ nullptr }
        , _pScene2D{ nullptr }
        , _layerMask{ MathUtil::kMaxUInt32 }
    {
    }

    void PhysicsAudioOcclusionQuery::setScene( const IPhysicsQueryScene3D* pScene, uint32 layerMask )
    {
        _pScene3D  = pScene;
        _pScene2D  = nullptr;
        _layerMask = layerMask;
    }

    void PhysicsAudioOcclusionQuery::setScene( const IPhysicsQueryScene2D* pScene, uint32 layerMask )
    {
        _pScene3D  = nullptr;
        _pScene2D  = pScene;
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
        return _pScene3D->raycast( origin, direction, distance - kListenerSkin - kEmitterSkin, filter, hit );
    }

    bool PhysicsAudioOcclusionQuery::isBlocked( const float2& from, const float2& to ) const
    {
        float2        direction = to - from;
        const float32 distance  = direction.getLength();
        if ( distance <= kListenerSkin + kEmitterSkin )
            return false;
        direction /= distance;
        PhysicsQueryFilter filter;
        filter._layerMask       = _layerMask;
        const float2     origin = from + direction * kListenerSkin;
        PhysicsCastHit2D hit;
        return _pScene2D->raycast( origin, direction, distance - kListenerSkin - kEmitterSkin, filter, hit );
    }

    AudioPropagation PhysicsAudioOcclusionQuery::computePropagation( const float3& listener, const float3& emitter ) const
    {
        if ( _pScene3D != nullptr )
        {
            float3 forward = emitter - listener;
            if ( forward.getLengthSquared() <= MathUtil::kEpsilon )
                return AudioPropagation::makeOcclusion( 0.0f );
            forward.normalize();
            // 수평으로 옆 방향(위 × 앞). 똑바로 위아래면 X 축을 옆으로 쓴다.
            float3 side = float3::Up.cross( forward );
            if ( side.getLengthSquared() <= MathUtil::kEpsilon )
                side = float3::Right;
            side.normalize();
            const float3 offset = side * kSideOffset;

            uint32 blockedCount = 0;
            blockedCount += isBlocked( listener, emitter ) ? 1u : 0u;
            blockedCount += isBlocked( listener + offset, emitter + offset ) ? 1u : 0u;
            blockedCount += isBlocked( listener - offset, emitter - offset ) ? 1u : 0u;
            return PhysicsAudioOcclusionQueryInternal::makeOcclusion( blockedCount );
        }
        if ( _pScene2D != nullptr )
        {
            const float2 from( listener._x, listener._y );
            const float2 to( emitter._x, emitter._y );
            float2       forward = to - from;
            if ( forward.getLengthSquared() <= MathUtil::kEpsilon )
                return AudioPropagation::makeOcclusion( 0.0f );
            forward.normalize();
            const float2 offset = float2( -forward._y, forward._x ) * kSideOffset;

            uint32 blockedCount = 0;
            blockedCount += isBlocked( from, to ) ? 1u : 0u;
            blockedCount += isBlocked( from + offset, to + offset ) ? 1u : 0u;
            blockedCount += isBlocked( from - offset, to - offset ) ? 1u : 0u;
            return PhysicsAudioOcclusionQueryInternal::makeOcclusion( blockedCount );
        }
        return AudioPropagation::makeOcclusion( 0.0f );
    }
} // namespace sw
