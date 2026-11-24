#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineBot.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct TrampolineBotInternal
        {
            static constexpr float32 kSafeRadiusRatio  = 0.7f;  ///< 무대 반지름의 이 비율 밖이면 가운데로 돌아온다
            static constexpr float32 kAttackRangeRatio = 0.5f;  ///< 돌진 거리(속도 × 시간)의 이 비율에 몸 닿는 거리를 더해 친다
            static constexpr float32 kJumpLeadSeconds  = 0.03f; ///< 면에 닿기 이만큼 전부터 점프를 누른다

            static bool isOnBoard( const TrampolinePlayer& body ) { return body._state == TrampolinePlayerState::Air || body._state == TrampolinePlayerState::Contact; }
        };
    } // namespace

    TrampolineInput TrampolineBot::computeInput( const TrampolineArena& arena, int32 player )
    {
        using Internal = TrampolineBotInternal;
        TrampolineInput               input;
        const TrampolinePlayer* const pBody = arena.findPlayer( player );
        if ( pBody == nullptr || Internal::isOnBoard( *pBody ) == false )
            return input;
        const TrampolineSettings& settings = arena.getSettings();

        // 점프: 닿는 순간을 겨눈다. 닿기 전에 눌러 둔 입력도 판정 창 안이면 인정된다.
        const bool bLandingSoon = pBody->_velocity._y < 0.0f && pBody->_position._y <= -pBody->_velocity._y * Internal::kJumpLeadSeconds;
        input._bJumpPressed     = pBody->_state == TrampolinePlayerState::Contact || bLandingSoon ? SW_TRUE : SW_FALSE;

        // 가장자리면 먼저 가운데로 — 떠밀리지 않고 스스로 떨어지지 않는다.
        const float3  flat{ pBody->_position._x, 0.0f, pBody->_position._z };
        const float32 radius = flat.getLength();
        if ( radius > settings._arenaRadius * Internal::kSafeRadiusRatio )
        {
            input._moveX = -flat._x / radius;
            input._moveZ = -flat._z / radius;
            return input;
        }

        const TrampolinePlayer* pTarget  = nullptr;
        float32                 distance = MathUtil::kMaxFloat;
        for ( int32 other = 0; other < arena.getPlayerCount(); ++other )
        {
            const TrampolinePlayer* const pOther = arena.findPlayer( other );
            if ( other == player || Internal::isOnBoard( *pOther ) == false )
                continue;
            const float32 otherDistance = float3::getDistance( pBody->_position, pOther->_position );
            if ( otherDistance >= distance )
                continue;
            pTarget  = pOther;
            distance = otherDistance;
        }
        if ( pTarget == nullptr )
            return input;
        const float3  toTarget{ pTarget->_position._x - pBody->_position._x, 0.0f, pTarget->_position._z - pBody->_position._z };
        const float32 flatDistance = toTarget.getLength();
        if ( flatDistance > MathUtil::kEpsilon )
        {
            input._moveX = toTarget._x / flatDistance;
            input._moveZ = toTarget._z / flatDistance;
        }
        const float32 attackRange = settings._attackDashSpeed * settings._attackDuration * Internal::kAttackRangeRatio + settings._bodyRadius * 2.0f + settings._attackRadius;
        const bool    bAttack     = pTarget->_invulnerableTimer.isActive() == false && distance <= attackRange && pBody->_attackCooldown.isActive() == false;
        input._bAttackPressed     = bAttack ? SW_TRUE : SW_FALSE;
        return input;
    }
} // namespace sw
