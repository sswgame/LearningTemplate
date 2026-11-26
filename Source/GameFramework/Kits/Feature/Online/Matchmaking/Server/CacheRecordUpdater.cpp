#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/CacheRecordUpdater.h"

#include "GameFramework/Runtime/Online/Cache/Server/CacheStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/CacheStoreRouter.h"

namespace sw
{
    CacheRecordUpdater::CacheRecordUpdater()
        : _pRouter{ nullptr }
    {
    }

    CacheRecordUpdater::~CacheRecordUpdater()
    {
        shutdown();
    }

    void CacheRecordUpdater::initialize( CacheStoreRouter* pRouter )
    {
        _pRouter    = pRouter;
        _replyOwner = pRouter != nullptr ? pRouter->openReplyOwner() : Subscription{};
    }

    void CacheRecordUpdater::shutdown()
    {
        _replyOwner.reset(); // 기다리던 캐시 답이 이 객체를 부르지 않게 지운다
        _pRouter = nullptr;
    }

    int32 CacheRecordUpdater::getPendingCount() const
    {
        return _pRouter != nullptr ? _pRouter->getPendingCount( _replyOwner ) : 0;
    }

    void CacheRecordUpdater::update( string_view key, int64 ttlMs, unique_ptr<ICacheRecordMutation> mutation )
    {
        if ( _pRouter == nullptr )
        {
            mutation->onFinished( MatchmakingServiceResult::fail( OnlineError::kUnavailable ), CacheValue{} );
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
        const CacheRequest read = CacheRequest::makeGet( operation->_key );
        _pRouter->submit( read, _replyOwner, [this, operation = std::move( operation )]( const CacheReply& reply ) mutable
        { onReadReply( std::move( operation ), reply ); } );
    }

    void CacheRecordUpdater::onReadReply( unique_ptr<Operation> operation, const CacheReply& reply )
    {
        if ( reply._result != CacheResult::Ok && reply._result != CacheResult::NotFound )
        {
            operation->_mutation->onFinished( MatchmakingServiceResult::fail( OnlineError::kUnavailable ), CacheValue{} );
            return;
        }
        operation->_bExists                   = reply._result == CacheResult::Ok ? SW_TRUE : SW_FALSE;
        operation->_oldValue                  = CacheValue::of( reply );
        operation->_newValue                  = CacheValue{};
        bool                           bErase = false;
        const MatchmakingServiceResult result = operation->_mutation->mutate( operation->_bExists != SW_FALSE, operation->_oldValue, operation->_newValue, bErase );
        if ( result != MatchmakingResult::Ok )
        {
            operation->_mutation->onFinished( result, operation->_oldValue );
            return;
        }
        if ( bErase && operation->_bExists == SW_FALSE )
        {
            operation->_mutation->onFinished( MatchmakingResult::Ok, CacheValue{} ); // 이미 없는 기록이다
            return;
        }
        operation->_bErase = bErase ? SW_TRUE : SW_FALSE;
        CacheRequest write;
        if ( bErase )
            write = CacheRequest::makeCompareAndErase( operation->_key, operation->_oldValue );
        else if ( operation->_bExists != SW_FALSE )
            write = CacheRequest::makeCompareAndSet( operation->_key, operation->_oldValue, operation->_newValue, operation->_ttlMs );
        else
            write = CacheRequest::makeSet( operation->_key, operation->_newValue, operation->_ttlMs, CacheCondition::IfAbsent );
        _pRouter->submit( write, _replyOwner, [this, operation = std::move( operation )]( const CacheReply& writeReply ) mutable
        { onWriteReply( std::move( operation ), writeReply ); } );
    }

    void CacheRecordUpdater::onWriteReply( unique_ptr<Operation> operation, const CacheReply& reply )
    {
        if ( reply._result == CacheResult::Ok )
        {
            operation->_mutation->onFinished( MatchmakingResult::Ok, operation->_bErase != SW_FALSE ? CacheValue{} : operation->_newValue );
            return;
        }
        // 다른 곳이 먼저 바꿨다. CAS 가 Conflict 이거나 비교 대상이 사라져 NotFound 이면 다시 읽고 다시 시도한다
        const bool bRaced = reply._result == CacheResult::Conflict || reply._result == CacheResult::NotFound;
        if ( bRaced && ++operation->_attempt < kMaxAttempt )
        {
            startRead( std::move( operation ) );
            return;
        }
        operation->_mutation->onFinished( bRaced ? MatchmakingServiceResult::fail( OnlineError::kBusy ) : MatchmakingServiceResult::fail( OnlineError::kUnavailable ), {} );
    }
} // namespace sw
