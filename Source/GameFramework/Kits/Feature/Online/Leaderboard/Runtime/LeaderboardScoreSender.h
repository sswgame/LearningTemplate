/**
 * @file LeaderboardScoreSender.h
 * @brief 전용 게임 서버가 판의 점수를 순위표에 내는 창구입니다. 게임 모듈이 include 하므로 공유 키트(Runtime)에 둡니다 — 구현은 서버 키트(`LeaderboardScoreSender`)입니다.
 * @details - 게임 서버 코드는 `IServerKitLocator`(ModuleAPI)로 이 인터페이스를 찾는다(`ServerKitDependencies<…, ServerKitOptional<ILeaderboardScoreSender>>`). 순위표 키트가 선 전용 서버에만 있다.
 *          - 클라이언트의 제출(`LeaderboardMethod::SubmitScore`)과 길이 다르다 — 보드의 `clientSubmit` 이 꺼져 있어도 받는다. 점수와 계정은 게임 서버가 안다(접속 주체가 계정이다).
 *          EOS 의 서버 신뢰 클라이언트 `IngestStat`, 유니티 Cloud Code 의 `LeaderboardsApi.AddLeaderboardPlayerScore`(서비스 토큰)에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardTypes.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Service/OnlineServerKitName.h"

namespace sw
{
    struct NetIdempotencyKey;

    /**
     * @class ILeaderboardScoreSender
     * @brief 서버 권위 점수 제출 인터페이스입니다. 서비스 스레드(서버 루프)에서 부릅니다.
     */
    class ILeaderboardScoreSender
    {
    public:
        static constexpr const utf8* kProviderKit = OnlineServerKitName::kLeaderboard; ///< 이 인터페이스를 내는 키트다. 받는 쪽의 의존 이름이 여기서 나온다

        ILeaderboardScoreSender()          = default;
        virtual ~ILeaderboardScoreSender() = default;

        ILeaderboardScoreSender( const ILeaderboardScoreSender& )            = delete;
        ILeaderboardScoreSender& operator=( const ILeaderboardScoreSender& ) = delete;

        /**
         * @brief 계정 @p accountID 의 점수를 보드에 맡깁니다. 저장은 나중에 끝나며 실패는 키트가 로그에 남깁니다.
         * @param idempotencyKey 판 하나의 제출마다 하나(`NetIdempotencyKey::makeRandom`) — Sum · Latest 보드가 같은 제출을 두 번 세지 않게 한다. 비면 맡기지 않습니다.
         * @return 등록하지 않은 보드면 UnknownBoard, 계정 · 키가 비었으면 NotAllowed 이고 아무것도 맡기지 않습니다.
         */
        [[nodiscard]] virtual LeaderboardResult submitScore( string_view boardID, AccountID accountID, int64 score, const NetIdempotencyKey& idempotencyKey, UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw
