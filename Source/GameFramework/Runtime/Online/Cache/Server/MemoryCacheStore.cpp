#include "pch.h"

#include "GameFramework/Runtime/Online/Cache/Server/MemoryCacheStore.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace sw
{
    namespace
    {
        struct MemoryCacheStoreInternal
        {

            static bool isExpired( int64 expiresAtMs, int64 nowMs ) { return expiresAtMs != 0 && nowMs >= expiresAtMs; }

            static int64 makeExpiresAt( int64 ttlMs, int64 nowMs ) { return ttlMs > 0 ? nowMs + ttlMs : 0; }

            /** @brief 부호 있는 10 진 정수 글을 읽습니다(RESP `INCRBY` 와 같은 엄격함 — 공백 · `+` · 빈 글은 아니다). */
            [[nodiscard]] static bool parseInteger( const vector<uint8>& bytes, int64& outValue )
            {
                if ( bytes.empty() || bytes.size() > 20 )
                    return false;
                size_t index     = 0;
                bool   bNegative = false;
                if ( bytes[0] == '-' )
                {
                    bNegative = true;
                    index     = 1;
                }
                if ( index == bytes.size() )
                    return false;
                uint64 magnitude = 0;
                for ( ; index < bytes.size(); ++index )
                {
                    const uint8 ch = bytes[index];
                    if ( ch < '0' || '9' < ch )
                        return false;
                    magnitude = magnitude * 10 + static_cast<uint64>( ch - '0' );
                    if ( magnitude > static_cast<uint64>( std::numeric_limits<int64>::max() ) + ( bNegative ? 1u : 0u ) )
                        return false;
                }
                outValue = bNegative ? static_cast<int64>( 0 - magnitude ) : static_cast<int64>( magnitude );
                return true;
            }

            static vector<uint8> makeIntegerText( int64 value )
            {
                utf8                       arrBuffer[constant::buffer::kMax32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + constant::buffer::kMax32, value );
                vector<uint8>              bytes;
                bytes.assign( reinterpret_cast<const uint8*>( arrBuffer ), reinterpret_cast<const uint8*>( result.ptr ) );
                return bytes;
            }

            /** @brief 넘침 없이 더할 수 있으면 true 입니다. */
            [[nodiscard]] static bool tryAdd( int64 left, int64 right, int64& outSum )
            {
                const bool bOverflow = ( right > 0 && left > std::numeric_limits<int64>::max() - right ) || ( right < 0 && left < std::numeric_limits<int64>::min() - right );
                if ( bOverflow )
                    return false;
                outSum = left + right;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MemoryCacheDatabase::MemoryCacheDatabase( const IClock& clock )
        : _clock{ clock }
        , _mutex{}
        , _mapValue{}
        , _mapScoreSet{}
        , _mapInbox{}
        , _inboxIDAllocator{}
    {
    }

    CacheReply MemoryCacheDatabase::execute( const CacheRequest& request )
    {
        CacheReply reply;
        reply._operation = request._operation;
        ScopedLock<mutex> lock{ _mutex };
        const int64       nowMs = _clock.nowMilliseconds().count(); // 만료는 같은 시계 안의 차이로만 잰다
        switch ( request._operation )
        {
            case CacheOperation::Get:
            case CacheOperation::Set:
            case CacheOperation::Erase:
            case CacheOperation::CompareAndSet:
            case CacheOperation::CompareAndErase:
            case CacheOperation::Increment:
            case CacheOperation::Expire:
            {
                executeValue( request, nowMs, reply );
                break;
            }
            case CacheOperation::ScoreSet:
            case CacheOperation::ScoreAdd:
            case CacheOperation::ScoreRemove:
            case CacheOperation::ScoreRank:
            case CacheOperation::ScoreRange:
            {
                executeScore( request, nowMs, reply );
                break;
            }
            case CacheOperation::Publish:
            {
                executePublish( request, reply );
                break;
            }
        }
        return reply;
    }

    Subscription MemoryCacheDatabase::registerInbox( uint64& outInboxID )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            outInboxID            = _inboxIDAllocator.allocate();
            _mapInbox[outInboxID] = Inbox{};
        }
        return _subscriptions.issue( outInboxID );
    }

    void MemoryCacheDatabase::releaseSubscription( uint64 id )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapInbox.erase( id );
    }

    void MemoryCacheDatabase::subscribe( uint64 inboxID, string_view channel )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listChannel = inboxIt->second._listChannel;
        VectorUtil::addUnique( listChannel, channel );
    }

    void MemoryCacheDatabase::unsubscribe( uint64 inboxID, string_view channel )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listChannel = inboxIt->second._listChannel;
        VectorUtil::removeIf( listChannel, [&channel]( const string& subscribed )
        { return subscribed == channel; } );
    }

    int32 MemoryCacheDatabase::takeMessages( uint64 inboxID, vector<CacheMessage>& outListMessage )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return 0;
        vector<CacheMessage>& listMessage = inboxIt->second._listMessage;
        const int32           takenCount  = static_cast<int32>( listMessage.size() );
        for ( CacheMessage& message : listMessage )
        {
            outListMessage.push_back( std::move( message ) );
        }
        listMessage.clear();
        return takenCount;
    }

    void MemoryCacheDatabase::clearData()
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapValue.clear();
        _mapScoreSet.clear();
    }

    MemoryCacheDatabase::ValueEntry* MemoryCacheDatabase::findValue( const string& key, int64 nowMs )
    {
        const auto valueIt = _mapValue.find( key );
        if ( valueIt == _mapValue.end() )
            return nullptr;
        if ( MemoryCacheStoreInternal::isExpired( valueIt->second._expiresAtMs, nowMs ) )
        {
            _mapValue.erase( valueIt );
            return nullptr;
        }
        return &valueIt->second;
    }

    MemoryCacheDatabase::ScoreSetEntry* MemoryCacheDatabase::findScoreSet( const string& key, int64 nowMs )
    {
        const auto setIt = _mapScoreSet.find( key );
        if ( setIt == _mapScoreSet.end() )
            return nullptr;
        const bool bGone = MemoryCacheStoreInternal::isExpired( setIt->second._expiresAtMs, nowMs ) || setIt->second._mapMemberToScore.empty();
        if ( bGone )
        {
            _mapScoreSet.erase( setIt );
            return nullptr;
        }
        return &setIt->second;
    }

    vector<CacheScoredMember> MemoryCacheDatabase::makeRanking( const ScoreSetEntry& entry )
    {
        vector<CacheScoredMember> listMember;
        listMember.reserve( entry._mapMemberToScore.size() );
        for ( const auto& [member, score] : entry._mapMemberToScore )
        {
            CacheScoredMember& scored = listMember.emplace_back();
            scored._member            = member;
            scored._score             = score;
        }
        VectorUtil::sort( listMember, Descending<&CacheScoredMember::_score, &CacheScoredMember::_member>{} );
        return listMember;
    }

    // 연산마다 짧은 분기인 switch 디스패처라 나누지 않는다. 연산 하나의 규칙(RESP 와 같은 동작)을 한곳에서 읽는다.
    void MemoryCacheDatabase::executeValue( const CacheRequest& request, int64 nowMs, CacheReply& outReply )
    {
        ValueEntry* pValue = findValue( request._key, nowMs );
        switch ( request._operation )
        {
            case CacheOperation::Get:
            {
                if ( pValue == nullptr )
                {
                    outReply._result = CacheResult::NotFound;
                    break;
                }
                outReply._value = pValue->_bytes;
                break;
            }
            case CacheOperation::Set:
            {
                const bool bBlocked = ( request._condition == CacheCondition::IfAbsent && pValue != nullptr ) ||
                                      ( request._condition == CacheCondition::IfPresent && pValue == nullptr );
                if ( bBlocked )
                {
                    outReply._result = CacheResult::Conflict;
                    break;
                }
                _mapScoreSet.erase( request._key ); // 같은 키의 다른 종류는 덮어쓴다(RESP `SET` 과 같다)
                ValueEntry& entry  = _mapValue[request._key];
                entry._bytes       = request._value;
                entry._expiresAtMs = MemoryCacheStoreInternal::makeExpiresAt( request._ttlMs, nowMs );
                break;
            }
            case CacheOperation::Erase:
            {
                const bool bHadValue = pValue != nullptr;
                const bool bHadSet   = findScoreSet( request._key, nowMs ) != nullptr;
                _mapValue.erase( request._key );
                _mapScoreSet.erase( request._key );
                if ( bHadValue == false && bHadSet == false )
                    outReply._result = CacheResult::NotFound;
                break;
            }
            case CacheOperation::CompareAndSet:
            {
                if ( pValue == nullptr || pValue->_bytes != request._expected )
                {
                    outReply._result = CacheResult::Conflict;
                    break;
                }
                pValue->_bytes       = request._value;
                pValue->_expiresAtMs = MemoryCacheStoreInternal::makeExpiresAt( request._ttlMs, nowMs );
                break;
            }
            case CacheOperation::CompareAndErase:
            {
                if ( pValue == nullptr || pValue->_bytes != request._expected )
                {
                    outReply._result = CacheResult::Conflict;
                    break;
                }
                _mapValue.erase( request._key );
                break;
            }
            case CacheOperation::Increment:
            {
                if ( pValue == nullptr && findScoreSet( request._key, nowMs ) != nullptr )
                {
                    outReply._result = CacheResult::Invalid; // 정렬 집합 키의 증감(RESP `INCRBY` 의 WRONGTYPE 과 같다) — 값을 만들면 집합이 가려진다
                    break;
                }
                int64 current = 0;
                if ( pValue != nullptr && MemoryCacheStoreInternal::parseInteger( pValue->_bytes, current ) == false )
                {
                    outReply._result = CacheResult::Invalid; // 정수가 아닌 값(RESP `ERR value is not an integer` 와 같다)
                    break;
                }
                int64 next = 0;
                if ( MemoryCacheStoreInternal::tryAdd( current, request._delta, next ) == false )
                {
                    outReply._result = CacheResult::Invalid;
                    break;
                }
                if ( pValue == nullptr )
                {
                    ValueEntry& entry  = _mapValue[request._key];
                    entry._expiresAtMs = MemoryCacheStoreInternal::makeExpiresAt( request._ttlMs, nowMs ); // 새 창만 만료를 건다
                    pValue             = &entry;
                }
                pValue->_bytes    = MemoryCacheStoreInternal::makeIntegerText( next );
                outReply._integer = next;
                break;
            }
            case CacheOperation::Expire:
            {
                ScoreSetEntry* pScoreSet = findScoreSet( request._key, nowMs );
                if ( pValue == nullptr && pScoreSet == nullptr )
                {
                    outReply._result = CacheResult::NotFound;
                    break;
                }
                if ( request._ttlMs <= 0 ) // RESP `PEXPIRE 0` 과 같이 바로 지운다
                {
                    _mapValue.erase( request._key );
                    _mapScoreSet.erase( request._key );
                    break;
                }
                const int64 expiresAtMs = nowMs + request._ttlMs;
                if ( pValue != nullptr )
                    pValue->_expiresAtMs = expiresAtMs;
                if ( pScoreSet != nullptr )
                    pScoreSet->_expiresAtMs = expiresAtMs;
                break;
            }
            default:
            {
                outReply._result = CacheResult::Invalid;
                break;
            }
        }
    }

    void MemoryCacheDatabase::executeScore( const CacheRequest& request, int64 nowMs, CacheReply& outReply )
    {
        if ( findValue( request._key, nowMs ) != nullptr )
        {
            outReply._result = CacheResult::Invalid; // 값 키에 정렬 집합 연산(RESP `WRONGTYPE`)
            return;
        }
        ScoreSetEntry* pScoreSet = findScoreSet( request._key, nowMs );
        switch ( request._operation )
        {
            case CacheOperation::ScoreSet:
            {
                _mapScoreSet[request._key]._mapMemberToScore[request._member] = request._score;
                break;
            }
            case CacheOperation::ScoreAdd:
            {
                int64 current = 0;
                if ( pScoreSet != nullptr )
                {
                    const auto memberIt = pScoreSet->_mapMemberToScore.find( request._member );
                    if ( memberIt != pScoreSet->_mapMemberToScore.end() )
                        current = memberIt->second;
                }
                const int64 next = current + request._score; // 둘 다 ±2^53 안이라 넘치지 않는다
                if ( next < -ICacheStore::kMaxAbsScore || ICacheStore::kMaxAbsScore < next )
                {
                    outReply._result = CacheResult::Invalid;
                    break;
                }
                _mapScoreSet[request._key]._mapMemberToScore[request._member] = next;
                outReply._integer                                             = next;
                outReply._score                                               = next;
                break;
            }
            case CacheOperation::ScoreRemove:
            {
                const bool bRemoved = pScoreSet != nullptr && pScoreSet->_mapMemberToScore.erase( request._member ) > 0;
                if ( bRemoved == false )
                    outReply._result = CacheResult::NotFound;
                break;
            }
            case CacheOperation::ScoreRank:
            {
                outReply._result = CacheResult::NotFound;
                if ( pScoreSet == nullptr )
                    break;
                const vector<CacheScoredMember> listRanked = makeRanking( *pScoreSet );
                for ( size_t rankIndex = 0; rankIndex < listRanked.size(); ++rankIndex )
                {
                    if ( listRanked[rankIndex]._member != request._member )
                        continue;
                    outReply._result  = CacheResult::Ok;
                    outReply._integer = static_cast<int64>( rankIndex );
                    outReply._score   = listRanked[rankIndex]._score;
                    break;
                }
                break;
            }
            case CacheOperation::ScoreRange:
            {
                if ( pScoreSet == nullptr )
                    break;
                const vector<CacheScoredMember> listRanked = makeRanking( *pScoreSet );
                const size_t                    beginIndex = MathUtil::min( static_cast<size_t>( request._offset ), listRanked.size() );
                const size_t                    endIndex   = MathUtil::min( beginIndex + static_cast<size_t>( request._count ), listRanked.size() );
                outReply._listMember.assign( listRanked.begin() + static_cast<ptrdiff_t>( beginIndex ), listRanked.begin() + static_cast<ptrdiff_t>( endIndex ) );
                break;
            }
            default:
            {
                outReply._result = CacheResult::Invalid;
                break;
            }
        }
    }

    void MemoryCacheDatabase::executePublish( const CacheRequest& request, CacheReply& outReply )
    {
        int64 receiverCount = 0;
        for ( auto& [inboxID, inbox] : _mapInbox )
        {
            (void)inboxID;
            if ( VectorUtil::contains( inbox._listChannel, request._key ) == false )
                continue;
            CacheMessage& message = inbox._listMessage.emplace_back();
            message._bytes        = request._value;
            message._channel      = request._key;
            ++receiverCount;
        }
        outReply._integer = receiverCount;
    }
} // namespace sw

namespace sw
{
    MemoryCacheStore::MemoryCacheStore( MemoryCacheDatabase* pDatabase )
        : _replyBuffer{}
        , _pDatabase{ pDatabase }
        , _inboxID{ 0 }
        , _inboxSubscription{ pDatabase->registerInbox( _inboxID ) }
        , _requestIDAllocator{}
        , _bShutdown{ SW_FALSE }
    {
    }

    MemoryCacheStore::~MemoryCacheStore() = default; // 받은편지함은 구독이 지운다(`shutdown` 했으면 이미 풀렸다)

    uint64 MemoryCacheStore::submit( const CacheRequest& request )
    {
        const uint64 requestID = _requestIDAllocator.allocate();
        CacheReply   reply;
        if ( _bShutdown == SW_TRUE )
            reply._result = CacheResult::Unavailable;
        else if ( request.isWellFormed() == false )
            reply._result = CacheResult::Invalid;
        else
            reply = _pDatabase->execute( request );
        reply._requestID = requestID;
        reply._operation = request._operation;
        _replyBuffer.push( std::move( reply ) );
        return requestID;
    }

    int32 MemoryCacheStore::pollReplies( vector<CacheReply>& outListReply )
    {
        const int32 replyCount = static_cast<int32>( _replyBuffer.getCount() );
        _replyBuffer.moveAllTo( outListReply );
        return replyCount;
    }

    void MemoryCacheStore::subscribe( string_view channel )
    {
        if ( _bShutdown == SW_FALSE && CacheRequest::isValidKey( channel ) )
            _pDatabase->subscribe( _inboxID, channel );
    }

    void MemoryCacheStore::unsubscribe( string_view channel )
    {
        if ( _bShutdown == SW_FALSE )
            _pDatabase->unsubscribe( _inboxID, channel );
    }

    int32 MemoryCacheStore::pollMessages( vector<CacheMessage>& outListMessage )
    {
        if ( _bShutdown == SW_TRUE )
            return 0;
        return _pDatabase->takeMessages( _inboxID, outListMessage );
    }

    void MemoryCacheStore::shutdown()
    {
        if ( _bShutdown == SW_TRUE )
            return;
        _bShutdown = SW_TRUE;
        _inboxSubscription.reset();
    }
} // namespace sw
