/**
 * @file MatchQueueService.h
 * @brief 매칭 대기열 — 모드마다 캐시 임대를 잡은 서버 하나가 매처를 돌고, 만든 경기를 전용 서버에 배정해 게임 서버 · 표를 낸 서버에 버스로 알립니다. 전송을 모른다(서비스 스레드 하나).
 * @details - 권한: 모드마다 임대 `mm/lease/<모드>`(값 = 서버 id, 10 초)를 없을 때만 잡고, 잡은 서버가 3 초마다 자기 값일 때만 연장한다. 버스가 없으면(서버 한 대) 늘 권한이다.
 *          - 표 흐름: 계정이 붙은 서버가 표를 만든다(파티 장이면 파티 전체 — 실력은 `IMatchRatingSource`). 권한이면 바로 매처에, 아니면 버스 `mm.queue.<모드>` 로. 빠지기는 `mm.cancel.<모드>`.
 *          - 배정: 경기가 생기면 기반 `ServerRegistryReader` 로 모드의 서버 종류 · 경기 지역 · 인원 자리를 고르고, 게임 서버에 `mm.assign.<서버>`(올 사람), 표를 낸 서버에
 *            `mm.result.<서버>` 로 결과를 보낸다 — 그 서버가 회원에게 알린다. 자리가 없으면 경기를 들고 기다렸다 다시, 30 초를 넘기면 NoServer.
 *          - 권한이 넘어가면 대기 중 표는 잃는다 — 낸 서버가 표마다 시한(모드 시한 + 30 초 + 10 초)을 들고 있다가 결과가 없으면 Timeout 을 알린다(클라이언트가 다시 줄 선다).
 *          - 로비 시작은 같은 배정 길(팀은 로비의 편 그대로, 자리가 없으면 바로 NoServer + 로비를 다시 연다).
 *          Nakama 메모리 매처 · OpenMatch 디렉터 + 할당기, PlayFab Matchmaking → Multiplayer Servers 와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Online/Directory/ServerRegistryReader.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchMaker.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;
    class IServerBus;

    /**
     * @class IMatchRatingSource
     * @brief 게임 조립이 주는 실력입니다(순위표 통계 · 게임 자체 MMR) — 메모리 조회여야 한다(서비스 스레드). 클라이언트는 실력을 보내지 않는다.
     */
    class IMatchRatingSource
    {
    public:
        virtual ~IMatchRatingSource()                                             = default;
        virtual int32 findRating( AccountID accountID, string_view modeID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 빌려 쓰는 것들입니다(대기열보다 오래 산다). */
    struct MatchQueueDependencies
    {
        EphemeralStoreRouter*     _pRouter{ nullptr };       ///< 필수 — 임대 · 서버 목록
        IServerBus*               _pBus{ nullptr };          ///< 서버 한 대면 null(이 서버가 늘 권한)
        PartyLobbyService*        _pPartyLobby{ nullptr };   ///< 파티 표 · 로비 다시 열기(없으면 혼자 표만)
        const IMatchRatingSource* _pRatingSource{ nullptr }; ///< 없으면 모두 `MatchQueueLimit::kDefaultRating`
        uint64                    _serverID{ 1 };            ///< 32 비트 안(표 · 경기 id 의 위 32 비트)
        int64                     _registryRefreshPeriodMs{ 2000 };
    };
} // namespace sw

namespace sw
{
    /** @brief 대기열 상수입니다(내보내는 클래스 밖 — 다른 모듈이 값으로 읽는다). */
    struct MatchQueueLimit
    {
        static constexpr int64  kLeaseTtlMs       = 10000;
        static constexpr int64  kLeaseRenewMs     = 3000; ///< 연장 · 잡기 시도 주기
        static constexpr int64  kNoServerGiveUpMs = 30000;
        static constexpr int64  kResultGraceMs    = 10000;
        static constexpr int32  kDefaultRating    = 1500;
        static constexpr uint64 kLobbyMatchBit    = 0x80000000ull; ///< 로비 경기 id 의 아래 32 비트 첫 비트(매처의 순번과 겹치지 않게)
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct MatchQueueCompletion
    {
        uint64            _requestTag{ 0 };
        uint64            _ticketID{ 0 };
        MatchmakingResult _result{ MatchmakingResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 하나에게 갈 결과입니다(이 서버에 없는 파티 회원 · 로비 회원도 — 바인딩이 접속 상태 창구로). */
    struct MatchQueueNotification
    {
        MatchAssignment _assignment{};
        AccountID       _recipientID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief 바인딩이 호스트에 구독 · 해지할 버스 주제입니다(권한을 얻고 잃을 때 바뀐다). */
    struct MatchQueueTopicChange
    {
        string _topic{};
        uint8  _bSubscribe{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MatchQueueService
     * @brief 매칭 대기열입니다.
     */
    class SW_GF_API MatchQueueService
    {
    public:
        MatchQueueService();
        ~MatchQueueService();

        MatchQueueService( const MatchQueueService& )            = delete;
        MatchQueueService& operator=( const MatchQueueService& ) = delete;

        void initialize( const MatchQueueDependencies& dependencies, const vector<MatchModeDefinition>& listMode );
        /** @brief 기다리던 캐시 요청을 취소합니다. 임대는 놓지 않는다(TTL 로 넘어간다). */
        void shutdown();
        /** @brief 임대 · 서버 목록 · 매처 · 배정 · 낸 표의 시한입니다. */
        void tick( int64 nowMs );

        void joinQueue( AccountID accountID, string_view modeID, string_view region, int64 nowMs, uint64 requestTag );
        void leaveQueue( AccountID accountID, uint64 requestTag );
        /** @brief 계정이 떠났다 — 그 계정이 든 표를 뺀다. */
        void removeAccount( AccountID accountID );
        /** @brief 파티 사람이 바뀌어 표가 깨졌다(`PartyLobbyService::drainBrokenTickets`) — 이 서버의 표면 빼고, 다른 서버의 표면 권한 · 낸 서버에 버스로 알린다. */
        void cancelTicket( uint64 ticketID );
        /** @brief 시작한 로비를 배정합니다. */
        void placeLobby( const LobbySnapshot& lobby, int64 nowMs );

        /** @brief 바인딩이 구독한 주제의 버스 메시지입니다. */
        void handleBusMessage( string_view topic, const vector<uint8>& bytes, int64 nowMs );
        void drainCompletions( vector<MatchQueueCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainNotifications( vector<MatchQueueNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        void drainTopicChanges( vector<MatchQueueTopicChange>& outListChange ) { _topicBuffer.drainTo( outListChange ); }

        bool  isAuthority( string_view modeID ) const;
        int32 getQueuedTicketCount( string_view modeID ) const; ///< 권한 서버의 매처 안
        int32 getLocalTicketCount() const { return static_cast<int32>( _mapLocalTicket.size() ); }
        bool  hasMode( string_view modeID ) const;

        static string makeLeaseKey( string_view modeID );
        static string makeQueueTopic( string_view modeID );
        static string makeCancelTopic( string_view modeID );
        static string makeResultTopic( uint64 serverID );
        static string makeAssignTopic( uint64 serverID );

    private:
        struct UnplacedMatch
        {
            MatchFormed _match{};
            int64       _formedMs{ 0 };
        };

        struct ModeState
        {
            MatchMaker                       _maker{};
            unique_ptr<ServerRegistryReader> _reader{};
            vector<UnplacedMatch>            _listUnplaced{}; ///< 자리가 없어 기다리는 경기
            int64                            _leaseAttemptMs{ 0 };
            uint64                           _leaseRequestID{ 0 };
            uint8                            _bAuthority{ SW_FALSE };
            uint8                            _bLeaseAttempted{ SW_FALSE };
        };

        struct LocalTicket
        {
            vector<AccountID> _listAccount{};
            string            _modeID{};
            uint64            _ticketID{ 0 };
            uint64            _partyID{ 0 };
            int64             _deadlineMs{ 0 };
        };

        struct PendingJoin
        {
            string    _modeID{};
            string    _region{};
            AccountID _accountID{ kInvalidAccountID };
            uint64    _requestTag{ 0 };
            int64     _nowMs{ 0 };
        };

        void               onPartyFound( uint64 lookupTag, MatchmakingResult result, const PartySnapshot& party );
        void               submitTicket( const PendingJoin& join, const vector<AccountID>& listAccount, uint64 partyID );
        void               tickLease( const string& modeID, ModeState& state, int64 nowMs );
        void               onLeaseReply( const EphemeralReply& reply );
        void               placeMatches( ModeState& state, int64 nowMs );
        [[nodiscard]] bool placeMatch( ModeState& state, const MatchFormed& match, MatchQueueOutcome failOutcome, int64 nowMs );
        void               deliverToTicket( uint64 originServerID, const MatchAssignment& assignment );
        void               notifyLocalTicket( uint64 ticketID, const MatchAssignment& assignment );
        void               removeFromQueue( const string& modeID, uint64 ticketID );
        ModeState*         findModeState( string_view modeID );
        int32              findRating( AccountID accountID, string_view modeID ) const;
        static void        fillTeam( const MatchFormed& match, AccountID accountID, MatchAssignment& inoutAssignment );

        unordered_map<string, MatchModeDefinition> _mapMode;
        unordered_map<string, ModeState>           _mapModeState;
        unordered_map<uint64, LocalTicket>         _mapLocalTicket; ///< 이 서버가 낸 표
        unordered_map<AccountID, uint64>           _mapAccountToTicket;
        unordered_map<uint64, PendingJoin>         _mapLookupToJoin; ///< 파티 찾기를 기다리는 줄 서기
        unordered_map<uint64, string>              _mapLeaseRequestToMode;
        EventBuffer<MatchQueueCompletion>          _completionBuffer;
        EventBuffer<MatchQueueNotification>        _notificationBuffer;
        EventBuffer<MatchQueueTopicChange>         _topicBuffer;
        MatchQueueDependencies                     _dependencies;
        uint64                                     _nextLookupTag;
        uint32                                     _sequence;
        uint32                                     _lobbySequence;
    };
} // namespace sw
