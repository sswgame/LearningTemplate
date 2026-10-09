#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Matchmaking/Service/MatchQueueService.h"

#include "Core/Network/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct MatchQueueServiceInternal
        {
            static constexpr const utf8* kQueuePrefix  = "mm.queue.";
            static constexpr const utf8* kCancelPrefix = "mm.cancel.";
            static constexpr int32       kIdShift      = 32;

            static bool hasPrefix( string_view text, string_view prefix ) { return text.size() > prefix.size() && StringUtil::startsWith( text, prefix ); }

            static void publish( IServerBus* pBus, const string& topic, const BitWriter& body )
            {
                pBus->publish( topic, body.getBytes().data(), body.getByteCount() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MatchQueueService::MatchQueueService()
        : _mapMode{}
        , _mapModeState{}
        , _mapLocalTicket{}
        , _mapAccountToTicket{}
        , _mapLookupToJoin{}
        , _mapLeaseRequestToMode{}
        , _completionBuffer{}
        , _notificationBuffer{}
        , _topicBuffer{}
        , _dependencies{}
        , _nextLookupTag{ 1 }
        , _sequence{ 0 }
        , _lobbySequence{ 0 }
    {
    }

    MatchQueueService::~MatchQueueService() { shutdown(); }

    string MatchQueueService::makeLeaseKey( string_view modeId ) { return "mm/lease/" + string( modeId ); }

    string MatchQueueService::makeQueueTopic( string_view modeId ) { return MatchQueueServiceInternal::kQueuePrefix + string( modeId ); }

    string MatchQueueService::makeCancelTopic( string_view modeId ) { return MatchQueueServiceInternal::kCancelPrefix + string( modeId ); }

    string MatchQueueService::makeResultTopic( uint64 serverId ) { return "mm.result." + ServiceKeyUtil::makeHex64( serverId ); }

    string MatchQueueService::makeAssignTopic( uint64 serverId ) { return "mm.assign." + ServiceKeyUtil::makeHex64( serverId ); }

    void MatchQueueService::initialize( const MatchQueueDependencies& dependencies, const vector<MatchModeDefinition>& listMode )
    {
        SW_ASSERT( dependencies._pRouter != nullptr );
        _dependencies = dependencies;
        for ( const MatchModeDefinition& mode : listMode )
        {
            if ( isValidMatchModeId( mode._modeId ) == false )
            {
                SW_LOG_ERROR( "MatchQueue: mode id '%#' breaks the id rule — skipped", mode._modeId );
                continue;
            }
            _mapMode[mode._modeId] = mode;
            ModeState& state       = _mapModeState[mode._modeId];
            state._maker.initialize( mode, dependencies._serverId );
            state._reader = make_unique<ServerRegistryReader>();
            state._reader->initialize( dependencies._pRouter, mode._serverKind, dependencies._registryRefreshPeriodMs );
            if ( dependencies._pBus == nullptr )
                state._bAuthority = SW_TRUE; // 서버 한 대 — 늘 권한
        }
        if ( dependencies._pBus != nullptr )
            _topicBuffer.push( MatchQueueTopicChange{ makeResultTopic( dependencies._serverId ), SW_TRUE } );
    }

    void MatchQueueService::shutdown()
    {
        if ( _dependencies._pRouter != nullptr )
        {
            for ( const auto& [requestId, modeId] : _mapLeaseRequestToMode )
            {
                _dependencies._pRouter->cancel( requestId );
            }
        }
        _mapLeaseRequestToMode.clear();
        for ( auto& [modeId, state] : _mapModeState )
        {
            if ( state._reader != nullptr )
                state._reader->shutdown();
        }
        _mapModeState.clear();
        _mapLookupToJoin.clear();
        _dependencies._pRouter = nullptr;
        _dependencies._pBus    = nullptr;
    }

    bool MatchQueueService::hasMode( string_view modeId ) const { return _mapMode.find( string( modeId ) ) != _mapMode.end(); }

    MatchQueueService::ModeState* MatchQueueService::findModeState( string_view modeId )
    {
        const auto stateIt = _mapModeState.find( string( modeId ) );
        return stateIt == _mapModeState.end() ? nullptr : &stateIt->second;
    }

    bool MatchQueueService::isAuthority( string_view modeId ) const
    {
        const auto stateIt = _mapModeState.find( string( modeId ) );
        return stateIt != _mapModeState.end() && stateIt->second._bAuthority != SW_FALSE;
    }

    int32 MatchQueueService::getQueuedTicketCount( string_view modeId ) const
    {
        const auto stateIt = _mapModeState.find( string( modeId ) );
        return stateIt == _mapModeState.end() ? 0 : stateIt->second._maker.getTicketCount();
    }

    int32 MatchQueueService::findRating( AccountId accountId, string_view modeId ) const
    {
        return _dependencies._pRatingSource != nullptr ? _dependencies._pRatingSource->findRating( accountId, modeId ) : MatchQueueLimit::kDefaultRating;
    }

    void MatchQueueService::tick( int64 nowMs )
    {
        if ( _dependencies._pRouter == nullptr )
            return;
        for ( auto& [modeId, state] : _mapModeState )
        {
            if ( _dependencies._pBus != nullptr )
                tickLease( modeId, state, nowMs );
            state._reader->tick( nowMs ); // 로비 배정은 권한이 아니어도 고른다
            if ( state._bAuthority != SW_FALSE )
                placeMatches( state, nowMs );
        }
        // 이 서버가 낸 표의 시한 — 권한이 넘어가 표를 잃었으면 결과가 오지 않는다
        vector<uint64> listExpired;
        for ( const auto& [ticketId, ticket] : _mapLocalTicket )
        {
            if ( nowMs > ticket._deadlineMs )
                listExpired.push_back( ticketId );
        }
        for ( const uint64 ticketId : listExpired )
        {
            MatchAssignment assignment;
            assignment._ticketId = ticketId;
            assignment._modeId   = _mapLocalTicket[ticketId]._modeId;
            assignment._outcome  = MatchQueueOutcome::Timeout;
            notifyLocalTicket( ticketId, assignment );
        }
    }

    void MatchQueueService::tickLease( const string& modeId, ModeState& state, int64 nowMs )
    {
        if ( state._leaseRequestId != 0 )
            return; // 답을 기다린다
        if ( state._bLeaseAttempted != SW_FALSE && nowMs - state._leaseAttemptMs < MatchQueueLimit::kLeaseRenewMs )
            return;
        const vector<uint8>    selfValue              = MatchmakingProtocol::encodeId( _dependencies._serverId );
        const EphemeralRequest request                = state._bAuthority != SW_FALSE
                                                          ? EphemeralRequest::makeCompareAndSet( makeLeaseKey( modeId ), selfValue, selfValue, MatchQueueLimit::kLeaseTtlMs )
                                                          : EphemeralRequest::makeSet( makeLeaseKey( modeId ), selfValue, MatchQueueLimit::kLeaseTtlMs, EphemeralCondition::IfAbsent );
        state._leaseRequestId                         = _dependencies._pRouter->submit( request, EphemeralStoreRouter::ReplyDelegate::create<&MatchQueueService::onLeaseReply>( this ) );
        state._leaseAttemptMs                         = nowMs;
        state._bLeaseAttempted                        = SW_TRUE;
        _mapLeaseRequestToMode[state._leaseRequestId] = modeId;
    }

    void MatchQueueService::onLeaseReply( const EphemeralReply& reply )
    {
        const auto modeIt = _mapLeaseRequestToMode.find( reply._requestId );
        if ( modeIt == _mapLeaseRequestToMode.end() )
            return;
        const string modeId = modeIt->second;
        _mapLeaseRequestToMode.erase( modeIt );
        ModeState* pState = findModeState( modeId );
        if ( pState == nullptr )
            return;
        pState->_leaseRequestId = 0;
        if ( reply._result == EphemeralResult::Unavailable || reply._result == EphemeralResult::Invalid )
            return; // 캐시가 아프다 — 지금 상태로 다음 주기에 다시
        const bool bHold = reply._result == EphemeralResult::Ok;
        if ( bHold == ( pState->_bAuthority != SW_FALSE ) )
            return;
        pState->_bAuthority = bHold ? SW_TRUE : SW_FALSE;
        _topicBuffer.push( MatchQueueTopicChange{ makeQueueTopic( modeId ), static_cast<uint8>( bHold ? SW_TRUE : SW_FALSE ) } );
        _topicBuffer.push( MatchQueueTopicChange{ makeCancelTopic( modeId ), static_cast<uint8>( bHold ? SW_TRUE : SW_FALSE ) } );
        if ( bHold == false )
        {
            // 권한을 잃었다 — 매처를 비운다(표는 새 권한 서버가 모르는 채 잃는다 — 낸 서버의 시한이 알린다)
            pState->_maker.initialize( _mapMode[modeId], _dependencies._serverId );
            pState->_listUnplaced.clear();
        }
        SW_LOG_INFO( "MatchQueue: server %# %# authority for mode '%#'", _dependencies._serverId, bHold ? "took" : "lost", modeId );
    }

    void MatchQueueService::joinQueue( AccountId accountId, string_view modeId, string_view region, int64 nowMs, uint64 requestTag )
    {
        if ( hasMode( modeId ) == false )
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingResult::UnknownMode } );
            return;
        }
        bool bLookupPending = false;
        for ( const auto& [lookupTag, join] : _mapLookupToJoin )
        {
            bLookupPending = bLookupPending || join._accountId == accountId;
        }
        if ( _mapAccountToTicket.count( accountId ) != 0 || bLookupPending )
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingResult::AlreadyQueued } );
            return;
        }
        PendingJoin join;
        join._accountId  = accountId;
        join._modeId     = string( modeId );
        join._region     = string( region );
        join._requestTag = requestTag;
        join._nowMs      = nowMs;
        if ( _dependencies._pPartyLobby == nullptr )
        {
            submitTicket( join, vector<AccountId>{ accountId }, 0 );
            return;
        }
        // 파티 장이면 파티 전체를 — 파티 기록을 읽은 뒤(onPartyFound)
        const uint64 lookupTag      = _nextLookupTag++;
        _mapLookupToJoin[lookupTag] = join;
        _dependencies._pPartyLobby->findPartyOfAccount( accountId, lookupTag, PartyLobbyService::PartyFoundDelegate::create<&MatchQueueService::onPartyFound>( this ) );
    }

    void MatchQueueService::onPartyFound( uint64 lookupTag, MatchmakingResult result, const PartySnapshot& party )
    {
        const auto joinIt = _mapLookupToJoin.find( lookupTag );
        if ( joinIt == _mapLookupToJoin.end() )
            return;
        const PendingJoin join = joinIt->second;
        _mapLookupToJoin.erase( joinIt );
        if ( result == MatchmakingResult::NotInParty )
        {
            submitTicket( join, vector<AccountId>{ join._accountId }, 0 );
            return;
        }
        if ( result != MatchmakingResult::Ok )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, result } );
            return;
        }
        if ( party.getLeaderId() != join._accountId )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::NotPartyLeader } );
            return;
        }
        bool bMemberQueued = party._queuedTicketId != 0;
        for ( const AccountId memberId : party._listMemberId )
        {
            bMemberQueued = bMemberQueued || _mapAccountToTicket.count( memberId ) != 0;
        }
        if ( bMemberQueued )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::AlreadyQueued } );
            return;
        }
        submitTicket( join, party._listMemberId, party._partyId );
    }

    void MatchQueueService::submitTicket( const PendingJoin& join, const vector<AccountId>& listAccount, uint64 partyId )
    {
        const MatchModeDefinition& mode = _mapMode[join._modeId];
        MatchTicket                ticket;
        ticket._ticketId       = ( _dependencies._serverId << MatchQueueServiceInternal::kIdShift ) | ++_sequence;
        ticket._region         = join._region;
        ticket._partyId        = partyId;
        ticket._originServerId = _dependencies._serverId;
        ticket._enqueuedMs     = join._nowMs;
        for ( const AccountId accountId : listAccount )
        {
            ticket._listMember.push_back( MatchMember{ accountId, findRating( accountId, join._modeId ) } );
        }
        if ( static_cast<int32>( ticket._listMember.size() ) > mode._teamSize )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::PartyTooLarge } );
            return;
        }

        LocalTicket& local = _mapLocalTicket[ticket._ticketId];
        local._listAccount = listAccount;
        local._modeId      = join._modeId;
        local._ticketId    = ticket._ticketId;
        local._partyId     = partyId;
        local._deadlineMs  = join._nowMs + mode._maxWaitMs + MatchQueueLimit::kNoServerGiveUpMs + MatchQueueLimit::kResultGraceMs;
        for ( const AccountId accountId : listAccount )
        {
            _mapAccountToTicket[accountId] = ticket._ticketId;
        }
        if ( partyId != 0 && _dependencies._pPartyLobby != nullptr )
            _dependencies._pPartyLobby->setPartyTicket( partyId, ticket._ticketId );
        _completionBuffer.push( MatchQueueCompletion{ join._requestTag, ticket._ticketId, MatchmakingResult::Ok } );

        ModeState* pState = findModeState( join._modeId );
        if ( pState->_bAuthority != SW_FALSE )
        {
            (void)pState->_maker.addTicket( ticket );
            return;
        }
        if ( _dependencies._pBus == nullptr )
            return;
        BitWriter body;
        MatchmakingProtocol::writeTicket( body, ticket );
        MatchQueueServiceInternal::publish( _dependencies._pBus, makeQueueTopic( join._modeId ), body );
    }

    void MatchQueueService::placeMatches( ModeState& state, int64 nowMs )
    {
        vector<MatchFormed> listFormed;
        vector<MatchTicket> listTimedOut;
        state._maker.process( nowMs, listFormed, listTimedOut );
        for ( const MatchTicket& ticket : listTimedOut )
        {
            MatchAssignment assignment;
            assignment._ticketId = ticket._ticketId;
            assignment._modeId   = state._maker.getDefinition()._modeId;
            assignment._outcome  = MatchQueueOutcome::Timeout;
            deliverToTicket( ticket._originServerId, assignment );
        }
        for ( MatchFormed& match : listFormed )
        {
            state._listUnplaced.push_back( UnplacedMatch{ std::move( match ), nowMs } );
        }
        for ( size_t index = 0; index < state._listUnplaced.size(); )
        {
            const UnplacedMatch& unplaced = state._listUnplaced[index];
            const bool           bGiveUp  = nowMs - unplaced._formedMs >= MatchQueueLimit::kNoServerGiveUpMs;
            // 자리가 없으면 다음 틱에 다시(서버 목록이 바뀌길) — 오래 기다렸으면 NoServer 로 끝낸다
            if ( placeMatch( state, unplaced._match, bGiveUp ? MatchQueueOutcome::NoServer : MatchQueueOutcome::Count, nowMs ) == false && bGiveUp == false )
            {
                ++index;
                continue;
            }
            state._listUnplaced.erase( state._listUnplaced.begin() + static_cast<ptrdiff_t>( index ) );
        }
    }

    void MatchQueueService::fillTeam( const MatchFormed& match, AccountId accountId, MatchAssignment& inoutAssignment )
    {
        for ( size_t team = 0; team < match._listTeam.size(); ++team )
        {
            for ( const MatchMember& member : match._listTeam[team] )
            {
                if ( member._accountId != accountId )
                    continue;
                inoutAssignment._team = static_cast<int32>( team );
                inoutAssignment._listTeammate.clear();
                for ( const MatchMember& mate : match._listTeam[team] )
                {
                    inoutAssignment._listTeammate.push_back( mate._accountId );
                }
                return;
            }
        }
    }

    bool MatchQueueService::placeMatch( ModeState& state, const MatchFormed& match, MatchQueueOutcome failOutcome, int64 nowMs )
    {
        const MatchModeDefinition& mode = _mapMode[match._modeId];
        ServerSelectionQuery       query;
        query._kind         = mode._serverKind;
        query._region       = match._region;
        query._buildVersion = mode._buildVersion;
        query._seatCount    = 0;
        for ( const vector<MatchMember>& team : match._listTeam )
        {
            query._seatCount += static_cast<int32>( team.size() );
        }
        ServerStatus picked;
        const bool   bPlaced = state._reader->pickServer( query, nowMs, picked );
        if ( bPlaced == false && failOutcome == MatchQueueOutcome::Count )
            return false;

        if ( bPlaced && _dependencies._pBus != nullptr ) // 게임 서버에 올 사람을 먼저 알린다
        {
            BitWriter body;
            MatchmakingProtocol::writeFormed( body, match );
            MatchQueueServiceInternal::publish( _dependencies._pBus, makeAssignTopic( picked._descriptor._serverId ), body );
        }
        for ( const MatchTicket& ticket : match._listTicket )
        {
            MatchAssignment assignment;
            assignment._ticketId = ticket._ticketId;
            assignment._matchId  = match._matchId;
            assignment._modeId   = match._modeId;
            assignment._outcome  = bPlaced ? MatchQueueOutcome::Found : failOutcome;
            if ( bPlaced )
            {
                assignment._serverId = picked._descriptor._serverId;
                assignment._address  = picked._descriptor._address;
                assignment._port     = picked._descriptor._port;
            }
            if ( ticket._listMember.empty() == false )
                fillTeam( match, ticket._listMember.front()._accountId, assignment ); // 표 하나는 한 팀
            deliverToTicket( ticket._originServerId, assignment );
        }
        if ( bPlaced == false )
            SW_LOG_WARNING( "MatchQueue: no '%#' server for match %# in region '%#' — gave up", mode._serverKind, match._matchId, match._region );
        return bPlaced;
    }

    void MatchQueueService::placeLobby( const LobbySnapshot& lobby, int64 nowMs )
    {
        ModeState*  pState = findModeState( lobby._modeId );
        MatchFormed match;
        match._modeId  = lobby._modeId;
        match._matchId = ( _dependencies._serverId << MatchQueueServiceInternal::kIdShift ) | MatchQueueLimit::kLobbyMatchBit | ++_lobbySequence;
        match._listTeam.resize( static_cast<size_t>( PartyLobbyLimit::kLobbyTeamCount ) );
        for ( const LobbyMember& member : lobby._listMember )
        {
            const size_t team = static_cast<size_t>( std::clamp( member._team, 0, PartyLobbyLimit::kLobbyTeamCount - 1 ) );
            match._listTeam[team].push_back( MatchMember{ member._accountId, findRating( member._accountId, lobby._modeId ) } );
        }
        ServerStatus picked;
        bool         bPlaced = false;
        if ( pState != nullptr )
        {
            const MatchModeDefinition& mode = _mapMode[lobby._modeId];
            ServerSelectionQuery       query;
            query._kind              = mode._serverKind;
            query._buildVersion      = mode._buildVersion;
            query._seatCount         = static_cast<int32>( lobby._listMember.size() );
            query._bAllowOtherRegion = SW_TRUE; // 로비는 지역을 고르지 않았다
            bPlaced                  = pState->_reader->pickServer( query, nowMs, picked );
        }
        if ( bPlaced && _dependencies._pBus != nullptr )
        {
            BitWriter body;
            MatchmakingProtocol::writeFormed( body, match );
            MatchQueueServiceInternal::publish( _dependencies._pBus, makeAssignTopic( picked._descriptor._serverId ), body );
        }
        for ( const LobbyMember& member : lobby._listMember )
        {
            MatchAssignment assignment;
            assignment._matchId = match._matchId;
            assignment._modeId  = lobby._modeId;
            assignment._outcome = bPlaced ? MatchQueueOutcome::Found : MatchQueueOutcome::NoServer;
            if ( bPlaced )
            {
                assignment._serverId = picked._descriptor._serverId;
                assignment._address  = picked._descriptor._address;
                assignment._port     = picked._descriptor._port;
            }
            fillTeam( match, member._accountId, assignment );
            _notificationBuffer.push( MatchQueueNotification{ assignment, member._accountId } );
        }
        if ( bPlaced == false && _dependencies._pPartyLobby != nullptr )
            _dependencies._pPartyLobby->reopenLobby( lobby._lobbyId ); // 다시 시작할 수 있게
    }

    void MatchQueueService::deliverToTicket( uint64 originServerId, const MatchAssignment& assignment )
    {
        if ( originServerId == _dependencies._serverId || _dependencies._pBus == nullptr )
        {
            notifyLocalTicket( assignment._ticketId, assignment );
            return;
        }
        BitWriter body;
        MatchmakingProtocol::writeAssignment( body, assignment );
        MatchQueueServiceInternal::publish( _dependencies._pBus, makeResultTopic( originServerId ), body );
    }

    void MatchQueueService::notifyLocalTicket( uint64 ticketId, const MatchAssignment& assignment )
    {
        const auto ticketIt = _mapLocalTicket.find( ticketId );
        if ( ticketIt == _mapLocalTicket.end() )
            return; // 이미 끝냄(시한 뒤 늦은 결과 · 빠진 표)
        for ( const AccountId accountId : ticketIt->second._listAccount )
        {
            _notificationBuffer.push( MatchQueueNotification{ assignment, accountId } );
            const auto accountIt = _mapAccountToTicket.find( accountId );
            if ( accountIt != _mapAccountToTicket.end() && accountIt->second == ticketId )
                _mapAccountToTicket.erase( accountIt );
        }
        if ( ticketIt->second._partyId != 0 && _dependencies._pPartyLobby != nullptr )
            _dependencies._pPartyLobby->setPartyTicket( ticketIt->second._partyId, 0 );
        _mapLocalTicket.erase( ticketIt );
    }

    void MatchQueueService::removeFromQueue( const string& modeId, uint64 ticketId )
    {
        ModeState* pState = findModeState( modeId );
        if ( pState != nullptr && pState->_bAuthority != SW_FALSE )
        {
            (void)pState->_maker.removeTicket( ticketId ); // 이미 짝지어졌거나 없는 표면 false — 지울 것이 없다
            return;
        }
        if ( _dependencies._pBus == nullptr )
            return;
        BitWriter body;
        body.writeVarUint( ticketId );
        MatchQueueServiceInternal::publish( _dependencies._pBus, makeCancelTopic( modeId ), body );
    }

    void MatchQueueService::handleBusMessage( string_view topic, const vector<uint8>& bytes, int64 nowMs )
    {
        using Internal = MatchQueueServiceInternal;
        (void)nowMs;
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( Internal::hasPrefix( topic, Internal::kQueuePrefix ) )
        {
            MatchTicket ticket;
            ModeState*  pState = findModeState( topic.substr( string_view( Internal::kQueuePrefix ).size() ) );
            if ( pState != nullptr && pState->_bAuthority != SW_FALSE && MatchmakingProtocol::readTicket( reader, ticket ) )
                (void)pState->_maker.addTicket( ticket );
            return;
        }
        if ( Internal::hasPrefix( topic, Internal::kCancelPrefix ) )
        {
            const uint64 ticketId = reader.readVarUint();
            ModeState*   pState   = findModeState( topic.substr( string_view( Internal::kCancelPrefix ).size() ) );
            if ( pState != nullptr && pState->_bAuthority != SW_FALSE && reader.hasOverflowed() == false )
                (void)pState->_maker.removeTicket( ticketId ); // 이미 짝지어졌거나 없는 표면 false — 지울 것이 없다
            return;
        }
        if ( topic == makeResultTopic( _dependencies._serverId ) )
        {
            MatchAssignment assignment;
            if ( MatchmakingProtocol::readAssignment( reader, assignment ) )
                notifyLocalTicket( assignment._ticketId, assignment );
        }
    }

    void MatchQueueService::leaveQueue( AccountId accountId, uint64 requestTag )
    {
        const auto accountIt = _mapAccountToTicket.find( accountId );
        if ( accountIt == _mapAccountToTicket.end() )
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingResult::NotQueued } );
            return;
        }
        const uint64 ticketId = accountIt->second;
        cancelTicket( ticketId );
        _completionBuffer.push( MatchQueueCompletion{ requestTag, ticketId, MatchmakingResult::Ok } );
    }

    void MatchQueueService::cancelTicket( uint64 ticketId )
    {
        MatchAssignment cancelled;
        cancelled._ticketId = ticketId;
        cancelled._outcome  = MatchQueueOutcome::Cancelled;
        const auto ticketIt = _mapLocalTicket.find( ticketId );
        if ( ticketIt != _mapLocalTicket.end() )
        {
            const string modeId = ticketIt->second._modeId;
            cancelled._modeId   = modeId;
            removeFromQueue( modeId, ticketId );
            notifyLocalTicket( ticketId, cancelled );
            return;
        }
        // 다른 서버가 낸 파티 표 — 모드를 모르니 모든 모드의 권한에서 빼고, 낸 서버(표 id 의 위 32 비트)에 Cancelled 를 알린다
        for ( const auto& [modeId, mode] : _mapMode )
        {
            removeFromQueue( modeId, ticketId );
        }
        deliverToTicket( ticketId >> MatchQueueServiceInternal::kIdShift, cancelled );
    }

    void MatchQueueService::removeAccount( AccountId accountId )
    {
        const auto accountIt = _mapAccountToTicket.find( accountId );
        if ( accountIt != _mapAccountToTicket.end() )
            cancelTicket( accountIt->second );
    }
} // namespace sw
