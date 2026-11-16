/**
 * @file MatchServerAgent.h
 * @brief 전용 게임 서버가 자기에게 배정된 경기 명단을 받아 접속한 계정이 명단에 있는지 확인합니다.
 * @details - 원본은 대기열이 남긴 캐시 기록 `mm/match/<자기 서버 id>/<경기>`(수명은 모드의 경기 시한)이다. 목록 `mm/matches/<자기 서버 id>` 를 1초마다 읽어 새 기록을 가져온다.
 *            버스 `mm.assign.<자기 서버 id>` 는 "최대 한 번" 이라 잃을 수 있으므로 기록을 읽기 전의 빠른 길로만 쓴다(감사 O-6).
 *          - 버스로만 받은 계정은 2분, 기록으로 받은 계정은 경기 시한까지 받는다. 경기 중 재접속도 받는다.
 *          - 게임 서버는 이 객체를 계정 키트의 접속 인증기에 받아들이기 정책(`IConnectAdmission`)으로 넘긴다. 티켓이 맞아도 명단에 없는 계정은 거절된다.
 *            인증기는 네트워크 스레드에서 묻고 기록 읽기 · 배정 · 시한은 서버 루프에서 바뀌므로 명단은 잠금 하나로 지킨다.
 *          서버가 매칭 결과를 미리 알고 접속할 때 확인하는 방식은 PlayFab Multiplayer Servers 의 세션 플레이어 목록 · Agones Allocation 의 영속 배정과 같다.
 *          버스 구독은 서버 조립 단계(또는 `MatchmakingServer`)가 `getAssignTopic` 으로 한다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/ConnectAdmission.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;
    class ServerBusMessage;

    /**
     * @class MatchServerAgent
     * @brief 게임 서버 쪽에서 배정을 받는 객체입니다. `isAccepted` 외에는 서버 루프 스레드 하나에서 씁니다.
     */
    class SW_KIT_SERVER_API MatchServerAgent final : public IConnectAdmission
    {
    public:
        static constexpr int64 kExpectTTLMs     = 120000; ///< 버스로만 받은 명단의 수명이다. 기록을 읽으면 경기 시한까지 늘어난다
        static constexpr int64 kRecordRefreshMs = 1000;   ///< 명단 기록 목록을 다시 읽는 주기이다
        static constexpr int32 kMaxRecordCount  = 256;    ///< 한 번에 읽는 목록 멤버 수이다. 끝나는 시각이 늦은(새) 기록부터 나온다

        MatchServerAgent();
        ~MatchServerAgent();

        MatchServerAgent( const MatchServerAgent& )            = delete;
        MatchServerAgent& operator=( const MatchServerAgent& ) = delete;

        /** @brief 이 게임 서버의 id 를 받습니다. 받아들이기 정책으로 내놓기 전(조립의 `initialize`)에 부릅니다. */
        void initialize( uint64 serverID );
        /** @brief 명단 기록을 읽을 캐시 라우터를 받습니다(빌려 쓴다, 필수). 라우터보다 먼저 `shutdown` 해야 합니다. */
        void startReading( EphemeralStoreRouter* pRouter );
        /** @brief 기다리던 캐시 요청을 버립니다. 받은 명단은 남습니다. */
        void   shutdown();
        string getAssignTopic() const;
        /** @brief 배정 주제(`MatchQueueBus::kAssignTopic`)의 메시지를 처리합니다. 읽지 못했으면(읽기가 로그와 카운터를 남겼다) false 를 반환합니다. */
        [[nodiscard]] bool handleAssign( const ServerBusMessage& message, UnixMilliseconds nowMs );
        /** @brief 시한이 지난 접속 예정자를 지우고, 주기가 되면 명단 기록 목록을 읽습니다. */
        void update( UnixMilliseconds nowMs );

        /** @brief 접속 예정자이면 true 입니다. 접속 인증기가 네트워크 스레드에서 부릅니다. */
        bool  isAccepted( AccountID accountID ) const override;
        int32 getExpectedCount() const;

    private:
        void onListReply( const EphemeralReply& reply );
        void onRecordReply( const EphemeralReply& reply );
        /** @brief 명단의 계정을 @p expiresMs 까지 받습니다. 이미 더 늦게까지 받는 계정은 그대로 둡니다. */
        void acceptMatch( const MatchFormed& match, int64 expiresMs );

    private:
        mutable mutex                                          _mutex;                  ///< 접속 예정자 목록을 지킨다(인증기는 네트워크 스레드)
        unordered_map<AccountID, int64> _mapAccountToExpiresMs SW_GUARDED_BY( _mutex ); ///< 접속 예정자와 받는 시한이다
        unordered_map<string, int64>                           _mapRecordToExpiresMs;   ///< 읽었거나 읽는 중인 기록 이름과 끝나는 시각이다. 같은 기록을 다시 읽지 않는다
        unordered_map<uint64, string>                          _mapRequestToRecord;     ///< 기다리는 기록 읽기이다
        EphemeralStoreRouter*                                  _pRouter;
        Subscription                                           _replyOwner; ///< 라우터 답의 주인 몫(`startReading` 이 열고 `shutdown` 이 놓는다)
        uint64                                                 _serverID;
        uint64                                                 _listRequestID; ///< 기다리는 목록 읽기이다. 0 이면 없다
        int64                                                  _nextListReadMs;
        int64                                                  _nowMs;
    };
} // namespace sw
