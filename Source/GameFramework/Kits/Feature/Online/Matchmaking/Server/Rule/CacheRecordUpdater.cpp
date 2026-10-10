#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/CacheRecordUpdater.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"

namespace sw
{
    CacheRecordUpdater::CacheRecordUpdater()
        : _mapRequestToOperation{}
        , _pRouter{ nullptr }
    {
    }

    CacheRecordUpdater::~CacheRecordUpdater() { shutdown(); }

    void CacheRecordUpdater::initialize( EphemeralStoreRouter* pRouter ) { _pRouter = pRouter; }

    void CacheRecordUpdater::shutdown()
    {
        if ( _pRouter != nullptr )
        {
            for ( const auto& [requestID, operation] : _mapRequestToOperation )
            {
                _pRouter->cancel( requestID );
            }
        }
        _mapRequestToOperation.clear();
        _pRouter = nullptr;
    }

    void CacheRecordUpdater::update( string_view key, int64 ttlMs, unique_ptr<ICacheRecordMutation> mutation )
    {
        if ( _pRouter == nullptr )
        {
            mutation->onFinished( MatchmakingResult::Unavailable, {} );
            return;
        }
        unique_ptr<Operation> operation = make_unique<Operation>();
        operation->_mutation            = std::move( mutation );
        operation->_key                 = string( key );
        operation->_ttlMs               = ttlMs;
        startRead( std::move( operation ) );
    }

    void CacheRecordUpdater::startRead( unique_ptr<Operation> operation )
    {
        const uint64 requestID = _pRouter->submit( EphemeralRequest::makeGet( operation->_key ),
                                                   EphemeralStoreRouter::ReplyDelegate::create<&CacheRecordUpdater::onReadReply>( this ) );
        _mapRequestToOperation.emplace( requestID, std::move( operation ) );
    }

    void CacheRecordUpdater::onReadReply( const EphemeralReply& reply )
    {
        const auto operationIt = _mapRequestToOperation.find( reply._requestID );
        if ( operationIt == _mapRequestToOperation.end() )
            return;
        unique_ptr<Operation> operation = std::move( operationIt->second );
        _mapRequestToOperation.erase( operationIt );
        if ( reply._result != EphemeralResult::Ok && reply._result != EphemeralResult::NotFound )
        {
            operation->_mutation->onFinished( MatchmakingResult::Unavailable, {} );
            return;
        }
        operation->_bExists  = reply._result == EphemeralResult::Ok ? SW_TRUE : SW_FALSE;
        operation->_oldBytes = reply._value;
        operation->_newBytes.clear();
        bool                    bErase = false;
        const MatchmakingResult result = operation->_mutation->mutate( operation->_bExists != SW_FALSE, operation->_oldBytes, operation->_newBytes, bErase );
        if ( result != MatchmakingResult::Ok )
        {
            operation->_mutation->onFinished( result, operation->_oldBytes );
            return;
        }
        if ( bErase && operation->_bExists == SW_FALSE )
        {
            operation->_mutation->onFinished( MatchmakingResult::Ok, {} ); // 이미 없다
            return;
        }
        operation->_bErase = bErase ? SW_TRUE : SW_FALSE;
        EphemeralRequest write;
        if ( bErase )
            write = EphemeralRequest::makeCompareAndErase( operation->_key, operation->_oldBytes );
        else if ( operation->_bExists != SW_FALSE )
            write = EphemeralRequest::makeCompareAndSet( operation->_key, operation->_oldBytes, operation->_newBytes, operation->_ttlMs );
        else
            write = EphemeralRequest::makeSet( operation->_key, operation->_newBytes, operation->_ttlMs, EphemeralCondition::IfAbsent );
        const uint64 requestID = _pRouter->submit( write, EphemeralStoreRouter::ReplyDelegate::create<&CacheRecordUpdater::onWriteReply>( this ) );
        _mapRequestToOperation.emplace( requestID, std::move( operation ) );
    }

    void CacheRecordUpdater::onWriteReply( const EphemeralReply& reply )
    {
        const auto operationIt = _mapRequestToOperation.find( reply._requestID );
        if ( operationIt == _mapRequestToOperation.end() )
            return;
        unique_ptr<Operation> operation = std::move( operationIt->second );
        _mapRequestToOperation.erase( operationIt );
        if ( reply._result == EphemeralResult::Ok )
        {
            operation->_mutation->onFinished( MatchmakingResult::Ok, operation->_bErase != SW_FALSE ? vector<uint8>{} : operation->_newBytes );
            return;
        }
        // 누가 먼저 바꿨다(CAS 의 Conflict · 비교 대상이 사라져 NotFound) — 다시 읽고 다시
        const bool bRaced = reply._result == EphemeralResult::Conflict || reply._result == EphemeralResult::NotFound;
        if ( bRaced && ++operation->_attempt < kMaxAttempt )
        {
            startRead( std::move( operation ) );
            return;
        }
        operation->_mutation->onFinished( bRaced ? MatchmakingResult::Conflict : MatchmakingResult::Unavailable, {} );
    }
} // namespace sw
