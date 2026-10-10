#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct PartyLobbyServiceInternal
        {
            static bool isValidSettingKey( string_view key )
            {
                if ( key.empty() || key.size() > static_cast<size_t>( PartyLobbyLimit::kMaxSettingKeySize ) )
                    return false;
                for ( const utf8 ch : key )
                {
                    const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
                    if ( bAllowed == false )
                        return false;
                }
                return true;
            }

            static bool isValidLobbyRequest( const LobbySnapshot& request )
            {
                const bool bNameOk  = request._name.empty() == false && request._name.size() <= static_cast<size_t>( PartyLobbyLimit::kMaxLobbyNameSize );
                const bool bSizeOk  = PartyLobbyLimit::kMinLobbySize <= request._maxMemberCount && request._maxMemberCount <= PartyLobbyLimit::kMaxLobbySize;
                const bool bCountOk = static_cast<int32>( request._listSetting.size() ) <= PartyLobbyLimit::kMaxLobbySetting;
                if ( bNameOk == false || bSizeOk == false || bCountOk == false || isValidMatchModeID( request._modeID ) == false )
                    return false;
                for ( const LobbySetting& setting : request._listSetting )
                {
                    if ( isValidSettingKey( setting._key ) == false || setting._value.size() > static_cast<size_t>( PartyLobbyLimit::kMaxSettingValueSize ) )
                        return false;
                }
                return true;
            }

            /** @brief 인원이 적은 편(같으면 0)입니다. */
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
    /** @brief 파티 기록 하나의 바꾸기 — 연산마다 규칙(순수)이고, 끝나면 서비스에 넘긴다. 계정 색인은 서비스가 성공 · 실패 뒤 따로 맞춘다. */
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
                    return MatchmakingResult::Conflict; // id 가 겹쳤다(서버 id + 순번이라 있을 수 없다)
                party._partyID        = _request._partyID;
                party._maxMemberCount = _pService->getMaxPartySize();
                party._listMemberID.push_back( _request._actorID );
                _snapshot   = party;
                outNewBytes = MatchmakingProtocol::encodeParty( party );
                return MatchmakingResult::Ok;
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
                    outNewBytes = oldBytes; // 같은 값으로 비교 후 쓰기 — 만료 연장 + 그새 해산되지 않았음을 확인
                    return MatchmakingResult::Ok;
                }
                case PartyLobbyOperation::PartyAccept:
                {
                    if ( party.hasMember( _request._actorID ) )
                        return MatchmakingResult::AlreadyInParty;
                    if ( static_cast<int32>( party._listMemberID.size() ) >= party._maxMemberCount )
                        return MatchmakingResult::PartyFull;
                    if ( party._queuedTicketID != 0 )
                        return MatchmakingResult::AlreadyQueued; // 줄 선 동안은 못 들어온다
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
                        return MatchmakingResult::Invalid; // 장은 자신을 내보내지 않는다(떠나기)
                    if ( party.hasMember( leaving ) == false )
                        return MatchmakingResult::NotInParty;
                    for ( size_t index = 0; index < party._listMemberID.size(); ++index )
                    {
                        if ( party._listMemberID[index] == leaving )
                        {
                            party._listMemberID.erase( party._listMemberID.begin() + static_cast<ptrdiff_t>( index ) ); // 장이 나가면 다음 사람이 첫째 — 장
                            break;
                        }
                    }
                    _brokenTicketID       = party._queuedTicketID; // 사람이 바뀌면 대기열에서 빠진다
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
            _snapshot   = party;
            outNewBytes = MatchmakingProtocol::encodeParty( party );
            return MatchmakingResult::Ok;
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
    /** @brief 로비 기록 하나의 바꾸기입니다(파티와 같은 모양). */
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
                _snapshot   = lobby;
                outNewBytes = MatchmakingProtocol::encodeLobby( lobby );
                return MatchmakingResult::Ok;
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
                        outNewBytes = oldBytes; // 이미 들어 있다 — 같은 값(만료 연장)
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
                            lobby._listMember.erase( lobby._listMember.begin() + static_cast<ptrdiff_t>( index ) ); // 방장이 나가면 다음 사람이 방장
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
            _snapshot   = lobby;
            outNewBytes = MatchmakingProtocol::encodeLobby( lobby );
            return MatchmakingResult::Ok;
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

    PartyLobbyService::~PartyLobbyService() { shutdown(); }

    void PartyLobbyService::initialize( EphemeralStoreRouter* pRouter, uint64 serverID, int32 maxPartySize )
    {
        _pRouter      = pRouter;
        _serverID     = serverID;
        _maxPartySize = std::clamp( maxPartySize, 2, PartyLobbyLimit::kMaxPartySize );
        _updater.initialize( pRouter );
    }

    void PartyLobbyService::shutdown()
    {
        _updater.shutdown();
        if ( _pRouter != nullptr )
        {
            for ( const auto& [requestID, step] : _mapRequestToStep )
            {
                _pRouter->cancel( requestID );
            }
        }
        _mapRequestToStep.clear();
        _mapListIDToList.clear();
        _pRouter = nullptr;
    }

    string PartyLobbyService::makePartyKey( uint64 partyID ) { return "mm/party/" + ServiceKeyUtil::makeHex64( partyID ); }

    string PartyLobbyService::makeAccountPartyKey( AccountID accountID ) { return "mm/acct/" + ServiceKeyUtil::makeHex64( accountID ); }

    string PartyLobbyService::makeInviteKey( AccountID accountID, uint64 partyID )
    {
        return "mm/pinv/" + ServiceKeyUtil::makeHex64( accountID ) + "/" + ServiceKeyUtil::makeHex64( partyID );
    }

    string PartyLobbyService::makeLobbyKey( uint64 lobbyID ) { return "mm/lobby/" + ServiceKeyUtil::makeHex64( lobbyID ); }

    string PartyLobbyService::makeLobbyListKey( string_view modeID ) { return "mm/lobbies/" + string( modeID ); }

    void PartyLobbyService::submitStep( const EphemeralRequest& cacheRequest, StepKind kind, const Request& request, uint64 listID, int32 slot )
    {
        if ( _pRouter == nullptr )
            return;
        const uint64 requestID = _pRouter->submit( cacheRequest, EphemeralStoreRouter::ReplyDelegate::create<&PartyLobbyService::onStepReply>( this ) );
        PendingStep& step      = _mapRequestToStep[requestID];
        step._request          = request;
        step._kind             = kind;
        step._listID           = listID;
        step._slot             = slot;
    }

    void PartyLobbyService::submitFireAndForget( const EphemeralRequest& cacheRequest )
    {
        if ( _pRouter != nullptr )
            (void)_pRouter->submit( cacheRequest, EphemeralStoreRouter::ReplyDelegate{} );
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
                    result = MatchmakingResult::Unavailable;
                else if ( reply._result != EphemeralResult::Ok || MatchmakingProtocol::decodeParty( reply._value, party ) == false ||
                          party.hasMember( step._request._actorID ) == false )
                    result = MatchmakingResult::NotInParty; // 색인만 남았다(파티 만료)
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
            submitFireAndForget( EphemeralRequest::makeScoreRemove( makeLobbyListKey( list._modeID ), ServiceKeyUtil::makeHex64( lobbyID ) ) ); // 만료로 사라진 로비
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
                    submitFireAndForget( EphemeralRequest::makeCompareAndErase( makeAccountPartyKey( request._actorID ), indexValue ) ); // 잡아 둔 색인을 되돌린다
                    break;
                }
                if ( request._operation == PartyLobbyOperation::PartyAccept )
                {
                    submitFireAndForget( EphemeralRequest::makeErase( makeInviteKey( request._actorID, request._partyID ) ) );
                    refreshIndexTtl( party );
                }
                notifyParty( party, kInvalidAccountID ); // 만든 사람에게도 — 클라이언트의 "내 파티" 는 알림이 정본
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
                if ( result == MatchmakingResult::NotInParty && request._operation == PartyLobbyOperation::PartyLeave && party._listMemberID.empty() )
                {
                    // 색인만 남았다(파티 기록이 만료) — 색인을 지우면 떠난 것과 같다
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
                return; // 완료 없음
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
            if ( bErased == false ) // 목록도 살아 있는 로비만큼 산다
                submitFireAndForget( EphemeralRequest::makeExpire( makeLobbyListKey( lobby._modeID ), PartyLobbyLimit::kLobbyTtlMs ) );
        }
        if ( request._operation == PartyLobbyOperation::LobbyReopen )
            return; // 완료 없음
        completeLobby( request, result, lobby );
    }

    void PartyLobbyService::refreshIndexTtl( const PartySnapshot& party )
    {
        for ( const AccountID memberID : party._listMemberID )
        {
            submitFireAndForget( EphemeralRequest::makeExpire( makeAccountPartyKey( memberID ), PartyLobbyLimit::kPartyTtlMs ) ); // 파티 기록과 함께 연장
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
        removed._party._partyID = party._partyID; // 빈 회원 — 이 파티에서 빠졌다
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
