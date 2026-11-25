#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/SecureRandom.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountStatSource.h"
#include "GameFramework/Runtime/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralRecordTable.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        constexpr int32 kMatchQueueIDShift = 32; ///< 티켓 · 경기 id 의 위 32 비트가 낸 서버 id 이다
    } // namespace
} // namespace sw

namespace sw
{
    MatchQueueService::MatchQueueService()
        : _mapMode{}
        , _mapModeState{}
        , _mapLocalTicket{}
        , _mapAccountToTicket{}
        , _partyLookupTable{}
        , _listRatingWait{}
        , _mapTicketToReservation{}
        , _completionBuffer{}
        , _notificationBuffer{}
        , _topicBuffer{}
        , _dependencies{}
        , _nextLookupTag{ 1 }
        , _nowMs{ 0 }
        , _sequence{ 0 }
    {
    }

    MatchQueueService::~MatchQueueService()
    {
        shutdown();
    }

    string MatchQueueService::makeLeaseKey( string_view modeID )
    {
        StringBuilder<constant::buffer::kMax128> key;
        key.appendFormat( "mm/lease/%#", modeID );
        return string( key.view() );
    }

    string MatchQueueService::makeQueueTopic( string_view modeID )
    {
        return MatchQueueBus::kQueueTopic.makeTopic( modeID );
    }

    string MatchQueueService::makeCancelTopic( string_view modeID )
    {
        return MatchQueueBus::kCancelTopic.makeTopic( modeID );
    }

    string MatchQueueService::makeBackfillTopic( string_view modeID )
    {
        return MatchQueueBus::kBackfillTopic.makeTopic( modeID );
    }

    string MatchQueueService::makeResultTopic( uint64 serverID )
    {
        return MatchQueueBus::kResultTopic.makeTopic( ServiceKeyUtil::makeHex64( serverID ) );
    }

    string MatchQueueService::makeOriginCancelTopic( uint64 serverID )
    {
        return MatchQueueBus::kOriginCancelTopic.makeTopic( ServiceKeyUtil::makeHex64( serverID ) );
    }

    string MatchQueueService::makeAssignTopic( uint64 serverID )
    {
        return MatchQueueBus::kAssignTopic.makeTopic( ServiceKeyUtil::makeHex64( serverID ) );
    }

    string MatchQueueService::makeMatchRecordKey( uint64 serverID, string_view recordName )
    {
        StringBuilder<constant::buffer::kMax128> key;
        key.appendFormat( "mm/match/%#/%#", ServiceKeyUtil::makeHex64( serverID ), recordName );
        return string( key.view() );
    }

    string MatchQueueService::makeMatchListKey( uint64 serverID )
    {
        StringBuilder<constant::buffer::kMax128> key;
        key.appendFormat( "mm/matches/%#", ServiceKeyUtil::makeHex64( serverID ) );
        return string( key.view() );
    }

    void MatchQueueService::storeMatchRecord( PendingPlacement placement )
    {
        const auto modeIt = _mapMode.find( placement._match._modeID );
        SW_ASSERT( modeIt != _mapMode.end() ); // 배정은 등록한 모드만 한다(로비 · 백필도 모드 상태를 찾은 뒤에 온다)
        EphemeralValue value;
        if ( modeIt == _mapMode.end() || EphemeralRecordTable<MatchFormed>{}.makeValue( placement._match, value ) == false )
        {
            failPlacement( placement ); // encode 가 오류 로그를 남겼다
            return;
        }
        const int64 ttlMs            = modeIt->second._matchTimeLimitMs;
        placement._expiresMs         = placement._match._placedMs + ttlMs;
        const EphemeralRequest write = EphemeralRequest::makeSet( makeMatchRecordKey( placement._serverID, placement._recordName ), std::move( value ), ttlMs );
        _dependencies._pRouter->submit( write, _replyOwner, [this, placement = std::move( placement )]( const EphemeralReply& reply ) mutable
        {
            if ( reply._result == EphemeralResult::Ok )
                listMatchRecord( std::move( placement ) );
            else
                failPlacement( placement );
        } );
    }

    void MatchQueueService::listMatchRecord( PendingPlacement placement )
    {
        // 목록에 올라야 게임 서버가 기록을 찾는다. 목록 쓰기의 답까지 본 뒤에 Found 를 낸다
        const EphemeralRequest write = EphemeralRequest::makeScoreSet( makeMatchListKey( placement._serverID ), placement._recordName, placement._expiresMs );
        _dependencies._pRouter->submit( write, _replyOwner, [this, placement = std::move( placement )]( const EphemeralReply& reply )
        {
            if ( reply._result == EphemeralResult::Ok )
                completePlacement( placement );
            else
                failPlacement( placement ); // 남은 기록은 게임 서버가 찾지 못한 채 수명으로 사라진다
        } );
    }

    void MatchQueueService::completePlacement( const PendingPlacement& placement )
    {
        const auto modeIt = _mapMode.find( placement._match._modeID );
        if ( modeIt != _mapMode.end() )
            _dependencies._pRouter->submit( EphemeralRequest::makeExpire( makeMatchListKey( placement._serverID ), modeIt->second._matchTimeLimitMs ) ); // 목록은 마지막 기록과 함께 사라진다. 지난 멤버는 게임 서버가 지운다
        if ( _dependencies._pBus != nullptr )                                                                                                            // 빠른 길이다. 잃어도 게임 서버가 기록을 읽는다
            (void)MatchQueueBus::kAssignTopic.publish( *_dependencies._pBus, ServiceKeyUtil::makeHex64( placement._serverID ), placement._match );       // 실패는 발행이 로그에 남겼다
        for ( const PendingDelivery& delivery : placement._listDelivery )
        {
            deliverPending( delivery );
        }
    }

    void MatchQueueService::failPlacement( const PendingPlacement& placement )
    {
        SW_LOG_WARNING_ID( "MatchQueueService.MatchRecordWriteFailed", "MatchQueue: match record '%#' for server %# could not be written - %# result(s) end as NoServer", placement._recordName,
                           placement._serverID, placement._listDelivery.size() );
        for ( const PendingDelivery& delivery : placement._listDelivery )
        {
            PendingDelivery failed       = delivery;
            failed._assignment._result   = MatchQueueResult::NoServer;
            failed._assignment._serverID = 0;
            failed._assignment._port     = 0;
            failed._assignment._address.clear();
            deliverPending( failed );
        }
        if ( placement._lobbyID != 0 && _dependencies._pPartyLobby != nullptr )
            _dependencies._pPartyLobby->reopenLobby( placement._lobbyID ); // 다시 시작할 수 있게 한다
    }

    void MatchQueueService::deliverPending( const PendingDelivery& delivery )
    {
        if ( delivery._recipientID != kInvalidAccountID )
        {
            _notificationBuffer.push( MatchQueueNotification{ delivery._assignment, delivery._recipientID } );
            return;
        }
        deliverToTicket( delivery._originServerID, delivery._assignment );
    }

    bool MatchQueueService::initialize( const MatchQueueDependencies& dependencies, const vector<MatchModeDef>& listMode )
    {
        SW_ASSERT( dependencies._pRouter != nullptr );
        if ( dependencies._cancelConfirmWaitMs <= 0 )
        {
            SW_LOG_ERROR_ID( "MatchQueueService.MatchQueueNoCancelWait", "MatchQueue: the cancel confirmation wait is missing (_cancelConfirmWaitMs in the matchmaking settings)" );
            return false;
        }
        if ( dependencies._seatReservationTTLMs <= 0 )
        {
            SW_LOG_ERROR_ID( "MatchQueueService.MatchQueueNoSeatReservation", "MatchQueue: the seat reservation window is missing (_seatReservationSeconds in the online config)" );
            return false;
        }
        for ( const MatchModeDef& mode : listMode )
        {
            if ( mode._ratingStat.empty() == false && dependencies._pStatSource == nullptr )
            {
                SW_LOG_ERROR_ID( "MatchQueueService.MatchQueueModeRatedStat", "MatchQueue: mode '%#' is rated by stat '%#' but no stat source is assembled (the leaderboard kit provides it)", mode._modeID,
                                 mode._ratingStat );
                return false;
            }
        }
        _dependencies = dependencies;
        _replyOwner   = dependencies._pRouter != nullptr ? dependencies._pRouter->openReplyOwner() : Subscription{};
        _sequence     = static_cast<uint32>( SecureRandom::nextUint64() ); // 순번은 난수에서 시작한다 — 0 에서 시작하면 재시작 뒤 캐시에 살아 있는 옛 기록(파티 30 분 · 로비 2 시간 · 티켓)과 id 가 겹친다(감사 O-7). 32 비트 순번이 한 바퀴 돌 때까지 겹치지 않는다
        for ( const MatchModeDef& mode : listMode )
        {
            if ( isValidMatchModeID( mode._modeID ) == false )
            {
                SW_LOG_ERROR_ID( "MatchQueueService.MatchQueueModeIdBreaks", "MatchQueue: mode id '%#' breaks the id rule — skipped", mode._modeID );
                continue;
            }
            MatchMaker maker;
            if ( maker.initialize( mode ) == false )
                continue; // 범위 밖 규칙은 `MatchMaker` 가 오류를 남겼다. 맞춰 돌리지 않고 이 모드를 등록하지 않는다
            _mapMode[mode._modeID] = mode;
            ModeState& state       = _mapModeState[mode._modeID];
            state._maker           = std::move( maker );
            state._reader          = make_unique<ServerRegistryReader>();
            state._reader->initialize( dependencies._pRouter, mode._serverKind, dependencies._registryRefreshPeriodMs, dependencies._seatReservationTTLMs );
            if ( dependencies._pBus == nullptr )
                state._bAuthority = SW_TRUE; // 서버가 한 대이므로 항상 권한을 가진다
        }
        if ( dependencies._pBus != nullptr )
        {
            _topicBuffer.push( MatchQueueTopicChange{ makeResultTopic( dependencies._serverID ), SW_TRUE } );
            _topicBuffer.push( MatchQueueTopicChange{ makeOriginCancelTopic( dependencies._serverID ), SW_TRUE } );
        }
        return true;
    }

    void MatchQueueService::shutdown()
    {
        _replyOwner.reset(); // 기다리던 임대 · 기록 쓰기의 답을 버린다. 기다리던 배정은 결과 없이 끝나고 티켓을 낸 서버의 시한이 Timeout 을 알린다
        for ( auto& [modeID, state] : _mapModeState )
        {
            if ( state._reader != nullptr )
                state._reader->shutdown();
        }
        _mapModeState.clear();
        _partyLookupTable.discardAll();
        _listRatingWait.clear();         // 통계 창은 내리지 않는다 — 먼저 내려갔을 수 있고, 내려갈 때 메모리를 비운다
        _mapTicketToReservation.clear(); // 늦게 온 잡기 답은 찾지 못하고 버려진다. 기록의 티켓은 멤버가 빠지기로 푼다(`applyLeaveParty`)
        _dependencies._pRouter = nullptr;
        _dependencies._pBus    = nullptr;
    }

    uint64 MatchQueueService::allocateID()
    {
        return ( _dependencies._serverID << kMatchQueueIDShift ) | ++_sequence;
    }

    bool MatchQueueService::hasMode( string_view modeID ) const
    {
        return _mapMode.find( string( modeID ) ) != _mapMode.end();
    }

    MatchQueueService::ModeState* MatchQueueService::findModeState( string_view modeID )
    {
        const auto stateIt = _mapModeState.find( string( modeID ) );
        return stateIt == _mapModeState.end() ? nullptr : &stateIt->second;
    }

    bool MatchQueueService::isAuthority( string_view modeID ) const
    {
        const auto stateIt = _mapModeState.find( string( modeID ) );
        return stateIt != _mapModeState.end() && stateIt->second._bAuthority != SW_FALSE;
    }

    int32 MatchQueueService::getQueuedTicketCount( string_view modeID ) const
    {
        const auto stateIt = _mapModeState.find( string( modeID ) );
        return stateIt == _mapModeState.end() ? 0 : stateIt->second._maker.getTicketCount();
    }

    bool MatchQueueService::findRating( AccountID accountID, const MatchModeDef& mode, int32& outRating ) const
    {
        outRating = mode._initialRating;
        if ( mode._ratingStat.empty() )
            return true; // 실력을 보지 않는 모드이다
        int64                   value  = 0;
        const AccountStatLookup lookup = _dependencies._pStatSource->findStat( accountID, mode._ratingStat, value );
        if ( lookup == AccountStatLookup::NotLoaded )
            return false;
        if ( lookup == AccountStatLookup::Found )
            outRating = static_cast<int32>( MathUtil::clamp<int64>( value, INT32_MIN, INT32_MAX ) );
        return true;
    }

    void MatchQueueService::update( UnixMilliseconds nowMs )
    {
        if ( _dependencies._pRouter == nullptr )
            return;
        for ( auto& [modeID, state] : _mapModeState )
        {
            if ( _dependencies._pBus != nullptr )
                updateLease( modeID, state, nowMs );
            state._reader->update( nowMs ); // 로비 배정은 권한이 없어도 서버를 고른다
            if ( state._bAuthority != SW_FALSE )
                placeMatches( state, nowMs );
        }
        updateRatingWaits( nowMs );
        _nowMs = nowMs.count();
        // 이 서버가 낸 티켓의 시한이다. 권한이 넘어가서 티켓을 잃었으면 결과가 오지 않는다. 빠지기의 확인 답이 시한 안에 오지 않으면(권한 서버가 없다) 빠진 것으로 끝낸다
        vector<pair<uint64, MatchQueueResult>> listExpired;
        for ( const auto& [ticketID, ticket] : _mapLocalTicket )
        {
            if ( nowMs.count() > ticket._deadlineMs )
                listExpired.push_back( make_pair( ticketID, MatchQueueResult::Timeout ) );
            else if ( ticket._cancelDeadlineMs != 0 && nowMs.count() > ticket._cancelDeadlineMs )
                listExpired.push_back( make_pair( ticketID, MatchQueueResult::Cancelled ) );
        }
        for ( const auto& [ticketID, result] : listExpired )
        {
            MatchAssignment assignment;
            assignment._ticketID = ticketID;
            assignment._modeID   = _mapLocalTicket[ticketID]._modeID;
            assignment._result   = result;
            notifyLocalTicket( ticketID, assignment );
        }
    }

    void MatchQueueService::updateLease( const string& modeID, ModeState& state, UnixMilliseconds nowMs )
    {
        if ( state._bLeasePending != SW_FALSE )
            return; // 응답을 기다린다
        if ( state._bLeaseAttempted != SW_FALSE && nowMs.count() - state._leaseAttemptMs < MatchQueueLimit::kLeaseRenewMs )
            return;
        const EphemeralValue   selfValue = EphemeralValueUtil::makeID( _dependencies._serverID );
        const EphemeralRequest request   = state._bAuthority != SW_FALSE
                                             ? EphemeralRequest::makeCompareAndSet( makeLeaseKey( modeID ), selfValue, selfValue, MatchQueueLimit::kLeaseTTLMs )
                                             : EphemeralRequest::makeSet( makeLeaseKey( modeID ), selfValue, MatchQueueLimit::kLeaseTTLMs, EphemeralCondition::IfAbsent );
        state._leaseAttemptMs            = nowMs.count();
        state._bLeaseAttempted           = SW_TRUE;
        state._bLeasePending             = SW_TRUE;
        _dependencies._pRouter->submit( request, _replyOwner, [this, modeID]( const EphemeralReply& reply )
        { onLeaseReply( modeID, reply ); } );
    }

    void MatchQueueService::onLeaseReply( const string& modeID, const EphemeralReply& reply )
    {
        ModeState* pState = findModeState( modeID );
        if ( pState == nullptr )
            return;
        pState->_bLeasePending = SW_FALSE;
        if ( reply._result == EphemeralResult::Unavailable || reply._result == EphemeralResult::Invalid )
            return; // 캐시에 문제가 있으므로 지금 상태로 두고 다음 주기에 다시 시도한다
        const bool bHold = reply._result == EphemeralResult::Ok;
        if ( bHold == ( pState->_bAuthority != SW_FALSE ) )
            return;
        pState->_bAuthority = bHold ? SW_TRUE : SW_FALSE;
        _topicBuffer.push( MatchQueueTopicChange{ makeQueueTopic( modeID ), static_cast<uint8>( bHold ? SW_TRUE : SW_FALSE ) } );
        _topicBuffer.push( MatchQueueTopicChange{ makeCancelTopic( modeID ), static_cast<uint8>( bHold ? SW_TRUE : SW_FALSE ) } );
        _topicBuffer.push( MatchQueueTopicChange{ makeBackfillTopic( modeID ), static_cast<uint8>( bHold ? SW_TRUE : SW_FALSE ) } );
        if ( bHold == false )
        {
            // 권한을 잃었으므로 매처를 비운다. 티켓은 새 권한 서버가 모르는 채 사라지며, 티켓을 낸 서버의 시한이 이를 알린다.
            pState->_maker.clearTickets();
            pState->_listUnplaced.clear();
        }
        SW_LOG_INFO_ID( "MatchQueueService.MatchQueueServerAuthorityMode", "MatchQueue: server %# %# authority for mode '%#'", _dependencies._serverID, bHold ? "took" : "lost", modeID );
    }

    void MatchQueueService::joinQueue( AccountID accountID, string_view modeID, string_view region, UnixMilliseconds nowMs, uint64 requestTag )
    {
        if ( hasMode( modeID ) == false )
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingResult::UnknownMode } );
            return;
        }
        if ( ServerRecord::isValidName( region ) == false ) // 와이어에서 온 값이다. 서버 지역 이름이 될 수 없는 값은 어떤 서버에도 배정되지 않아 시한까지 기다리기만 한다
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingServiceResult::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        if ( _mapAccountToTicket.count( accountID ) != 0 || isJoinPending( accountID ) )
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingResult::AlreadyQueued } );
            return;
        }
        PendingJoin join;
        join._accountID  = accountID;
        join._modeID     = string( modeID );
        join._region     = string( region );
        join._requestTag = requestTag;
        join._nowMs      = nowMs;
        if ( _dependencies._pPartyLobby == nullptr )
        {
            prepareTicket( join, vector<AccountID>{ accountID }, 0 );
            return;
        }
        // 파티장이면 파티 전체를 넣는다. 파티 기록을 읽은 뒤(onPartyFound)에 처리한다
        const uint64 lookupTag = _nextLookupTag++;
        (void)_partyLookupTable.track( lookupTag, accountID, [this, join]( const PartyLookup& lookup, PendingRequestEnd end )
        {
            if ( end == PendingRequestEnd::Answered )
                applyJoinParty( join, lookup._result, lookup._party );
            else
                _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::NotQueued } ); // 찾는 동안 빠졌다
        } );                                                                                                         // 표는 방금 낸 번호다
        _dependencies._pPartyLobby->findPartyOfAccount( accountID, lookupTag, SW_DELEGATE_METHOD( PartyLobbyService::PartyFoundDelegate, &MatchQueueService::onPartyFound, this ) );
    }

    void MatchQueueService::onPartyFound( uint64 lookupTag, MatchmakingServiceResult result, const PartySnapshot& party )
    {
        (void)_partyLookupTable.answer( lookupTag, PartyLookup{ result, party } ); // 모르는 표는 빠진 줄 서기의 늦은 답이다
    }

    void MatchQueueService::applyJoinParty( const PendingJoin& join, MatchmakingServiceResult result, const PartySnapshot& party )
    {
        if ( result == MatchmakingResult::NotInParty )
        {
            prepareTicket( join, vector<AccountID>{ join._accountID }, 0 );
            return;
        }
        if ( result != MatchmakingResult::Ok )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, result } );
            return;
        }
        if ( party.getLeaderID() != join._accountID )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::NotPartyLeader } );
            return;
        }
        bool bMemberQueued = party._queuedTicketID != 0;
        for ( const AccountID memberID : party._listMemberID )
        {
            bMemberQueued = bMemberQueued || _mapAccountToTicket.count( memberID ) != 0 || isJoinPending( memberID );
        }
        if ( bMemberQueued )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::AlreadyQueued } );
            return;
        }
        prepareTicket( join, party._listMemberID, party._partyID );
    }

    bool MatchQueueService::isJoinPending( AccountID accountID ) const
    {
        if ( _partyLookupTable.hasOwnedBy( accountID ) )
            return true;
        for ( const RatingWait& wait : _listRatingWait )
        {
            if ( VectorUtil::contains( wait._listAccount, accountID ) )
                return true;
        }
        for ( const auto& [ticketID, reservation] : _mapTicketToReservation )
        {
            if ( reservation._bDropped == SW_FALSE && VectorUtil::containsBy<&MatchMember::_accountID>( reservation._ticket._listMember, accountID ) )
                return true;
        }
        return false;
    }

    bool MatchQueueService::isRatingLoaded( const MatchModeDef& mode, AccountID accountID ) const
    {
        int32 rating = 0;
        return findRating( accountID, mode, rating );
    }

    void MatchQueueService::prepareTicket( const PendingJoin& join, const vector<AccountID>& listAccount, uint64 partyID )
    {
        const MatchModeDef& mode = _mapMode[join._modeID];
        RatingWait          wait;
        for ( const AccountID accountID : listAccount )
        {
            if ( isRatingLoaded( mode, accountID ) == false )
                wait._listRetained.push_back( accountID );
        }
        if ( wait._listRetained.empty() || static_cast<int32>( listAccount.size() ) > mode._teamSize ) // 인원이 넘치면 기다릴 것 없이 `submitTicket` 이 거절한다
        {
            submitTicket( join, listAccount, partyID );
            return;
        }
        for ( const AccountID accountID : wait._listRetained )
        {
            _dependencies._pStatSource->retainStats( accountID );
        }
        wait._join        = join;
        wait._listAccount = listAccount;
        wait._partyID     = partyID;
        _listRatingWait.push_back( std::move( wait ) );
    }

    void MatchQueueService::releaseRetained( const RatingWait& wait )
    {
        for ( const AccountID accountID : wait._listRetained )
        {
            _dependencies._pStatSource->releaseStats( accountID );
        }
    }

    void MatchQueueService::updateRatingWaits( UnixMilliseconds nowMs )
    {
        for ( size_t index = 0; index < _listRatingWait.size(); )
        {
            const RatingWait&   wait    = _listRatingWait[index];
            const MatchModeDef& mode    = _mapMode[wait._join._modeID];
            bool                bLoaded = true;
            for ( const AccountID accountID : wait._listAccount )
            {
                bLoaded = bLoaded && isRatingLoaded( mode, accountID );
            }
            if ( bLoaded == false && nowMs.count() - wait._join._nowMs.count() < MatchQueueLimit::kRatingWaitMs )
            {
                ++index;
                continue;
            }
            const RatingWait finished = std::move( _listRatingWait[index] );
            _listRatingWait.erase( _listRatingWait.begin() + static_cast<ptrdiff_t>( index ) );
            submitTicket( finished._join, finished._listAccount, finished._partyID );
            releaseRetained( finished );
        }
    }

    void MatchQueueService::submitTicket( const PendingJoin& join, const vector<AccountID>& listAccount, uint64 partyID )
    {
        const MatchModeDef& mode = _mapMode[join._modeID];
        MatchTicket         ticket;
        ticket._ticketID       = allocateID();
        ticket._region         = join._region;
        ticket._partyID        = partyID;
        ticket._originServerID = _dependencies._serverID;
        ticket._enqueuedMs     = join._nowMs.count();
        for ( const AccountID accountID : listAccount )
        {
            int32 rating = 0;
            if ( findRating( accountID, mode, rating ) == false )
            {
                _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::RatingUnavailable } );
                return;
            }
            ticket._listMember.push_back( MatchMember{ accountID, rating } );
        }
        if ( static_cast<int32>( ticket._listMember.size() ) > mode._teamSize )
        {
            _completionBuffer.push( MatchQueueCompletion{ join._requestTag, 0, MatchmakingResult::PartyTooLarge } );
            return;
        }

        if ( partyID == 0 || _dependencies._pPartyLobby == nullptr )
        {
            enqueueTicket( join, ticket );
            return;
        }
        PartyReservation& reservation = _mapTicketToReservation[ticket._ticketID];
        reservation._join             = join;
        reservation._ticket           = ticket;
        _dependencies._pPartyLobby->reservePartyTicket( partyID, ticket._ticketID, listAccount, SW_DELEGATE_METHOD( PartyLobbyService::PartyFoundDelegate, &MatchQueueService::onPartyTicketReserved, this ) );
    }

    void MatchQueueService::onPartyTicketReserved( uint64 ticketID, MatchmakingServiceResult result, const PartySnapshot& party )
    {
        (void)party; // 멤버는 기록이 견주었다
        const auto reservationIt = _mapTicketToReservation.find( ticketID );
        if ( reservationIt == _mapTicketToReservation.end() )
            return;
        const PartyReservation reservation = std::move( reservationIt->second );
        _mapTicketToReservation.erase( reservationIt );
        const bool bDropped = reservation._bDropped != SW_FALSE;
        if ( result == MatchmakingResult::Ok && bDropped == false )
        {
            enqueueTicket( reservation._join, reservation._ticket );
            return;
        }
        // 빠졌거나 잡지 못했다. 쓰기의 답만 잃었을 수 있으므로(kUnavailable) 어느 쪽이든 푼다 — 비교 후 지우기라 남의 티켓은 남는다.
        _dependencies._pPartyLobby->clearPartyTicket( reservation._ticket._partyID, ticketID );
        if ( bDropped == false ) // 빠진 요청의 완료는 빠질 때 갔다
            _completionBuffer.push( MatchQueueCompletion{ reservation._join._requestTag, 0, result } );
    }

    void MatchQueueService::enqueueTicket( const PendingJoin& join, const MatchTicket& ticket )
    {
        const MatchModeDef& mode  = _mapMode[join._modeID];
        LocalTicket&        local = _mapLocalTicket[ticket._ticketID];
        local._modeID             = join._modeID;
        local._ticketID           = ticket._ticketID;
        local._partyID            = ticket._partyID;
        local._deadlineMs         = join._nowMs.count() + mode._maxWaitMs + MatchQueueLimit::kNoServerGiveUpMs + MatchQueueLimit::kResultGraceMs;
        for ( const MatchMember& member : ticket._listMember )
        {
            local._listAccount.push_back( member._accountID );
            _mapAccountToTicket[member._accountID] = ticket._ticketID;
        }
        _completionBuffer.push( MatchQueueCompletion{ join._requestTag, ticket._ticketID, MatchmakingResult::Ok } );

        ModeState* pState = findModeState( join._modeID );
        if ( pState->_bAuthority != SW_FALSE )
        {
            (void)pState->_maker.addTicket( ticket );
            return;
        }
        if ( _dependencies._pBus == nullptr )
            return;
        (void)MatchQueueBus::kQueueTopic.publish( *_dependencies._pBus, join._modeID, ticket ); // 쓰지 못했으면 발행이 로그에 남겼고, 티켓은 시한으로 끝난다
    }

    void MatchQueueService::placeMatches( ModeState& state, UnixMilliseconds nowMs )
    {
        vector<MatchBackfilled> listBackfilled;
        state._maker.processBackfills( nowMs.count(), listBackfilled ); // 경기 중 빈 슬롯을 새 경기보다 먼저 채운다
        for ( const MatchBackfilled& backfilled : listBackfilled )
        {
            deliverBackfill( backfilled, nowMs );
        }
        vector<MatchFormed> listFormed;
        vector<MatchTicket> listTimedOut;
        state._maker.process( nowMs.count(), listFormed, listTimedOut );
        for ( const MatchTicket& ticket : listTimedOut )
        {
            MatchAssignment assignment;
            assignment._ticketID = ticket._ticketID;
            assignment._modeID   = state._maker.getDefinition()._modeID;
            assignment._result   = MatchQueueResult::Timeout;
            deliverToTicket( ticket._originServerID, assignment );
        }
        for ( MatchFormed& match : listFormed )
        {
            match._matchID = allocateID();
            state._listUnplaced.push_back( UnplacedMatch{ std::move( match ), nowMs.count() } );
        }
        for ( size_t index = 0; index < state._listUnplaced.size(); )
        {
            const UnplacedMatch& unplaced = state._listUnplaced[index];
            const bool           bGiveUp  = nowMs.count() - unplaced._formedMs >= MatchQueueLimit::kNoServerGiveUpMs;
            // 배정할 서버가 없으면 다음 틱에 다시 시도한다. 서버 목록이 바뀌기를 기다리는 것이며, 오래 기다렸으면 NoServer 로 끝낸다
            if ( placeMatch( state, unplaced._match, bGiveUp ? MatchQueueResult::NoServer : MatchQueueResult::Count, nowMs ) == false && bGiveUp == false )
            {
                ++index;
                continue;
            }
            state._listUnplaced.erase( state._listUnplaced.begin() + static_cast<ptrdiff_t>( index ) );
        }
    }

    void MatchQueueService::deliverBackfill( const MatchBackfilled& backfilled, UnixMilliseconds nowMs )
    {
        const MatchBackfillRequest& request = backfilled._request;
        MatchFormed                 joined;
        joined._matchID       = request._matchID;
        joined._modeID        = request._modeID;
        joined._region        = request._region;
        joined._averageRating = request._averageRating;
        joined._listTeam.resize( request._listOpenSlot.size() );
        joined._listTeam[static_cast<size_t>( backfilled._team )]._listMember = backfilled._ticket._listMember;
        joined._listTicket.push_back( backfilled._ticket );
        MatchAssignment assignment;
        assignment._ticketID = backfilled._ticket._ticketID;
        assignment._matchID  = request._matchID;
        assignment._modeID   = request._modeID;
        assignment._result   = MatchQueueResult::Found;
        assignment._serverID = request._serverID;
        assignment._address  = request._address;
        // 포트 샤딩 — 티켓(파티)은 한 샤드에 함께 간다. 대표 계정이 샤드를 정한다(새 경기 배정과 같은 규칙).
        const AccountID leaderID = backfilled._ticket._listMember.empty() ? AccountID{ 0 } : backfilled._ticket._listMember.front()._accountID;
        assignment._port         = ServerDescriptor::pickShardPort( request._port, request._portCount, leaderID );
        assignment._team         = backfilled._team;
        for ( const MatchMember& member : backfilled._ticket._listMember )
        {
            assignment._listTeammate.push_back( member._accountID ); // 경기 중인 팀원은 게임 서버가 알려 준다
        }
        PendingPlacement placement;
        placement._match = std::move( joined );              // 게임 서버의 배정 주제로 가면 같은 경기 id 에 더해진다
        StringBuilder<constant::buffer::kMax128> recordName; // 경기의 첫 명단 기록은 그대로 두고 더한 사람만 따로 남긴다
        recordName.appendFormat( "%#.%#", ServiceKeyUtil::makeHex64( request._matchID ), ServiceKeyUtil::makeHex64( backfilled._ticket._ticketID ) );
        placement._recordName      = string( recordName.view() );
        placement._serverID        = request._serverID;
        placement._match._placedMs = nowMs.count();
        placement._listDelivery.push_back( PendingDelivery{ std::move( assignment ), backfilled._ticket._originServerID, kInvalidAccountID } );
        storeMatchRecord( std::move( placement ) );
    }

    MatchmakingServiceResult MatchQueueService::submitBackfill( const MatchBackfillRequest& request )
    {
        ModeState* pState = findModeState( request._modeID );
        if ( pState == nullptr )
        {
            SW_LOG_WARNING_ID( "MatchQueueService.MatchQueueBackfillUnknownMode", "MatchQueue: backfill for unknown mode '%#' (match %#) refused", request._modeID, request._matchID );
            return MatchmakingResult::UnknownMode;
        }
        if ( MatchMaker::isBackfillShapeValid( pState->_maker.getDefinition(), request ) == false )
        {
            SW_LOG_WARNING_ID( "MatchQueueService.MatchQueueBackfillMatchDoes", "MatchQueue: backfill for match %# does not fit mode '%#' - refused", request._matchID, request._modeID );
            return MatchmakingServiceResult::fail( OnlineError::kInvalidRequest );
        }
        if ( pState->_bAuthority != SW_FALSE )
            return pState->_maker.addBackfill( request );
        if ( _dependencies._pBus != nullptr )
            (void)MatchQueueBus::kBackfillTopic.publish( *_dependencies._pBus, request._modeID, request ); // 실패는 발행이 로그에 남겼다. 게임 서버가 다시 보낸다
        return MatchmakingResult::Ok;
    }

    void MatchQueueService::fillTeam( const MatchFormed& match, AccountID accountID, MatchAssignment& inoutAssignment )
    {
        for ( size_t team = 0; team < match._listTeam.size(); ++team )
        {
            for ( const MatchMember& member : match._listTeam[team]._listMember )
            {
                if ( member._accountID != accountID )
                    continue;
                inoutAssignment._team = static_cast<int32>( team );
                inoutAssignment._listTeammate.clear();
                for ( const MatchMember& mate : match._listTeam[team]._listMember )
                {
                    inoutAssignment._listTeammate.push_back( mate._accountID );
                }
                return;
            }
        }
    }

    bool MatchQueueService::placeMatch( ModeState& state, const MatchFormed& match, MatchQueueResult failResult, UnixMilliseconds nowMs )
    {
        const MatchModeDef&  mode = _mapMode[match._modeID];
        ServerSelectionQuery query;
        query._kind         = mode._serverKind;
        query._region       = match._region;
        query._buildVersion = mode._buildVersion;
        query._seatCount    = 0;
        for ( const MatchFormedTeam& team : match._listTeam )
        {
            query._seatCount += static_cast<int32>( team._listMember.size() );
        }
        ServerStatus picked;
        const bool   bPlaced = state._reader->pickServer( query, nowMs, picked );
        if ( bPlaced == false && failResult == MatchQueueResult::Count )
            return false;

        PendingPlacement placement;
        for ( const MatchTicket& ticket : match._listTicket )
        {
            MatchAssignment assignment;
            assignment._ticketID = ticket._ticketID;
            assignment._matchID  = match._matchID;
            assignment._modeID   = match._modeID;
            assignment._result   = bPlaced ? MatchQueueResult::Found : failResult;
            if ( bPlaced )
            {
                assignment._serverID = picked._descriptor._serverID;
                assignment._address  = picked._descriptor._address;
                // 포트 샤딩 — 티켓(파티)은 한 샤드에 함께 간다. 대표 계정이 샤드를 정한다.
                assignment._port = picked._descriptor.pickPort( ticket._listMember.empty() ? 0 : ticket._listMember.front()._accountID );
            }
            if ( ticket._listMember.empty() == false )
                fillTeam( match, ticket._listMember.front()._accountID, assignment ); // 티켓 하나는 한 팀에 들어간다
            if ( bPlaced )
                placement._listDelivery.push_back( PendingDelivery{ std::move( assignment ), ticket._originServerID, kInvalidAccountID } );
            else
                deliverToTicket( ticket._originServerID, assignment );
        }
        if ( bPlaced == false )
        {
            SW_LOG_WARNING_ID( "MatchQueueService.MatchQueueNoServerMatch", "MatchQueue: no '%#' server for match %# in region '%#' — gave up", mode._serverKind, match._matchID, match._region );
            return false;
        }
        placement._match           = match;
        placement._recordName      = ServiceKeyUtil::makeHex64( match._matchID );
        placement._serverID        = picked._descriptor._serverID;
        placement._match._placedMs = nowMs.count();
        storeMatchRecord( std::move( placement ) ); // 게임 서버가 수락을 판정하는 원본이다. Found 는 기록이 남은 뒤에 간다
        return true;
    }

    void MatchQueueService::placeLobby( const LobbySnapshot& lobby, UnixMilliseconds nowMs )
    {
        ModeState*  pState = findModeState( lobby._modeID );
        MatchFormed match;
        match._modeID  = lobby._modeID;
        match._matchID = allocateID();
        match._listTeam.resize( static_cast<size_t>( PartyLobbyLimit::kLobbyTeamCount ) );
        const auto  modeIt      = _mapMode.find( lobby._modeID );
        const int32 lobbyRating = modeIt != _mapMode.end() ? modeIt->second._initialRating : 0; // 모르는 모드는 아래 배치가 거절한다
        for ( const LobbyMember& member : lobby._listMember )
        {
            const size_t team = static_cast<size_t>( MathUtil::clamp( member._team, 0, PartyLobbyLimit::kLobbyTeamCount - 1 ) );
            match._listTeam[team]._listMember.push_back( MatchMember{ member._accountID, lobbyRating } ); // 로비 경기는 실력으로 고르지 않는다
        }
        ServerStatus picked;
        bool         bPlaced = false;
        if ( pState != nullptr )
        {
            const MatchModeDef&  mode = _mapMode[lobby._modeID];
            ServerSelectionQuery query;
            query._kind              = mode._serverKind;
            query._buildVersion      = mode._buildVersion;
            query._seatCount         = MathUtil::max( static_cast<int32>( lobby._listMember.size() ), mode._teamCount * mode._teamSize ); // 덜 찬 로비도 경기 하나 몫을 통째로 잡는다 — 남은 자리에 다른 경기가 들어오지 않는다
            query._bAllowOtherRegion = SW_TRUE;                                                                                           // 로비는 지역을 고르지 않았으므로 다른 지역도 허용한다
            bPlaced                  = pState->_reader->pickServer( query, nowMs, picked );
        }
        PendingPlacement placement;
        for ( const LobbyMember& member : lobby._listMember )
        {
            MatchAssignment assignment;
            assignment._matchID = match._matchID;
            assignment._modeID  = lobby._modeID;
            assignment._result  = bPlaced ? MatchQueueResult::Found : MatchQueueResult::NoServer;
            if ( bPlaced )
            {
                assignment._serverID = picked._descriptor._serverID;
                assignment._address  = picked._descriptor._address;
                assignment._port     = picked._descriptor.pickPort( member._accountID ); // 포트 샤딩 — 계정마다 정해진 샤드의 포트
            }
            fillTeam( match, member._accountID, assignment );
            if ( bPlaced )
                placement._listDelivery.push_back( PendingDelivery{ std::move( assignment ), 0, member._accountID } );
            else
                _notificationBuffer.push( MatchQueueNotification{ assignment, member._accountID } );
        }
        if ( bPlaced == false )
        {
            if ( _dependencies._pPartyLobby != nullptr )
                _dependencies._pPartyLobby->reopenLobby( lobby._lobbyID ); // 다시 시작할 수 있게 한다
            return;
        }
        placement._match           = std::move( match );
        placement._recordName      = ServiceKeyUtil::makeHex64( placement._match._matchID );
        placement._serverID        = picked._descriptor._serverID;
        placement._lobbyID         = lobby._lobbyID;
        placement._match._placedMs = nowMs.count();
        storeMatchRecord( std::move( placement ) ); // 게임 서버가 수락을 판정하는 원본이다. Found 는 기록이 남은 뒤에 간다
    }

    void MatchQueueService::deliverToTicket( uint64 originServerID, const MatchAssignment& assignment )
    {
        if ( originServerID == _dependencies._serverID || _dependencies._pBus == nullptr )
        {
            notifyLocalTicket( assignment._ticketID, assignment );
            return;
        }
        (void)MatchQueueBus::kResultTopic.publish( *_dependencies._pBus, ServiceKeyUtil::makeHex64( originServerID ), assignment ); // 실패는 발행이 로그에 남겼고, 티켓은 시한으로 끝난다
    }

    void MatchQueueService::notifyLocalTicket( uint64 ticketID, const MatchAssignment& assignment )
    {
        const auto ticketIt = _mapLocalTicket.find( ticketID );
        if ( ticketIt == _mapLocalTicket.end() )
            return; // 이미 끝났다. 시한 뒤에 온 늦은 결과이거나 이미 빠진 티켓이다
        // 빠지기의 답이다 — 경기가 먼저 났으면(Found) 빠지지 못했다
        const MatchmakingResult leaveResult = assignment._result == MatchQueueResult::Cancelled ? MatchmakingResult::Ok : MatchmakingResult::NotQueued;
        for ( const uint64 leaveRequestTag : ticketIt->second._listLeaveRequestTag )
        {
            _completionBuffer.push( MatchQueueCompletion{ leaveRequestTag, ticketID, leaveResult } );
        }
        for ( const AccountID accountID : ticketIt->second._listAccount )
        {
            _notificationBuffer.push( MatchQueueNotification{ assignment, accountID } );
            const auto accountIt = _mapAccountToTicket.find( accountID );
            if ( accountIt != _mapAccountToTicket.end() && accountIt->second == ticketID )
                _mapAccountToTicket.erase( accountIt );
        }
        if ( ticketIt->second._partyID != 0 && _dependencies._pPartyLobby != nullptr )
            _dependencies._pPartyLobby->clearPartyTicket( ticketIt->second._partyID, ticketID );
        _mapLocalTicket.erase( ticketIt );
    }

    void MatchQueueService::confirmCancel( ModeState& state, uint64 ticketID )
    {
        if ( state._maker.removeTicket( ticketID ) == false )
            return;                // 이미 짝지어졌다 — 낸 서버는 Found 를 받는다(빠지기보다 경기가 먼저다). 없는 표도 같다
        MatchAssignment cancelled; // 권한 서버가 실제로 뺀 뒤에만 Cancelled 를 낸 서버에 알린다
        cancelled._ticketID = ticketID;
        cancelled._modeID   = state._maker.getDefinition()._modeID;
        cancelled._result   = MatchQueueResult::Cancelled;
        deliverToTicket( ticketID >> kMatchQueueIDShift, cancelled );
    }

    void MatchQueueService::requestCancel( uint64 ticketID, uint64 leaveRequestTag )
    {
        LocalTicket& ticket = _mapLocalTicket[ticketID];
        if ( leaveRequestTag != 0 )
            ticket._listLeaveRequestTag.push_back( leaveRequestTag );
        if ( ticket._cancelDeadlineMs != 0 )
            return; // 이미 빼는 중이다 — 같은 답을 기다린다
        ticket._cancelDeadlineMs = _nowMs + _dependencies._cancelConfirmWaitMs;
        const string modeID      = ticket._modeID;
        removeFromQueue( modeID, ticketID ); // 권한이 이 서버면 여기서 바로 답이 온다(notifyLocalTicket)
    }

    void MatchQueueService::removeFromQueue( const string& modeID, uint64 ticketID )
    {
        ModeState* pState = findModeState( modeID );
        if ( pState != nullptr && pState->_bAuthority != SW_FALSE )
        {
            confirmCancel( *pState, ticketID );
            return;
        }
        if ( _dependencies._pBus == nullptr )
            return;
        (void)MatchQueueBus::kCancelTopic.publish( *_dependencies._pBus, modeID, MatchTicketCancel{ ticketID } ); // 실패는 발행이 로그에 남겼다 — 낸 서버의 확인 시한이 끝낸다
    }

    void MatchQueueService::handleBusMessage( const ServerBusMessage& message, UnixMilliseconds nowMs )
    {
        (void)nowMs;
        if ( MatchQueueBus::kQueueTopic.matches( message ) )
        {
            MatchTicket ticket;
            ModeState*  pState        = findModeState( MatchQueueBus::kQueueTopic.findSuffix( message ) );
            const bool  bAcceptTicket = pState != nullptr && pState->_bAuthority != SW_FALSE && MatchQueueBus::kQueueTopic.read( message, ticket ) == ServerBusReadResult::Read;
            if ( bAcceptTicket )
                (void)pState->_maker.addTicket( ticket );
            return;
        }
        if ( MatchQueueBus::kCancelTopic.matches( message ) )
        {
            MatchTicketCancel cancel;
            ModeState*        pState        = findModeState( MatchQueueBus::kCancelTopic.findSuffix( message ) );
            const bool        bAcceptCancel = pState != nullptr && pState->_bAuthority != SW_FALSE && MatchQueueBus::kCancelTopic.read( message, cancel ) == ServerBusReadResult::Read;
            if ( bAcceptCancel )
                confirmCancel( *pState, cancel._ticketID );
            return;
        }
        if ( MatchQueueBus::kBackfillTopic.matches( message ) )
        {
            MatchBackfillRequest request;
            const bool           bAccept = MatchQueueBus::kBackfillTopic.read( message, request ) == ServerBusReadResult::Read && isAuthority( request._modeID );
            if ( bAccept )
                (void)submitBackfill( request ); // 거절은 로그에 남겼다. 권한을 잃은 사이에 왔으면 다시 보내지 않고 게임 서버의 재요청을 기다린다
            return;
        }
        if ( message.getTopic() == makeOriginCancelTopic( _dependencies._serverID ) )
        {
            MatchTicketCancel cancel;
            if ( MatchQueueBus::kOriginCancelTopic.read( message, cancel ) == ServerBusReadResult::Read )
                (void)cancelOwnTicket( cancel._ticketID ); // 이미 끝난 티켓이면 할 일이 없다
            return;
        }
        if ( message.getTopic() == makeResultTopic( _dependencies._serverID ) )
        {
            MatchAssignment assignment;
            if ( MatchQueueBus::kResultTopic.read( message, assignment ) == ServerBusReadResult::Read )
                notifyLocalTicket( assignment._ticketID, assignment );
        }
    }

    bool MatchQueueService::dropPendingJoin( AccountID accountID )
    {
        if ( _partyLookupTable.hasOwnedBy( accountID ) )
        {
            _partyLookupTable.cancelOwnedBy( accountID );
            return true;
        }
        for ( size_t index = 0; index < _listRatingWait.size(); ++index )
        {
            if ( VectorUtil::contains( _listRatingWait[index]._listAccount, accountID ) == false )
                continue;
            const RatingWait dropped = std::move( _listRatingWait[index] );
            _listRatingWait.erase( _listRatingWait.begin() + static_cast<ptrdiff_t>( index ) );
            _completionBuffer.push( MatchQueueCompletion{ dropped._join._requestTag, 0, MatchmakingResult::NotQueued } );
            releaseRetained( dropped );
            return true;
        }
        for ( auto& [ticketID, reservation] : _mapTicketToReservation )
        {
            if ( reservation._bDropped != SW_FALSE || VectorUtil::containsBy<&MatchMember::_accountID>( reservation._ticket._listMember, accountID ) == false )
                continue;
            dropReservation( reservation );
            return true;
        }
        return false;
    }

    void MatchQueueService::leaveQueue( AccountID accountID, uint64 requestTag )
    {
        if ( dropPendingJoin( accountID ) ) // 파티를 찾는 동안 빠졌다. 티켓은 아직 없다
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingResult::Ok } );
            return;
        }
        const auto accountIt = _mapAccountToTicket.find( accountID );
        if ( accountIt != _mapAccountToTicket.end() )
        {
            requestCancel( accountIt->second, requestTag ); // 답은 권한 서버의 확인(Cancelled) 또는 먼저 난 경기(Found)에서 간다
            return;
        }
        if ( _dependencies._pPartyLobby == nullptr )
        {
            _completionBuffer.push( MatchQueueCompletion{ requestTag, 0, MatchmakingResult::NotQueued } );
            return;
        }
        // 이 서버의 티켓이 없다. 다른 서버(죽었을 수 있다)가 낸 파티 티켓이 파티 기록에 남아 있으면 그것을 푼다
        const uint64 lookupTag = _nextLookupTag++;
        (void)_partyLookupTable.track( lookupTag, 0, [this, leave = LeaveRequest{ accountID, requestTag }]( const PartyLookup& lookup )
        { applyLeaveParty( leave, lookup._result, lookup._party ); } ); // 표는 방금 낸 번호다
        _dependencies._pPartyLobby->findPartyOfAccount( accountID, lookupTag, SW_DELEGATE_METHOD( PartyLobbyService::PartyFoundDelegate, &MatchQueueService::onPartyFound, this ) );
    }

    void MatchQueueService::applyLeaveParty( const LeaveRequest& leave, MatchmakingServiceResult result, const PartySnapshot& party )
    {
        if ( result != MatchmakingResult::Ok && result != MatchmakingResult::NotInParty )
        {
            _completionBuffer.push( MatchQueueCompletion{ leave._requestTag, 0, result } );
            return;
        }
        const uint64 ticketID = party._queuedTicketID;
        if ( result == MatchmakingResult::NotInParty || ticketID == 0 || party.hasMember( leave._accountID ) == false )
        {
            _completionBuffer.push( MatchQueueCompletion{ leave._requestTag, 0, MatchmakingResult::NotQueued } );
            return;
        }
        // 낸 서버가 살아 있으면 그 서버가 Cancelled 를 받아 멤버에게 알리고, 죽었으면 아무도 받지 않는다. 어느 쪽이든 기록은 여기서 지운다(비교 후 지우기라 새 티켓은 남는다)
        cancelTicket( ticketID );
        _dependencies._pPartyLobby->clearPartyTicket( party._partyID, ticketID );
        _completionBuffer.push( MatchQueueCompletion{ leave._requestTag, ticketID, MatchmakingResult::Ok } );
    }

    bool MatchQueueService::cancelOwnTicket( uint64 ticketID )
    {
        if ( _mapLocalTicket.find( ticketID ) != _mapLocalTicket.end() )
        {
            requestCancel( ticketID, 0 ); // 권한 서버가 뺀 뒤에 멤버에게 Cancelled 가 간다
            return true;
        }
        const auto reservationIt = _mapTicketToReservation.find( ticketID );
        if ( reservationIt == _mapTicketToReservation.end() )
            return false;
        dropReservation( reservationIt->second );
        return true;
    }

    void MatchQueueService::dropReservation( PartyReservation& reservation )
    {
        if ( reservation._bDropped != SW_FALSE )
            return;
        reservation._bDropped = SW_TRUE; // 잡기는 이미 나갔다. 답이 오면 푼다(`onPartyTicketReserved`)
        _completionBuffer.push( MatchQueueCompletion{ reservation._join._requestTag, 0, MatchmakingResult::NotQueued } );
    }

    void MatchQueueService::cancelTicket( uint64 ticketID )
    {
        if ( cancelOwnTicket( ticketID ) )
            return;
        // 다른 서버가 낸 파티 티켓이다. 파티 기록에 잡힌 뒤 대기열에 넣기 전이면 권한 서버는 그 티켓을 모르므로, 낸 서버(티켓 id 의 위쪽 32비트)가 제 순서대로 빼게 한다
        const uint64 originServerID = ticketID >> kMatchQueueIDShift;
        if ( _dependencies._pBus != nullptr && originServerID != _dependencies._serverID )
            (void)MatchQueueBus::kOriginCancelTopic.publish( *_dependencies._pBus, ServiceKeyUtil::makeHex64( originServerID ), MatchTicketCancel{ ticketID } ); // 실패는 발행이 로그에 남겼다 — 아래 권한 서버의 취소와 티켓의 시한이 남는다
        // 낸 서버가 죽었을 수 있으므로 권한에서도 뺀다. 모드를 모르므로 모든 모드의 권한에서 뺀다 — 실제로 뺀 권한 서버가 낸 서버에 Cancelled 를 알린다
        for ( const auto& [modeID, mode] : _mapMode )
        {
            removeFromQueue( modeID, ticketID );
        }
    }

    void MatchQueueService::removeAccount( AccountID accountID )
    {
        (void)dropPendingJoin( accountID ); // 떠난 계정의 티켓이 파티 답 뒤에 생기면 없는 사람이 경기에 묶인다
        const auto accountIt = _mapAccountToTicket.find( accountID );
        if ( accountIt != _mapAccountToTicket.end() )
            cancelTicket( accountIt->second );
    }
} // namespace sw
