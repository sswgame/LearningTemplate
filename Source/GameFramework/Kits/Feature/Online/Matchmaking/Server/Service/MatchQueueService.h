/**
 * @file MatchQueueService.h
 * @brief 매칭 대기열입니다. 모드마다 캐시 임대를 잡은 서버 하나가 매처를 돌리고, 만들어진 경기를 전용 서버에 배정해 게임 서버와 티켓을 낸 서버에 버스로 알립니다. 전송은 모르며 서비스 스레드 하나에서 씁니다.
 * @details - 권한: 모드마다 임대 `mm/lease/<모드>`(값은 서버 id, 10초)를 없을 때만 잡는다. 잡은 서버는 3초마다 자기 값일 때만 연장한다. 버스가 없으면(서버 한 대) 항상 권한을 가진다.
 *          - 티켓 흐름: 계정이 접속한 서버가 티켓을 만든다. 파티장이면 파티 전체의 티켓이며 실력은 `IMatchRatingSource` 에서 얻는다.
 *            권한이 있으면 바로 매처에 넣고, 없으면 버스 `mm.queue.<모드>` 로 보낸다. 빠지기는 `mm.cancel.<모드>` 로 보낸다.
 *          - 배정: 경기가 만들어지면 기반 계층의 `ServerRegistryReader` 로 모드의 서버 종류, 경기 지역, 인원 슬롯에 맞는 서버를 고른다.
 *            게임 서버에는 `mm.assign.<서버>`(접속할 사람 목록)로, 티켓을 낸 서버에는 `mm.result.<서버>` 로 결과를 보낸다. 결과를 받은 서버가 멤버에게 알린다.
 *            자리가 없으면 경기를 쥐고 기다렸다가 다시 시도하고, 30초를 넘기면 NoServer 로 끝낸다.
 *          - 권한이 넘어가면 대기 중인 티켓을 잃는다. 티켓을 낸 서버가 티켓마다 시한(모드 시한 + 30초 + 10초)을 보관하다가 결과가 없으면 Timeout 을 알리고, 클라이언트가 다시 줄을 선다.
 *          - 로비 시작도 같은 배정 경로를 쓴다. 팀은 로비의 팀 그대로이고, 자리가 없으면 바로 NoServer 로 끝내고 로비를 다시 연다.
 *
 *          Nakama 의 메모리 매처, OpenMatch 의 디렉터와 할당기, PlayFab 의 Matchmaking 에서 Multiplayer Servers 로 이어지는 흐름에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Directory/ServerRegistryReader.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchMaker.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;
    class IServerBus;
    class PartyLobbyService;

    /**
     * @class IMatchRatingSource
     * @brief 게임 조립이 제공하는 실력 값입니다. 순위표 통계나 게임 자체 MMR 이 해당합니다. 서비스 스레드에서 부르므로 메모리 조회여야 하며, 클라이언트는 실력을 보내지 않습니다.
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
    /** @brief 빌려 쓰는 객체들입니다. 대기열보다 오래 살아야 합니다. */
    struct MatchQueueDependencies
    {
        EphemeralStoreRouter*     _pRouter{ nullptr };       ///< 필수이다. 임대와 서버 목록에 쓴다
        IServerBus*               _pBus{ nullptr };          ///< 서버가 한 대이면 null 이며 이 서버가 항상 권한을 가진다
        PartyLobbyService*        _pPartyLobby{ nullptr };   ///< 파티 티켓과 로비 다시 열기에 쓴다. 없으면 혼자 티켓만 다룬다
        const IMatchRatingSource* _pRatingSource{ nullptr }; ///< 없으면 모두 `MatchQueueLimit::kDefaultRating`
        uint64                    _serverID{ 1 };            ///< 32비트 안의 값이다. 티켓과 경기 id 의 위쪽 32비트가 된다
        int64                     _registryRefreshPeriodMs{ 2000 };
    };
} // namespace sw

namespace sw
{
    /** @brief 대기열 상수입니다. 다른 모듈이 값으로 읽으므로 내보내는 클래스 밖에 둡니다. */
    struct MatchQueueLimit
    {
        static constexpr int64  kLeaseTtlMs       = 10000;
        static constexpr int64  kLeaseRenewMs     = 3000; ///< 임대를 연장하거나 잡아 보는 주기이다
        static constexpr int64  kNoServerGiveUpMs = 30000;
        static constexpr int64  kResultGraceMs    = 10000;
        static constexpr int32  kDefaultRating    = 1500;
        static constexpr uint64 kLobbyMatchBit    = 0x80000000ull; ///< 로비 경기 id 의 아래쪽 32비트 중 첫 비트이다. 매처의 순번과 겹치지 않게 한다
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
    /** @brief 계정 하나에게 갈 결과입니다. 이 서버에 접속하지 않은 파티 멤버와 로비 멤버도 포함하며, 바인딩이 접속 상태 관리자를 통해 전달합니다. */
    struct MatchQueueNotification
    {
        MatchAssignment _assignment{};
        AccountID       _recipientID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief 바인딩이 호스트에 구독하거나 해지할 버스 주제입니다. 권한을 얻거나 잃을 때 바뀝니다. */
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
     * @brief 매칭 대기열 서비스입니다.
     */
    class SW_KIT_API MatchQueueService
    {
    public:
        MatchQueueService();
        ~MatchQueueService();

        MatchQueueService( const MatchQueueService& )            = delete;
        MatchQueueService& operator=( const MatchQueueService& ) = delete;

        void initialize( const MatchQueueDependencies& dependencies, const vector<MatchModeDefinition>& listMode );
        /** @brief 기다리던 캐시 요청을 취소합니다. 임대는 놓지 않으며 TTL 이 지나면 넘어갑니다. */
        void shutdown();
        /** @brief 임대, 서버 목록, 매처, 배정, 낸 티켓의 시한을 처리합니다. */
        void tick( int64 nowMs );

        void joinQueue( AccountID accountID, string_view modeID, string_view region, int64 nowMs, uint64 requestTag );
        void leaveQueue( AccountID accountID, uint64 requestTag );
        /** @brief 계정이 떠났을 때 부릅니다. 그 계정이 든 티켓을 뺍니다. */
        void removeAccount( AccountID accountID );
        /** @brief 파티 멤버가 바뀌어 티켓이 깨졌을 때 부릅니다(`PartyLobbyService::drainBrokenTickets`). 이 서버의 티켓이면 빼고, 다른 서버의 티켓이면 권한 서버와 티켓을 낸 서버에 버스로 알립니다. */
        void cancelTicket( uint64 ticketID );
        /** @brief 시작한 로비를 전용 서버에 배정합니다. */
        void placeLobby( const LobbySnapshot& lobby, int64 nowMs );

        /** @brief 바인딩이 구독한 주제로 온 버스 메시지를 처리합니다. */
        void handleBusMessage( string_view topic, const vector<uint8>& bytes, int64 nowMs );
        void drainCompletions( vector<MatchQueueCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainNotifications( vector<MatchQueueNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        void drainTopicChanges( vector<MatchQueueTopicChange>& outListChange ) { _topicBuffer.drainTo( outListChange ); }

        bool  isAuthority( string_view modeID ) const;
        int32 getQueuedTicketCount( string_view modeID ) const; ///< 권한 서버의 매처 안에 있는 티켓 수이다
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
            vector<UnplacedMatch>            _listUnplaced{}; ///< 서버 자리가 없어 기다리는 경기이다
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
        unordered_map<uint64, LocalTicket>         _mapLocalTicket; ///< 이 서버가 낸 티켓이다
        unordered_map<AccountID, uint64>           _mapAccountToTicket;
        unordered_map<uint64, PendingJoin>         _mapLookupToJoin; ///< 파티 찾기를 기다리는 줄 서기 요청이다
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
