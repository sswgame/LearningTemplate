/**
 * @file LeaderboardClient.h
 * @brief 순위표 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 상위 순위, 내 주변 순위, 통계, 점수 제출, 업적, 시즌 결과를 요청하고 업적 달성 알림을 쌓아 둡니다.
 * @details - 요청 · 응답 규칙은 `OnlineKitClient` 에 있다. 업무 결과는 `_reply._result` 에, 전송 오류와 공통 오류는 `_errorCode` 에 있다.
 *          - 점수 제출은 서버가 `_bClientSubmit` 이 켜진 보드만 받으며 아니면 NotAllowed 이다. 보통 점수는 서버 게임 로직이 낸다.
 */
#pragma once
#include "Core/Container/DrainGate.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/Service/OnlineKitClient.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardProtocol.h"

namespace sw
{
    /**
     * @class LeaderboardClient
     * @brief 연결 하나의 순위표 클라이언트입니다.
     */
    class SW_KIT_API LeaderboardClient final : public OnlineKitClient<LeaderboardProtocol, LeaderboardReply>
    {
    public:
        LeaderboardClient();

        uint64 requestTop( string_view boardID, int32 offset, int32 count, const ReplyDelegate& onReply );
        uint64 requestAround( string_view boardID, int32 radius, const ReplyDelegate& onReply );
        uint64 requestStats( const ReplyDelegate& onReply );
        uint64 submitScore( string_view boardID, int64 score, const ReplyDelegate& onReply );
        uint64 requestAchievements( const ReplyDelegate& onReply );
        uint64 requestSeasonResult( string_view boardID, uint32 seasonID, const ReplyDelegate& onReply );
        /** @brief 친구 순위표입니다. @p listAccountID 는 친구 키트의 친구 목록에 자기를 더한 것이며 응답의 순위는 목록 안 순위입니다. */
        uint64 requestScoresOf( string_view boardID, const vector<AccountID>& listAccountID, const ReplyDelegate& onReply );
        /** @brief 등록한 업적마다 진행률입니다. 숨긴 업적은 달성 전에 가려져 옵니다. */
        uint64 requestAchievementProgress( const ReplyDelegate& onReply );

        /** @brief 업적 달성 알림 버퍼를 꺼낼 권한을 @p outLease 에 받습니다. 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다(알림을 서로 가져가는 일을 막는다). */
        [[nodiscard]] bool claimAchievementUnlocks( DrainLease& outLease ) { return _unlockGate.claim( outLease ); }
        /** @brief 쌓인 업적 달성 알림을 꺼냅니다. @p lease 는 `claimAchievementUnlocks` 로 받은 표여야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void drainAchievementUnlocks( const DrainLease& lease, vector<AchievementState>& outListAchievement )
        {
            SW_ASSERT( _unlockGate.isHeldBy( lease ) );
            if ( _unlockGate.isHeldBy( lease ) )
                _unlockBuffer.drainTo( outListAchievement );
        }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override { return LeaderboardProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override;

    private:
        uint64 send( uint16 method, const LeaderboardRequest& request, const ReplyDelegate& onReply );

        DrainGate                     _unlockGate;
        EventBuffer<AchievementState> _unlockBuffer;
    };
} // namespace sw
