#include "pch.h"

#include "GameFramework/Base/Online/Directory/ServerRegistryReader.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"

namespace sw
{
    ServerRegistryReader::ServerRegistryReader()
        : _listSnapshot{}
        , _listBuilding{}
        , _mapRequestToServer{}
        , _kind{}
        , _pRouter{ nullptr }
        , _indexRequestID{ 0 }
        , _refreshPeriodMs{ 2000 }
        , _lastRefreshStartMs{ 0 }
        , _refreshCount{ 0 }
        , _outstandingCount{ 0 }
        , _bRefreshRequested{ SW_TRUE }
    {
    }

    ServerRegistryReader::~ServerRegistryReader() { shutdown(); }

    void ServerRegistryReader::initialize( EphemeralStoreRouter* pRouter, string_view kind, int64 refreshPeriodMs )
    {
        _pRouter           = pRouter;
        _kind              = string( kind );
        _refreshPeriodMs   = refreshPeriodMs;
        _bRefreshRequested = SW_TRUE;
    }

    void ServerRegistryReader::shutdown()
    {
        if ( _pRouter != nullptr )
        {
            // 라우터가 이 객체보다 오래 산다 — 기다리던 답이 사라진 객체를 부르지 않게 취소한다.
            if ( _indexRequestID != 0 )
                _pRouter->cancel( _indexRequestID );
            for ( const auto& [requestID, serverID] : _mapRequestToServer )
            {
                _pRouter->cancel( requestID );
            }
        }
        _pRouter          = nullptr;
        _indexRequestID   = 0;
        _outstandingCount = 0;
        _listSnapshot.clear();
        _listBuilding.clear();
        _mapRequestToServer.clear();
    }

    void ServerRegistryReader::tick( int64 nowMs )
    {
        if ( _pRouter == nullptr || _outstandingCount > 0 )
            return;
        if ( _bRefreshRequested == SW_FALSE && nowMs - _lastRefreshStartMs < _refreshPeriodMs )
            return;
        startRefresh( nowMs );
    }

    void ServerRegistryReader::requestRefresh() { _bRefreshRequested = SW_TRUE; }

    void ServerRegistryReader::startRefresh( int64 nowMs )
    {
        _bRefreshRequested  = SW_FALSE;
        _lastRefreshStartMs = nowMs;
        _listBuilding.clear();
        _mapRequestToServer.clear();
        _outstandingCount = 1;
        _indexRequestID   = _pRouter->submit( EphemeralRequest::makeScoreRange( ServerRecord::makeIndexKey( _kind ), 0, kMaxServerCount ),
                                              EphemeralStoreRouter::ReplyDelegate::create<&ServerRegistryReader::onIndexReply>( this ) );
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
            const uint64 requestID         = _pRouter->submit( EphemeralRequest::makeGet( ServerRecord::makeRecordKey( serverID ) ),
                                                               EphemeralStoreRouter::ReplyDelegate::create<&ServerRegistryReader::onRecordReply>( this ) );
            _mapRequestToServer[requestID] = serverID;
            ++_outstandingCount;
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
        if ( reply._result == EphemeralResult::Ok )
        {
            ServerStatus status;
            if ( ServerRecord::decode( reply._value, status ) && status._descriptor._serverID == serverID )
                _listBuilding.push_back( std::move( status ) );
        }
        else if ( reply._result == EphemeralResult::NotFound && serverID != 0 && _pRouter != nullptr )
        {
            // 기록이 시한으로 사라졌다 — 서버가 죽었다. 색인의 낡은 멤버를 지운다(어느 읽는 쪽이 지워도 같다).
            (void)_pRouter->submit( EphemeralRequest::makeScoreRemove( ServerRecord::makeIndexKey( _kind ), ServerRecord::makeMember( serverID ) ),
                                    EphemeralStoreRouter::ReplyDelegate{} );
        }
        if ( _outstandingCount == 0 )
            finishRefresh();
    }

    void ServerRegistryReader::finishRefresh()
    {
        _listSnapshot.swap( _listBuilding ); // 색인을 읽었으면 빈 목록도 맞다(서버가 모두 죽었다)
        _listBuilding.clear();
        _mapRequestToServer.clear();
        ++_refreshCount;
    }

    bool ServerRegistryReader::pickServer( const ServerSelectionQuery& query, int64 nowMs, ServerStatus& outStatus )
    {
        int32 index = -1;
        if ( ServerSelection::pickServer( _listSnapshot, query, nowMs, index ) == false )
            return false;
        ServerStatus& picked = _listSnapshot[static_cast<size_t>( index )];
        picked._load += query._seatCount;
        outStatus = picked;
        return true;
    }
} // namespace sw
