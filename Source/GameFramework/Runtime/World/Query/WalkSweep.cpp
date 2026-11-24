#include "pch.h"

#include "GameFramework/Runtime/World/Query/WalkSweep.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"

namespace sw
{
    namespace
    {
        struct WalkSweepInternal
        {
            static constexpr int32   kMaxSlide = 2; ///< 한 걸음에 미끄러지는 벽의 수(구석은 둘)
            static constexpr float32 kMinStep  = 1.0e-4f;
        };
    } // namespace

    float3 WalkSweep::slide( const GameObjectManager& manager, const float3& center, const float2& delta, float32 radius )
    {
        using Internal   = WalkSweepInternal;
        float3 position  = center;
        float2 remaining = delta;
        for ( int32 slideIndex = 0; slideIndex <= Internal::kMaxSlide; ++slideIndex )
        {
            const float32 length = remaining.getLength();
            if ( length < Internal::kMinStep )
                return position;
            const float2    direction = remaining * ( 1.0f / length );
            const float3    planar{ direction._x, 0.0f, direction._y };
            CharacterRayHit hit;
            if ( CharacterRayHit::sphereCast3D( manager, position, planar, length + kSkin, radius, MathUtil::kMaxUInt32, 0, hit ) == false )
                return position + planar * length;
            // 벽에서 멀어지는 걸음은 그대로 걷는다. 아니면 닿는 데까지 걷고 벽 쪽 성분만 지운다.
            const float2  normal = float2{ hit._normal._x, hit._normal._z }.normalizeOr( float2{ 0.0f, 0.0f }, Internal::kMinStep );
            const float32 into   = direction._x * normal._x + direction._y * normal._y;
            if ( into >= 0.0f )
                return position + planar * length;
            const float32 moved = MathUtil::clamp( hit._distance - kSkin, 0.0f, length );
            position            = position + planar * moved;
            remaining           = ( direction - normal * into ) * ( length - moved );
        }
        return position;
    }
} // namespace sw
