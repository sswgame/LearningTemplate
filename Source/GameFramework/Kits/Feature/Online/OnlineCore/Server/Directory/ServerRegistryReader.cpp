#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Directory/ServerRegistryReader.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"

namespace sw
{
    ServerRegistryReader::ServerRegistryReader()
        : _listSnapshot{}
        , _listBuilding{}
        , _mapRequestToServer{}
        , _mapRequestToSeat{}
        , _mapServerToReserved{}
        , _kind{}
        , _pRouter{ nullptr }
        , _indexRequestID{ 0 }
        , _refreshPeriodMs{ 2000 }
        , _seatReservationTTLMs{ 0 }
        , _lastRefreshStartMs{ 0 }
        , _refreshCount{ 0 }
        , _outstandingCount{ 0 }
        , _bRefreshRequested{ SW_TRUE }
    {
    }

    ServerRegistryReader::~ServerRegistryReader()
    {
        shutdown();
    }

    void ServerRegistryReader::initialize( EphemeralStoreRouter* pRouter, string_view kind, int64 refreshPeriodMs, int64 seatReservationTTLMs )
    {
        SW_ASSERT( seatReservationTTLMs > 0 ); // 서버 설정의 필수 칸이다 — 0 이면 예약이 바로 사라져 서버 사이에서 넘친다
        _seatReservationTTLMs = seatReservationTTLMs;
        _pRouter              = pRouter;
        _replyOwner           = pRouter != nullptr ? pRouter->openReplyOwner() : Subscription{};
        _kind                 = string( kind );
        _refreshPeriodMs      = refreshPeriodMs;
        _bRefreshRequested    = SW_TRUE;
    }

    void ServerRegistryReader::shutdown()
    {
        _replyOwner.reset(); // 라우터가 이 객체보다 오래 산다 — 기다리던 답이 사라진 객체를 부르지 않게 지운다
        _pRouter          = nullptr;
        _indexRequestID   = 0;
        _outstandingCount = 0;
        _listSnapshot.clear();
        _listBuilding.clear();
        _mapRequestToServer.clear();
        _mapRequestToSeat.clear();
        _mapServerToReserved.clear();
    }

    void ServerRegistryReader::update( UnixMilliseconds nowMs )
    {
        if ( _pRouter == nullptr || _outstandingCount > 0 )
            return;
        if ( _bRefreshRequested == SW_FALSE && nowMs - _lastRefreshStartMs < _refreshPeriodMs )
            return;
        startRefresh( nowMs );
    }

    void ServerRegistryReader::requestRefresh()
    {
        _bRefreshRequested = SW_TRUE;
    }

    void ServerRegistryReader::startRefresh( UnixMilliseconds nowMs )
    {
        _bRefreshRequested  = SW_FALSE;
        _lastRefreshStartMs = nowMs;
        _listBuilding.clear();
        _mapRequestToServer.clear();
        _mapRequestToSeat.clear();
        _mapServerToReserved.clear();
        _outstandingCount = 1;
        _indexRequestID   = _pRouter->submit( EphemeralRequest::makeScoreRange( ServerRecord::makeIndexKey( _kind ), 0, kMaxServerCount ),
                                              EphemeralStoreRouter::ReplyDelegate::create<&ServerRegistryReader::onIndexReply>( this ), _replyOwner );
    }

    void ServerRegistryReader::onIndexReply( const EphemeralReply& reply )
    {
        --_outstandingCount;
        _indexRequestID = 0;
        if ( reply._result != EphemeralResult::Ok || _pRouter == nullptr )
        {
            // 색인을 못 읽었다(캐시가 아프다) — 옛 스냅숏을 그대로 두고 다음 주기에 다시.
            _listBuilding.clear();
            ++_refreshCount;
            return;
        }
        for ( const EphemeralScoredMember& member : reply._listMember )
        {
            uint64 serverID = 0;
            if ( ServerRecord::parseMember( member._member, serverID ) == false )
                continue;
            const uint64 requestID           = _pRouter->submit( EphemeralRequest::makeGet( ServerRecord::makeRecordKey( serverID ) ),
                                                                 EphemeralStoreRouter::ReplyDelegate::create<&ServerRegistryReader::onRecordReply>( this ), _replyOwner );
            _mapRequestToServer[requestID]   = serverID;
            const uint64 seatRequestID       = _pRouter->submit( EphemeralRequest::makeGet( ServerRecord::makeSeatKey( serverID ) ),
                                                                 EphemeralStoreRouter::ReplyDelegate::create<&ServerRegistryReader::onSeatReply>( this ), _replyOwner );
            _mapRequestToSeat[seatRequestID] = serverID;
            _outstandingCount += 2;
        }
        if ( _outstandingCount == 0 )
            finishRefresh();
    }

    void ServerRegistryReader::onRecordReply( const EphemeralReply& reply )
    {
        --_outstandingCount;
        const auto   serverIt = _mapRequestToServer.find( reply._requestID );
        const uint64 serverID = serverIt != _mapRequestToServer.end() ? serverIt->second : 0;
        if ( serverIt != _mapRequestToServer.end() )
            _mapRequestToServer.erase( serverIt );
        const bool bServerRecordExpired = reply._result == EphemeralResult::NotFound && serverID != 0 && _pRouter != nullptr;
        if ( reply._result == EphemeralResult::Ok )
        {
            ServerStatus status;
            if ( ServerRecord::decode( reply._value, status ) && status._descriptor._serverID == serverID )
                _listBuilding.push_back( std::move( status ) );
        }
        else if ( bServerRecordExpired )
        {
            // 기록이 시한으로 사라졌다 — 서버가 죽었다. 색인의 낡은 멤버를 지운다(어느 읽는 쪽이 지워도 같다).
            _pRouter->submit( EphemeralRequest::makeScoreRemove( ServerRecord::makeIndexKey( _kind ), ServerRecord::makeMember( serverID ) ) );
        }
        if ( _outstandingCount == 0 )
            finishRefresh();
    }

    void ServerRegistryReader::onSeatReply( const EphemeralReply& reply )
    {
        --_outstandingCount;
        const auto seatIt = _mapRequestToSeat.find( reply._requestID );
        if ( seatIt != _mapRequestToSeat.end() )
        {
            int64      reserved = 0;
            const bool bRead    = reply._result == EphemeralResult::Ok &&
                               StringUtil::parseInt64( string_view( reinterpret_cast<const utf8*>( reply._value.data() ), reply._value.size() ), reserved );
            if ( bRead && reserved > 0 ) // 확정이 창을 넘겨 음수가 될 수 있다 — 0 으로 본다
                _mapServerToReserved[seatIt->second] = static_cast<int32>( reserved );
            _mapRequestToSeat.erase( seatIt );
        }
        if ( _outstandingCount == 0 )
            finishRefresh();
    }

    void ServerRegistryReader::finishRefresh()
    {
        for ( ServerStatus& status : _listBuilding )
        {
            const auto reservedIt = _mapServerToReserved.find( status._descriptor._serverID );
            if ( reservedIt != _mapServerToReserved.end() )
                status._load += reservedIt->second; // 다른 배정 서버가 잡아 둔 좌석이다
        }
        _listSnapshot.swap( _listBuilding ); // 색인을 읽었으면 빈 목록도 맞다(서버가 모두 죽었다)
        _listBuilding.clear();
        _mapRequestToServer.clear();
        _mapRequestToSeat.clear();
        _mapServerToReserved.clear();
        ++_refreshCount;
    }

    bool ServerRegistryReader::pickServer( const ServerSelectionQuery& query, UnixMilliseconds nowMs, ServerStatus& outStatus )
    {
        int32 index = constant::invalid_index::kInt32;
        if ( ServerSelectionQuery::pickServer( _listSnapshot, query, nowMs.count(), index ) == false )
            return false;
        ServerStatus& picked = _listSnapshot[static_cast<size_t>( index )];
        picked._load += query._seatCount;
        if ( _pRouter != nullptr && query._seatCount > 0 ) // 다른 배정 서버가 다음 읽기에서 본다
            _pRouter->submit( EphemeralRequest::makeIncrement( ServerRecord::makeSeatKey( picked._descriptor._serverID ), query._seatCount, _seatReservationTTLMs ) );
        outStatus = picked;
        return true;
    }
} // namespace sw
