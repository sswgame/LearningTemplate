/**
 * @file MatchServerAgent.h
 * @brief 전용 게임 서버가 자기에게 배정된 경기 명단을 받아 접속한 계정이 명단에 있는지 확인합니다.
 * @details - 원본은 대기열이 남긴 캐시 기록 `mm/match/<자기 서버 id>/<경기>`(수명은 모드의 경기 시한)이다. 목록 `mm/matches/<자기 서버 id>` 를 1초마다 읽어 새 기록을 가져온다.
 *            버스 `mm.assign.<자기 서버 id>` 는 "최대 한 번" 이라 잃을 수 있으므로 기록을 읽기 전의 빠른 길로만 쓴다(감사 O-6).
 *          - 버스로만 받은 계정은 2분, 기록으로 받은 계정은 경기 시한까지 받는다. 경기 중 재접속도 받는다.
 *          - 자리를 꺼낸 계정(나가서 그 자리를 맡긴 사람)은 더 받지 않는다 — 자리의 주인은 채운 사람이다. 명단의 사람이 정원을 넘으면 오래된 경기부터 받지 않는다
 *            (예약 창 안에 오지 않아 자리가 다음 경기에 간 사람이다). 받은 사람마다 자리가 있다.
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
    /** @brief 명단에서 읽은 계정 하나의 자리입니다. 나가면 이 자리를 채워 달라고 맡긴다(`MatchBackfillSender::submitPlayerLeft`). */
    struct MatchSeat
    {
        string _modeID{};
        uint64 _matchID{ 0 };
        uint64 _ticketID{ 0 }; ///< 이 자리를 준 티켓이다(로비 경기는 0). 같은 경기에 백필로 다시 들어온 사람은 티켓이 다르다
        int64  _expiresMs{ 0 };
        int64  _placedMs{ 0 }; ///< 이 자리를 준 명단의 배정 시각(`MatchFormed::_placedMs`)이다. 버스와 기록은 순서 없이 온다 — 더 오래된 명단은 자리를 바꾸지 않는다
        int32  _team{ 0 };
        int32  _teamCount{ 0 };
        int32  _averageRating{ 0 };
        uint8  _bTaken{ SW_FALSE }; ///< 꺼냈다(`takeSeat`). 같은 명단이 버스와 기록으로 두 번 와도 다시 채우지 않는다
    };
} // namespace sw

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

        /** @brief 이 게임 서버의 id 와 정원(받을 사람 수)을 받습니다. 받아들이기 정책으로 내놓기 전(조립의 `initialize`)에 부릅니다. */
        void initialize( uint64 serverID, int32 seatCapacity );
        /** @brief 정원을 바꿉니다. 넘친 명단은 오래된 경기부터 받지 않습니다. 서버 루프에서 부릅니다. */
        void setSeatCapacity( int32 seatCapacity );
        /** @brief 명단 기록을 읽을 캐시 라우터를 받습니다(빌려 쓴다, 필수). 라우터보다 먼저 `shutdown` 해야 합니다. */
        void startReading( EphemeralStoreRouter* pRouter );
        /** @brief 기다리던 캐시 요청을 버립니다. 받은 명단은 남습니다. */
        void   shutdown();
        string getAssignTopic() const;
        /** @brief 배정 주제(`MatchQueueBus::kAssignTopic`)의 메시지를 처리합니다. 읽지 못했으면(읽기가 로그와 카운터를 남겼다) false 를 반환합니다. */
        [[nodiscard]] bool handleAssign( const ServerBusMessage& message, UnixMilliseconds nowMs );
        /** @brief 시한이 지난 접속 예정자를 지우고, 주기가 되면 명단 기록 목록을 읽습니다. */
        void update( UnixMilliseconds nowMs );

        /** @brief 접속 예정자이면 true 입니다. 접속 인증기가 네트워크 스레드에서 부릅니다. 아니면 다음 `update` 가 주기를 기다리지 않고 목록을 읽는다 — Found 는 기록이 남은 뒤에 가므로 막 배정받은 사람의 재시도가 1 초 주기에 막히지 않는다. */
        bool  isAccepted( AccountID accountID ) const override;
        int32 getExpectedCount() const;
        /** @brief 경기 @p matchID 에 남은(꺼내지 않은) 자리 수입니다. 서버 루프에서 부릅니다. */
        int32 getSeatCount( uint64 matchID ) const;
        /** @brief 계정 @p accountID 의 자리를 꺼냅니다(한 번만 — 같은 사람이 두 번 나가도 자리 하나). 꺼낸 뒤에는 그 계정을 받지 않습니다. 명단에 없거나 이미 꺼냈으면 false 입니다. 서버 루프에서 부릅니다. */
        [[nodiscard]] bool takeSeat( AccountID accountID, MatchSeat& outSeat );

    private:
        void onListReply( const EphemeralReply& reply );
        void onRecordReply( const string& recordName, const EphemeralReply& reply );
        /** @brief 명단의 계정을 @p expiresMs 까지 받습니다. 이미 더 늦게까지 받는 계정은 그대로 둡니다. */
        void acceptMatch( const MatchFormed& match, int64 expiresMs );
        /** @brief 받을 사람이 정원을 넘으면 가장 오래된 경기의 사람부터 받지 않습니다(경기 하나만 남으면 그만둔다). */
        void trimToCapacity();

    private:
        mutable mutex                                          _mutex;                  ///< 접속 예정자 목록을 지킨다(인증기는 네트워크 스레드)
        unordered_map<AccountID, int64> _mapAccountToExpiresMs SW_GUARDED_BY( _mutex ); ///< 접속 예정자와 받는 시한이다
        mutable uint8 _bReadRequested                          SW_GUARDED_BY( _mutex ); ///< 명단에 없는 계정이 접속을 시도했다. 다음 `update` 가 목록을 바로 읽는다
        unordered_map<string, int64>                           _mapRecordToExpiresMs;   ///< 읽었거나 읽는 중인 기록 이름과 끝나는 시각이다. 같은 기록을 다시 읽지 않는다
        unordered_map<AccountID, MatchSeat>                    _mapAccountToSeat;       ///< 명단의 계정마다 경기 · 팀이다(서버 루프만 쓴다)
        EphemeralStoreRouter*                                  _pRouter;
        Subscription                                           _replyOwner; ///< 라우터 답의 주인 몫(`startReading` 이 열고 `shutdown` 이 놓는다)
        uint64                                                 _serverID;
        uint8                                                  _bListPending; ///< 목록 읽기를 기다린다
        int64                                                  _nextListReadMs;
        int64                                                  _nowMs;
        int32                                                  _seatCapacity; ///< 이 서버의 정원이다
    };
} // namespace sw
