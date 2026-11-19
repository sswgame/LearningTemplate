/**
 * @file RespawnTestWorld.h
 * @brief 부활 테스트의 세계입니다. 땅은 x 구간들(높이 `_groundY`)이고, 그 밖은 낭떠러지입니다. 자리 준비는 깃발 하나로 막습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"

namespace sw
{
    /** @brief 땅 구간 [_minX, _maxX] 입니다. */
    struct RespawnTestGround
    {
        float32 _minX{ 0.0f };
        float32 _maxX{ 0.0f };
    };

    /** @brief 땅 구간 위만 안전한 2D 세계입니다. */
    class RespawnTestWorld final : public IRespawnWorld
    {
    public:
        vector<RespawnTestGround> _listGround;
        float32                   _groundY;
        uint8                     _bReady : 1; ///< 거짓이면 어느 자리도 준비되지 않았다(셀이 아직 오르지 않음)

        RespawnTestWorld()
            : _listGround{}
            , _groundY{ 0.0f }
            , _bReady{ SW_TRUE }
        {
        }

        optional<float3> findSafeLocation( const float3& position, float32 radius ) const override
        {
            optional<float3> best{};
            float32          bestDistance = 0.0f;
            for ( const RespawnTestGround& ground : _listGround )
            {
                const float3  candidate{ MathUtil::clamp( position._x, ground._minX, ground._maxX ), _groundY, position._z };
                const float32 distance = float3::getDistance( candidate, position );
                if ( distance > radius + 1.0e-4f || ( best.has_value() && distance >= bestDistance ) )
                    continue;
                best         = candidate;
                bestDistance = distance;
            }
            return best;
        }

        bool isLocationReady( const float3& position ) const override
        {
            (void)position;
            return _bReady == SW_TRUE;
        }
    };
} // namespace sw
