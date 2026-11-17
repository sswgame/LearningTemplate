/**
 * @file MatchBackfillSender.h
 * @brief 전용 게임 서버가 경기 중 빈 슬롯(백필)을 권한 매처에 맡기는 입구(`IMatchBackfillSender`, Runtime)의 구현입니다(감사 O-6).
 * @details - 게임 서버가 아는 것은 경기 · 모드 · 팀마다 빈 슬롯 · 남은 사람의 평균 실력뿐이다. 채운 사람이 접속할 서버 id · 지역 · 주소 · 포트는 이 서버의 설명(`ServerDescriptor`,
 *            디렉터리 등록과 같은 값)에서 채운다 — 호출자가 다른 서버를 적어 사람을 엉뚱한 곳으로 보낼 수 없다.
 *          - 매칭 키트가 `gameServer` 로 조립되면 `IMatchBackfillSender` 로 건다. 다른 키트 · 게임 서버 조립이 `find<IMatchBackfillSender>` 로 찾는다.
 *          PlayFab Matchmaking 의 `CreateServerBackfillTicket`(서버 정보는 게임 서버가 아는 자기 정보), Agones · GameLift FlexMatch 의 `StartMatchBackfill` 에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchBackfill.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"

namespace sw
{
    class MatchQueueService;
    class MatchServerAgent;
} // namespace sw

namespace sw
{
    /**
     * @class MatchBackfillSender
     * @brief 이 게임 서버의 설명으로 백필 요청을 채워 대기열에 맡깁니다.
     */
    class SW_KIT_SERVER_API MatchBackfillSender final : public IMatchBackfillSender
    {
    public:
        MatchBackfillSender();

        /** @brief 대기열 · 명단(빌려 쓴다, 필수)과 이 서버의 설명을 받습니다. */
        void initialize( MatchQueueService* pQueue, MatchServerAgent* pAgent, const ServerDescriptor& descriptor );

        [[nodiscard]] MatchmakingServiceResult submitBackfill( const MatchBackfillSlots& slots, UnixMilliseconds nowMs ) override;
        [[nodiscard]] MatchmakingServiceResult submitPlayerLeft( AccountID leftAccountID, UnixMilliseconds nowMs ) override;

    private:
        ServerDescriptor   _descriptor;
        MatchQueueService* _pQueue;
        MatchServerAgent*  _pAgent;
    };
} // namespace sw
