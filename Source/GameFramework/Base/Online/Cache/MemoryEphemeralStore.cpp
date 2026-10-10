#include "pch.h"

#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"

#include "Core/Time/MonotonicClock.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace sw
{
    namespace
    {
        struct MemoryEphemeralStoreInternal
        {
            static constexpr int64 kNanosecondsPerMillisecond = 1000000;

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
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + constant::kMaxBuffer32, value );
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

            static bool isRankedBefore( const EphemeralScoredMember& left, const EphemeralScoredMember& right )
            {
                if ( left._score != right._score )
                    return left._score > right._score;
                return left._member > right._member;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MemoryEphemeralDatabase::MemoryEphemeralDatabase()
        : _mutex{}
        , _mapValue{}
        , _mapScoreSet{}
        , _mapInbox{}
        , _manualTimeMs{ 0 }
        , _nextInboxID{ 1 }
        , _bManualTime{ SW_FALSE }
    {
    }

    EphemeralReply MemoryEphemeralDatabase::execute( const EphemeralRequest& request )
    {
        EphemeralReply reply;
        reply._operation = request._operation;
        std::scoped_lock<mutex> lock{ _mutex };
        const int64             nowMs = getNowMsLocked();
        switch ( request._operation )
        {
            case EphemeralOperation::Get:
            case EphemeralOperation::Set:
            case EphemeralOperation::Erase:
            case EphemeralOperation::CompareAndSet:
            case EphemeralOperation::CompareAndErase:
            case EphemeralOperation::Increment:
            case EphemeralOperation::Expire:
            {
                executeValue( request, nowMs, reply );
                break;
            }
            case EphemeralOperation::ScoreSet:
            case EphemeralOperation::ScoreAdd:
            case EphemeralOperation::ScoreRemove:
            case EphemeralOperation::ScoreRank:
            case EphemeralOperation::ScoreRange:
            {
                executeScore( request, nowMs, reply );
                break;
            }
            case EphemeralOperation::Publish:
            {
                executePublish( request, reply );
                break;
            }
        }
        return reply;
    }

    uint64 MemoryEphemeralDatabase::registerInbox()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const uint64            inboxID = _nextInboxID++;
        _mapInbox[inboxID]              = Inbox{};
        return inboxID;
    }

    void MemoryEphemeralDatabase::unregisterInbox( uint64 inboxID )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapInbox.erase( inboxID );
    }

    void MemoryEphemeralDatabase::subscribe( uint64 inboxID, string_view channel )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listChannel = inboxIt->second._listChannel;
        const auto      channelIt   = std::find( listChannel.begin(), listChannel.end(), channel );
        if ( channelIt == listChannel.end() )
            listChannel.push_back( string{ channel } );
    }

    void MemoryEphemeralDatabase::unsubscribe( uint64 inboxID, string_view channel )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listChannel = inboxIt->second._listChannel;
        listChannel.erase( std::remove( listChannel.begin(), listChannel.end(), channel ), listChannel.end() );
    }

    int32 MemoryEphemeralDatabase::takeMessages( uint64 inboxID, vector<EphemeralMessage>& outListMessage )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return 0;
        vector<EphemeralMessage>& listMessage = inboxIt->second._listMessage;
        const int32               takenCount  = static_cast<int32>( listMessage.size() );
        for ( EphemeralMessage& message : listMessage )
        {
            outListMessage.push_back( std::move( message ) );
        }
        listMessage.clear();
        return takenCount;
    }

    void MemoryEphemeralDatabase::clearData()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapValue.clear();
        _mapScoreSet.clear();
    }

    void MemoryEphemeralDatabase::setManualTimeMs( int64 nowMs )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _manualTimeMs = nowMs;
        _bManualTime  = SW_TRUE;
    }

    void MemoryEphemeralDatabase::advanceTimeMs( int64 deltaMs )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bManualTime == SW_FALSE )
        {
            _manualTimeMs = getNowMsLocked();
            _bManualTime  = SW_TRUE;
        }
        _manualTimeMs += deltaMs;
    }

    int64 MemoryEphemeralDatabase::getNowMs() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return getNowMsLocked();
    }

    int64 MemoryEphemeralDatabase::getNowMsLocked() const
    {
        if ( _bManualTime == SW_TRUE )
            return _manualTimeMs;
        return MonotonicClock::nowNanoseconds() / MemoryEphemeralStoreInternal::kNanosecondsPerMillisecond;
    }

    MemoryEphemeralDatabase::ValueEntry* MemoryEphemeralDatabase::findValue( const string& key, int64 nowMs )
    {
        const auto valueIt = _mapValue.find( key );
        if ( valueIt == _mapValue.end() )
            return nullptr;
        if ( MemoryEphemeralStoreInternal::isExpired( valueIt->second._expiresAtMs, nowMs ) )
        {
            _mapValue.erase( valueIt );
            return nullptr;
        }
        return &valueIt->second;
    }

    MemoryEphemeralDatabase::ScoreSetEntry* MemoryEphemeralDatabase::findScoreSet( const string& key, int64 nowMs )
    {
        const auto setIt = _mapScoreSet.find( key );
        if ( setIt == _mapScoreSet.end() )
            return nullptr;
        const bool bGone = MemoryEphemeralStoreInternal::isExpired( setIt->second._expiresAtMs, nowMs ) || setIt->second._mapMemberToScore.empty();
        if ( bGone )
        {
            _mapScoreSet.erase( setIt );
            return nullptr;
        }
        return &setIt->second;
    }

    vector<EphemeralScoredMember> MemoryEphemeralDatabase::makeRanking( const ScoreSetEntry& entry )
    {
        vector<EphemeralScoredMember> listMember;
        listMember.reserve( entry._mapMemberToScore.size() );
        for ( const auto& [member, score] : entry._mapMemberToScore )
        {
            EphemeralScoredMember& scored = listMember.emplace_back();
            scored._member                = member;
            scored._score                 = score;
        }
        std::sort( listMember.begin(), listMember.end(), MemoryEphemeralStoreInternal::isRankedBefore );
        return listMember;
    }

    void MemoryEphemeralDatabase::executeValue( const EphemeralRequest& request, int64 nowMs, EphemeralReply& outReply )
    {
        ValueEntry* pValue = findValue( request._key, nowMs );
        switch ( request._operation )
        {
            case EphemeralOperation::Get:
            {
                if ( pValue == nullptr )
                {
                    outReply._result = EphemeralResult::NotFound;
                    break;
                }
                outReply._value = pValue->_bytes;
                break;
            }
            case EphemeralOperation::Set:
            {
                const bool bBlocked = ( request._condition == EphemeralCondition::IfAbsent && pValue != nullptr ) ||
                                      ( request._condition == EphemeralCondition::IfPresent && pValue == nullptr );
                if ( bBlocked )
                {
                    outReply._result = EphemeralResult::Conflict;
                    break;
                }
                _mapScoreSet.erase( request._key ); // 같은 키의 다른 종류는 덮어쓴다(RESP `SET` 과 같다)
                ValueEntry& entry  = _mapValue[request._key];
                entry._bytes       = request._value;
                entry._expiresAtMs = MemoryEphemeralStoreInternal::makeExpiresAt( request._ttlMs, nowMs );
                break;
            }
            case EphemeralOperation::Erase:
            {
                const bool bHadValue = pValue != nullptr;
                const bool bHadSet   = findScoreSet( request._key, nowMs ) != nullptr;
                _mapValue.erase( request._key );
                _mapScoreSet.erase( request._key );
                if ( bHadValue == false && bHadSet == false )
                    outReply._result = EphemeralResult::NotFound;
                break;
            }
            case EphemeralOperation::CompareAndSet:
            {
                if ( pValue == nullptr || pValue->_bytes != request._expected )
                {
                    outReply._result = EphemeralResult::Conflict;
                    break;
                }
                pValue->_bytes       = request._value;
                pValue->_expiresAtMs = MemoryEphemeralStoreInternal::makeExpiresAt( request._ttlMs, nowMs );
                break;
            }
            case EphemeralOperation::CompareAndErase:
            {
                if ( pValue == nullptr || pValue->_bytes != request._expected )
                {
                    outReply._result = EphemeralResult::Conflict;
                    break;
                }
                _mapValue.erase( request._key );
                break;
            }
            case EphemeralOperation::Increment:
            {
                int64 current = 0;
                if ( pValue != nullptr && MemoryEphemeralStoreInternal::parseInteger( pValue->_bytes, current ) == false )
                {
                    outReply._result = EphemeralResult::Invalid; // 정수가 아닌 값(RESP `ERR value is not an integer` 와 같다)
                    break;
                }
                int64 next = 0;
                if ( MemoryEphemeralStoreInternal::tryAdd( current, request._delta, next ) == false )
                {
                    outReply._result = EphemeralResult::Invalid;
                    break;
                }
                if ( pValue == nullptr )
                {
                    ValueEntry& entry  = _mapValue[request._key];
                    entry._expiresAtMs = MemoryEphemeralStoreInternal::makeExpiresAt( request._ttlMs, nowMs ); // 새 창만 만료를 건다
                    pValue             = &entry;
                }
                pValue->_bytes    = MemoryEphemeralStoreInternal::makeIntegerText( next );
                outReply._integer = next;
                break;
            }
            case EphemeralOperation::Expire:
            {
                ScoreSetEntry* pScoreSet = findScoreSet( request._key, nowMs );
                if ( pValue == nullptr && pScoreSet == nullptr )
                {
                    outReply._result = EphemeralResult::NotFound;
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
                outReply._result = EphemeralResult::Invalid;
                break;
            }
        }
    }

    void MemoryEphemeralDatabase::executeScore( const EphemeralRequest& request, int64 nowMs, EphemeralReply& outReply )
    {
        if ( findValue( request._key, nowMs ) != nullptr )
        {
            outReply._result = EphemeralResult::Invalid; // 값 키에 정렬 집합 연산(RESP `WRONGTYPE`)
            return;
        }
        ScoreSetEntry* pScoreSet = findScoreSet( request._key, nowMs );
        switch ( request._operation )
        {
            case EphemeralOperation::ScoreSet:
            {
                _mapScoreSet[request._key]._mapMemberToScore[request._member] = request._score;
                break;
            }
            case EphemeralOperation::ScoreAdd:
            {
                int64 current = 0;
                if ( pScoreSet != nullptr )
                {
                    const auto memberIt = pScoreSet->_mapMemberToScore.find( request._member );
                    if ( memberIt != pScoreSet->_mapMemberToScore.end() )
                        current = memberIt->second;
                }
                const int64 next = current + request._score; // 둘 다 ±2^53 안이라 넘치지 않는다
                if ( next < -IEphemeralStore::kMaxAbsScore || IEphemeralStore::kMaxAbsScore < next )
                {
                    outReply._result = EphemeralResult::Invalid;
                    break;
                }
                _mapScoreSet[request._key]._mapMemberToScore[request._member] = next;
                outReply._integer                                             = next;
                outReply._score                                               = next;
                break;
            }
            case EphemeralOperation::ScoreRemove:
            {
                const bool bRemoved = pScoreSet != nullptr && pScoreSet->_mapMemberToScore.erase( request._member ) > 0;
                if ( bRemoved == false )
                    outReply._result = EphemeralResult::NotFound;
                break;
            }
            case EphemeralOperation::ScoreRank:
            {
                outReply._result = EphemeralResult::NotFound;
                if ( pScoreSet == nullptr )
                    break;
                const vector<EphemeralScoredMember> listRanked = makeRanking( *pScoreSet );
                for ( size_t rankIndex = 0; rankIndex < listRanked.size(); ++rankIndex )
                {
                    if ( listRanked[rankIndex]._member != request._member )
                        continue;
                    outReply._result  = EphemeralResult::Ok;
                    outReply._integer = static_cast<int64>( rankIndex );
                    outReply._score   = listRanked[rankIndex]._score;
                    break;
                }
                break;
            }
            case EphemeralOperation::ScoreRange:
            {
                if ( pScoreSet == nullptr )
                    break;
                const vector<EphemeralScoredMember> listRanked = makeRanking( *pScoreSet );
                const size_t                        beginIndex = std::min( static_cast<size_t>( request._offset ), listRanked.size() );
                const size_t                        endIndex   = std::min( beginIndex + static_cast<size_t>( request._count ), listRanked.size() );
                outReply._listMember.assign( listRanked.begin() + static_cast<ptrdiff_t>( beginIndex ), listRanked.begin() + static_cast<ptrdiff_t>( endIndex ) );
                break;
            }
            default:
            {
                outReply._result = EphemeralResult::Invalid;
                break;
            }
        }
    }

    void MemoryEphemeralDatabase::executePublish( const EphemeralRequest& request, EphemeralReply& outReply )
    {
        int64 receiverCount = 0;
        for ( auto& [inboxID, inbox] : _mapInbox )
        {
            (void)inboxID;
            const auto channelIt = std::find( inbox._listChannel.begin(), inbox._listChannel.end(), request._key );
            if ( channelIt == inbox._listChannel.end() )
                continue;
            EphemeralMessage& message = inbox._listMessage.emplace_back();
            message._bytes            = request._value;
            message._channel          = request._key;
            ++receiverCount;
        }
        outReply._integer = receiverCount;
    }
} // namespace sw

namespace sw
{
    MemoryEphemeralStore::MemoryEphemeralStore( MemoryEphemeralDatabase* pDatabase )
        : _listReply{}
        , _pDatabase{ pDatabase }
        , _inboxID{ pDatabase->registerInbox() }
        , _nextRequestID{ 1 }
        , _bShutdown{ SW_FALSE }
    {
    }

    MemoryEphemeralStore::~MemoryEphemeralStore()
    {
        if ( _bShutdown == SW_FALSE )
            _pDatabase->unregisterInbox( _inboxID );
    }

    uint64 MemoryEphemeralStore::submit( const EphemeralRequest& request )
    {
        const uint64   requestID = _nextRequestID++;
        EphemeralReply reply;
        if ( _bShutdown == SW_TRUE )
            reply._result = EphemeralResult::Unavailable;
        else if ( request.isWellFormed() == false )
            reply._result = EphemeralResult::Invalid;
        else
            reply = _pDatabase->execute( request );
        reply._requestID = requestID;
        reply._operation = request._operation;
        _listReply.push_back( std::move( reply ) );
        return requestID;
    }

    int32 MemoryEphemeralStore::pollReplies( vector<EphemeralReply>& outListReply )
    {
        const int32 replyCount = static_cast<int32>( _listReply.size() );
        for ( EphemeralReply& reply : _listReply )
        {
            outListReply.push_back( std::move( reply ) );
        }
        _listReply.clear();
        return replyCount;
    }

    void MemoryEphemeralStore::subscribe( string_view channel )
    {
        if ( _bShutdown == SW_FALSE && EphemeralRequest::isValidKey( channel ) )
            _pDatabase->subscribe( _inboxID, channel );
    }

    void MemoryEphemeralStore::unsubscribe( string_view channel )
    {
        if ( _bShutdown == SW_FALSE )
            _pDatabase->unsubscribe( _inboxID, channel );
    }

    int32 MemoryEphemeralStore::pollMessages( vector<EphemeralMessage>& outListMessage )
    {
        if ( _bShutdown == SW_TRUE )
            return 0;
        return _pDatabase->takeMessages( _inboxID, outListMessage );
    }

    void MemoryEphemeralStore::shutdown()
    {
        if ( _bShutdown == SW_TRUE )
            return;
        _bShutdown = SW_TRUE;
        _pDatabase->unregisterInbox( _inboxID );
    }
} // namespace sw
