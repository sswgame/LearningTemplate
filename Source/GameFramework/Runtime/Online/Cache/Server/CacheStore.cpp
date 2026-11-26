#include "pch.h"

#include "GameFramework/Runtime/Online/Cache/Server/CacheStore.h"

#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct CacheStoreInternal
        {
            static CacheRequest makeKeyed( CacheOperation operation, string_view key )
            {
                CacheRequest request;
                request._key       = string{ key };
                request._operation = operation;
                return request;
            }

            static bool isScoreInRange( int64 score ) { return -ICacheStore::kMaxAbsScore <= score && score <= ICacheStore::kMaxAbsScore; }

            static bool isValidMember( const string& member ) { return member.empty() == false && member.size() <= static_cast<size_t>( OnlineLimit::kMaxKeySize ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CacheRequest CacheRequest::makeGet( string_view key )
    {
        return CacheStoreInternal::makeKeyed( CacheOperation::Get, key );
    }

    CacheValue CacheValue::of( const CacheReply& reply )
    {
        return CacheValue{ reply._value };
    }

    CacheRequest CacheRequest::makeSet( string_view key, CacheValue value, int64 ttlMs, CacheCondition condition )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::Set, key );
        request._value       = std::move( value._bytes );
        request._ttlMs       = ttlMs;
        request._condition   = condition;
        return request;
    }

    CacheRequest CacheRequest::makeErase( string_view key )
    {
        return CacheStoreInternal::makeKeyed( CacheOperation::Erase, key );
    }

    CacheRequest CacheRequest::makeCompareAndSet( string_view key, CacheValue expected, CacheValue value, int64 ttlMs )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::CompareAndSet, key );
        request._expected    = std::move( expected._bytes );
        request._value       = std::move( value._bytes );
        request._ttlMs       = ttlMs;
        return request;
    }

    CacheRequest CacheRequest::makeCompareAndErase( string_view key, CacheValue expected )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::CompareAndErase, key );
        request._expected    = std::move( expected._bytes );
        return request;
    }

    CacheRequest CacheRequest::makeIncrement( string_view key, int64 delta, int64 ttlMsWhenCreated )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::Increment, key );
        request._delta       = delta;
        request._ttlMs       = ttlMsWhenCreated;
        return request;
    }

    CacheRequest CacheRequest::makeExpire( string_view key, int64 ttlMs )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::Expire, key );
        request._ttlMs       = ttlMs;
        return request;
    }

    CacheRequest CacheRequest::makeScoreSet( string_view key, string_view member, int64 score )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::ScoreSet, key );
        request._member      = string{ member };
        request._score       = score;
        return request;
    }

    CacheRequest CacheRequest::makeScoreAdd( string_view key, string_view member, int64 delta )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::ScoreAdd, key );
        request._member      = string{ member };
        request._score       = delta;
        return request;
    }

    CacheRequest CacheRequest::makeScoreRemove( string_view key, string_view member )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::ScoreRemove, key );
        request._member      = string{ member };
        return request;
    }

    CacheRequest CacheRequest::makeScoreRank( string_view key, string_view member )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::ScoreRank, key );
        request._member      = string{ member };
        return request;
    }

    CacheRequest CacheRequest::makeScoreRange( string_view key, int32 offset, int32 count )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::ScoreRange, key );
        request._offset      = offset;
        request._count       = count;
        return request;
    }

    CacheRequest CacheRequest::makePublish( string_view channel, vector<uint8> messageBytes )
    {
        CacheRequest request = CacheStoreInternal::makeKeyed( CacheOperation::Publish, channel );
        request._value       = std::move( messageBytes );
        return request;
    }

    bool CacheRequest::isWellFormed() const
    {
        if ( isValidKey( _key ) == false )
            return false;
        const bool bValueFits = _value.size() <= static_cast<size_t>( ICacheStore::kMaxValueSize ) &&
                                _expected.size() <= static_cast<size_t>( ICacheStore::kMaxValueSize );
        if ( bValueFits == false )
            return false;
        switch ( _operation )
        {
            case CacheOperation::Set:
            case CacheOperation::CompareAndSet:
            case CacheOperation::Increment:
                return _ttlMs >= 0;
            case CacheOperation::ScoreSet:
            case CacheOperation::ScoreAdd:
                return CacheStoreInternal::isValidMember( _member ) && CacheStoreInternal::isScoreInRange( _score );
            case CacheOperation::ScoreRemove:
            case CacheOperation::ScoreRank:
                return CacheStoreInternal::isValidMember( _member );
            case CacheOperation::ScoreRange:
                return _offset >= 0 && _count >= 0;
            case CacheOperation::Get:
            case CacheOperation::Erase:
            case CacheOperation::CompareAndErase:
            case CacheOperation::Expire:
            case CacheOperation::Publish:
                return true;
        }
        return false;
    }

    bool CacheRequest::isValidKey( string_view key )
    {
        return ServiceKeyUtil::isTokenText( key, OnlineLimit::kMaxKeySize, "_.:/-" );
    }
} // namespace sw
