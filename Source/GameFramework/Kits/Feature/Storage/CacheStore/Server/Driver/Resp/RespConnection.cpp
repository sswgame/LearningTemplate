#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespConnection.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    RespConnection::RespConnection( bool bPushMode )
        : _parser{}
        , _listAwaitTag{}
        , _listReplyRecord{}
        , _listPushValue{}
        , _pendingSendBytes{}
        , _plainBytes{}
        , _cipherBytes{}
        , _tlsSession{}
        , _handle{}
        , _state{ RespConnectionState::Closed }
        , _bPushMode{ static_cast<uint8>( bPushMode ? SW_TRUE : SW_FALSE ) }
        , _bAuthRejected{ SW_FALSE }
    {
    }

    RespConnection::~RespConnection() = default;

    bool RespConnection::beginConnect( IStreamTransport& transport, const NetAddress& address, ITLSContext* pTLSContext, const RespCommand* pAuthCommand )
    {
        _parser.reset();
        _listAwaitTag.clear();
        _pendingSendBytes.clear();
        _bAuthRejected = SW_FALSE;
        _tlsSession.reset();
        // 세션은 연결을 걸기 전에 — 열림 콜백이 다른 스레드에서 먼저 와도 평문이 선에 나가지 않게.
        if ( pTLSContext != nullptr )
        {
            _tlsSession = pTLSContext->createSession();
            if ( _tlsSession == nullptr )
                return false;
        }
        if ( pAuthCommand != nullptr )
        {
            pAuthCommand->appendTo( _pendingSendBytes );
            _listAwaitTag.push_back( RespCommandTag{ 0, kAuthStep } );
        }
        _handle = transport.connect( address );
        if ( _handle.isValid() == false )
        {
            _listAwaitTag.clear();
            _pendingSendBytes.clear();
            _state = RespConnectionState::Closed;
            return false;
        }
        _state = RespConnectionState::Connecting;
        return true;
    }

    bool RespConnection::sendCommand( IStreamTransport& transport, const RespCommand& command, const RespCommandTag& tag )
    {
        if ( _state == RespConnectionState::Closed )
            return false;
        if ( _bPushMode == SW_FALSE )
            _listAwaitTag.push_back( tag );
        if ( _state == RespConnectionState::Connecting )
        {
            command.appendTo( _pendingSendBytes );
            return true;
        }
        _plainBytes.clear();
        command.appendTo( _plainBytes );
        if ( writePlainLocked( transport, _plainBytes.data(), _plainBytes.size() ) == false )
        {
            abort( transport, "send failed" );
            return false;
        }
        return true;
    }

    void RespConnection::abort( IStreamTransport& transport, const utf8* pReason )
    {
        if ( _state == RespConnectionState::Closed )
            return;
        SW_LOG_WARNING( "RESP connection %# aborted: %#", static_cast<uint32>( _handle._index ), pReason );
        const StreamConnectionHandle handle = _handle;
        _handle                             = StreamConnectionHandle{};
        _state                              = RespConnectionState::Closed;
        failAwaitingTags();
        transport.close( handle, StreamCloseMode::Abort );
    }

    bool RespConnection::handleOpened( IStreamTransport& transport, StreamConnectionHandle handle )
    {
        if ( handle != _handle || _state != RespConnectionState::Connecting )
            return false;
        _state = RespConnectionState::Open;
        // TLS 세션은 핸드셰이크 전 평문을 모았다가 끝나면 보낸다 — 클라이언트 Hello 와 함께 지금 넘긴다.
        const vector<uint8> pendingBytes = std::move( _pendingSendBytes );
        _pendingSendBytes.clear();
        const bool bWritten = pendingBytes.empty() ? ( _tlsSession == nullptr || flushCiphertext( transport ) )
                                                   : writePlainLocked( transport, pendingBytes.data(), pendingBytes.size() );
        if ( bWritten == false )
            abort( transport, "could not write the queued commands" );
        return true;
    }

    bool RespConnection::handleReceived( IStreamTransport& transport, StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        if ( handle != _handle || _state != RespConnectionState::Open )
            return handle == _handle;
        if ( _tlsSession == nullptr )
        {
            _parser.append( pData, static_cast<size_t>( size ) );
        }
        else
        {
            const bool bFed = _tlsSession->feedCiphertext( pData, size );
            (void)flushCiphertext( transport ); // 핸드셰이크 답 · 세션 표
            _plainBytes.clear();
            const bool bRead = bFed && _tlsSession->getState() != TLSSessionState::Failed && _tlsSession->readPlaintext( _plainBytes );
            if ( bRead == false )
            {
                abort( transport, _tlsSession->getFailureText() );
                return true;
            }
            (void)flushCiphertext( transport );
            _parser.append( _plainBytes.data(), _plainBytes.size() );
        }
        if ( drainParsedValues() == false )
            abort( transport, _parser.hasFailed() ? _parser.getFailureText() : "unexpected reply" );
        else if ( _bAuthRejected == SW_TRUE )
            abort( transport, "AUTH was rejected" );
        return true;
    }

    bool RespConnection::handleClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        if ( handle != _handle )
            return false;
        if ( _state != RespConnectionState::Closed )
            SW_LOG_WARNING( "RESP connection %# closed: %#", static_cast<uint32>( handle._index ), toString( reason ) );
        _handle = StreamConnectionHandle{};
        _state  = RespConnectionState::Closed;
        failAwaitingTags();
        return true;
    }

    void RespConnection::takeReplyRecords( vector<RespReplyRecord>& outListRecord )
    {
        for ( RespReplyRecord& record : _listReplyRecord )
        {
            outListRecord.push_back( std::move( record ) );
        }
        _listReplyRecord.clear();
    }

    void RespConnection::takePushValues( vector<RespValue>& outListValue )
    {
        for ( RespValue& value : _listPushValue )
        {
            outListValue.push_back( std::move( value ) );
        }
        _listPushValue.clear();
    }

    bool RespConnection::writePlainLocked( IStreamTransport& transport, const uint8* pData, size_t size )
    {
        if ( _tlsSession == nullptr )
        {
            const StreamSendResult result = transport.send( _handle, pData, static_cast<int32>( size ) );
            return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
        }
        if ( _tlsSession->writePlaintext( pData, static_cast<int32>( size ) ) == false )
            return false;
        return flushCiphertext( transport );
    }

    bool RespConnection::flushCiphertext( IStreamTransport& transport )
    {
        _cipherBytes.clear();
        _tlsSession->takeCiphertext( _cipherBytes );
        if ( _cipherBytes.empty() )
            return true;
        const StreamSendResult result = transport.send( _handle, _cipherBytes.data(), static_cast<int32>( _cipherBytes.size() ) );
        return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
    }

    bool RespConnection::drainParsedValues()
    {
        RespValue value;
        for ( ;; )
        {
            const RespParseResult result = _parser.next( value );
            if ( result == RespParseResult::NeedMore )
                return true;
            if ( result == RespParseResult::Error )
                return false;
            // AUTH 답은 두 모드 모두 첫 꼬리표다(구독 연결도 AUTH 는 꼬리표로 짝짓는다).
            const bool bAuthReply = _listAwaitTag.empty() == false && _listAwaitTag.front()._step == kAuthStep;
            if ( bAuthReply )
            {
                _listAwaitTag.pop_front();
                if ( value.isError() )
                {
                    SW_LOG_ERROR( "RESP AUTH rejected: %#", string( value.getText() ).c_str() );
                    _bAuthRejected = SW_TRUE;
                    return true;
                }
                continue;
            }
            if ( _bPushMode == SW_TRUE )
            {
                _listPushValue.push_back( std::move( value ) );
                continue;
            }
            if ( _listAwaitTag.empty() )
                return false; // 보낸 적 없는 답 — 순서가 어긋났다
            RespReplyRecord& record = _listReplyRecord.emplace_back();
            record._tag             = _listAwaitTag.front();
            record._value           = std::move( value );
            _listAwaitTag.pop_front();
        }
    }

    void RespConnection::failAwaitingTags()
    {
        for ( const RespCommandTag& tag : _listAwaitTag )
        {
            if ( tag._step == kAuthStep )
                continue;
            RespReplyRecord& record = _listReplyRecord.emplace_back();
            record._tag             = tag;
            record._bFailed         = SW_TRUE;
        }
        _listAwaitTag.clear();
        _pendingSendBytes.clear();
        _tlsSession.reset();
    }
} // namespace sw
