#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"

namespace sw
{
    namespace
    {
        struct EphemeralStoreInternal
        {
            static EphemeralRequest makeKeyed( EphemeralOperation operation, string_view key )
            {
                EphemeralRequest request;
                request._key       = string{ key };
                request._operation = operation;
                return request;
            }

            static bool isScoreInRange( int64 score ) { return -IEphemeralStore::kMaxAbsScore <= score && score <= IEphemeralStore::kMaxAbsScore; }

            static bool isValidMember( const string& member ) { return member.empty() == false && member.size() <= static_cast<size_t>( IEphemeralStore::kMaxKeySize ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    EphemeralRequest EphemeralRequest::makeGet( string_view key ) { return EphemeralStoreInternal::makeKeyed( EphemeralOperation::Get, key ); }

    EphemeralRequest EphemeralRequest::makeSet( string_view key, vector<uint8> valueBytes, int64 ttlMs, EphemeralCondition condition )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::Set, key );
        request._value           = std::move( valueBytes );
        request._ttlMs           = ttlMs;
        request._condition       = condition;
        return request;
    }

    EphemeralRequest EphemeralRequest::makeErase( string_view key ) { return EphemeralStoreInternal::makeKeyed( EphemeralOperation::Erase, key ); }

    EphemeralRequest EphemeralRequest::makeCompareAndSet( string_view key, vector<uint8> expectedBytes, vector<uint8> valueBytes, int64 ttlMs )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::CompareAndSet, key );
        request._expected        = std::move( expectedBytes );
        request._value           = std::move( valueBytes );
        request._ttlMs           = ttlMs;
        return request;
    }

    EphemeralRequest EphemeralRequest::makeCompareAndErase( string_view key, vector<uint8> expectedBytes )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::CompareAndErase, key );
        request._expected        = std::move( expectedBytes );
        return request;
    }

    EphemeralRequest EphemeralRequest::makeIncrement( string_view key, int64 delta, int64 ttlMsWhenCreated )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::Increment, key );
        request._delta           = delta;
        request._ttlMs           = ttlMsWhenCreated;
        return request;
    }

    EphemeralRequest EphemeralRequest::makeExpire( string_view key, int64 ttlMs )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::Expire, key );
        request._ttlMs           = ttlMs;
        return request;
    }

    EphemeralRequest EphemeralRequest::makeScoreSet( string_view key, string_view member, int64 score )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::ScoreSet, key );
        request._member          = string{ member };
        request._score           = score;
        return request;
    }

    EphemeralRequest EphemeralRequest::makeScoreAdd( string_view key, string_view member, int64 delta )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::ScoreAdd, key );
        request._member          = string{ member };
        request._score           = delta;
        return request;
    }

    EphemeralRequest EphemeralRequest::makeScoreRemove( string_view key, string_view member )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::ScoreRemove, key );
        request._member          = string{ member };
        return request;
    }

    EphemeralRequest EphemeralRequest::makeScoreRank( string_view key, string_view member )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::ScoreRank, key );
        request._member          = string{ member };
        return request;
    }

    EphemeralRequest EphemeralRequest::makeScoreRange( string_view key, int32 offset, int32 count )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::ScoreRange, key );
        request._offset          = offset;
        request._count           = count;
        return request;
    }

    EphemeralRequest EphemeralRequest::makePublish( string_view channel, vector<uint8> messageBytes )
    {
        EphemeralRequest request = EphemeralStoreInternal::makeKeyed( EphemeralOperation::Publish, channel );
        request._value           = std::move( messageBytes );
        return request;
    }

    bool EphemeralRequest::isWellFormed() const
    {
        if ( isValidKey( _key ) == false )
            return false;
        const bool bValueFits = _value.size() <= static_cast<size_t>( IEphemeralStore::kMaxValueSize ) &&
                                _expected.size() <= static_cast<size_t>( IEphemeralStore::kMaxValueSize );
        if ( bValueFits == false )
            return false;
        switch ( _operation )
        {
            case EphemeralOperation::Set:
            case EphemeralOperation::CompareAndSet:
            case EphemeralOperation::Increment:
                return _ttlMs >= 0;
            case EphemeralOperation::ScoreSet:
            case EphemeralOperation::ScoreAdd:
                return EphemeralStoreInternal::isValidMember( _member ) && EphemeralStoreInternal::isScoreInRange( _score );
            case EphemeralOperation::ScoreRemove:
            case EphemeralOperation::ScoreRank:
                return EphemeralStoreInternal::isValidMember( _member );
            case EphemeralOperation::ScoreRange:
                return _offset >= 0 && _count >= 0;
            case EphemeralOperation::Get:
            case EphemeralOperation::Erase:
            case EphemeralOperation::CompareAndErase:
            case EphemeralOperation::Expire:
            case EphemeralOperation::Publish:
                return true;
        }
        return false;
    }

    bool EphemeralRequest::isValidKey( string_view key )
    {
        if ( key.empty() || key.size() > static_cast<size_t>( IEphemeralStore::kMaxKeySize ) )
            return false;
        for ( const utf8 ch : key )
        {
            const bool bLower = 'a' <= ch && ch <= 'z';
            const bool bDigit = '0' <= ch && ch <= '9';
            const bool bMark  = ch == '_' || ch == '.' || ch == ':' || ch == '/' || ch == '-';
            if ( bLower == false && bDigit == false && bMark == false )
                return false;
        }
        return true;
    }
} // namespace sw
