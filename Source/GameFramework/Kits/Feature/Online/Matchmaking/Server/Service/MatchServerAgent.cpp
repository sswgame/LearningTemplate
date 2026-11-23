#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Runtime/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"

namespace sw
{
    MatchServerAgent::MatchServerAgent()
        : _mutex{}
        , _mapAccountToExpiresMs{}
        , _bReadRequested{ SW_FALSE }
        , _mapRecordToExpiresMs{}
        , _mapAccountToSeat{}
        , _mapRequestToRecord{}
        , _pRouter{ nullptr }
        , _replyOwner{}
        , _serverID{ 0 }
        , _listRequestID{ 0 }
        , _nextListReadMs{ 0 }
        , _nowMs{ 0 }
    {
    }

    MatchServerAgent::~MatchServerAgent()
    {
        shutdown();
    }

    void MatchServerAgent::initialize( uint64 serverID )
    {
        _serverID = serverID;
    }

    void MatchServerAgent::startReading( EphemeralStoreRouter* pRouter )
    {
        SW_ASSERT( pRouter != nullptr );
        _pRouter    = pRouter;
        _replyOwner = pRouter->openReplyOwner();
    }

    void MatchServerAgent::shutdown()
    {
        _replyOwner.reset(); // 기다리던 답이 이 객체를 부르지 않게 지운다
        _mapRequestToRecord.clear();
        _mapRecordToExpiresMs.clear();
        _mapAccountToSeat.clear();
        _listRequestID = 0;
        _pRouter       = nullptr;
    }

    string MatchServerAgent::getAssignTopic() const
    {
        return MatchQueueService::makeAssignTopic( _serverID );
    }

    bool MatchServerAgent::handleAssign( const ServerBusMessage& message, UnixMilliseconds nowMs )
    {
        MatchFormed match;
        if ( MatchQueueBus::kAssignTopic.read( message, match ) != ServerBusReadResult::Read )
            return false; // 읽기가 (타입, 결과, 버전)마다 한 번 오류 로그와 카운터를 남겼다
        acceptMatch( match, nowMs.count() + kExpectTTLMs );
        return true;
    }

    void MatchServerAgent::acceptMatch( const MatchFormed& match, int64 expiresMs )
    {
        for ( size_t team = 0; team < match._listTeam.size(); ++team )
        {
            for ( const MatchMember& member : match._listTeam[team]._listMember )
            {
                uint64 ticketID = 0;
                for ( const MatchTicket& ticket : match._listTicket )
                {
                    if ( VectorUtil::containsBy<&MatchMember::_accountID>( ticket._listMember, member._accountID ) )
                        ticketID = ticket._ticketID;
                }
                MatchSeat& seat = _mapAccountToSeat[member._accountID]; // 백필 기록도 같은 경기 id · 팀 수를 싣는다
                if ( seat._matchID != match._matchID || seat._ticketID != ticketID )
                    seat._bTaken = SW_FALSE; // 새 자리다
                seat._modeID        = match._modeID;
                seat._matchID       = match._matchID;
                seat._ticketID      = ticketID;
                seat._expiresMs     = seat._expiresMs > expiresMs ? seat._expiresMs : expiresMs;
                seat._team          = static_cast<int32>( team );
                seat._teamCount     = static_cast<int32>( match._listTeam.size() );
                seat._averageRating = match._averageRating;
            }
        }
        ScopedLock<mutex> lock{ _mutex };
        for ( const MatchFormedTeam& team : match._listTeam )
        {
            for ( const MatchMember& member : team._listMember )
            {
                int64& acceptedUntilMs = _mapAccountToExpiresMs[member._accountID];
                acceptedUntilMs        = acceptedUntilMs > expiresMs ? acceptedUntilMs : expiresMs;
            }
        }
    }

    bool MatchServerAgent::takeSeat( AccountID accountID, MatchSeat& outSeat )
    {
        const auto seatIt  = _mapAccountToSeat.find( accountID );
        const bool bNoSeat = seatIt == _mapAccountToSeat.end() || seatIt->second._expiresMs <= _nowMs || seatIt->second._bTaken != SW_FALSE;
        if ( bNoSeat )
            return false;
        seatIt->second._bTaken = SW_TRUE; // 지우지 않고 표시한다 — 지우면 뒤에 온 같은 명단이 자리를 되살린다
        outSeat                = seatIt->second;
        return true;
    }

    void MatchServerAgent::update( UnixMilliseconds nowMs )
    {
        _nowMs              = nowMs.count();
        bool bReadRequested = false;
        {
            ScopedLock<mutex> lock{ _mutex };
            bReadRequested = _bReadRequested != SW_FALSE;
            for ( auto expectedIt = _mapAccountToExpiresMs.begin(); expectedIt != _mapAccountToExpiresMs.end(); )
            {
                if ( _nowMs >= expectedIt->second )
                    expectedIt = _mapAccountToExpiresMs.erase( expectedIt );
                else
                    ++expectedIt;
            }
        }
        for ( auto seatIt = _mapAccountToSeat.begin(); seatIt != _mapAccountToSeat.end(); )
        {
            if ( _nowMs >= seatIt->second._expiresMs )
                seatIt = _mapAccountToSeat.erase( seatIt );
            else
                ++seatIt;
        }
        for ( auto recordIt = _mapRecordToExpiresMs.begin(); recordIt != _mapRecordToExpiresMs.end(); )
        {
            if ( _nowMs >= recordIt->second )
                recordIt = _mapRecordToExpiresMs.erase( recordIt );
            else
                ++recordIt;
        }
        if ( _pRouter == nullptr || _listRequestID != 0 || ( _nowMs < _nextListReadMs && bReadRequested == false ) )
            return;
        {
            ScopedLock<mutex> lock{ _mutex };
            _bReadRequested = SW_FALSE; // 이 읽기가 지금까지의 요청을 덮는다
        }
        _nextListReadMs = _nowMs + kRecordRefreshMs;
        _listRequestID  = _pRouter->submit( EphemeralRequest::makeScoreRange( MatchQueueService::makeMatchListKey( _serverID ), 0, kMaxRecordCount ),
                                            SW_DELEGATE_METHOD( EphemeralStoreRouter::ReplyDelegate, &MatchServerAgent::onListReply, this ), _replyOwner );
    }

    void MatchServerAgent::onListReply( const EphemeralReply& reply )
    {
        if ( reply._requestID != _listRequestID )
            return;
        _listRequestID = 0;
        if ( reply._result != EphemeralResult::Ok )
            return; // 목록이 없거나(배정이 없다) 캐시에 닿지 못했다. 다음 주기에 다시 읽는다
        const string listKey = MatchQueueService::makeMatchListKey( _serverID );
        for ( const EphemeralScoredMember& member : reply._listMember )
        {
            if ( member._score <= _nowMs )
            {
                _pRouter->submit( EphemeralRequest::makeScoreRemove( listKey, member._member ) ); // 끝난 경기이다. 기록은 수명으로 이미 사라졌다
                continue;
            }
            if ( _mapRecordToExpiresMs.count( member._member ) != 0 )
                continue;
            _mapRecordToExpiresMs[member._member] = member._score;
            const uint64 requestID                = _pRouter->submit( EphemeralRequest::makeGet( MatchQueueService::makeMatchRecordKey( _serverID, member._member ) ),
                                                                      SW_DELEGATE_METHOD( EphemeralStoreRouter::ReplyDelegate, &MatchServerAgent::onRecordReply, this ), _replyOwner );
            _mapRequestToRecord[requestID]        = member._member;
        }
    }

    void MatchServerAgent::onRecordReply( const EphemeralReply& reply )
    {
        const auto requestIt = _mapRequestToRecord.find( reply._requestID );
        if ( requestIt == _mapRequestToRecord.end() )
            return;
        const string recordName = requestIt->second;
        _mapRequestToRecord.erase( requestIt );
        const auto recordIt = _mapRecordToExpiresMs.find( recordName );
        if ( recordIt == _mapRecordToExpiresMs.end() )
            return; // 읽는 사이에 시한이 지났다
        if ( reply._result == EphemeralResult::Unavailable )
        {
            _mapRecordToExpiresMs.erase( recordIt ); // 다음 주기에 다시 읽는다
            return;
        }
        if ( reply._result != EphemeralResult::Ok )
            return; // 수명이 다한 기록이다. 끝나는 시각까지 다시 읽지 않는다
        MatchFormed match;
        uint32      readVersion = 0;
        if ( MixedBuildCodec::decode( reply._value.data(), reply._value.size(), MatchFormed::kBusVersion, match, readVersion ) != MixedBuildReadResult::Read )
        {
            SW_LOG_WARNING_ID( "MatchServerAgent.MatchRecordUnreadable", "MatchServerAgent: match record '%#' (version %#) could not be read - its players are refused", recordName, readVersion );
            return;
        }
        acceptMatch( match, recordIt->second );
    }

    bool MatchServerAgent::isAccepted( AccountID accountID ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const bool        bAccepted = _mapAccountToExpiresMs.find( accountID ) != _mapAccountToExpiresMs.end();
        if ( bAccepted == false )
            _bReadRequested = SW_TRUE; // 한 틱에 목록 읽기 하나뿐이다(기다리는 읽기가 있으면 더 내지 않는다)
        return bAccepted;
    }

    int32 MatchServerAgent::getExpectedCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<int32>( _mapAccountToExpiresMs.size() );
    }
} // namespace sw
