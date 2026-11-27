/**
 * @file TeamAttitude.h
 * @brief 두 팀 사이의 태도(아군, 중립, 적)를 판정합니다. 팀은 판이 매긴 번호(0 부터)이고 `kNoTeam` 은 팀 없음입니다.
 * @details 언리얼의 `ETeamAttitude` 와 `FGenericTeamId` 에 해당한다. 팀 번호는 `MatchState::addTeam` 과 `RTSSimulation::addPlayer` 가 매기는 배열 인덱스이므로
 *          int32 그대로 둔다(언리얼은 uint8 이고 NoTeam 이 255). 판정기 교체(동맹 테이블, 팀킬 허용)는 쓰는 판이 생기면 여기에 추가한다.
 *          키트가 역할 이름으로 쓰는 팀 enum(`SRPGTeam`, `ConquestTeam`)은 각자의 `isHostile` 을 둔다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

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
    // 주인이 열거형(`TeamAttitude`)이라 멤버를 둘 수 없어 그 옆 자유 함수로 둔다(언리얼 `FGenericTeamId::GetAttitude` 자리). 판 규칙(`MatchState`)과 장르 키트가 같은 규칙을 쓴다.

    inline constexpr int32 kNoTeam = constant::invalid_index::kInt32; ///< 팀 없음. 누구와도 중립이다

    /** @brief @p team 이 @p other 를 보는 태도입니다. 어느 한쪽이 팀 없음(음수)이면 중립이고, 같으면 아군이고, 다르면 적입니다. */
    [[nodiscard]] constexpr TeamAttitude computeTeamAttitude( const int32 team, const int32 other )
    {
        if ( team < 0 || other < 0 )
            return TeamAttitude::Neutral;
        return team == other ? TeamAttitude::Friendly : TeamAttitude::Hostile;
    }

    /** @brief 서로 적이면 true 입니다. */
    [[nodiscard]] constexpr bool isHostileTeam( const int32 team, const int32 other )
    {
        return computeTeamAttitude( team, other ) == TeamAttitude::Hostile;
    }

    /** @brief 같은 팀이면 true 입니다. 팀 없음끼리는 같은 팀이 아닙니다. */
    [[nodiscard]] constexpr bool isFriendlyTeam( const int32 team, const int32 other )
    {
        return computeTeamAttitude( team, other ) == TeamAttitude::Friendly;
    }
} // namespace sw
