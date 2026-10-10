/**
 * @file TeamAttitude.h
 * @brief 두 팀 사이의 태도(아군 · 중립 · 적) 판정입니다 — 팀은 판이 매긴 번호(0 부터)이고 `TeamAttitudeUtil::kNoTeam` 은 팀 없음입니다.
 * @details 언리얼 `ETeamAttitude` · `FGenericTeamId` 의 자리입니다. 팀 번호는 `MatchState::addTeam` · `RTSWorld::addPlayer` 가 매기는 배열 자리라
 *          int32 그대로 둡니다(언리얼은 uint8 · NoTeam 255). 판정기 교체(동맹 표 · 팀킬 허용)는 쓰는 판이 생기면 여기에 붙입니다.
 *          키트가 역할 이름으로 쓰는 팀 enum(`SRPGTeam` · `ConquestTeam`)은 각자의 `isHostile` 을 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 한 팀이 다른 팀을 보는 태도입니다. */
    enum class TeamAttitude : uint8
    {
        Friendly = 0, ///< 같은 팀
        Neutral,      ///< 어느 한쪽이 팀 없음
        Hostile       ///< 다른 팀
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TeamAttitudeUtil
     * @brief 팀 번호 둘의 태도입니다. 판 규칙(`MatchState`)과 장르 키트(배틀로얄 · 기체 대전 · RTS)가 같은 규칙을 씁니다.
     */
    struct TeamAttitudeUtil
    {
        static constexpr int32 kNoTeam = -1; ///< 팀 없음 — 누구와도 중립

        /** @brief @p team 이 @p other 를 보는 태도입니다. 어느 한쪽이 팀 없음(음수)이면 중립, 같으면 아군, 다르면 적입니다. */
        static constexpr TeamAttitude computeAttitude( int32 team, int32 other )
        {
            if ( team < 0 || other < 0 )
                return TeamAttitude::Neutral;
            return team == other ? TeamAttitude::Friendly : TeamAttitude::Hostile;
        }

        /** @brief 서로 적인가입니다. */
        static constexpr bool isHostile( int32 team, int32 other ) { return computeAttitude( team, other ) == TeamAttitude::Hostile; }

        /** @brief 같은 팀인가입니다(팀 없음끼리는 같은 팀이 아닙니다). */
        static constexpr bool isFriendly( int32 team, int32 other ) { return computeAttitude( team, other ) == TeamAttitude::Friendly; }
    };
} // namespace sw
