/**
 * @file LeaderboardScoreSender.h
 * @brief 전용 게임 서버의 점수 제출 창구(`ILeaderboardScoreSender`, Runtime)의 구현입니다 — 순위표 로직에 그대로 맡깁니다.
 * @details 완료는 요청 태그 `kRequestTag` 로 돌아와 바인딩(`LeaderboardServer`)이 결과를 로그에 남긴다 — 답할 호출이 없어 실패가 소리 없이 사라지지 않게 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardScoreSender.h"

namespace sw
{
    class LeaderboardService;

    /**
     * @class LeaderboardScoreSender
     * @brief 서버 권위 점수 제출입니다.
     */
    class SW_KIT_SERVER_API LeaderboardScoreSender final : public ILeaderboardScoreSender
    {
    public:
        static constexpr uint64 kRequestTag = ~uint64{ 0 }; ///< 서버 제출의 완료를 가르는 태그다(호출 번호는 1 부터 늘어 여기 닿지 않는다)

        /** @brief @p service 는 빌려 쓴다(이 객체보다 오래 산다). */
        explicit LeaderboardScoreSender( LeaderboardService& service );

        [[nodiscard]] LeaderboardResult submitScore( string_view boardID, AccountID accountID, int64 score, const NetIdempotencyKey& idempotencyKey, UnixMilliseconds nowMs ) override;

    private:
        LeaderboardService& _service;
    };
} // namespace sw
