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
                if ( bNameOk == false || bSizeOk == false || bCountOk == false || isValidMatchModeId( request._modeId ) == false )
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
            , _removedId{ kInvalidAccountId }
            , _brokenTicketId{ 0 }
        {
        }

        MatchmakingResult mutate( bool bExists, const vector<uint8>& oldBytes, vector<uint8>& outNewBytes, bool& outbErase ) override
        {
            PartySnapshot party;
            _removedId      = kInvalidAccountId;
            _brokenTicketId = 0;
            if ( _request._operation == PartyLobbyOperation::PartyCreate )
            {
                if ( bExists )
                    return MatchmakingResult::Conflict; // id 가 겹쳤다(서버 id + 순번이라 있을 수 없다)
                party._partyId        = _request._partyId;
                party._maxMemberCount = _pService->getMaxPartySize();
                party._listMemberId.push_back( _request._actorId );
                _snapshot   = party;
                outNewBytes = MatchmakingProtocol::encodeParty( party );
                return MatchmakingResult::Ok;
            }
            if ( bExists == false || MatchmakingProtocol::decodeParty( oldBytes, party ) == false )
            {
                _snapshot          = PartySnapshot{};
                _snapshot._partyId = _request._partyId;
                return MatchmakingResult::NotInParty;
            }
            _snapshot = party;
            switch ( _request._operation )
            {
                case PartyLobbyOperation::PartyInvite:
                {
                    if ( party.getLeaderId() != _request._actorId )
                        return MatchmakingResult::NotPartyLeader;
                    if ( party.hasMember( _request._targetId ) )
                        return MatchmakingResult::AlreadyInParty;
                    if ( static_cast<int32>( party._listMemberId.size() ) >= party._maxMemberCount )
                        return MatchmakingResult::PartyFull;
                    outNewBytes = oldBytes; // 같은 값으로 비교 후 쓰기 — 만료 연장 + 그새 해산되지 않았음을 확인
                    return MatchmakingResult::Ok;
                }
                case PartyLobbyOperation::PartyAccept:
                {
                    if ( party.hasMember( _request._actorId ) )
                        return MatchmakingResult::AlreadyInParty;
                    if ( static_cast<int32>( party._listMemberId.size() ) >= party._maxMemberCount )
                        return MatchmakingResult::PartyFull;
                    if ( party._queuedTicketId != 0 )
                        return MatchmakingResult::AlreadyQueued; // 줄 선 동안은 못 들어온다
                    party._listMemberId.push_back( _request._actorId );
                    break;
                }
                case PartyLobbyOperation::PartyLeave:
                case PartyLobbyOperation::PartyKick:
                {
                    const bool      bKick   = _request._operation == PartyLobbyOperation::PartyKick;
                    const AccountId leaving = bKick ? _request._targetId : _request._actorId;
                    if ( bKick && party.getLeaderId() != _request._actorId )
                        return MatchmakingResult::NotPartyLeader;
                    if ( bKick && leaving == _request._actorId )
                        return MatchmakingResult::Invalid; // 장은 자신을 내보내지 않는다(떠나기)
                    if ( party.hasMember( leaving ) == false )
                        return MatchmakingResult::NotInParty;
                    for ( size_t index = 0; index < party._listMemberId.size(); ++index )
                    {
                        if ( party._listMemberId[index] == leaving )
                        {
                            party._listMemberId.erase( party._listMemberId.begin() + static_cast<ptrdiff_t>( index ) ); // 장이 나가면 다음 사람이 첫째 — 장
                            break;
                        }
                    }
                    _brokenTicketId       = party._queuedTicketId; // 사람이 바뀌면 대기열에서 빠진다
                    party._queuedTicketId = 0;
                    _removedId            = leaving;
                    outbErase             = party._listMemberId.empty();
                    break;
                }
                case PartyLobbyOperation::PartySetTicket:
                {
                    party._queuedTicketId = _request._ticketId;
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
            _pService->finishParty( _request, result, party, bApplied ? _removedId : kInvalidAccountId, bApplied ? _brokenTicketId : 0 );
        }

    private:
        PartyLobbyService::Request _request;
        PartySnapshot              _snapshot;
        PartyLobbyService*         _pService;
        AccountId                  _removedId;
        uint64                     _brokenTicketId;
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
            , _removedId{ kInvalidAccountId }
            , _bErased{ SW_FALSE }
        {
        }

        MatchmakingResult mutate( bool bExists, const vector<uint8>& oldBytes, vector<uint8>& outNewBytes, bool& outbErase ) override
        {
            LobbySnapshot lobby;
            _removedId = kInvalidAccountId;
            _bErased   = SW_FALSE;
            if ( _request._operation == PartyLobbyOperation::LobbyCreate )
            {
                if ( bExists )
                    return MatchmakingResult::Conflict;
                lobby          = _request._lobby;
                lobby._lobbyId = _request._lobbyId;
                lobby._state   = LobbyState::Open;
                lobby._listMember.clear();
                lobby._listMember.push_back( LobbyMember{ _request._actorId, 0, SW_FALSE } );
                _snapshot   = lobby;
                outNewBytes = MatchmakingProtocol::encodeLobby( lobby );
                return MatchmakingResult::Ok;
            }
            if ( bExists == false || MatchmakingProtocol::decodeLobby( oldBytes, lobby ) == false )
            {
                _snapshot          = LobbySnapshot{};
                _snapshot._lobbyId = _request._lobbyId;
                return MatchmakingResult::NotInLobby;
            }
            _snapshot = lobby;
            switch ( _request._operation )
            {
                case PartyLobbyOperation::LobbyJoin:
                {
                    if ( lobby.hasMember( _request._actorId ) )
                    {
                        outNewBytes = oldBytes; // 이미 들어 있다 — 같은 값(만료 연장)
                        return MatchmakingResult::Ok;
                    }
                    if ( lobby._state != LobbyState::Open )
                        return MatchmakingResult::Invalid;
                    if ( static_cast<int32>( lobby._listMember.size() ) >= lobby._maxMemberCount )
                        return MatchmakingResult::LobbyFull;
                    lobby._listMember.push_back( LobbyMember{ _request._actorId, PartyLobbyServiceInternal::findSmallerTeam( lobby ), SW_FALSE } );
                    break;
                }
                case PartyLobbyOperation::LobbyLeave:
                {
                    bool bRemoved = false;
                    for ( size_t index = 0; index < lobby._listMember.size(); ++index )
                    {
                        if ( lobby._listMember[index]._accountId == _request._actorId )
                        {
                            lobby._listMember.erase( lobby._listMember.begin() + static_cast<ptrdiff_t>( index ) ); // 방장이 나가면 다음 사람이 방장
                            bRemoved = true;
                            break;
                        }
                    }
                    if ( bRemoved == false )
                        return MatchmakingResult::NotInLobby;
                    _removedId = _request._actorId;
                    outbErase  = lobby._listMember.empty();
                    _bErased   = outbErase ? SW_TRUE : SW_FALSE;
                    break;
                }
                case PartyLobbyOperation::LobbyReady:
                {
                    bool bFound = false;
                    for ( LobbyMember& member : lobby._listMember )
                    {
                        if ( member._accountId == _request._actorId )
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
                    if ( lobby.getOwnerId() != _request._actorId )
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
            _pService->finishLobby( _request, result, lobby, bApplied ? _removedId : kInvalidAccountId, bApplied && _bErased != SW_FALSE );
        }

    private:
        PartyLobbyService::Request _request;
        LobbySnapshot              _snapshot;
        PartyLobbyService*         _pService;
        AccountId                  _removedId;
        uint8                      _bErased;
    };
} // namespace sw

namespace sw
{
    PartyLobbyService::PartyLobbyService()
        : _updater{}
        , _mapRequestToStep{}
        , _mapListIdToList{}
        , _mapAccountToLobby{}
        , _completionBuffer{}
        , _notificationBuffer{}
        , _lobbyStartBuffer{}
        , _brokenTicketBuffer{}
        , _pRouter{ nullptr }
        , _serverId{ 0 }
        , _nextListId{ 1 }
        , _maxPartySize{ 4 }
        , _sequence{ 0 }
    {
    }

    PartyLobbyService::~PartyLobbyService() { shutdown(); }

    void PartyLobbyService::initialize( EphemeralStoreRouter* pRouter, uint64 serverId, int32 maxPartySize )
    {
        _pRouter      = pRouter;
        _serverId     = serverId;
        _maxPartySize = std::clamp( maxPartySize, 2, PartyLobbyLimit::kMaxPartySize );
        _updater.initialize( pRouter );
    }

    void PartyLobbyService::shutdown()
    {
        _updater.shutdown();
        if ( _pRouter != nullptr )
        {
            for ( const auto& [requestId, step] : _mapRequestToStep )
            {
                _pRouter->cancel( requestId );
            }
        }
        _mapRequestToStep.clear();
        _mapListIdToList.clear();
        _pRouter = nullptr;
    }

    string PartyLobbyService::makePartyKey( uint64 partyId ) { return "mm/party/" + ServiceKeyUtil::makeHex64( partyId ); }

    string PartyLobbyService::makeAccountPartyKey( AccountId accountId ) { return "mm/acct/" + ServiceKeyUtil::makeHex64( accountId ); }

    string PartyLobbyService::makeInviteKey( AccountId accountId, uint64 partyId )
    {
        return "mm/pinv/" + ServiceKeyUtil::makeHex64( accountId ) + "/" + ServiceKeyUtil::makeHex64( partyId );
    }

    string PartyLobbyService::makeLobbyKey( uint64 lobbyId ) { return "mm/lobby/" + ServiceKeyUtil::makeHex64( lobbyId ); }

    string PartyLobbyService::makeLobbyListKey( string_view modeId ) { return "mm/lobbies/" + string( modeId ); }

    void PartyLobbyService::submitStep( const EphemeralRequest& cacheRequest, StepKind kind, const Request& request, uint64 listId, int32 slot )
    {
        if ( _pRouter == nullptr )
            return;
        const uint64 requestId = _pRouter->submit( cacheRequest, EphemeralStoreRouter::ReplyDelegate::create<&PartyLobbyService::onStepReply>( this ) );
        PendingStep& step      = _mapRequestToStep[requestId];
        step._request          = request;
        step._kind             = kind;
        step._listId           = listId;
        step._slot             = slot;
    }

    void PartyLobbyService::submitFireAndForget( const EphemeralRequest& cacheRequest )
    {
        if ( _pRouter != nullptr )
            (void)_pRouter->submit( cacheRequest, EphemeralStoreRouter::ReplyDelegate{} );
    }

    void PartyLobbyService::createParty( AccountId accountId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyCreate;
        request._actorId    = accountId;
        request._partyId    = allocateId();
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeSet( makeAccountPartyKey( accountId ), MatchmakingProtocol::encodeId( request._partyId ), PartyLobbyLimit::kPartyTtlMs,
                                               EphemeralCondition::IfAbsent ),
                    StepKind::IndexReserve, request );
    }

    void PartyLobbyService::inviteToParty( AccountId leaderId, AccountId targetId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyInvite;
        request._actorId    = leaderId;
        request._targetId   = targetId;
        request._requestTag = requestTag;
        if ( targetId == kInvalidAccountId || targetId == leaderId )
        {
            completeParty( request, MatchmakingResult::Invalid, PartySnapshot{} );
            return;
        }
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( leaderId ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::acceptPartyInvite( AccountId accountId, uint64 partyId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyAccept;
        request._actorId    = accountId;
        request._partyId    = partyId;
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeGet( makeInviteKey( accountId, partyId ) ), StepKind::InviteRead, request );
    }

    void PartyLobbyService::leaveParty( AccountId accountId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyLeave;
        request._actorId    = accountId;
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( accountId ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::kickFromParty( AccountId leaderId, AccountId targetId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::PartyKick;
        request._actorId    = leaderId;
        request._targetId   = targetId;
        request._requestTag = requestTag;
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( leaderId ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::setPartyTicket( uint64 partyId, uint64 ticketId )
    {
        Request request;
        request._operation = PartyLobbyOperation::PartySetTicket;
        request._partyId   = partyId;
        request._ticketId  = ticketId;
        startPartyMutation( request );
    }

    void PartyLobbyService::findPartyOfAccount( AccountId accountId, uint64 lookupTag, const PartyFoundDelegate& onFound )
    {
        Request request;
        request._operation    = PartyLobbyOperation::PartyFind;
        request._actorId      = accountId;
        request._requestTag   = lookupTag;
        request._onPartyFound = onFound;
        if ( _pRouter == nullptr )
        {
            if ( onFound.isBound() )
                onFound( lookupTag, MatchmakingResult::Unavailable, PartySnapshot{} );
            return;
        }
        submitStep( EphemeralRequest::makeGet( makeAccountPartyKey( accountId ) ), StepKind::IndexRead, request );
    }

    void PartyLobbyService::createLobby( AccountId accountId, const LobbySnapshot& lobbyRequest, int64 nowMs, uint64 requestTag )
    {
        Request request;
        request._operation        = PartyLobbyOperation::LobbyCreate;
        request._actorId          = accountId;
        request._lobby            = lobbyRequest;
        request._lobby._createdMs = nowMs;
        request._lobbyId          = allocateId();
        request._nowMs            = nowMs;
        request._requestTag       = requestTag;
        if ( PartyLobbyServiceInternal::isValidLobbyRequest( lobbyRequest ) == false )
        {
            completeLobby( request, MatchmakingResult::Invalid, LobbySnapshot{} );
            return;
        }
        startLobbyMutation( request );
    }

    void PartyLobbyService::listLobbies( string_view modeId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyList;
        request._modeId     = string( modeId );
        request._requestTag = requestTag;
        if ( isValidMatchModeId( modeId ) == false )
        {
            completeLobby( request, MatchmakingResult::UnknownMode, LobbySnapshot{} );
            return;
        }
        submitStep( EphemeralRequest::makeScoreRange( makeLobbyListKey( modeId ), 0, PartyLobbyLimit::kMaxLobbyList ), StepKind::ListRange, request );
    }

    void PartyLobbyService::joinLobby( AccountId accountId, uint64 lobbyId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyJoin;
        request._actorId    = accountId;
        request._lobbyId    = lobbyId;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::leaveLobby( AccountId accountId, uint64 lobbyId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyLeave;
        request._actorId    = accountId;
        request._lobbyId    = lobbyId;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::setLobbyReady( AccountId accountId, uint64 lobbyId, bool bReady, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyReady;
        request._actorId    = accountId;
        request._lobbyId    = lobbyId;
        request._bReady     = bReady ? SW_TRUE : SW_FALSE;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::startLobby( AccountId accountId, uint64 lobbyId, uint64 requestTag )
    {
        Request request;
        request._operation  = PartyLobbyOperation::LobbyStart;
        request._actorId    = accountId;
        request._lobbyId    = lobbyId;
        request._requestTag = requestTag;
        startLobbyMutation( request );
    }

    void PartyLobbyService::reopenLobby( uint64 lobbyId )
    {
        Request request;
        request._operation = PartyLobbyOperation::LobbyReopen;
        request._lobbyId   = lobbyId;
        startLobbyMutation( request );
    }

    void PartyLobbyService::removeAccount( AccountId accountId )
    {
        leaveParty( accountId, 0 );
        const auto lobbyIt = _mapAccountToLobby.find( accountId );
        if ( lobbyIt != _mapAccountToLobby.end() )
            leaveLobby( accountId, lobbyIt->second, 0 );
    }

    void PartyLobbyService::startPartyMutation( const Request& request )
    {
        _updater.update( makePartyKey( request._partyId ), PartyLobbyLimit::kPartyTtlMs, make_unique<PartyMutation>( this, request ) );
    }

    void PartyLobbyService::startLobbyMutation( const Request& request )
    {
        _updater.update( makeLobbyKey( request._lobbyId ), PartyLobbyLimit::kLobbyTtlMs, make_unique<LobbyMutation>( this, request ) );
    }

    void PartyLobbyService::onStepReply( const EphemeralReply& reply )
    {
        const auto stepIt = _mapRequestToStep.find( reply._requestId );
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
                submitStep( EphemeralRequest::makeSet( makeAccountPartyKey( step._request._actorId ), MatchmakingProtocol::encodeId( step._request._partyId ),
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
                          party.hasMember( step._request._actorId ) == false )
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
                const uint64 listId  = _nextListId++;
                PendingList& list    = _mapListIdToList[listId];
                list._modeId         = step._request._modeId;
                list._requestTag     = step._request._requestTag;
                list._remainingCount = static_cast<int32>( reply._listMember.size() );
                list._listLobby.resize( reply._listMember.size() );
                list._listFound.assign( reply._listMember.size(), SW_FALSE );
                for ( size_t index = 0; index < reply._listMember.size(); ++index )
                {
                    uint64 lobbyId = 0;
                    if ( ServiceKeyUtil::parseHex64( reply._listMember[index]._member, lobbyId ) == false )
                        lobbyId = 0;
                    list._listLobby[index]._lobbyId = lobbyId;
                }
                for ( size_t index = 0; index < reply._listMember.size(); ++index )
                {
                    Request getRequest  = step._request;
                    getRequest._lobbyId = list._listLobby[index]._lobbyId;
                    submitStep( EphemeralRequest::makeGet( makeLobbyKey( getRequest._lobbyId ) ), StepKind::ListGet, getRequest, listId, static_cast<int32>( index ) );
                }
                break;
            }
            case StepKind::ListGet:
            {
                handleListGet( step._listId, step._slot, reply );
                break;
            }
        }
    }

    void PartyLobbyService::handleIndexRead( const Request& request, const EphemeralReply& reply )
    {
        const bool        bFind   = request._operation == PartyLobbyOperation::PartyFind;
        uint64            partyId = 0;
        MatchmakingResult result  = MatchmakingResult::Ok;
        if ( reply._result == EphemeralResult::NotFound )
            result = MatchmakingResult::NotInParty;
        else if ( reply._result != EphemeralResult::Ok )
            result = MatchmakingResult::Unavailable;
        else if ( MatchmakingProtocol::decodeId( reply._value, partyId ) == false )
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
        next._partyId = partyId;
        if ( bFind )
        {
            submitStep( EphemeralRequest::makeGet( makePartyKey( partyId ) ), StepKind::PartyRead, next );
            return;
        }
        startPartyMutation( next );
    }

    void PartyLobbyService::handleListGet( uint64 listId, int32 slot, const EphemeralReply& reply )
    {
        const auto listIt = _mapListIdToList.find( listId );
        if ( listIt == _mapListIdToList.end() )
            return;
        PendingList&  list    = listIt->second;
        const size_t  index   = static_cast<size_t>( slot );
        const uint64  lobbyId = list._listLobby[index]._lobbyId;
        LobbySnapshot lobby;
        if ( reply._result == EphemeralResult::Ok && MatchmakingProtocol::decodeLobby( reply._value, lobby ) )
        {
            list._listLobby[index] = std::move( lobby );
            list._listFound[index] = SW_TRUE;
        }
        else if ( reply._result == EphemeralResult::NotFound )
        {
            submitFireAndForget( EphemeralRequest::makeScoreRemove( makeLobbyListKey( list._modeId ), ServiceKeyUtil::makeHex64( lobbyId ) ) ); // 만료로 사라진 로비
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
        _mapListIdToList.erase( listIt );
        _completionBuffer.push( std::move( completion ) );
    }

    void PartyLobbyService::finishParty( const Request& request, MatchmakingResult result, const PartySnapshot& party, AccountId removedId, uint64 brokenTicketId )
    {
        const vector<uint8> indexValue = MatchmakingProtocol::encodeId( request._partyId );
        switch ( request._operation )
        {
            case PartyLobbyOperation::PartyCreate:
            case PartyLobbyOperation::PartyAccept:
            {
                if ( result != MatchmakingResult::Ok )
                {
                    submitFireAndForget( EphemeralRequest::makeCompareAndErase( makeAccountPartyKey( request._actorId ), indexValue ) ); // 잡아 둔 색인을 되돌린다
                    break;
                }
                if ( request._operation == PartyLobbyOperation::PartyAccept )
                {
                    submitFireAndForget( EphemeralRequest::makeErase( makeInviteKey( request._actorId, request._partyId ) ) );
                    refreshIndexTtl( party );
                }
                notifyParty( party, kInvalidAccountId ); // 만든 사람에게도 — 클라이언트의 "내 파티" 는 알림이 정본
                break;
            }
            case PartyLobbyOperation::PartyInvite:
            {
                if ( result != MatchmakingResult::Ok )
                    break;
                submitFireAndForget( EphemeralRequest::makeSet( makeInviteKey( request._targetId, request._partyId ), MatchmakingProtocol::encodeId( request._actorId ),
                                                                PartyLobbyLimit::kInviteTtlMs ) );
                PartyLobbyNotification notification;
                notification._recipientId       = request._targetId;
                notification._pushKind          = MatchmakingMethod::kPushPartyInvite;
                notification._invite._partyId   = request._partyId;
                notification._invite._inviterId = request._actorId;
                _notificationBuffer.push( std::move( notification ) );
                refreshIndexTtl( party );
                break;
            }
            case PartyLobbyOperation::PartyLeave:
            case PartyLobbyOperation::PartyKick:
            {
                if ( result == MatchmakingResult::NotInParty && request._operation == PartyLobbyOperation::PartyLeave && party._listMemberId.empty() )
                {
                    // 색인만 남았다(파티 기록이 만료) — 색인을 지우면 떠난 것과 같다
                    submitFireAndForget( EphemeralRequest::makeCompareAndErase( makeAccountPartyKey( request._actorId ), indexValue ) );
                    result = MatchmakingResult::Ok;
                    break;
                }
                if ( result != MatchmakingResult::Ok )
                    break;
                submitFireAndForget( EphemeralRequest::makeCompareAndErase( makeAccountPartyKey( removedId ), indexValue ) );
                if ( brokenTicketId != 0 )
                    _brokenTicketBuffer.push( brokenTicketId );
                refreshIndexTtl( party );
                notifyParty( party, removedId );
                break;
            }
            case PartyLobbyOperation::PartySetTicket:
            {
                if ( result == MatchmakingResult::Ok )
                    notifyParty( party, kInvalidAccountId );
                return; // 완료 없음
            }
            default:
            {
                break;
            }
        }
        completeParty( request, result, party );
    }

    void PartyLobbyService::finishLobby( const Request& request, MatchmakingResult result, const LobbySnapshot& lobby, AccountId removedId, bool bErased )
    {
        if ( result == MatchmakingResult::Ok )
        {
            switch ( request._operation )
            {
                case PartyLobbyOperation::LobbyCreate:
                {
                    const string listKey = makeLobbyListKey( lobby._modeId );
                    submitFireAndForget( EphemeralRequest::makeScoreSet( listKey, ServiceKeyUtil::makeHex64( lobby._lobbyId ), lobby._createdMs ) );
                    _mapAccountToLobby[request._actorId] = lobby._lobbyId;
                    break;
                }
                case PartyLobbyOperation::LobbyJoin:
                {
                    _mapAccountToLobby[request._actorId] = lobby._lobbyId;
                    notifyLobby( lobby, kInvalidAccountId );
                    break;
                }
                case PartyLobbyOperation::LobbyLeave:
                {
                    const auto lobbyIt = _mapAccountToLobby.find( request._actorId );
                    if ( lobbyIt != _mapAccountToLobby.end() && lobbyIt->second == request._lobbyId )
                        _mapAccountToLobby.erase( lobbyIt );
                    if ( bErased )
                        submitFireAndForget( EphemeralRequest::makeScoreRemove( makeLobbyListKey( lobby._modeId ), ServiceKeyUtil::makeHex64( request._lobbyId ) ) );
                    notifyLobby( lobby, removedId );
                    break;
                }
                case PartyLobbyOperation::LobbyStart:
                {
                    notifyLobby( lobby, kInvalidAccountId );
                    _lobbyStartBuffer.push( LobbyStartRequest{ lobby } );
                    break;
                }
                default:
                {
                    notifyLobby( lobby, kInvalidAccountId );
                    break;
                }
            }
            if ( bErased == false ) // 목록도 살아 있는 로비만큼 산다
                submitFireAndForget( EphemeralRequest::makeExpire( makeLobbyListKey( lobby._modeId ), PartyLobbyLimit::kLobbyTtlMs ) );
        }
        if ( request._operation == PartyLobbyOperation::LobbyReopen )
            return; // 완료 없음
        completeLobby( request, result, lobby );
    }

    void PartyLobbyService::refreshIndexTtl( const PartySnapshot& party )
    {
        for ( const AccountId memberId : party._listMemberId )
        {
            submitFireAndForget( EphemeralRequest::makeExpire( makeAccountPartyKey( memberId ), PartyLobbyLimit::kPartyTtlMs ) ); // 파티 기록과 함께 연장
        }
    }

    void PartyLobbyService::notifyParty( const PartySnapshot& party, AccountId removedId )
    {
        for ( const AccountId memberId : party._listMemberId )
        {
            PartyLobbyNotification notification;
            notification._recipientId = memberId;
            notification._pushKind    = MatchmakingMethod::kPushParty;
            notification._party       = party;
            _notificationBuffer.push( std::move( notification ) );
        }
        if ( removedId == kInvalidAccountId )
            return;
        PartyLobbyNotification removed;
        removed._recipientId    = removedId;
        removed._pushKind       = MatchmakingMethod::kPushParty;
        removed._party._partyId = party._partyId; // 빈 회원 — 이 파티에서 빠졌다
        _notificationBuffer.push( std::move( removed ) );
    }

    void PartyLobbyService::notifyLobby( const LobbySnapshot& lobby, AccountId removedId )
    {
        for ( const LobbyMember& member : lobby._listMember )
        {
            PartyLobbyNotification notification;
            notification._recipientId = member._accountId;
            notification._pushKind    = MatchmakingMethod::kPushLobby;
            notification._lobby       = lobby;
            _notificationBuffer.push( std::move( notification ) );
        }
        if ( removedId == kInvalidAccountId )
            return;
        PartyLobbyNotification removed;
        removed._recipientId    = removedId;
        removed._pushKind       = MatchmakingMethod::kPushLobby;
        removed._lobby._lobbyId = lobby._lobbyId;
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
