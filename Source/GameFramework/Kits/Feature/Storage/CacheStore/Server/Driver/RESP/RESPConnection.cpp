#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/RESP/RESPConnection.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    RESPConnection::RESPConnection( bool bPushMode )
        : _parser{}
        , _listAwaitTag{}
        , _listReplyRecord{}
        , _listPushValue{}
        , _pendingSendBytes{}
        , _plainBytes{}
        , _cipherBytes{}
        , _tlsSession{}
        , _handle{}
        , _state{ RESPConnectionState::Closed }
        , _bPushMode{ static_cast<uint8>( bPushMode ? SW_TRUE : SW_FALSE ) }
        , _bAuthRejected{ SW_FALSE }
    {
    }

    RESPConnection::~RESPConnection() = default;

    bool RESPConnection::beginConnect( IStreamTransport& transport, const NetAddress& address, ITLSContext* pTLSContext, const RESPCommand* pAuthCommand )
    {
        _parser.reset();
        _listAwaitTag.clear();
        _pendingSendBytes.clear();
        _bAuthRejected = SW_FALSE;
        _tlsSession.reset();
        // TLS 세션은 연결을 시작하기 전에 만든다. 열림 콜백이 다른 스레드에서 먼저 와도 평문이 그대로 전송되지 않게 하려는 것이다.
        if ( pTLSContext != nullptr )
        {
            _tlsSession = pTLSContext->createSession();
            if ( _tlsSession == nullptr )
                return false;
        }
        if ( pAuthCommand != nullptr )
        {
            pAuthCommand->appendTo( _pendingSendBytes );
            _listAwaitTag.push_back( RESPCommandTag{ 0, kAuthStep } );
        }
        _handle = transport.connect( address );
        if ( _handle.isValid() == false )
        {
            _listAwaitTag.clear();
            _pendingSendBytes.clear();
            _state = RESPConnectionState::Closed;
            return false;
        }
        _state = RESPConnectionState::Connecting;
        return true;
    }

    bool RESPConnection::sendCommand( IStreamTransport& transport, const RESPCommand& command, const RESPCommandTag& tag )
    {
        if ( _state == RESPConnectionState::Closed )
            return false;
        if ( _bPushMode == SW_FALSE )
            _listAwaitTag.push_back( tag );
        if ( _state == RESPConnectionState::Connecting )
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

    void RESPConnection::abort( IStreamTransport& transport, const utf8* pReason )
    {
        if ( _state == RESPConnectionState::Closed )
            return;
        SW_LOG_WARNING_ID( "RespConnection.RESPConnectionAborted", "RESP connection %# aborted: %#", static_cast<uint32>( _handle.index() ), pReason );
        closeForShutdown( transport );
    }

    void RESPConnection::closeForShutdown( IStreamTransport& transport )
    {
        if ( _state == RESPConnectionState::Closed )
            return;
        const StreamConnectionHandle handle = _handle;
        _handle                             = StreamConnectionHandle{};
        _state                              = RESPConnectionState::Closed;
        failAwaitingTags();
        transport.close( handle, StreamCloseMode::Abort );
    }

    bool RESPConnection::handleOpened( IStreamTransport& transport, StreamConnectionHandle handle )
    {
        if ( handle != _handle || _state != RESPConnectionState::Connecting )
            return false;
        _state = RESPConnectionState::Open;
        // TLS 세션은 핸드셰이크 전의 평문을 모았다가 핸드셰이크가 끝나면 보낸다. 클라이언트 Hello 와 함께 지금 넘긴다.
        const vector<uint8> pendingBytes = std::move( _pendingSendBytes );
        _pendingSendBytes.clear();
        const bool bWritten = pendingBytes.empty() ? ( _tlsSession == nullptr || flushCiphertext( transport ) )
                                                   : writePlainLocked( transport, pendingBytes.data(), pendingBytes.size() );
        if ( bWritten == false )
            abort( transport, "could not write the queued commands" );
        return true;
    }

    bool RESPConnection::handleReceived( IStreamTransport& transport, StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        if ( handle != _handle || _state != RESPConnectionState::Open )
            return handle == _handle;
        if ( _tlsSession == nullptr )
        {
            _parser.append( pData, static_cast<size_t>( size ) );
        }
        else
        {
            const bool bFed = _tlsSession->feedCiphertext( pData, size );
            (void)flushCiphertext( transport ); // 핸드셰이크 답과 세션 티켓을 보낸다
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
        if ( pollParsedValues() == false )
            abort( transport, _parser.hasFailed() ? _parser.getFailureText() : "unexpected reply" );
        else if ( _bAuthRejected == SW_TRUE )
            abort( transport, "AUTH was rejected" );
        return true;
    }

    bool RESPConnection::handleClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        if ( handle != _handle )
            return false;
        if ( _state != RESPConnectionState::Closed )
            SW_LOG_WARNING_ID( "RespConnection.RESPConnectionClosed", "RESP connection %# closed: %#", static_cast<uint32>( handle.index() ), toString( reason ) );
        _handle = StreamConnectionHandle{};
        _state  = RESPConnectionState::Closed;
        failAwaitingTags();
        return true;
    }

    void RESPConnection::takeReplyRecords( vector<RESPReplyRecord>& outListRecord )
    {
        for ( RESPReplyRecord& record : _listReplyRecord )
        {
            outListRecord.push_back( std::move( record ) );
        }
        _listReplyRecord.clear();
    }

    void RESPConnection::takePushValues( vector<RESPValue>& outListValue )
    {
        for ( RESPValue& value : _listPushValue )
        {
            outListValue.push_back( std::move( value ) );
        }
        _listPushValue.clear();
    }

    bool RESPConnection::writePlainLocked( IStreamTransport& transport, const uint8* pData, size_t size )
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

    bool RESPConnection::flushCiphertext( IStreamTransport& transport )
    {
        _cipherBytes.clear();
        _tlsSession->takeCiphertext( _cipherBytes );
        if ( _cipherBytes.empty() )
            return true;
        const StreamSendResult result = transport.send( _handle, _cipherBytes.data(), static_cast<int32>( _cipherBytes.size() ) );
        return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
    }

    bool RESPConnection::pollParsedValues()
    {
        RESPValue value;
        for ( ;; )
        {
            const RESPParseResult result = _parser.next( value );
            if ( result == RESPParseResult::NeedMore )
                return true;
            if ( result == RESPParseResult::Error )
                return false;
            // AUTH 답은 두 모드 모두 첫 태그이다. 구독 연결도 AUTH 는 태그로 짝짓는다.
            const bool bAuthReply = _listAwaitTag.empty() == false && _listAwaitTag.front()._step == kAuthStep;
            if ( bAuthReply )
            {
                _listAwaitTag.pop_front();
                if ( value.isError() )
                {
                    SW_LOG_ERROR_ID( "RespConnection.RESPAUTHRejected", "RESP AUTH rejected: %#", string( value.getText() ).c_str() );
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
                return false; // 보낸 적 없는 답이므로 순서가 어긋난 것이다
            RESPReplyRecord& record = _listReplyRecord.emplace_back();
            record._tag             = _listAwaitTag.front();
            record._value           = std::move( value );
            _listAwaitTag.pop_front();
        }
    }

    void RESPConnection::failAwaitingTags()
    {
        for ( const RESPCommandTag& tag : _listAwaitTag )
        {
            if ( tag._step == kAuthStep )
                continue;
            RESPReplyRecord& record = _listReplyRecord.emplace_back();
            record._tag             = tag;
            record._bFailed         = SW_TRUE;
        }
        _listAwaitTag.clear();
        _pendingSendBytes.clear();
        _tlsSession.reset();
    }
} // namespace sw
