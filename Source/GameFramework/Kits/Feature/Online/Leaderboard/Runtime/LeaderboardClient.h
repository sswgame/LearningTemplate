/**
 * @file LeaderboardClient.h
 * @brief 순위표 키트의 클라이언트입니다. 요청은 `call<LeaderboardMethod::GetTop>( request, onReply )` 이고, 이 클래스는 업적 달성 알림을 쌓아 둡니다.
 * @details - 요청 · 응답 규칙은 `OnlineProtocolClient` 에 있다. 응답 타입은 메서드마다 따로이다(`LeaderboardMethod`). 업무 결과는 `_reply._result`(도메인 값만)에, 전송 오류와 공통 범주는 `_errorCode` 에 있다.
 *          - 점수 제출은 서버가 `_bClientSubmit` 이 켜진 보드만 받으며 아니면 NotAllowed 이다. 보통 점수는 서버 게임 로직이 낸다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    /**
     * @class LeaderboardClient
     * @brief 연결 하나의 순위표 클라이언트입니다.
     */
    class SW_KIT_API LeaderboardClient final : public OnlineProtocolClient<LeaderboardClient, LeaderboardProtocol>
    {
    public:
        LeaderboardClient();

        /** @brief 업적 달성 알림 버퍼를 꺼낼 권한을 @p outLease 에 받습니다. 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다(알림을 서로 가져가는 일을 막는다). */
        [[nodiscard]] bool claimAchievementUnlocks( ConsumerLease& outLease ) { return _unlockConsumerLock.claim( outLease ); }
        /** @brief 쌓인 업적 달성 알림을 꺼냅니다. @p lease 는 `claimAchievementUnlocks` 로 받은 표여야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void pollAchievementUnlocks( const ConsumerLease& lease, vector<AchievementState>& outListAchievement )
        {
            SW_ASSERT( _unlockConsumerLock.isHeldBy( lease ) );
            if ( _unlockConsumerLock.isHeldBy( lease ) )
                _unlockBuffer.moveAllTo( outListAchievement );
        }

        // OnlineProtocolClient 의 훅
        void onPush( LeaderboardMethod::Achievement, const AchievementState& body );

    private:
        ConsumerLock                  _unlockConsumerLock;
        EventBuffer<AchievementState> _unlockBuffer;
    };
} // namespace sw
