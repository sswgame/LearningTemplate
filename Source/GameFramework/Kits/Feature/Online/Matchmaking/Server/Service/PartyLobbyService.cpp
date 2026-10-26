#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct PartyLobbyServiceInternal
        {
            /** @brief 키가 비지 않았고 `[0-9a-z_.]` 만 쓰는지입니다. 길이 상한은 `LobbySetting::_key` 의 `PROPERTY( MaxCount )` 를 읽기가 본다. */
            static bool isValidSettingKey( string_view key )
            {
                if ( key.empty() )
                    return false;
                for ( const utf8 ch : key )
                {
                    const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
                    if ( bAllowed == false )
                        return false;
                }
                return true;
            }

            /**
             * @brief 만들기 요청이 `PROPERTY` 로 적을 수 없는 규칙(이름 있음, 정원 하한, 모드 id, 설정 키 글자)을 지키는지입니다.
             * @details 상한(이름 · 설정 수 · 값 길이 · 정원 위)은 `LobbySnapshot` 의 `PROPERTY( Max · MaxCount )` 가 원본이고, 요청 본문을 읽는 고정 배치형 코덱이 막는다(`MatchmakingServer`).
             *          여기 다시 적으면 범위가 두 곳이 된다. 정원 하한은 와이어 범위(`Min = 0`)가 와이어 형식이라 바꾸지 않고 여기서 본다.
             */
            static bool isValidLobbyRequest( const LobbySnapshot& request )
            {
                const bool bInvalidLobbyRequest = request._name.empty() || request._maxMemberCount < PartyLobbyLimit::kMinLobbySize || isValidMatchModeID( request._modeID ) == false;
                if ( bInvalidLobbyRequest )
                    return false;
                for ( const LobbySetting& setting : request._listSetting )
                {
                    if ( isValidSettingKey( setting._key ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 인원이 적은 팀을 반환합니다. 같으면 0번 팀입니다. */
            static int32 findSmallerTeam( const LobbySnapshot& lobby )
            {
                int32 arrCount[PartyLobbyLimit::kLobbyTeamCount] = {};
                for ( const LobbyMember& member : lobby._listMember )
                {
                    if ( 0 <= member._team && member._team < PartyLobbyLimit::kLobbyTeamCount )
                        ++arrCount[member._team];
                }
                int32 smallestTeam = 0;
                for ( int32 team = 1; team < PartyLobbyLimit::kLobbyTeamCount; ++team )
                {
                    if ( arrCount[team] < arrCount[smallestTeam] )
                        smallestTeam = team;
                }
                return smallestTeam;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 파티 기록 하나를 바꾸는 규칙입니다. 연산마다 순수한 규칙이며 끝나면 서비스에 넘깁니다. 계정 인덱스는 서비스가 성공이나 실패 뒤에 따로 맞춥니다. */
    class PartyMutation final : public ICacheRecordMutation
    {
    public:
        PartyMutation( PartyLobbyService* pService, const PartyLobbyService::Request& request )
            : _request{ request }
            , _snapshot{}
            , _pService{ pService }
            , _removedID{ kInvalidAccountID }
            , _brokenTicketID{ 0 }
        {
        }

        MatchmakingResult mutate( bool bExists, const vector<uint8>& oldBytes, vector<uint8>& outNewBytes, bool& outbErase ) override
        {
            PartySnapshot party;
            _removedID      = kInvalidAccountID;
            _brokenTicketID = 0;
            if ( _request._operation == PartyLobbyOperation::PartyCreate )
            {
                if ( bExists )
                    return MatchmakingResult::Conflict; // id 가 겹쳤다. 서버 id 와 순번으로 만들므로 생길 수 없는 경우이다
                party._partyID        = _request._partyID;
                party._maxMemberCount = _pService->getMaxPartySize();
                party._listMemberID.push_back( _request._actorID );
                _snapshot = party;
                return MatchmakingProtocol::encodeParty( party, outNewBytes ) ? MatchmakingResult::Ok : MatchmakingResult::Invalid;
            }
            if ( bExists == false || MatchmakingProtocol::decodeParty( oldBytes, party ) == false )
            {
                _snapshot          = PartySnapshot{};
                _snapshot._partyID = _request._partyID;
                return MatchmakingResult::NotInParty;
            }
            _snapshot = party;
            switch ( _request._operation )
            {
                case PartyLobbyOperation::PartyInvite:
                {
                    if ( party.getLeaderID() != _request._actorID )
                        return MatchmakingResult::NotPartyLeader;
                    if ( party.hasMember( _request._targetID ) )
                        return MatchmakingResult::AlreadyInParty;
                    if ( static_cast<int32>( party._listMemberID.size() ) >= party._maxMemberCount )
                        return MatchmakingResult::PartyFull;
                    outNewBytes = oldBytes; // 같은 값으로 비교 후 쓰기를 한다. 만료를 연장하고 그사이 해산되지 않았음을 확인하는 것이다
                    return MatchmakingResult::Ok;
                }
                case PartyLobbyOperation::PartyAccept:
                {
                    if ( party.hasMember( _request._actorID ) )
                        return MatchmakingResult::AlreadyInParty;
                    if ( static_cast<int32>( party._listMemberID.size() ) >= party._maxMemberCount )
                        return MatchmakingResult::PartyFull;
                    if ( party._queuedTicketID != 0 )
                        return MatchmakingResult::AlreadyQueued; // 줄을 서 있는 동안은 들어올 수 없다
                    party._listMemberID.push_back( _request._actorID );
                    break;
                }
                case PartyLobbyOperation::PartyLeave:
                case PartyLobbyOperation::PartyKick:
                {
                    const bool      bKick   = _request._operation == PartyLobbyOperation::PartyKick;
                    const AccountID leaving = bKick ? _request._targetID : _request._actorID;
                    if ( bKick && party.getLeaderID() != _request._actorID )
                        return MatchmakingResult::NotPartyLeader;
                    if ( bKick && leaving == _request._actorID )
                        return MatchmakingResult::Invalid; // 파티장은 자신을 내보낼 수 없다. 떠나기를 써야 한다
                    if ( party.hasMember( leaving ) == false )
                        return MatchmakingResult::NotInParty;
                    for ( size_t index = 0; index < party._listMemberID.size(); ++index )
                    {
                        if ( party._listMemberID[index] == leaving )
                        {
                            party._listMemberID.erase( party._listMemberID.begin() + static_cast<ptrdiff_t>( index ) ); // 파티장이 나가면 다음 사람이 첫 항목이 되어 파티장이 된다
                            break;
                        }
                    }
                    _brokenTicketID       = party._queuedTicketID; // 멤버가 바뀌면 대기열에서 빠진다
                    party._queuedTicketID = 0;
                    _removedID            = leaving;
                    outbErase             = party._listMemberID.empty();
                    break;
                }
                case PartyLobbyOperation::PartySetTicket:
                {
                    party._queuedTicketID = _request._ticketID;
                    break;
                }
                default:
                {
                    return MatchmakingResult::Invalid;
                }
            }
            _snapshot = party;
            return MatchmakingProtocol::encodeParty( party, outNewBytes ) ? MatchmakingResult::Ok : MatchmakingResult::Invalid;
        }

        void onFinished( MatchmakingResult result, const vector<uint8>& finalBytes ) override
        {
            PartySnapshot party = _snapshot;
            if ( result == MatchmakingResult::Ok && finalBytes.empty() == false )
                (void)MatchmakingProtocol::decodeParty( finalBytes, party );
            const bool bApplied = result == MatchmakingResult::Ok;
            _pService->finishParty( _request, result, party, bApplied ? _removedID : kInvalidAccountID, bApplied ? _brokenTicketID : 0 );
        }

    private:
        PartyLobbyService::Request _request;
        PartySnapshot              _snapshot;
        PartyLobbyService*         _pService;
        AccountID                  _removedID;
        uint64                     _brokenTicketID;
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 기록 하나를 바꾸는 규칙입니다. 파티와 같은 구조입니다. */
    class LobbyMutation final : public ICacheRecordMutation
    {
    public:
        LobbyMutation( PartyLobbyService* pService, const PartyLobbyService::Request& request )
            : _request{ request }
            , _snapshot{}
            , _pService{ pService }
            , _removedID{ kInvalidAccountID }
            , _bErased{ SW_FALSE }
        {
        }

        MatchmakingResult mutate( bool bExists, const vector<uint8>& oldBytes, vector<uint8>& outNewBytes, bool& outbErase ) override
        {
            LobbySnapshot lobby;
            _removedID = kInvalidAccountID;
            _bErased   = SW_FALSE;
            if ( _request._operation == PartyLobbyOperation::LobbyCreate )
            {
                if ( bExists )
                    return MatchmakingResult::Conflict;
                lobby          = _request._lobby;
                lobby._lobbyID = _request._lobbyID;
                lobby._state   = LobbyState::Open;
                lobby._listMember.clear();
                lobby._listMember.push_back( LobbyMember{ _request._actorID, 0, SW_FALSE } );
                _snapshot = lobby;
                return MatchmakingProtocol::encodeLobby( lobby, outNewBytes ) ? MatchmakingResult::Ok : MatchmakingResult::Invalid;
            }
            if ( bExists == false || MatchmakingProtocol::decodeLobby( oldBytes, lobby ) == false )
            {
                _snapshot          = LobbySnapshot{};
                _snapshot._lobbyID = _request._lobbyID;
                return MatchmakingResult::NotInLobby;
            }
            _snapshot = lobby;
            switch ( _request._operation )
            {
                case PartyLobbyOperation::LobbyJoin:
                {
                    if ( lobby.hasMember( _request._actorID ) )
                    {
                        outNewBytes = oldBytes; // 이미 들어 있으므로 같은 값으로 쓴다. 만료를 연장하는 효과가 있다
                        return MatchmakingResult::Ok;
                    }
                    if ( lobby._state != LobbyState::Open )
                        return MatchmakingResult::Invalid;
                    if ( static_cast<int32>( lobby._listMember.size() ) >= lobby._maxMemberCount )
                        return MatchmakingResult::LobbyFull;
                    lobby._listMember.push_back( LobbyMember{ _request._actorID, PartyLobbyServiceInternal::findSmallerTeam( lobby ), SW_FALSE } );
                    break;
                }
                case PartyLobbyOperation::LobbyLeave:
                {
                    bool bRemoved = false;
                    for ( size_t index = 0; index < lobby._listMember.size(); ++index )
                    {
                        if ( lobby._listMember[index]._accountID == _request._actorID )
                        {
                            lobby._listMember.erase( lobby._listMember.begin() + static_cast<ptrdiff_t>( index ) ); // 방장이 나가면 다음 사람이 방장이 된다
                            bRemoved = true;
                            break;
                        }
                    }
                    if ( bRemoved == false )
                        return MatchmakingResult::NotInLobby;
                    _removedID = _request._actorID;
                    outbErase  = lobby._listMember.empty();
                    _bErased   = outbErase ? SW_TRUE : SW_FALSE;
                    break;
                }
                case PartyLobbyOperation::LobbyReady:
                {
                    bool bFound = false;
                    for ( LobbyMember& member : lobby._listMember )
                    {
                        if ( member._accountID == _request._actorID )
                        {
                            member._bReady = _request._bReady;
                            bFound         = true;
                        }
                    }
                    if ( bFound == false )
                        return MatchmakingResult::NotInLobby;
                    break;
                }
                case PartyLobbyOperation::LobbyStart:
                {
                    if ( lobby.getOwnerID() != _request._actorID )
                        return MatchmakingResult::NotLobbyOwner;
                    if ( lobby._state != LobbyState::Open )
                        return MatchmakingResult::Invalid;
                    if ( lobby.isEveryoneReady() == false )
                        return MatchmakingResult::LobbyNotReady;
                    lobby._state = LobbyState::Starting;
                    break;
                }
                case PartyLobbyOperation::LobbyReopen:
                {
                    lobby._state = LobbyState::Open;
                    break;
                }
                default:
                {
                    return MatchmakingResult::Invalid;
                }
            }
            _snapshot = lobby;
            return MatchmakingProtocol::encodeLobby( lobby, outNewBytes ) ? MatchmakingResult::Ok : MatchmakingResult::Invalid;
        }

        void onFinished( MatchmakingResult result, const vector<uint8>& finalBytes ) override
        {
            LobbySnapshot lobby = _snapshot;
            if ( result == MatchmakingResult::Ok && finalBytes.empty() == false )
                (void)MatchmakingProtocol::decodeLobby( finalBytes, lobby );
            const bool bApplied = result == MatchmakingResult::Ok;
            _pService->finishLobby( _request, result, lobby, bApplied ? _removedID : kInvalidAccountID, bApplied && _bErased != SW_FALSE );
        }

    private:
        PartyLobbyService::Request _request;
        LobbySnapshot              _snapshot;
        PartyLobbyService*         _pService;
        AccountID                  _removedID;
        uint8                      _bErased;
    };
} // namespace sw

namespace sw
{
    PartyLobbyService::PartyLobbyService()
        : _updater{}
        , _mapRequestToStep{}
        , _mapListIDToList{}
        , _mapAccountToLobby{}
        , _completionBuffer{}
        , _notificationBuffer{}
        , _lobbyStartBuffer{}
        , _brokenTicketBuffer{}
        , _pRouter{ nullptr }
        , _serverID{ 0 }
        , _nextListID{ 1 }
        , _maxPartySize{ 4 }
        , _sequence{ 0 }
    {
    }

    PartyLobbyService::~PartyLobbyService()
    {
        shutdown();
    }

    bool PartyLobbyService::initialize( EphemeralStoreRouter* pRouter, uint64 serverID, int32 maxPartySize )
    {
        // 실패하면 라우터 없는 서비스로 남긴다. 값을 범위 안으로 맞춰 돌지 않는다.
        _pRouter                = nullptr;
        const bool bPartySizeOk = PartyLobbyLimit::kMinPartySize <= maxPartySize && maxPartySize <= PartyLobbyLimit::kMaxPartySize;
        if ( bPartySizeOk == false )
        {
            SW_LOG_ERROR( "PartyLobbyService: refused to start - max party size %# is outside [%#, %#]", maxPartySize, PartyLobbyLimit::kMinPartySize,
                          PartyLobbyLimit::kMaxPartySize );
            return false;
        }
        _pRouter      = pRouter;
        _replyOwner   = pRouter != nullptr ? pRouter->openReplyOwner() : Subscription{};
        _serverID     = serverID;
        _maxPartySize = maxPartySize;
        _updater.initialize( pRouter );
        return true;
    }

    void PartyLobbyService::shutdown()
    {
        _updater.shutdown();
        _replyOwner.reset(); // 기다리던 캐시 답이 이 객체를 부르지 않게 지운다
        _mapRequestToStep.clear();
        _mapListIDToList.clear();
        _pRouter = nullptr;
    }

    string PartyLobbyService::makePartyKey( uint64 partyID )
    {
        return "mm/party/" + ServiceKeyUtil::makeHex64( partyID );
    }

    string PartyLobbyService::makeAccountPartyKey( AccountID accountID )
    {
        return "mm/acct/" + ServiceKeyUtil::makeHex64( accountID );
    }

    string PartyLobbyService::makeInviteKey( AccountID accountID, uint64 partyID )
    {
        return "mm/pinv/" + ServiceKeyUtil::makeHex64( accountID ) + "/" + ServiceKeyUtil::makeHex64( partyID );
    }

    string PartyLobbyService::makeLobbyKey( uint64 lobbyID )
    {
        return "mm/lobby/" + ServiceKeyUtil::makeHex64( lobbyID );
    }

    string PartyLobbyService::makeLobbyListKey( string_view modeID )
    {
        return "mm/lobbies/" + string( modeID );
    }

    void PartyLobbyService::submitStep( const EphemeralRequest& cacheRequest, StepKind kind, const Request& request, uint64 listID, int32 slot )
    {
        if ( _pRouter == nullptr )
            return;
        const uint64 requestID = _pRouter->submit( cacheRequest, EphemeralStoreRouter::ReplyDelegate::create<&PartyLobbyService::onStepReply>( this ), _replyOwner );
        PendingStep& step      = _mapRequestToStep[requestID];
        step._request          = request;
        step._kind             = kind;
        step._listID           = listID;
        step._slot             = slot;
    }

    void PartyLobbyService::submitFireAndForget( const EphemeralRequest& cacheRequest )
    {
        if ( _pRouter != nullptr )
            _pRouter->submit( cacheRequest );
    }

    void PartyLobbyService::createParty( AccountID accountID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyCreate;
        request._actorID    = accountID;
        request._partyID    = allocateID();
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeSet( makeAccountPartyKey( accountID ), MatchmakingProtocol::encodeID( request._partyID ), PartyLobbyLimit::kPartyTtlMs,
                                               EphemeralCondition::IfAbsent ),
                    StepKind::IndexReserve, request );
    }

    void PartyLobbyService::inviteToParty( AccountID leaderID, AccountID targetID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyInvite;
        request._actorID    = leaderID;
        request._targetID   = targetID;
        request._requestTag = requestTag;
        if ( targetID == kInvalidAccountID || targetID == leaderID )
        {
            completeParty( request, MatchmakingResult::Invalid, PartySnapshot{} );
            return;
        }
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( leaderID ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::acceptPartyInvite( AccountID accountID, uint64 partyID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyAccept;
        request._actorID    = accountID;
        request._partyID    = partyID;
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeGet( makeInviteKey( accountID, partyID ) ), StepKind::InviteRead, request );
    }

    void PartyLobbyService::leaveParty( AccountID accountID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyLeave;
        request._actorID    = accountID;
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( accountID ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::kickFromParty( AccountID leaderID, AccountID targetID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyKick;
        request._actorID    = leaderID;
        request._targetID   = targetID;
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( leaderID ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::setPartyTicket( uint64 partyID, uint64 ticketID )
    {
        Request request;
        request._operation = PartyLobbyOperation::PartySetTicket;
        request._partyID   = partyID;
        request._ticketID  = ticketID;
        startPartyMutation( request );
    }

    void PartyLobbyService::findPartyOfAccount( AccountID accountID, uint64 lookupTag, const PartyFoundDelegate& onFound )
    {
        Request request;
        request._operation    = PartyLobbyOperation::PartyFind;
        request._actorID      = accountID;
        request._requestTag   = lookupTag;
        request._onPartyFound = onFound;
        if ( _pRouter == nullptr )
        {
            if ( onFound.isBound() )
                onFound( lookupTag, MatchmakingResult::Unavailable, PartySnapshot{} );
            return;
        }
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( accountID ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::createLobby( AccountID accountID, const LobbySnapshot& lobbyRequest, int64 nowMs, uint64 requestTag )
    {
        Request request;
        request._operation        = PartyLobbyOperation::LobbyCreate;
        request._actorID          = accountID;
        request._lobby            = lobbyRequest;
        request._lobby._createdMs = nowMs;
        request._lobbyID          = allocateID();
        request._nowMs            = nowMs;
        request._requestTag       = requestTag;
        if ( PartyLobbyServiceInternal::isValidLobbyRequest( lobbyRequest ) == false )
        {
            completeLobby( request, MatchmakingResult::Invalid, LobbySnapshot{} );
            return;
        }
        startLobbyMutation( request );
    }

    void PartyLobbyService::listLobbies( string_view modeID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyList;
        request._modeID     = string( modeID );
        request._requestTag = requestTag;
        if ( isValidMatchModeID( modeID ) == false )
        {
            completeLobby( request, MatchmakingResult::UnknownMode, LobbySnapshot{} );
            return;
        }
        submitStep( EphemeralRequest::makeScoreRange( makeLobbyListKey( modeID ), 0, PartyLobbyLimit::kMaxLobbyList ), StepKind::ListRange, request );
    }

    void PartyLobbyService::joinLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyJoin;
        request._actorID    = accountID;
        request._lobbyID    = lobbyID;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::leaveLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyLeave;
        request._actorID    = accountID;
        request._lobbyID    = lobbyID;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::setLobbyReady( AccountID accountID, uint64 lobbyID, bool bReady, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyReady;
        request._actorID    = accountID;
        request._lobbyID    = lobbyID;
        request._bReady     = bReady ? SW_TRUE : SW_FALSE;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::startLobby( AccountID accountID, uint64 lobbyID, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyStart;
        request._actorID    = accountID;
        request._lobbyID    = lobbyID;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::reopenLobby( uint64 lobbyID )
    {
        Request request;
        request._operation = PartyLobbyOperation::LobbyReopen;
        request._lobbyID   = lobbyID;
        startLobbyMutation( request );
    }

    void PartyLobbyService::removeAccount( AccountID accountID )
    {
        leaveParty( accountID, 0 );
        const auto lobbyIt = _mapAccountToLobby.find( accountID );
        if ( lobbyIt != _mapAccountToLobby.end() )
            leaveLobby( accountID, lobbyIt->second, 0 );
    }

    void PartyLobbyService::startPartyMutation( const Request& request )
    {
        _updater.update( makePartyKey( request._partyID ), PartyLobbyLimit::kPartyTtlMs, make_unique<PartyMutation>( this, request ) );
    }

    void PartyLobbyService::startLobbyMutation( const Request& request )
    {
        _updater.update( makeLobbyKey( request._lobbyID ), PartyLobbyLimit::kLobbyTtlMs, make_unique<LobbyMutation>( this, request ) );
    }

    void PartyLobbyService::onStepReply( const EphemeralReply& reply )
    {
        const auto stepIt = _mapRequestToStep.find( reply._requestID );
        if ( stepIt == _mapRequestToStep.end() )
            return;
        const PendingStep step = std::move( stepIt->second );
        _mapRequestToStep.erase( stepIt );
        const bool bUnavailable = reply._result != EphemeralResult::Ok && reply._result != EphemeralResult::NotFound && reply._result != EphemeralResult::Conflict;
        switch ( step._kind )
        {
            case StepKind::IndexReserve:
            {
                if ( reply._result == EphemeralResult::Conflict )
                    completeParty( step._request, MatchmakingResult::AlreadyInParty, PartySnapshot{} );
                else if ( reply._result != EphemeralResult::Ok )
                    completeParty( step._request, MatchmakingResult::Unavailable, PartySnapshot{} );
                else
                    startPartyMutation( step._request );
                break;
            }
            case StepKind::IndexRead:
            {
                handleIndexRead( step._request, reply );
                break;
            }
            case StepKind::InviteRead:
            {
                if ( bUnavailable )
                {
                    completeParty( step._request, MatchmakingResult::Unavailable, PartySnapshot{} );
                    break;
                }
                if ( reply._result != EphemeralResult::Ok )
                {
                    completeParty( step._request, MatchmakingResult::InviteMissing, PartySnapshot{} );
                    break;
                }
                submitStep( EphemeralRequest::makeSet( makeAccountPartyKey( step._request._actorID ), MatchmakingProtocol::encodeID( step._request._partyID ),
                                                       PartyLobbyLimit::kPartyTtlMs, EphemeralCondition::IfAbsent ),
                            StepKind::IndexReserve, step._request );
                break;
            }
            case StepKind::PartyRead:
            {
                PartySnapshot     party;
                MatchmakingResult result = MatchmakingResult::Ok;
                if ( bUnavailable )
                {
                    result = MatchmakingResult::Unavailable;
                }
                else
                {
                    const bool bNotInParty = reply._result != EphemeralResult::Ok || MatchmakingProtocol::decodeParty( reply._value, party ) == false ||
                                             party.hasMember( step._request._actorID ) == false;
                    if ( bNotInParty )
                        result = MatchmakingResult::NotInParty; // 인덱스만 남았다. 파티가 만료됐다
                }
                if ( step._request._onPartyFound.isBound() )
                    step._request._onPartyFound( step._request._requestTag, result, party );
                break;
            }
            case StepKind::ListRange:
            {
                if ( reply._result != EphemeralResult::Ok && reply._result != EphemeralResult::NotFound )
                {
                    completeLobby( step._request, MatchmakingResult::Unavailable, LobbySnapshot{} );
                    break;
                }
                if ( reply._listMember.empty() )
                {
                    completeLobby( step._request, MatchmakingResult::Ok, LobbySnapshot{} );
                    break;
                }
                const uint64 listID  = _nextListID++;
                PendingList& list    = _mapListIDToList[listID];
                list._modeID         = step._request._modeID;
                list._requestTag     = step._request._requestTag;
                list._remainingCount = static_cast<int32>( reply._listMember.size() );
                list._listLobby.resize( reply._listMember.size() );
                list._listFound.assign( reply._listMember.size(), SW_FALSE );
                for ( size_t index = 0; index < reply._listMember.size(); ++index )
                {
                    uint64 lobbyID = 0;
                    if ( ServiceKeyUtil::parseHex64( reply._listMember[index]._member, lobbyID ) == false )
                        lobbyID = 0;
                    list._listLobby[index]._lobbyID = lobbyID;
                }
                for ( size_t index = 0; index < reply._listMember.size(); ++index )
                {
                    Request getRequest  = step._request;
                    getRequest._lobbyID = list._listLobby[index]._lobbyID;
                    submitStep( EphemeralRequest::makeGet( makeLobbyKey( getRequest._lobbyID ) ), StepKind::ListGet, getRequest, listID, static_cast<int32>( index ) );
                }
                break;
            }
            case StepKind::ListGet:
            {
                handleListGet( step._listID, step._slot, reply );
                break;
            }
        }
    }

    void PartyLobbyService::handleIndexRead( const Request& request, const EphemeralReply& reply )
    {
        const bool        bFind   = request._operation == PartyLobbyOperation::PartyFind;
        uint64            partyID = 0;
        MatchmakingResult result  = MatchmakingResult::Ok;
        if ( reply._result == EphemeralResult::NotFound )
            result = MatchmakingResult::NotInParty;
        else if ( reply._result != EphemeralResult::Ok )
            result = MatchmakingResult::Unavailable;
        else if ( MatchmakingProtocol::decodeID( reply._value, partyID ) == false )
            result = MatchmakingResult::NotInParty;
        if ( result != MatchmakingResult::Ok )
        {
            if ( bFind )
            {
                if ( request._onPartyFound.isBound() )
                    request._onPartyFound( request._requestTag, result, PartySnapshot{} );
            }
            else
            {
                completeParty( request, result, PartySnapshot{} );
            }
            return;
        }
        Request next  = request;
        next._partyID = partyID;
        if ( bFind )
        {
            submitStep( EphemeralRequest::makeGet( makePartyKey( partyID ) ), StepKind::PartyRead, next );
            return;
        }
        startPartyMutation( next );
    }

    void PartyLobbyService::handleListGet( uint64 listID, int32 slot, const EphemeralReply& reply )
    {
        const auto listIt = _mapListIDToList.find( listID );
        if ( listIt == _mapListIDToList.end() )
            return;
        PendingList&  list    = listIt->second;
        const size_t  index   = static_cast<size_t>( slot );
        const uint64  lobbyID = list._listLobby[index]._lobbyID;
        LobbySnapshot lobby;
        if ( reply._result == EphemeralResult::Ok && MatchmakingProtocol::decodeLobby( reply._value, lobby ) )
        {
            list._listLobby[index] = std::move( lobby );
            list._listFound[index] = SW_TRUE;
        }
        else if ( reply._result == EphemeralResult::NotFound )
        {
            submitFireAndForget( EphemeralRequest::makeScoreRemove( makeLobbyListKey( list._modeID ), ServiceKeyUtil::makeHex64( lobbyID ) ) ); // 만료로 사라진 로비이다
        }
        if ( --list._remainingCount > 0 )
            return;
        PartyLobbyCompletion completion;
        completion._operation  = PartyLobbyOperation::LobbyList;
        completion._requestTag = list._requestTag;
        completion._result     = MatchmakingResult::Ok;
        for ( size_t found = 0; found < list._listLobby.size(); ++found )
        {
            if ( list._listFound[found] != SW_FALSE )
                completion._listLobby.push_back( std::move( list._listLobby[found] ) );
        }
        _mapListIDToList.erase( listIt );
        _completionBuffer.push( std::move( completion ) );
    }

    void PartyLobbyService::finishParty( const Request& request, MatchmakingResult result, const PartySnapshot& party, AccountID removedID, uint64 brokenTicketID )
    {
        const vector<uint8> indexValue = MatchmakingProtocol::encodeID( request._partyID );
        switch ( request._operation )
        {
            case PartyLobbyOperation::PartyCreate:
            case PartyLobbyOperation::PartyAccept:
            {
                if ( result != MatchmakingResult::Ok )
                {
                    submitFireAndForget( EphemeralRequest::makeCompareAndErase( makeAccountPartyKey( request._actorID ), indexValue ) ); // 잡아 둔 인덱스를 되돌린다
                    break;
                }
                if ( request._operation == PartyLobbyOperation::PartyAccept )
                {
                    submitFireAndForget( EphemeralRequest::makeErase( makeInviteKey( request._actorID, request._partyID ) ) );
                    refreshIndexTtl( party );
                }
                notifyParty( party, kInvalidAccountID ); // 만든 사람에게도 보낸다. 클라이언트의 "내 파티" 는 알림이 원본이기 때문이다
                break;
            }
            case PartyLobbyOperation::PartyInvite:
            {
                if ( result != MatchmakingResult::Ok )
                    break;
                submitFireAndForget( EphemeralRequest::makeSet( makeInviteKey( request._targetID, request._partyID ), MatchmakingProtocol::encodeID( request._actorID ),
                                                                PartyLobbyLimit::kInviteTtlMs ) );
                PartyLobbyNotification notification;
                notification._recipientID       = request._targetID;
                notification._pushKind          = MatchmakingMethod::kPushPartyInvite;
                notification._invite._partyID   = request._partyID;
                notification._invite._inviterID = request._actorID;
                _notificationBuffer.push( std::move( notification ) );
                refreshIndexTtl( party );
                break;
            }
            case PartyLobbyOperation::PartyLeave:
            case PartyLobbyOperation::PartyKick:
            {
                const bool bStaleLeaveIndex = result == MatchmakingResult::NotInParty && request._operation == PartyLobbyOperation::PartyLeave && party._listMemberID.empty();
                if ( bStaleLeaveIndex )
                {
                    // 인덱스만 남았다. 파티 기록이 만료된 것이며 인덱스를 지우면 떠난 것과 같다
                    submitFireAndForget( EphemeralRequest::makeCompareAndErase( makeAccountPartyKey( request._actorID ), indexValue ) );
                    result = MatchmakingResult::Ok;
                    break;
                }
                if ( result != MatchmakingResult::Ok )
                    break;
                submitFireAndForget( EphemeralRequest::makeCompareAndErase( makeAccountPartyKey( removedID ), indexValue ) );
                if ( brokenTicketID != 0 )
                    _brokenTicketBuffer.push( brokenTicketID );
                refreshIndexTtl( party );
                notifyParty( party, removedID );
                break;
            }
            case PartyLobbyOperation::PartySetTicket:
            {
                if ( result == MatchmakingResult::Ok )
                    notifyParty( party, kInvalidAccountID );
                return; // 완료는 없다
            }
            default:
            {
                break;
            }
        }
        completeParty( request, result, party );
    }

    void PartyLobbyService::finishLobby( const Request& request, MatchmakingResult result, const LobbySnapshot& lobby, AccountID removedID, bool bErased )
    {
        if ( result == MatchmakingResult::Ok )
        {
            switch ( request._operation )
            {
                case PartyLobbyOperation::LobbyCreate:
                {
                    const string listKey = makeLobbyListKey( lobby._modeID );
                    submitFireAndForget( EphemeralRequest::makeScoreSet( listKey, ServiceKeyUtil::makeHex64( lobby._lobbyID ), lobby._createdMs ) );
                    _mapAccountToLobby[request._actorID] = lobby._lobbyID;
                    break;
                }
                case PartyLobbyOperation::LobbyJoin:
                {
                    _mapAccountToLobby[request._actorID] = lobby._lobbyID;
                    notifyLobby( lobby, kInvalidAccountID );
                    break;
                }
                case PartyLobbyOperation::LobbyLeave:
                {
                    const auto lobbyIt = _mapAccountToLobby.find( request._actorID );
                    if ( lobbyIt != _mapAccountToLobby.end() && lobbyIt->second == request._lobbyID )
                        _mapAccountToLobby.erase( lobbyIt );
                    if ( bErased )
                        submitFireAndForget( EphemeralRequest::makeScoreRemove( makeLobbyListKey( lobby._modeID ), ServiceKeyUtil::makeHex64( request._lobbyID ) ) );
                    notifyLobby( lobby, removedID );
                    break;
                }
                case PartyLobbyOperation::LobbyStart:
                {
                    notifyLobby( lobby, kInvalidAccountID );
                    _lobbyStartBuffer.push( LobbyStartRequest{ lobby } );
                    break;
                }
                default:
                {
                    notifyLobby( lobby, kInvalidAccountID );
                    break;
                }
            }
            if ( bErased == false ) // 목록도 살아 있는 로비만큼만 유지한다
                submitFireAndForget( EphemeralRequest::makeExpire( makeLobbyListKey( lobby._modeID ), PartyLobbyLimit::kLobbyTtlMs ) );
        }
        if ( request._operation == PartyLobbyOperation::LobbyReopen )
            return; // 완료는 없다
        completeLobby( request, result, lobby );
    }

    void PartyLobbyService::refreshIndexTtl( const PartySnapshot& party )
    {
        for ( const AccountID memberID : party._listMemberID )
        {
            submitFireAndForget( EphemeralRequest::makeExpire( makeAccountPartyKey( memberID ), PartyLobbyLimit::kPartyTtlMs ) ); // 파티 기록과 함께 연장한다
        }
    }

    void PartyLobbyService::notifyParty( const PartySnapshot& party, AccountID removedID )
    {
        for ( const AccountID memberID : party._listMemberID )
        {
            PartyLobbyNotification notification;
            notification._recipientID = memberID;
            notification._pushKind    = MatchmakingMethod::kPushParty;
            notification._party       = party;
            _notificationBuffer.push( std::move( notification ) );
        }
        if ( removedID == kInvalidAccountID )
            return;
        PartyLobbyNotification removed;
        removed._recipientID    = removedID;
        removed._pushKind       = MatchmakingMethod::kPushParty;
        removed._party._partyID = party._partyID; // 멤버가 비어 있으면 이 파티에서 빠진 것이다
        _notificationBuffer.push( std::move( removed ) );
    }

    void PartyLobbyService::notifyLobby( const LobbySnapshot& lobby, AccountID removedID )
    {
        for ( const LobbyMember& member : lobby._listMember )
        {
            PartyLobbyNotification notification;
            notification._recipientID = member._accountID;
            notification._pushKind    = MatchmakingMethod::kPushLobby;
            notification._lobby       = lobby;
            _notificationBuffer.push( std::move( notification ) );
        }
        if ( removedID == kInvalidAccountID )
            return;
        PartyLobbyNotification removed;
        removed._recipientID    = removedID;
        removed._pushKind       = MatchmakingMethod::kPushLobby;
        removed._lobby._lobbyID = lobby._lobbyID;
        _notificationBuffer.push( std::move( removed ) );
    }

    void PartyLobbyService::completeParty( const Request& request, MatchmakingResult result, const PartySnapshot& party )
    {
        PartyLobbyCompletion completion;
        completion._operation  = request._operation;
        completion._requestTag = request._requestTag;
        completion._result     = result;
        if ( result == MatchmakingResult::Ok )
            completion._party = party;
        _completionBuffer.push( std::move( completion ) );
    }

    void PartyLobbyService::completeLobby( const Request& request, MatchmakingResult result, const LobbySnapshot& lobby )
    {
        PartyLobbyCompletion completion;
        completion._operation  = request._operation;
        completion._requestTag = request._requestTag;
        completion._result     = result;
        if ( result == MatchmakingResult::Ok )
            completion._lobby = lobby;
        _completionBuffer.push( std::move( completion ) );
    }
} // namespace sw
