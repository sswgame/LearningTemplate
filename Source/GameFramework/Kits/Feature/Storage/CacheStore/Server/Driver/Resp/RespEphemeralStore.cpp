#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespEphemeralStore.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RespEphemeralStoreInternal
        {
            static constexpr int64 kNanosecondsPerMillisecond = 1000000;
            static constexpr int64 kFirstBackoffMs            = 100;

            // 걸음 번호 — 한 요청의 몇 번째 명령인지(답 순서는 연결이 지킨다).
            static constexpr uint32 kStepFirst          = 0;
            static constexpr uint32 kStepSecond         = 1;
            static constexpr uint32 kStepThird          = 2;
            static constexpr uint32 kStepFourth         = 3;
            static constexpr uint32 kStepCompareUnwatch = 2;
            static constexpr uint32 kStepCompareMulti   = 3;
            static constexpr uint32 kStepCompareWrite   = 4;
            static constexpr uint32 kStepCompareExec    = 5;

            static bool isWrongType( const RespValue& value ) { return value.isError() && value.getText().substr( 0, 9 ) == "WRONGTYPE"; }

            static int64 makeNextBackoffMs( int64 backoffMs, int64 maxBackoffMs )
            {
                if ( backoffMs <= 0 )
                    return kFirstBackoffMs;
                return std::min( backoffMs * 2, maxBackoffMs );
            }

            static void sleepOneMillisecond() { MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + kNanosecondsPerMillisecond ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RespEphemeralStore::RespEphemeralStore()
        : _settings{}
        , _transportSettings{}
        , _mutex{}
        , _commandConnection{ false }
        , _subscribeConnection{ true }
        , _listMessage{}
        , _listRecordScratch{}
        , _listPushScratch{}
        , _listChannel{}
        , _listOperation{}
        , _transport{}
        , _tlsContext{}
        , _nextCommandConnectNanoseconds{ 0 }
        , _nextSubscribeConnectNanoseconds{ 0 }
        , _commandBackoffMs{ 0 }
        , _subscribeBackoffMs{ 0 }
        , _nextRequestId{ 1 }
        , _subscriptionAckCount{ 0 }
        , _subscriptionSentCount{ 0 }
        , _blockingRequestId{ 0 }
        , _bInitialized{ SW_FALSE }
        , _bShutdown{ SW_FALSE }
    {
    }

    RespEphemeralStore::~RespEphemeralStore() { shutdown(); }

    bool RespEphemeralStore::initialize( unique_ptr<IStreamTransport> transport, const StreamTransportSettings& transportSettings, const RespStoreSettings& settings,
                                         unique_ptr<ITlsContext> tlsContext, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        if ( transport == nullptr )
        {
            outError = "RESP store needs a stream transport";
            return false;
        }
        if ( settings._address.isValid() == false )
        {
            outError = "RESP store needs a host:port endpoint";
            return false;
        }
        _settings          = settings;
        _transportSettings = transportSettings;
        _transport         = std::move( transport );
        _tlsContext        = std::move( tlsContext );
        if ( _transport->initialize( this, _transportSettings ) == false )
        {
            outError = "RESP store could not start its stream transport";
            _transport.reset();
            return false;
        }
        _bInitialized = SW_TRUE;
        std::scoped_lock<mutex> lock{ _mutex };
        (void)ensureCommandConnection();
        return true;
    }

    uint64 RespEphemeralStore::submit( const EphemeralRequest& request )
    {
        const uint64 requestId      = _nextRequestId++;
        Operation&   operation      = _listOperation.emplace_back();
        operation._request          = request;
        operation._reply._requestId = requestId;
        operation._reply._operation = request._operation;
        if ( _bInitialized == SW_FALSE || _bShutdown == SW_TRUE )
        {
            finishOperation( operation, EphemeralResult::Unavailable );
            return requestId;
        }
        if ( request.isWellFormed() == false )
        {
            finishOperation( operation, EphemeralResult::Invalid );
            return requestId;
        }
        if ( request._operation == EphemeralOperation::ScoreRange && request._count <= 0 )
        {
            finishOperation( operation, EphemeralResult::Ok );
            return requestId;
        }
        // 비교 후 쓰기가 GET 답을 기다리는 동안은 내보내지 않는다(막힘이 풀리면 `sendDeferredOperations` 가 맡긴 순서대로 보낸다).
        if ( _blockingRequestId == 0 )
            sendOperation( operation );
        return requestId;
    }

    int32 RespEphemeralStore::pollReplies( vector<EphemeralReply>& outListReply )
    {
        if ( _bInitialized == SW_TRUE && _bShutdown == SW_FALSE )
        {
            pumpTransport();
            vector<RespReplyRecord> listRecord;
            {
                std::scoped_lock<mutex> lock{ _mutex };
                _commandConnection.takeReplyRecords( listRecord );
            }
            for ( const RespReplyRecord& record : listRecord )
            {
                Operation* pOperation = findOperation( record._tag._requestId );
                if ( pOperation == nullptr || pOperation->_bDone == SW_TRUE )
                    continue;
                if ( record._bFailed == SW_TRUE )
                    failOperation( *pOperation );
                else
                    handleReply( *pOperation, record._tag._step, record._value );
            }
            abortTimedOutRequests();
        }
        int32 replyCount = 0;
        while ( _listOperation.empty() == false && _listOperation.front()._bDone == SW_TRUE )
        {
            outListReply.push_back( std::move( _listOperation.front()._reply ) );
            _listOperation.pop_front();
            ++replyCount;
        }
        return replyCount;
    }

    void RespEphemeralStore::subscribe( string_view channel )
    {
        if ( _bInitialized == SW_FALSE || _bShutdown == SW_TRUE || EphemeralRequest::isValidKey( channel ) == false )
            return;
        if ( std::find( _listChannel.begin(), _listChannel.end(), channel ) != _listChannel.end() )
            return;
        _listChannel.emplace_back( channel );
        uint64 targetAckCount = 0;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _subscribeConnection.getState() == RespConnectionState::Closed )
            {
                (void)ensureSubscribeConnection(); // 열리면 모든 채널을 다시 구독한다(이 채널 포함)
            }
            else
            {
                RespCommand command{ "SUBSCRIBE" };
                command.addText( makeKey( channel ) );
                if ( _subscribeConnection.sendCommand( *_transport, command, RespCommandTag{} ) )
                    ++_subscriptionSentCount;
            }
            targetAckCount = _subscriptionSentCount;
        }
        waitForSubscriptionAck( targetAckCount );
    }

    void RespEphemeralStore::unsubscribe( string_view channel )
    {
        const auto channelIt = std::find( _listChannel.begin(), _listChannel.end(), channel );
        if ( channelIt == _listChannel.end() )
            return;
        _listChannel.erase( channelIt );
        uint64 targetAckCount = 0;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _subscribeConnection.getState() == RespConnectionState::Closed )
                return;
            RespCommand command{ "UNSUBSCRIBE" };
            command.addText( makeKey( channel ) );
            if ( _subscribeConnection.sendCommand( *_transport, command, RespCommandTag{} ) )
                ++_subscriptionSentCount;
            targetAckCount = _subscriptionSentCount;
        }
        waitForSubscriptionAck( targetAckCount );
    }

    int32 RespEphemeralStore::pollMessages( vector<EphemeralMessage>& outListMessage )
    {
        if ( _bInitialized == SW_TRUE && _bShutdown == SW_FALSE )
        {
            pumpTransport();
            vector<RespValue> listValue;
            {
                std::scoped_lock<mutex> lock{ _mutex };
                if ( _listChannel.empty() == false )
                    (void)ensureSubscribeConnection();
                _subscribeConnection.takePushValues( listValue );
            }
            for ( const RespValue& value : listValue )
            {
                handlePushValue( value );
            }
        }
        const int32 messageCount = static_cast<int32>( _listMessage.size() );
        for ( EphemeralMessage& message : _listMessage )
        {
            outListMessage.push_back( std::move( message ) );
        }
        _listMessage.clear();
        return messageCount;
    }

    void RespEphemeralStore::shutdown()
    {
        if ( _bInitialized == SW_FALSE || _bShutdown == SW_TRUE )
            return;
        _bShutdown = SW_TRUE;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _commandConnection.abort( *_transport, "store shut down" );
            _subscribeConnection.abort( *_transport, "store shut down" );
        }
        for ( Operation& operation : _listOperation )
        {
            if ( operation._bDone == SW_FALSE )
                failOperation( operation );
        }
        _transport->shutdown(); // 콜백이 우리 잠금을 잡으므로 잠금 밖에서
        _listChannel.clear();
        _listMessage.clear();
        _blockingRequestId = 0;
    }

    void RespEphemeralStore::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        (void)bAccepted;
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _commandConnection.handleOpened( *_transport, handle ) )
        {
            _commandBackoffMs = 0;
            return;
        }
        if ( _subscribeConnection.handleOpened( *_transport, handle ) )
            _subscribeBackoffMs = 0;
    }

    void RespEphemeralStore::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _commandConnection.handleReceived( *_transport, handle, pData, size ) )
            return;
        (void)_subscribeConnection.handleReceived( *_transport, handle, pData, size );
    }

    void RespEphemeralStore::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const int64             nowNanoseconds = MonotonicClock::nowNanoseconds();
        if ( _commandConnection.handleClosed( handle, reason ) )
        {
            _nextCommandConnectNanoseconds = nowNanoseconds + _commandBackoffMs * RespEphemeralStoreInternal::kNanosecondsPerMillisecond;
            _commandBackoffMs              = RespEphemeralStoreInternal::makeNextBackoffMs( _commandBackoffMs, _settings._maxBackoffMs );
            return;
        }
        if ( _subscribeConnection.handleClosed( handle, reason ) )
        {
            _subscriptionAckCount            = _subscriptionSentCount; // 오지 않을 확인을 기다리지 않는다
            _nextSubscribeConnectNanoseconds = nowNanoseconds + _subscribeBackoffMs * RespEphemeralStoreInternal::kNanosecondsPerMillisecond;
            _subscribeBackoffMs              = RespEphemeralStoreInternal::makeNextBackoffMs( _subscribeBackoffMs, _settings._maxBackoffMs );
        }
    }

    void RespEphemeralStore::pumpTransport()
    {
        if ( _transportSettings._ioThreadCount == 0 )
            (void)_transport->pollIO( 0 );
    }

    RespEphemeralStore::Operation* RespEphemeralStore::findOperation( uint64 requestId )
    {
        if ( _listOperation.empty() )
            return nullptr;
        const uint64 firstRequestId = _listOperation.front()._reply._requestId;
        if ( requestId < firstRequestId || requestId - firstRequestId >= _listOperation.size() )
            return nullptr;
        return &_listOperation[static_cast<size_t>( requestId - firstRequestId )];
    }

    void RespEphemeralStore::sendDeferredOperations()
    {
        for ( Operation& operation : _listOperation )
        {
            if ( _blockingRequestId != 0 )
                return;
            if ( operation._bDone == SW_TRUE || operation._bSent == SW_TRUE )
                continue;
            sendOperation( operation );
        }
    }

    void RespEphemeralStore::sendOperation( Operation& operation )
    {
        operation._bSent             = SW_TRUE;
        operation._sentAtNanoseconds = MonotonicClock::nowNanoseconds();
        bool bConnected              = false;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            bConnected = ensureCommandConnection();
        }
        if ( bConnected == false )
        {
            finishOperation( operation, EphemeralResult::Unavailable ); // 물러남 동안 — 기다리게 하지 않는다
            return;
        }
        const EphemeralRequest& request = operation._request;
        const string            key     = makeKey( request._key );
        switch ( request._operation )
        {
            case EphemeralOperation::Get:
            {
                sendCommand( operation, RespCommand{ "GET" }.addText( key ), RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::Set:
            {
                RespCommand command{ "SET" };
                command.addText( key ).addBytes( request._value );
                if ( request._ttlMs > 0 )
                    command.addText( "PX" ).addInteger( request._ttlMs );
                if ( request._condition == EphemeralCondition::IfAbsent )
                    command.addText( "NX" );
                else if ( request._condition == EphemeralCondition::IfPresent )
                    command.addText( "XX" );
                sendCommand( operation, command, RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::Erase:
            {
                sendCommand( operation, RespCommand{ "DEL" }.addText( key ), RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::CompareAndSet:
            case EphemeralOperation::CompareAndErase:
            {
                // WATCH 는 이 연결의 상태다 — GET 답을 볼 때까지 뒤 요청을 내보내지 않는다.
                _blockingRequestId = operation._reply._requestId;
                sendCommand( operation, RespCommand{ "WATCH" }.addText( key ), RespEphemeralStoreInternal::kStepFirst );
                sendCommand( operation, RespCommand{ "GET" }.addText( key ), RespEphemeralStoreInternal::kStepSecond );
                break;
            }
            case EphemeralOperation::Increment:
            {
                if ( request._ttlMs <= 0 )
                {
                    sendCommand( operation, RespCommand{ "INCRBY" }.addText( key ).addInteger( request._delta ), RespEphemeralStoreInternal::kStepFirst );
                    break;
                }
                // 새로 생긴 키에만 만료를 건다 — 없을 때만 0 을 만료와 함께 두고 더한다(한 트랜잭션, Lua 없이).
                sendCommand( operation, RespCommand{ "MULTI" }, RespEphemeralStoreInternal::kStepFirst );
                sendCommand( operation, RespCommand{ "SET" }.addText( key ).addText( "0" ).addText( "PX" ).addInteger( request._ttlMs ).addText( "NX" ),
                             RespEphemeralStoreInternal::kStepSecond );
                sendCommand( operation, RespCommand{ "INCRBY" }.addText( key ).addInteger( request._delta ), RespEphemeralStoreInternal::kStepThird );
                sendCommand( operation, RespCommand{ "EXEC" }, RespEphemeralStoreInternal::kStepFourth );
                break;
            }
            case EphemeralOperation::Expire:
            {
                if ( request._ttlMs <= 0 )
                    sendCommand( operation, RespCommand{ "DEL" }.addText( key ), RespEphemeralStoreInternal::kStepFirst );
                else
                    sendCommand( operation, RespCommand{ "PEXPIRE" }.addText( key ).addInteger( request._ttlMs ), RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::ScoreSet:
            {
                sendCommand( operation, RespCommand{ "ZADD" }.addText( key ).addInteger( request._score ).addText( request._member ),
                             RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::ScoreAdd:
            {
                sendCommand( operation, RespCommand{ "ZINCRBY" }.addText( key ).addInteger( request._score ).addText( request._member ),
                             RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::ScoreRemove:
            {
                sendCommand( operation, RespCommand{ "ZREM" }.addText( key ).addText( request._member ), RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::ScoreRank:
            {
                sendCommand( operation, RespCommand{ "ZREVRANK" }.addText( key ).addText( request._member ), RespEphemeralStoreInternal::kStepFirst );
                sendCommand( operation, RespCommand{ "ZSCORE" }.addText( key ).addText( request._member ), RespEphemeralStoreInternal::kStepSecond );
                break;
            }
            case EphemeralOperation::ScoreRange:
            {
                const int64 stopIndex = static_cast<int64>( request._offset ) + request._count - 1;
                sendCommand( operation,
                             RespCommand{ "ZREVRANGE" }.addText( key ).addInteger( request._offset ).addInteger( stopIndex ).addText( "WITHSCORES" ),
                             RespEphemeralStoreInternal::kStepFirst );
                break;
            }
            case EphemeralOperation::Publish:
            {
                sendCommand( operation, RespCommand{ "PUBLISH" }.addText( key ).addBytes( request._value ), RespEphemeralStoreInternal::kStepFirst );
                break;
            }
        }
    }

    void RespEphemeralStore::sendCommand( Operation& operation, const RespCommand& command, uint32 step )
    {
        if ( operation._bDone == SW_TRUE )
            return;
        bool bSent = false;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            bSent = _commandConnection.sendCommand( *_transport, command, RespCommandTag{ operation._reply._requestId, step } );
        }
        if ( bSent )
            ++operation._awaitingReplyCount;
        else
            failOperation( operation );
    }

    void RespEphemeralStore::handleReply( Operation& operation, uint32 step, const RespValue& value )
    {
        --operation._awaitingReplyCount;
        EphemeralReply& reply = operation._reply;
        switch ( operation._request._operation )
        {
            case EphemeralOperation::Get:
            {
                if ( value.isNull() || RespEphemeralStoreInternal::isWrongType( value ) )
                    reply._result = EphemeralResult::NotFound; // 정렬 집합 키는 값으로 없다(메모리 구현과 같다)
                else if ( value._type != RespType::BulkString )
                    reply._result = EphemeralResult::Invalid;
                else
                    reply._value = value._bytes;
                break;
            }
            case EphemeralOperation::Set:
            {
                if ( value.isNull() )
                    reply._result = EphemeralResult::Conflict;
                else if ( value.isText( "OK" ) == false )
                    reply._result = EphemeralResult::Invalid;
                break;
            }
            case EphemeralOperation::Erase:
            case EphemeralOperation::Expire:
            case EphemeralOperation::ScoreRemove:
            {
                if ( value._type != RespType::Integer )
                    reply._result = EphemeralResult::Invalid;
                else if ( value._integer == 0 )
                    reply._result = EphemeralResult::NotFound;
                break;
            }
            case EphemeralOperation::CompareAndSet:
            case EphemeralOperation::CompareAndErase:
            {
                handleCompareReply( operation, step, value );
                break;
            }
            case EphemeralOperation::Increment:
            {
                const bool bSingleCommand = operation._request._ttlMs <= 0;
                if ( bSingleCommand )
                {
                    if ( value._type == RespType::Integer )
                        reply._integer = value._integer;
                    else
                        reply._result = EphemeralResult::Invalid; // 정수가 아닌 값 · 넘침
                    break;
                }
                if ( step != RespEphemeralStoreInternal::kStepFourth )
                    break; // MULTI · QUEUED
                const bool bExecuted = value._type == RespType::Array && value._listElement.size() == 2;
                if ( bExecuted == false || value._listElement[1]._type != RespType::Integer )
                {
                    reply._result = EphemeralResult::Invalid;
                    break;
                }
                reply._integer = value._listElement[1]._integer;
                break;
            }
            case EphemeralOperation::ScoreSet:
            {
                if ( value._type != RespType::Integer )
                    reply._result = EphemeralResult::Invalid; // WRONGTYPE
                break;
            }
            case EphemeralOperation::ScoreAdd:
            {
                int64 score = 0;
                if ( value.tryReadIntegerText( score ) == false )
                {
                    reply._result = EphemeralResult::Invalid;
                    break;
                }
                reply._integer = score;
                reply._score   = score;
                break;
            }
            case EphemeralOperation::ScoreRank:
            {
                if ( reply._result != EphemeralResult::Ok )
                    break;
                if ( value.isNull() )
                {
                    reply._result = EphemeralResult::NotFound;
                    break;
                }
                if ( step == RespEphemeralStoreInternal::kStepFirst )
                {
                    if ( value._type == RespType::Integer )
                        reply._integer = value._integer;
                    else
                        reply._result = EphemeralResult::Invalid; // WRONGTYPE
                    break;
                }
                int64 score = 0;
                if ( value.tryReadIntegerText( score ) )
                    reply._score = score;
                else
                    reply._result = EphemeralResult::Invalid;
                break;
            }
            case EphemeralOperation::ScoreRange:
            {
                if ( value._type != RespType::Array || value._listElement.size() % 2 != 0 )
                {
                    reply._result = EphemeralResult::Invalid;
                    break;
                }
                for ( size_t index = 0; index < value._listElement.size(); index += 2 )
                {
                    EphemeralScoredMember& member = reply._listMember.emplace_back();
                    member._member                = string( value._listElement[index].getText() );
                    if ( value._listElement[index + 1].tryReadIntegerText( member._score ) == false )
                        reply._result = EphemeralResult::Invalid;
                }
                break;
            }
            case EphemeralOperation::Publish:
            {
                if ( value._type == RespType::Integer )
                    reply._integer = value._integer;
                else
                    reply._result = EphemeralResult::Invalid;
                break;
            }
        }
        if ( operation._awaitingReplyCount == 0 && operation._bDone == SW_FALSE )
            operation._bDone = SW_TRUE;
    }

    void RespEphemeralStore::handleCompareReply( Operation& operation, uint32 step, const RespValue& value )
    {
        EphemeralReply& reply = operation._reply;
        if ( step == RespEphemeralStoreInternal::kStepSecond ) // GET — 다음 명령을 정한다
        {
            const bool bMatches = value._type == RespType::BulkString && value._bytes == operation._request._expected;
            if ( bMatches == false )
            {
                reply._result = EphemeralResult::Conflict;
                sendCommand( operation, RespCommand{ "UNWATCH" }, RespEphemeralStoreInternal::kStepCompareUnwatch );
            }
            else
            {
                const string key = makeKey( operation._request._key );
                sendCommand( operation, RespCommand{ "MULTI" }, RespEphemeralStoreInternal::kStepCompareMulti );
                if ( operation._request._operation == EphemeralOperation::CompareAndSet )
                {
                    RespCommand command{ "SET" };
                    command.addText( key ).addBytes( operation._request._value );
                    if ( operation._request._ttlMs > 0 )
                        command.addText( "PX" ).addInteger( operation._request._ttlMs );
                    sendCommand( operation, command, RespEphemeralStoreInternal::kStepCompareWrite );
                }
                else
                {
                    sendCommand( operation, RespCommand{ "DEL" }.addText( key ), RespEphemeralStoreInternal::kStepCompareWrite );
                }
                sendCommand( operation, RespCommand{ "EXEC" }, RespEphemeralStoreInternal::kStepCompareExec );
            }
            // 이 요청의 명령은 모두 선에 올랐다 — 뒤 요청이 이어 나가도 WATCH 사이에 끼지 않는다.
            if ( _blockingRequestId == operation._reply._requestId )
            {
                _blockingRequestId = 0;
                sendDeferredOperations();
            }
            return;
        }
        if ( step == RespEphemeralStoreInternal::kStepCompareExec && value.isNull() )
            reply._result = EphemeralResult::Conflict; // WATCH 뒤에 누가 바꿨다
        else if ( step == RespEphemeralStoreInternal::kStepCompareExec && value._type != RespType::Array )
            reply._result = EphemeralResult::Invalid;
    }

    void RespEphemeralStore::finishOperation( Operation& operation, EphemeralResult result )
    {
        operation._reply._result      = result;
        operation._awaitingReplyCount = 0;
        operation._bDone              = SW_TRUE;
    }

    void RespEphemeralStore::failOperation( Operation& operation )
    {
        operation._reply._value.clear();
        operation._reply._listMember.clear();
        finishOperation( operation, EphemeralResult::Unavailable );
        if ( _blockingRequestId == operation._reply._requestId )
        {
            _blockingRequestId = 0;
            if ( _bShutdown == SW_FALSE )
                sendDeferredOperations();
        }
    }

    bool RespEphemeralStore::ensureCommandConnection()
    {
        if ( _commandConnection.getState() != RespConnectionState::Closed )
            return true;
        if ( MonotonicClock::nowNanoseconds() < _nextCommandConnectNanoseconds )
            return false;
        const RespCommand authCommand = makeAuthCommand();
        const bool        bAuth       = _settings._password.empty() == false;
        if ( _commandConnection.beginConnect( *_transport, _settings._address, _tlsContext.get(), bAuth ? &authCommand : nullptr ) )
            return true;
        _nextCommandConnectNanoseconds = MonotonicClock::nowNanoseconds() + _commandBackoffMs * RespEphemeralStoreInternal::kNanosecondsPerMillisecond;
        _commandBackoffMs              = RespEphemeralStoreInternal::makeNextBackoffMs( _commandBackoffMs, _settings._maxBackoffMs );
        return false;
    }

    bool RespEphemeralStore::ensureSubscribeConnection()
    {
        if ( _subscribeConnection.getState() != RespConnectionState::Closed )
            return true;
        if ( MonotonicClock::nowNanoseconds() < _nextSubscribeConnectNanoseconds )
            return false;
        const RespCommand authCommand = makeAuthCommand();
        const bool        bAuth       = _settings._password.empty() == false;
        if ( _subscribeConnection.beginConnect( *_transport, _settings._address, _tlsContext.get(), bAuth ? &authCommand : nullptr ) == false )
        {
            _nextSubscribeConnectNanoseconds = MonotonicClock::nowNanoseconds() + _subscribeBackoffMs * RespEphemeralStoreInternal::kNanosecondsPerMillisecond;
            _subscribeBackoffMs              = RespEphemeralStoreInternal::makeNextBackoffMs( _subscribeBackoffMs, _settings._maxBackoffMs );
            return false;
        }
        // 새 연결은 구독이 없다 — 채널을 모두 다시 구독한다(끊긴 동안의 메시지는 없다 — 최대 한 번 배달).
        for ( const string& channel : _listChannel )
        {
            RespCommand command{ "SUBSCRIBE" };
            command.addText( makeKey( channel ) );
            if ( _subscribeConnection.sendCommand( *_transport, command, RespCommandTag{} ) )
                ++_subscriptionSentCount;
        }
        return true;
    }

    void RespEphemeralStore::abortTimedOutRequests()
    {
        const int64 timeoutNanoseconds = _settings._timeoutMs * RespEphemeralStoreInternal::kNanosecondsPerMillisecond;
        const int64 nowNanoseconds     = MonotonicClock::nowNanoseconds();
        bool        bExpired           = false;
        for ( const Operation& operation : _listOperation )
        {
            if ( operation._bDone == SW_TRUE || operation._bSent == SW_FALSE )
                continue;
            bExpired = nowNanoseconds - operation._sentAtNanoseconds > timeoutNanoseconds;
            break; // 답은 보낸 순서대로 온다 — 가장 오래된 것만 보면 된다
        }
        if ( bExpired == false )
            return;
        vector<RespReplyRecord> listRecord;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _commandConnection.abort( *_transport, "request timed out" );
            _commandConnection.takeReplyRecords( listRecord );
            // 시한은 서버 쪽 문제라 바로 다시 걸지 않는다.
            _nextCommandConnectNanoseconds = nowNanoseconds + _commandBackoffMs * RespEphemeralStoreInternal::kNanosecondsPerMillisecond;
            _commandBackoffMs              = RespEphemeralStoreInternal::makeNextBackoffMs( _commandBackoffMs, _settings._maxBackoffMs );
        }
        for ( const RespReplyRecord& record : listRecord )
        {
            Operation* pOperation = findOperation( record._tag._requestId );
            if ( pOperation != nullptr && pOperation->_bDone == SW_FALSE )
                failOperation( *pOperation );
        }
    }

    void RespEphemeralStore::handlePushValue( const RespValue& value )
    {
        if ( value._type != RespType::Array || value._listElement.size() < 3 )
            return;
        const RespValue& kind = value._listElement[0];
        if ( kind.isText( "subscribe" ) || kind.isText( "unsubscribe" ) )
        {
            ++_subscriptionAckCount;
            return;
        }
        if ( kind.isText( "message" ) == false )
            return;
        const string_view channel = value._listElement[1].getText();
        if ( channel.substr( 0, _settings._keyPrefix.size() ) != _settings._keyPrefix )
            return;
        EphemeralMessage& message = _listMessage.emplace_back();
        message._channel          = string( channel.substr( _settings._keyPrefix.size() ) );
        message._bytes            = value._listElement[2]._bytes;
    }

    void RespEphemeralStore::waitForSubscriptionAck( uint64 targetAckCount )
    {
        const Deadline    deadline = Deadline::afterMilliseconds( _settings._timeoutMs );
        vector<RespValue> listValue;
        while ( _subscriptionAckCount < targetAckCount && deadline.isExpired() == false )
        {
            pumpTransport();
            listValue.clear();
            {
                std::scoped_lock<mutex> lock{ _mutex };
                _subscribeConnection.takePushValues( listValue );
                if ( _subscribeConnection.getState() == RespConnectionState::Closed )
                    _subscriptionAckCount = std::max( _subscriptionAckCount, _subscriptionSentCount );
            }
            for ( const RespValue& value : listValue )
            {
                handlePushValue( value );
            }
            if ( _subscriptionAckCount < targetAckCount )
                RespEphemeralStoreInternal::sleepOneMillisecond();
        }
        if ( _subscriptionAckCount < targetAckCount )
            SW_LOG_WARNING( "RESP subscription was not confirmed within %# ms", _settings._timeoutMs );
    }

    string RespEphemeralStore::makeKey( string_view key ) const
    {
        string prefixed = _settings._keyPrefix;
        prefixed.append( key.data(), key.size() );
        return prefixed;
    }

    RespCommand RespEphemeralStore::makeAuthCommand() const
    {
        RespCommand command{ "AUTH" };
        if ( _settings._userName.empty() == false )
            command.addText( _settings._userName );
        command.addText( _settings._password );
        return command;
    }
} // namespace sw
