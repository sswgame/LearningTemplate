/**
 * @file PartyLobbyService.h
 * @brief 파티 · 로비 — 캐시 기록 하나씩을 비교 후 쓰기(`CacheRecordUpdater`)로 바꾸고, 회원에게 갈 알림 · 로비 시작 요청을 냅니다. 전송을 모른다(서비스 스레드 하나).
 * @details - 파티 `mm/party/<id>`(30 분, 바뀔 때마다 연장) · 계정 → 파티 색인 `mm/acct/<계정>`(IfAbsent — 한 계정 한 파티) · 초대 `mm/pinv/<계정>/<파티>`(5 분).
 *          - 로비 `mm/lobby/<id>`(2 시간) + 모드별 목록 정렬 집합 `mm/lobbies/<모드>`(점수 = 만든 시각 — 새것부터). 목록에서 사라진 로비(만료)는 목록을 읽을 때 지운다.
 *          - 서버 여럿이 같은 캐시를 나눠 쓰면 어느 서버의 계정이든 같은 기록을 바꾼다(주인 서버가 없다 — 서버가 죽어도 파티가 남는다).
 *          - 이 서버가 들여보낸 로비는 메모리에 적어 두고(`removeAccount` — 계정이 떠나면 파티 · 로비에서 뺀다).
 *          EOS Lobbies · PlayFab Lobby(서비스 쪽 공유 상태 · 소유자 넘김), Nakama Parties(장 · 초대)와 같은 규칙이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/CacheRecordUpdater.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingProtocol.h"

namespace sw
{
    struct EphemeralReply;
    struct EphemeralRequest;

    /** @brief 파티 · 로비 연산입니다. */
    enum class PartyLobbyOperation : uint8
    {
        PartyCreate = 0,
        PartyInvite,
        PartyAccept,
        PartyLeave,
        PartyKick,
        PartySetTicket, ///< 대기열이 쓴다 — 완료 없음
        PartyFind,      ///< 대기열이 쓴다 — 델리게이트로
        LobbyCreate,
        LobbyList,
        LobbyJoin,
        LobbyLeave,
        LobbyReady,
        LobbyStart,
        LobbyReopen ///< 배정 실패 — 완료 없음
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다(꼬리표 0 인 요청 — 계정이 떠나 서버가 낸 것 — 도 온다). */
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
    /** @brief 계정 하나에게 갈 알림입니다(`_pushKind` 가 무엇을 싣는지 정한다 — 파티 · 로비 스냅숏 또는 초대). */
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
    /** @brief 로비가 시작했다 — 대기열 서비스의 전용 서버 배정이 받는다(실패하면 `reopenLobby`). */
    struct LobbyStartRequest
    {
        LobbySnapshot _lobby{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class PartyLobbyService
     * @brief 파티 · 로비 로직입니다.
     */
    class SW_GF_API PartyLobbyService
    {
    public:
        using PartyFoundDelegate = Delegate<void( uint64, MatchmakingResult, const PartySnapshot& )>; ///< (꼬리표, 결과, 파티)

        /** @brief 요청 하나의 문맥입니다(캐시 단계 · 바꾸기 객체가 들고 다닌다 — 키트 안). */
        struct Request
        {
            LobbySnapshot       _lobby{}; ///< LobbyCreate 의 요청
            PartyFoundDelegate  _onPartyFound{};
            string              _modeID{};
            AccountID           _actorID{ kInvalidAccountID };
            AccountID           _targetID{ kInvalidAccountID };
            uint64              _partyID{ 0 };
            uint64              _lobbyID{ 0 };
            uint64              _ticketID{ 0 };
            uint64              _requestTag{ 0 };
            int64               _nowMs{ 0 };
            PartyLobbyOperation _operation{ PartyLobbyOperation::PartyCreate };
            uint8               _bReady{ SW_FALSE };
        };

        PartyLobbyService();
        ~PartyLobbyService();

        PartyLobbyService( const PartyLobbyService& )            = delete;
        PartyLobbyService& operator=( const PartyLobbyService& ) = delete;

        /** @brief @p pRouter 는 빌려 쓴다. 새 파티 · 로비 id = @p serverID << 32 | 순번(서버마다 겹치지 않는다). */
        void initialize( EphemeralStoreRouter* pRouter, uint64 serverID, int32 maxPartySize = 4 );
        /** @brief 기다리던 캐시 요청을 취소합니다(라우터보다 먼저 내려갈 때). */
        void shutdown();

        // 파티
        void createParty( AccountID accountID, uint64 requestTag );
        void inviteToParty( AccountID leaderID, AccountID targetID, uint64 requestTag );
        void acceptPartyInvite( AccountID accountID, uint64 partyID, uint64 requestTag );
        void leaveParty( AccountID accountID, uint64 requestTag );
        void kickFromParty( AccountID leaderID, AccountID targetID, uint64 requestTag );
        /** @brief 파티의 대기열 표를 적습니다(0 = 빠짐). 완료 없음. */
        void setPartyTicket( uint64 partyID, uint64 ticketID );
        /** @brief 계정의 파티를 찾습니다 — 결과는 @p onFound( @p lookupTag, Ok · NotInParty · Unavailable, 파티 )로 한 번(나중에). */
        void findPartyOfAccount( AccountID accountID, uint64 lookupTag, const PartyFoundDelegate& onFound );

        // 로비
        /** @brief @p request 의 이름 · 모드 · 정원 · 설정으로 만듭니다(만든 이가 방장). */
        void createLobby( AccountID accountID, const LobbySnapshot& request, int64 nowMs, uint64 requestTag );
        void listLobbies( string_view modeID, uint64 requestTag );
        void joinLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag );
        void leaveLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag );
        void setLobbyReady( AccountID accountID, uint64 lobbyID, bool bReady, uint64 requestTag );
        void startLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag );
        /** @brief 시작한 로비를 다시 Open 으로(배정 실패). 완료 없음 — 회원에게 알림. */
        void reopenLobby( uint64 lobbyID );

        /** @brief 계정이 이 서버에서 떠났다 — 파티에서, 이 서버가 들여보낸 로비에서 뺀다. */
        void removeAccount( AccountID accountID );

        void drainCompletions( vector<PartyLobbyCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainNotifications( vector<PartyLobbyNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        void drainLobbyStarts( vector<LobbyStartRequest>& outListStart ) { _lobbyStartBuffer.drainTo( outListStart ); }
        /** @brief 사람이 바뀌어 대기열에서 빠져야 할 파티 표입니다. */
        void drainBrokenTickets( vector<uint64>& outListTicketID ) { _brokenTicketBuffer.drainTo( outListTicketID ); }

        int32 getPendingCount() const { return _updater.getPendingCount() + static_cast<int32>( _mapRequestToStep.size() ); }

        static string makePartyKey( uint64 partyID );
        static string makeAccountPartyKey( AccountID accountID );
        static string makeInviteKey( AccountID accountID, uint64 partyID );
        static string makeLobbyKey( uint64 lobbyID );
        static string makeLobbyListKey( string_view modeID );

        /** @brief 바꾸기 객체가 부른다(키트 안). */
        void  finishParty( const Request& request, MatchmakingResult result, const PartySnapshot& party, AccountID removedID, uint64 brokenTicketID );
        void  finishLobby( const Request& request, MatchmakingResult result, const LobbySnapshot& lobby, AccountID removedID, bool bErased );
        int32 getMaxPartySize() const { return _maxPartySize; }

    private:
        enum class StepKind : uint8
        {
            IndexReserve = 0, ///< 계정 색인 IfAbsent(만들기 · 수락)
            IndexRead,        ///< 계정 색인 읽기(초대 · 떠나기 · 내보내기 · 찾기)
            InviteRead,       ///< 초대 키 읽기(수락)
            PartyRead,        ///< 파티 읽기(찾기)
            ListRange,        ///< 로비 목록 정렬 집합
            ListGet           ///< 목록의 로비 하나
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
        void   refreshIndexTtl( const PartySnapshot& party );
        void   notifyParty( const PartySnapshot& party, AccountID removedID );
        void   notifyLobby( const LobbySnapshot& lobby, AccountID removedID );
        void   completeParty( const Request& request, MatchmakingResult result, const PartySnapshot& party );
        void   completeLobby( const Request& request, MatchmakingResult result, const LobbySnapshot& lobby );
        uint64 allocateID() { return ( _serverID << 32 ) | ++_sequence; }

        CacheRecordUpdater                  _updater;
        unordered_map<uint64, PendingStep>  _mapRequestToStep;
        unordered_map<uint64, PendingList>  _mapListIDToList;
        unordered_map<AccountID, uint64>    _mapAccountToLobby; ///< 이 서버가 들여보낸 로비
        EventBuffer<PartyLobbyCompletion>   _completionBuffer;
        EventBuffer<PartyLobbyNotification> _notificationBuffer;
        EventBuffer<LobbyStartRequest>      _lobbyStartBuffer;
        EventBuffer<uint64>                 _brokenTicketBuffer;
        EphemeralStoreRouter*               _pRouter;
        uint64                              _serverID;
        uint64                              _nextListID;
        int32                               _maxPartySize;
        uint32                              _sequence;
    };
} // namespace sw
