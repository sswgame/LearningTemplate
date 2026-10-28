/**
 * @file PartyLobbyService.h
 * @brief 파티와 로비를 관리합니다. 캐시 기록 하나씩을 비교 후 쓰기(`CacheRecordUpdater`)로 바꾸고, 멤버에게 갈 알림과 로비 시작 요청을 만듭니다. 전송은 모르며 서비스 스레드 하나에서 씁니다.
 * @details - 파티는 `mm/party/<id>` 에 저장하며 30분 시한이고 바뀔 때마다 연장한다.
 *            계정에서 파티로 가는 인덱스는 `mm/acct/<계정>` 이며 IfAbsent 로 써서 한 계정이 한 파티에만 들어가게 한다. 초대는 `mm/pinv/<계정>/<파티>` 에 저장하며 5분 시한이다.
 *          - 로비는 `mm/lobby/<id>` 에 저장하며 2시간 시한이다. 모드별 목록은 정렬 집합 `mm/lobbies/<모드>` 이고 점수는 만든 시각이라 새 로비부터 나온다.
 *            목록에서 만료로 사라진 로비는 목록을 읽을 때 지운다.
 *          - 서버 여러 대가 같은 캐시를 나눠 쓰면 어느 서버의 계정이든 같은 기록을 바꾼다. 주인 서버가 없으므로 서버가 죽어도 파티가 남는다.
 *          - 이 서버가 들여보낸 로비는 메모리에 적어 둔다. 계정이 떠나면 `removeAccount` 가 파티와 로비에서 뺀다.
 *
 *          EOS Lobbies, PlayFab Lobby(서비스 쪽 공유 상태와 소유자 넘김), Nakama Parties(파티장과 초대)와 같은 규칙이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/CacheRecordUpdater.h"

namespace sw
{
    struct EphemeralReply;
    struct EphemeralRequest;

    /** @brief 파티와 로비 연산 종류입니다. */
    enum class PartyLobbyOperation : uint8
    {
        PartyCreate = 0,
        PartyInvite,
        PartyAccept,
        PartyLeave,
        PartyKick,
        PartySetTicket, ///< 대기열이 쓰며 완료는 없다
        PartyFind,      ///< 대기열이 쓰며 결과는 델리게이트로 전달한다
        LobbyCreate,
        LobbyList,
        LobbyJoin,
        LobbyLeave,
        LobbyReady,
        LobbyStart,
        LobbyReopen ///< 배정에 실패했을 때 쓰며 완료는 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. 요청 태그가 0인 요청도 오며, 계정이 떠나서 서버가 낸 요청이 그렇습니다. */
    struct PartyLobbyCompletion
    {
        vector<LobbySnapshot> _listLobby{}; ///< LobbyList
        PartySnapshot         _party{};
        LobbySnapshot         _lobby{};
        uint64                _requestTag{ 0 };
        MatchmakingResult     _result{ MatchmakingResult::Ok };
        PartyLobbyOperation   _operation{ PartyLobbyOperation::PartyCreate };
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 하나에게 갈 알림입니다. `_pushKind` 가 무엇을 싣는지 정하며 파티 스냅샷, 로비 스냅샷, 초대 중 하나입니다. */
    struct PartyLobbyNotification
    {
        PartySnapshot _party{};
        LobbySnapshot _lobby{};
        PartyInvite   _invite{};
        AccountID     _recipientID{ kInvalidAccountID };
        uint16        _pushKind{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 로비가 시작됐음을 알립니다. 대기열 서비스의 전용 서버 배정이 받으며, 실패하면 `reopenLobby` 를 부릅니다. */
    struct LobbyStartRequest
    {
        LobbySnapshot _lobby{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class PartyLobbyService
     * @brief 파티와 로비 로직입니다.
     */
    class SW_KIT_SERVER_API PartyLobbyService
    {
    public:
        using PartyFoundDelegate = Delegate<void( uint64, MatchmakingResult, const PartySnapshot& )>; ///< 인자는 (요청 태그, 결과, 파티)이다

        /** @brief 요청 하나의 컨텍스트입니다. 캐시 단계와 바꾸기 객체가 가지고 다니며 키트 안에서만 씁니다. */
        struct Request
        {
            LobbySnapshot       _lobby{}; ///< LobbyCreate 의 요청이다
            PartyFoundDelegate  _onPartyFound{};
            string              _modeID{};
            AccountID           _actorID{ kInvalidAccountID };
            AccountID           _targetID{ kInvalidAccountID };
            uint64              _partyID{ 0 };
            uint64              _lobbyID{ 0 };
            uint64              _ticketID{ 0 };
            uint64              _requestTag{ 0 };
            UnixMilliseconds    _nowMs{ 0 };
            PartyLobbyOperation _operation{ PartyLobbyOperation::PartyCreate };
            uint8               _bReady{ SW_FALSE };
        };

        PartyLobbyService();
        ~PartyLobbyService();

        PartyLobbyService( const PartyLobbyService& )            = delete;
        PartyLobbyService& operator=( const PartyLobbyService& ) = delete;

        /**
         * @brief @p pRouter 는 빌려 씁니다. 새 파티와 로비의 id 는 @p serverID 를 32비트 왼쪽으로 민 값에 순번을 합친 것이며 서버끼리 겹치지 않습니다.
         * @details @p maxPartySize 가 [`PartyLobbyLimit::kMinPartySize`, `kMaxPartySize`] 밖이면 오류를 남기고 false 를 반환하며 라우터를 잡지 않는다.
         *          예전에는 범위 안으로 조용히 맞췄다(폴백 금지 원칙).
         */
        [[nodiscard]] bool initialize( EphemeralStoreRouter* pRouter, uint64 serverID, int32 maxPartySize = 4 );
        /** @brief 기다리던 캐시 요청을 취소합니다. 라우터보다 먼저 종료될 때 부릅니다. */
        void shutdown();

        // 파티
        void createParty( AccountID accountID, uint64 requestTag );
        void inviteToParty( AccountID leaderID, AccountID targetID, uint64 requestTag );
        void acceptPartyInvite( AccountID accountID, uint64 partyID, uint64 requestTag );
        void leaveParty( AccountID accountID, uint64 requestTag );
        void kickFromParty( AccountID leaderID, AccountID targetID, uint64 requestTag );
        /** @brief 파티의 대기열 티켓을 기록합니다. 0이면 대기열에서 빠진 것입니다. 완료는 없습니다. */
        void setPartyTicket( uint64 partyID, uint64 ticketID );
        /** @brief 계정의 파티를 찾습니다. 결과는 나중에 @p onFound( @p lookupTag, Ok 나 NotInParty 나 Unavailable, 파티 )로 한 번 전달합니다. */
        void findPartyOfAccount( AccountID accountID, uint64 lookupTag, const PartyFoundDelegate& onFound );

        // 로비
        /** @brief @p request 의 이름, 모드, 정원, 설정으로 로비를 만듭니다. 만든 사람이 방장입니다. */
        void createLobby( AccountID accountID, const LobbySnapshot& request, UnixMilliseconds nowMs, uint64 requestTag );
        void listLobbies( string_view modeID, uint64 requestTag );
        void joinLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag );
        void leaveLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag );
        void setLobbyReady( AccountID accountID, uint64 lobbyID, bool bReady, uint64 requestTag );
        void startLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag );
        /** @brief 시작한 로비를 다시 Open 으로 되돌립니다. 배정에 실패했을 때 부르며 완료는 없고 멤버에게 알립니다. */
        void reopenLobby( uint64 lobbyID );

        /** @brief 계정이 이 서버에서 떠났을 때 부릅니다. 파티와, 이 서버가 들여보낸 로비에서 뺍니다. */
        void removeAccount( AccountID accountID );

        void drainCompletions( vector<PartyLobbyCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainNotifications( vector<PartyLobbyNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        void drainLobbyStarts( vector<LobbyStartRequest>& outListStart ) { _lobbyStartBuffer.drainTo( outListStart ); }
        /** @brief 멤버가 바뀌어 대기열에서 빠져야 할 파티 티켓입니다. */
        void drainBrokenTickets( vector<uint64>& outListTicketID ) { _brokenTicketBuffer.drainTo( outListTicketID ); }

        int32 getPendingCount() const { return _updater.getPendingCount() + static_cast<int32>( _mapRequestToStep.size() ); }

        static string makePartyKey( uint64 partyID );
        static string makeAccountPartyKey( AccountID accountID );
        static string makeInviteKey( AccountID accountID, uint64 partyID );
        static string makeLobbyKey( uint64 lobbyID );
        static string makeLobbyListKey( string_view modeID );

        /** @brief 바꾸기 객체가 부릅니다. 키트 안에서만 씁니다. */
        void  finishParty( const Request& request, MatchmakingResult result, const PartySnapshot& party, AccountID removedID, uint64 brokenTicketID );
        void  finishLobby( const Request& request, MatchmakingResult result, const LobbySnapshot& lobby, AccountID removedID, bool bErased );
        int32 getMaxPartySize() const { return _maxPartySize; }

    private:
        enum class StepKind : uint8
        {
            IndexReserve = 0, ///< 계정 인덱스를 IfAbsent 로 쓴다. 파티 만들기와 초대 수락에서 쓴다
            IndexRead,        ///< 계정 인덱스를 읽는다. 초대, 떠나기, 내보내기, 찾기에서 쓴다
            InviteRead,       ///< 초대 키를 읽는다. 수락에서 쓴다
            PartyRead,        ///< 파티를 읽는다. 찾기에서 쓴다
            ListRange,        ///< 로비 목록 정렬 집합을 읽는다
            ListGet           ///< 목록의 로비 하나를 읽는다
        };

        struct PendingStep
        {
            Request  _request{};
            uint64   _listID{ 0 };
            int32    _slot{ 0 };
            StepKind _kind{ StepKind::IndexRead };
        };

        struct PendingList
        {
            vector<LobbySnapshot> _listLobby{};
            vector<uint8>         _listFound{};
            string                _modeID{};
            uint64                _requestTag{ 0 };
            int32                 _remainingCount{ 0 };
        };

        void   submitStep( const EphemeralRequest& cacheRequest, StepKind kind, const Request& request, uint64 listID = 0, int32 slot = 0 );
        void   submitFireAndForget( const EphemeralRequest& cacheRequest );
        void   onStepReply( const EphemeralReply& reply );
        void   handleIndexRead( const Request& request, const EphemeralReply& reply );
        void   handleListGet( uint64 listID, int32 slot, const EphemeralReply& reply );
        void   startPartyMutation( const Request& request );
        void   startLobbyMutation( const Request& request );
        void   refreshIndexTTL( const PartySnapshot& party );
        void   notifyParty( const PartySnapshot& party, AccountID removedID );
        void   notifyLobby( const LobbySnapshot& lobby, AccountID removedID );
        void   completeParty( const Request& request, MatchmakingResult result, const PartySnapshot& party );
        void   completeLobby( const Request& request, MatchmakingResult result, const LobbySnapshot& lobby );
        uint64 allocateID() { return ( _serverID << 32 ) | ++_sequence; }

        CacheRecordUpdater                  _updater;
        unordered_map<uint64, PendingStep>  _mapRequestToStep;
        unordered_map<uint64, PendingList>  _mapListIDToList;
        unordered_map<AccountID, uint64>    _mapAccountToLobby; ///< 이 서버가 들여보낸 로비이다
        EventBuffer<PartyLobbyCompletion>   _completionBuffer;
        EventBuffer<PartyLobbyNotification> _notificationBuffer;
        EventBuffer<LobbyStartRequest>      _lobbyStartBuffer;
        EventBuffer<uint64>                 _brokenTicketBuffer;
        EphemeralStoreRouter*               _pRouter;
        Subscription                        _replyOwner; ///< 라우터 답의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        uint64                              _serverID;
        IDAllocator<uint64>                 _lobbyListIDAllocator;
        int32                               _maxPartySize;
        uint32                              _sequence;
    };
} // namespace sw
