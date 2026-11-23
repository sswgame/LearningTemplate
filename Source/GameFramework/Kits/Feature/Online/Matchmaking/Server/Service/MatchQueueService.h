/**
 * @file MatchQueueService.h
 * @brief 매칭 대기열입니다. 모드마다 캐시 임대를 잡은 서버 하나가 매처를 돌리고, 만들어진 경기를 전용 서버에 배정해 게임 서버와 티켓을 낸 서버에 버스로 알립니다. 전송은 모르며 서비스 스레드 하나에서 씁니다.
 * @details - 권한: 모드마다 임대 `mm/lease/<모드>`(값은 서버 id, 10초)를 없을 때만 잡는다. 잡은 서버는 3초마다 자기 값일 때만 연장한다. 버스가 없으면(서버 한 대) 항상 권한을 가진다.
 *          - 백필: 게임 서버가 빈 슬롯(`MatchBackfillRequest`, 버스 `mm.backfill.<모드>`)를 보내면 권한 매처가 새 경기보다 먼저 채운다. 채운 사람은 같은 경기 id 의 배정으로
 *            게임 서버(배정 주제)와 티켓을 낸 서버(결과 주제)에 알린다.
 *          - 티켓 흐름: 계정이 접속한 서버가 티켓을 만든다. 파티장이면 파티 전체의 티켓이며 실력은 모드의 실력 통계를 순위표 키트의 통계 창(`IAccountStatSource`)에서 읽는다.
 *            메모리에 없는 계정(다른 서버의 파티원, 막 접속한 사람)은 올려 달라고 한 뒤 3초까지 기다리고, 그래도 없으면 RatingUnavailable 이다.
 *            권한이 있으면 바로 매처에 넣고, 없으면 버스 `mm.queue.<모드>` 로 보낸다. 빠지기는 `mm.cancel.<모드>` 로 보낸다.
 *            다른 서버가 낸 파티 티켓의 빠지기는 낸 서버에도 `mm.origincancel.<서버>` 로 보낸다 — 파티 기록에 잡혔지만 아직 대기열에 넣지 않은 티켓은 낸 서버만 안다.
 *          - 배정: 경기가 만들어지면 기반 계층의 `ServerRegistryReader` 로 모드의 서버 종류, 경기 지역, 인원 슬롯에 맞는 서버를 고른다.
 *            게임 서버에는 명단을 캐시 기록 `mm/match/<서버>/<경기>`(수명은 모드의 경기 시한, 목록은 정렬 집합 `mm/matches/<서버>`, 점수는 끝나는 시각)으로 남기고
 *            `mm.assign.<서버>` 로도 알린다. 버스 배정과 Found 는 기록 · 목록 쓰기가 Ok 를 받은 뒤에 나가고, 쓰기가 지면 NoServer 로 끝난다. 버스는 "최대 한 번" 이라 잃을 수 있으므로 게임 서버(`MatchServerAgent`)는 기록을 주기적으로 읽어 수락을 판정한다(Agones Allocation · PlayFab MPS 의 영속 배정).
 *            티켓을 낸 서버에는 `mm.result.<서버>` 로 결과를 보낸다. 결과를 받은 서버가 멤버에게 알린다.
 *            배정할 서버가 없으면 경기를 쥐고 기다렸다가 다시 시도하고, 30초를 넘기면 NoServer 로 끝낸다.
 *          - 권한이 넘어가면 대기 중인 티켓을 잃는다. 티켓을 낸 서버가 티켓마다 시한(모드 시한 + 30초 + 10초)을 보관하다가 결과가 없으면 Timeout 을 알리고, 클라이언트가 다시 줄을 선다.
 *          - 로비 시작도 같은 배정 경로를 쓴다. 팀은 로비의 팀 그대로이고, 배정할 서버가 없으면 바로 NoServer 로 끝내고 로비를 다시 연다.
 *
 *          Nakama 의 메모리 매처, OpenMatch 의 디렉터와 할당기, PlayFab 의 Matchmaking 에서 Multiplayer Servers 로 이어지는 흐름에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchMaker.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Directory/ServerRegistryReader.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;
    class IAccountStatSource;
    class PartyLobbyService;
    class ServerBus;
    class ServerBusMessage;
} // namespace sw

namespace sw
{
    /** @brief 빌려 쓰는 객체들입니다. 대기열보다 오래 살아야 합니다. */
    struct MatchQueueDependencies
    {
        EphemeralStoreRouter* _pRouter{ nullptr };     ///< 필수이다. 임대와 서버 목록에 쓴다
        ServerBus*            _pBus{ nullptr };        ///< 서버가 한 대이면 null 이며 이 서버가 항상 권한을 가진다
        PartyLobbyService*    _pPartyLobby{ nullptr }; ///< 파티 티켓과 로비 다시 열기에 쓴다. 없으면 혼자 티켓만 다룬다
        IAccountStatSource*   _pStatSource{ nullptr }; ///< 실력 모드(`_ratingStat` 이 있는 모드)가 있으면 필수이다. 클라이언트는 실력을 보내지 않는다
        uint64                _serverID{ 1 };          ///< 32비트 안의 값이다. 티켓과 경기 id 의 위쪽 32비트가 된다
        int64                 _registryRefreshPeriodMs{ 2000 };
        int64                 _cancelConfirmWaitMs{ 0 };  ///< 필수 — 빠지기가 권한 서버의 확인 답을 기다리는 시간(키트 설정 `_cancelConfirmWaitMs`). 넘으면 권한이 없는 것으로 보고 끝낸다
        int64                 _seatReservationTTLMs{ 0 }; ///< 필수 — 서버 사이의 좌석 예약 창(서버 설정 `_seatReservationSeconds`). 0 이면 `initialize` 가 거절한다
    };
} // namespace sw

namespace sw
{
    /** @brief 대기열 상수입니다. 다른 모듈이 값으로 읽으므로 내보내는 클래스 밖에 둡니다. */
    struct MatchQueueLimit
    {
        static constexpr int64 kLeaseTTLMs       = 10000;
        static constexpr int64 kLeaseRenewMs     = 3000; ///< 임대를 연장하거나 잡아 보는 주기이다
        static constexpr int64 kNoServerGiveUpMs = 30000;
        static constexpr int64 kResultGraceMs    = 10000;
        static constexpr int64 kRatingWaitMs     = 3000; ///< 줄 서는 사람의 통계를 읽기를 기다리는 시간이다. 넘으면 RatingUnavailable 이다
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct MatchQueueCompletion
    {
        uint64                   _requestTag{ 0 };
        uint64                   _ticketID{ 0 };
        MatchmakingServiceResult _result{ MatchmakingResult::Ok };
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
    class SW_KIT_SERVER_API MatchQueueService
    {
    public:
        MatchQueueService();
        ~MatchQueueService();

        MatchQueueService( const MatchQueueService& )            = delete;
        MatchQueueService& operator=( const MatchQueueService& ) = delete;

        /** @brief 실력 모드가 있는데 통계 창이 없으면 오류 로그와 false 입니다(기본 실력으로 조용히 돌지 않는다). */
        [[nodiscard]] bool initialize( const MatchQueueDependencies& dependencies, const vector<MatchModeDef>& listMode );
        /** @brief 기다리던 캐시 요청을 취소합니다. 임대는 놓지 않으며 TTL 이 지나면 넘어갑니다. */
        void shutdown();
        /** @brief 임대, 서버 목록, 매처, 배정, 낸 티켓의 시한을 처리합니다. */
        void update( UnixMilliseconds nowMs );

        void joinQueue( AccountID accountID, string_view modeID, string_view region, UnixMilliseconds nowMs, uint64 requestTag );
        /** @brief 이 서버에 티켓이 없으면 파티 기록의 티켓을 풉니다 — 티켓을 낸 서버가 죽어 다른 서버로 붙은 파티가 "줄 섬" 에 갇히지 않게 한다(감사 O-7). */
        void leaveQueue( AccountID accountID, uint64 requestTag );
        /** @brief 계정이 떠났을 때 부릅니다. 그 계정이 포함된 티켓을 뺍니다. */
        void removeAccount( AccountID accountID );
        /** @brief 파티 멤버가 바뀌어 티켓이 깨졌을 때 부릅니다(`PartyLobbyService::pollBrokenTickets`). 이 서버의 티켓이면 빼고, 다른 서버의 티켓이면 권한 서버와 티켓을 낸 서버에 버스로 알립니다. */
        void cancelTicket( uint64 ticketID );
        /** @brief 시작한 로비를 전용 서버에 배정합니다. */
        void placeLobby( const LobbySnapshot& lobby, UnixMilliseconds nowMs );

        /** @brief 바인딩이 구독한 주제로 온 버스 메시지를 처리합니다. */
        void handleBusMessage( const ServerBusMessage& message, UnixMilliseconds nowMs );
        /**
         * @brief 경기 중 빈 슬롯을 맡깁니다. 이 서버가 그 모드의 권한이면 매처에 바로 넣고, 아니면 버스로 권한 서버에 보냅니다.
         * @details 모르는 모드는 UnknownMode, 모드의 모양(`MatchMaker::isBackfillShapeValid`)에 맞지 않으면 kInvalidRequest 이다 — 버스로 보내기 전에 확인해 권한 서버에서 조용히 버려지지 않는다.
         *          게임 서버는 `MatchBackfillSender`(서버 정보를 자기 설명에서 채운다)로 부르고, 버스로 온 요청과 테스트가 직접 부릅니다.
         */
        [[nodiscard]] MatchmakingServiceResult submitBackfill( const MatchBackfillRequest& request );
        void                                   takeCompletions( vector<MatchQueueCompletion>& outListCompletion ) { _completionBuffer.moveAllTo( outListCompletion ); }
        void                                   pollNotifications( vector<MatchQueueNotification>& outListNotification ) { _notificationBuffer.moveAllTo( outListNotification ); }
        void                                   pollTopicChanges( vector<MatchQueueTopicChange>& outListChange ) { _topicBuffer.moveAllTo( outListChange ); }

        bool  isAuthority( string_view modeID ) const;
        int32 getQueuedTicketCount( string_view modeID ) const; ///< 권한 서버의 매처 안에 있는 티켓 수이다
        int32 getLocalTicketCount() const { return static_cast<int32>( _mapLocalTicket.size() ); }
        bool  hasMode( string_view modeID ) const;

        static string makeLeaseKey( string_view modeID );
        static string makeQueueTopic( string_view modeID );
        static string makeCancelTopic( string_view modeID );
        static string makeBackfillTopic( string_view modeID );
        static string makeResultTopic( uint64 serverID );
        static string makeOriginCancelTopic( uint64 serverID );
        static string makeAssignTopic( uint64 serverID );
        /** @brief 게임 서버 하나에 배정된 명단 기록의 키입니다. @p recordName 은 경기 id 16진이고, 백필은 뒤에 `.<티켓 id 16진>` 이 붙습니다. */
        static string makeMatchRecordKey( uint64 serverID, string_view recordName );
        /** @brief 게임 서버 하나의 명단 기록 목록(정렬 집합, 멤버는 기록 이름, 점수는 끝나는 시각)의 키입니다. */
        static string makeMatchListKey( uint64 serverID );

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
            vector<UnplacedMatch>            _listUnplaced{}; ///< 배정할 서버가 없어 기다리는 경기이다
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
            int64             _cancelDeadlineMs{ 0 }; ///< 빠지기를 보낸 뒤 권한 서버의 답을 기다리는 시한(0 이면 빠지는 중이 아니다)
            vector<uint64>    _listLeaveRequestTag{}; ///< 이 티켓을 뺀 `leaveQueue` 요청들이다(파티원마다 낼 수 있다). 끝날 때(Cancelled · Found · Timeout) 모두 답한다
        };

        /** @brief 이 서버가 낸 티켓을 빼 달라고 권한 서버에 맡깁니다. 확인 답(Cancelled)이 와야 끝나며, 그 전에 Found 가 오면 경기가 이긴다(감사 O-18). */
        void requestCancel( uint64 ticketID, uint64 leaveRequestTag );
        /** @brief 이 서버가 낸 티켓을 뺍니다. 넣은 티켓은 권한 서버의 확인을 기다리고, 파티 기록의 답을 기다리는 티켓은 NotQueued 로 끝냅니다. 이 서버의 티켓이 아니면 false 입니다. */
        [[nodiscard]] bool cancelOwnTicket( uint64 ticketID );
        /** @brief 권한 서버가 티켓을 매처에서 실제로 뺐으면 낸 서버에 Cancelled 를 알립니다. 이미 짝지어졌으면 아무것도 하지 않습니다(Found 가 간다). */
        void confirmCancel( ModeState& state, uint64 ticketID );

        struct PendingJoin
        {
            string           _modeID{};
            string           _region{};
            AccountID        _accountID{ kInvalidAccountID };
            uint64           _requestTag{ 0 };
            UnixMilliseconds _nowMs{ 0 };
        };

        /** @brief 파티 찾기를 기다리는 빠지기입니다. 이 서버에 티켓이 없는 계정의 파티 기록 티켓을 풀려고 읽습니다. */
        struct LeaveRequest
        {
            AccountID _accountID{ kInvalidAccountID };
            uint64    _requestTag{ 0 };
        };

        /** @brief 통계를 읽기를 기다리는 줄 서기입니다. 다른 서버의 파티원은 이 서버 메모리에 통계가 없습니다. */
        struct RatingWait
        {
            PendingJoin       _join{};
            vector<AccountID> _listAccount{};
            vector<AccountID> _listRetained{}; ///< 여기서 올려 달라고 한 계정이다. 끝나면 내린다
            uint64            _partyID{ 0 };
        };

        /** @brief 파티 기록에 티켓 잡기를 기다리는 줄 서기입니다. 잡은 뒤에만 티켓이 대기열에 들어갑니다. */
        struct PartyReservation
        {
            PendingJoin _join{};
            MatchTicket _ticket{};
            uint8       _bDropped{ SW_FALSE }; ///< 기다리는 동안 빠졌다. 잡히면 바로 푼다
        };

        /** @brief 명단 기록 쓰기를 기다리는 결과 하나입니다. `_recipientID` 가 있으면(로비 멤버) 그 계정에 바로 알리고, 없으면 티켓을 낸 서버로 보냅니다. */
        struct PendingDelivery
        {
            MatchAssignment _assignment{};
            uint64          _originServerID{ 0 };
            AccountID       _recipientID{ kInvalidAccountID };
        };

        /** @brief 명단 기록 쓰기를 기다리는 배정입니다. */
        struct PendingPlacement
        {
            MatchFormed             _match{};
            vector<PendingDelivery> _listDelivery{};
            string                  _recordName{};  ///< 경기 id 16진이다. 백필은 뒤에 `.<티켓 id 16진>` 이 붙는다
            uint64                  _serverID{ 0 }; ///< 배정한 게임 서버이다
            uint64                  _lobbyID{ 0 };  ///< 로비 경기이면 쓰기가 질 때 로비를 다시 연다. 0 이면 로비가 아니다
            int64                   _expiresMs{ 0 };
        };

        /** @brief 티켓과 경기(모드 · 로비 모두)의 새 id 입니다. 위 32 비트가 이 서버, 아래가 순번 하나라 모드 · 로비끼리, 재시작 앞뒤로 겹치지 않는다(명단 기록의 이름이 경기 id 다). */
        uint64 allocateID();
        void   onPartyFound( uint64 lookupTag, MatchmakingServiceResult result, const PartySnapshot& party );
        /** @brief 파티 기록에 남은 다른 서버의 티켓을 권한 매처와 낸 서버에서 빼고 기록에서 지웁니다. */
        void onLeavePartyFound( uint64 lookupTag, MatchmakingServiceResult result, const PartySnapshot& party );
        /** @brief 통계가 모두 있으면 티켓을 내고, 아니면 올려 달라고 한 뒤 기다립니다. */
        void prepareTicket( const PendingJoin& join, const vector<AccountID>& listAccount, uint64 partyID );
        /** @brief 다 읽었거나 시한이 지난 줄 서기의 티켓을 냅니다. 시한이 지났으면 `submitTicket` 이 RatingUnavailable 로 끝냅니다. */
        void               updateRatingWaits( UnixMilliseconds nowMs );
        [[nodiscard]] bool isRatingLoaded( const MatchModeDef& mode, AccountID accountID ) const;
        void               releaseRetained( const RatingWait& wait );
        [[nodiscard]] bool isJoinPending( AccountID accountID ) const;
        /** @brief 파티 찾기 · 통계 읽기 · 파티 기록 잡기를 기다리는, 그 계정이 든 줄 서기를 NotQueued 로 끝냅니다. 있었으면 true 입니다. */
        [[nodiscard]] bool dropPendingJoin( AccountID accountID );
        /** @brief 파티 기록의 답을 기다리는 티켓을 버립니다. 요청은 NotQueued 로 한 번만 끝나고, 답이 오면 기록을 풉니다. */
        void dropReservation( PartyReservation& reservation );
        /** @brief 티켓을 만듭니다. 파티면 파티 기록에 먼저 잡고(`onPartyTicketReserved`), 혼자면 바로 대기열에 넣습니다. */
        void submitTicket( const PendingJoin& join, const vector<AccountID>& listAccount, uint64 partyID );
        void onPartyTicketReserved( uint64 ticketID, MatchmakingServiceResult result, const PartySnapshot& party );
        void enqueueTicket( const PendingJoin& join, const MatchTicket& ticket );
        /** @brief 백필로 채운 티켓을 게임 서버(배정)와 티켓을 낸 서버(결과)에 알립니다. */
        void deliverBackfill( const MatchBackfilled& backfilled, UnixMilliseconds nowMs );
        /**
         * @brief 배정한 명단을 캐시 기록으로 남기고, 기록 · 목록 쓰기가 모두 Ok 를 받은 뒤에야 버스 배정과 Found 를 냅니다(감사 O-6).
         * @details 게임 서버는 기록으로 수락을 판정하므로, 쓰지 못한 배정에 Found 를 내면 그 사람은 접속에서 거절된다. 쓰기가 지면 모두 NoServer 를 받고 다시 줄을 선다.
         */
        void storeMatchRecord( PendingPlacement placement );
        void onMatchRecordReply( const EphemeralReply& reply );
        void onMatchListReply( const EphemeralReply& reply );
        /** @brief 기록이 남은 배정의 버스 배정과 Found 를 냅니다. */
        void completePlacement( const PendingPlacement& placement );
        /** @brief 기록을 남기지 못한 배정을 NoServer 로 끝냅니다. 로비 경기이면 로비를 다시 엽니다. */
        void               failPlacement( const PendingPlacement& placement );
        void               deliverPending( const PendingDelivery& delivery );
        void               updateLease( const string& modeID, ModeState& state, UnixMilliseconds nowMs );
        void               onLeaseReply( const EphemeralReply& reply );
        void               placeMatches( ModeState& state, UnixMilliseconds nowMs );
        [[nodiscard]] bool placeMatch( ModeState& state, const MatchFormed& match, MatchQueueResult failResult, UnixMilliseconds nowMs );
        void               deliverToTicket( uint64 originServerID, const MatchAssignment& assignment );
        void               notifyLocalTicket( uint64 ticketID, const MatchAssignment& assignment );
        void               removeFromQueue( const string& modeID, uint64 ticketID );
        ModeState*         findModeState( string_view modeID );
        /** @brief 모드의 실력입니다. 실력 모드인데 이 서버가 그 계정의 통계를 아직 읽지 않았으면 false 입니다. */
        [[nodiscard]] bool findRating( AccountID accountID, const MatchModeDef& mode, int32& outRating ) const;
        static void        fillTeam( const MatchFormed& match, AccountID accountID, MatchAssignment& inoutAssignment );

    private:
        unordered_map<string, MatchModeDef>     _mapMode;
        unordered_map<string, ModeState>        _mapModeState;
        unordered_map<uint64, LocalTicket>      _mapLocalTicket; ///< 이 서버가 낸 티켓이다
        unordered_map<AccountID, uint64>        _mapAccountToTicket;
        unordered_map<uint64, PendingJoin>      _mapLookupToJoin;  ///< 파티 찾기를 기다리는 줄 서기 요청이다
        unordered_map<uint64, LeaveRequest>     _mapLookupToLeave; ///< 파티 찾기를 기다리는 빠지기 요청이다
        vector<RatingWait>                      _listRatingWait;
        unordered_map<uint64, PartyReservation> _mapTicketToReservation; ///< 파티 기록에 잡히기를 기다리는 티켓이다
        unordered_map<uint64, string>           _mapLeaseRequestToMode;
        unordered_map<uint64, PendingPlacement> _mapRecordWrite; ///< 명단 기록 쓰기의 답을 기다리는 배정이다(요청 id 열쇠)
        unordered_map<uint64, PendingPlacement> _mapListWrite;   ///< 기록은 남았고 목록 쓰기의 답을 기다리는 배정이다(요청 id 열쇠)
        EventBuffer<MatchQueueCompletion>       _completionBuffer;
        EventBuffer<MatchQueueNotification>     _notificationBuffer;
        EventBuffer<MatchQueueTopicChange>      _topicBuffer;
        MatchQueueDependencies                  _dependencies;
        Subscription                            _replyOwner; ///< 라우터 답의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        uint64                                  _nextLookupTag;
        int64                                   _nowMs;    ///< 마지막 `update` 의 시각이다(빠지기의 확인 시한을 센다)
        uint32                                  _sequence; ///< `allocateID` 의 순번이다
    };
} // namespace sw

namespace sw
{
    /** @brief 매칭의 서버 버스 주제입니다. 내보내는 클래스의 정적 멤버로 두지 않습니다(지연 로드 모듈은 데이터를 가져올 수 없습니다). */
    struct MatchQueueBus
    {
        static constexpr ServerBusTopicFamily<MatchTicket>          kQueueTopic{ "mm.queue." };               ///< 뒤에 모드 id. 권한 서버로 가는 티켓이다
        static constexpr ServerBusTopicFamily<MatchTicketCancel>    kCancelTopic{ "mm.cancel." };             ///< 뒤에 모드 id. 권한 서버로 가는 티켓 취소이다
        static constexpr ServerBusTopicFamily<MatchTicketCancel>    kOriginCancelTopic{ "mm.origincancel." }; ///< 뒤에 서버 16진. 티켓을 낸 서버로 가는 다른 서버의 취소이다
        static constexpr ServerBusTopicFamily<MatchAssignment>      kResultTopic{ "mm.result." };             ///< 뒤에 서버 16진. 티켓을 낸 서버로 가는 결과이다
        static constexpr ServerBusTopicFamily<MatchFormed>          kAssignTopic{ "mm.assign." };             ///< 뒤에 게임 서버 16진. 게임 서버로 가는 접속 예정자 목록이다
        static constexpr ServerBusTopicFamily<MatchBackfillRequest> kBackfillTopic{ "mm.backfill." };         ///< 뒤에 모드 id. 게임 서버가 권한 서버로 보내는 경기 중 빈 슬롯이다
    };
} // namespace sw
