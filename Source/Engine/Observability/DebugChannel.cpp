#include "pch.h"

#include "Engine/Observability/DebugChannel.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Container/StringUtil.h"
    #include "Core/Log/Logger.h"
    #include "Core/Time/MonotonicClock.h"

    #include <charconv>

namespace sw
{
    SW_LOG_CALLER( "DebugChannel" );

    namespace
    {
        struct DebugChannelInternal
        {
            static constexpr int64 kPollSleepNanoseconds = 1 * constant::kNanosecondsPerSecond / constant::kMillisecondsPerSecond;

            /** @brief 길이에 걸리는 시간이 같은 비교입니다(토큰의 앞부분이 맞는 만큼 빨리 거절하지 않는다). */
            static bool equalsConstantTime( string_view left, string_view right )
            {
                uint8        difference = left.size() == right.size() ? 0 : 1;
                const size_t common     = left.size() < right.size() ? left.size() : right.size();
                for ( size_t index = 0; index < common; ++index )
                {
                    difference |= static_cast<uint8>( left[index] ^ right[index] );
                }
                return difference == 0;
            }

            static void appendNumber( string& outText, uint64 value )
            {
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), value );
                outText.append( arrBuffer, result.ptr );
            }

            static int64 nowMilli() { return MonotonicClock::nowNanoseconds() / ( constant::kNanosecondsPerSecond / constant::kMillisecondsPerSecond ); }
        };
    } // namespace

    DebugChannel::DebugChannel()
        : _listConnection{}
        , _listRequest{}
        , _settings{}
        , _execute{}
        , _mutex{}
        , _pTransport{ nullptr }
    {
    }

    DebugChannel::~DebugChannel()
    {
        SW_ASSERT( _pTransport == nullptr );
    }

    bool DebugChannel::initialize( IStreamTransport* pTransport, const StreamTransportConfig& transportSettings, const DebugChannelConfig& settings,
                                   DebugChannelExecuteDelegate execute )
    {
        if ( pTransport == nullptr || execute.isBound() == false )
            return false;
        if ( settings._token.empty() )
        {
            SW_LOG_ERROR( "Debug channel needs a token (-gv_debugChannelToken) - not started" );
            return false;
        }
        if ( settings._bindAddress._ipv4 != NetAddress::makeLoopback( 0 )._ipv4 )
        {
            SW_LOG_ERROR( "Debug channel binds to the loopback address only (got %#) - not started", settings._bindAddress.toString().c_str() );
            return false;
        }
        _settings = settings;
        _execute  = execute;
        if ( pTransport->initialize( this, transportSettings ) == false )
        {
            SW_LOG_ERROR( "Debug channel could not start its stream transport" );
            return false;
        }
        _pTransport = pTransport;
        if ( _pTransport->listen( settings._bindAddress ) == false )
        {
            SW_LOG_ERROR( "Debug channel could not listen on %#", settings._bindAddress.toString().c_str() );
            shutdown();
            return false;
        }
        SW_LOG_INFO( "Debug channel listening on 127.0.0.1:%# (development builds only, token required)", static_cast<uint32>( _pTransport->getListenPort() ) );
        return true;
    }

    void DebugChannel::shutdown()
    {
        if ( _pTransport != nullptr )
            _pTransport->shutdown();
        _pTransport = nullptr;
        ScopedLock<mutex> lock{ _mutex };
        _listConnection.clear();
        _listRequest.clear();
    }

    uint16 DebugChannel::getListenPort() const
    {
        return _pTransport != nullptr ? _pTransport->getListenPort() : 0;
    }

    void DebugChannel::sendReply( uint64 packedHandle, bool bOk, string_view body ) const
    {
        string reply = bOk ? "OK " : "ERR ";
        DebugChannelInternal::appendNumber( reply, body.size() );
        reply += '\n';
        reply += body;
        (void)_pTransport->send( StreamConnectionHandle::fromPacked( packedHandle ), reinterpret_cast<const uint8*>( reply.data() ), static_cast<int32>( reply.size() ) );
    }

    uint32 DebugChannel::pump()
    {
        vector<Request> listRequest;
        {
            ScopedLock<mutex> lock{ _mutex };
            listRequest.swap( _listRequest );
        }
        for ( const Request& request : listRequest )
        {
            string     reply;
            const bool bOk = _execute( request._command, reply );
            sendReply( request._packedHandle, bOk, reply );
        }
        return static_cast<uint32>( listRequest.size() );
    }

    void DebugChannel::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        if ( bAccepted == false )
            return;
        ScopedLock<mutex> lock{ _mutex };
        Connection&       connection = _listConnection.emplace_back();
        connection._packedHandle     = handle.packed();
    }

    void DebugChannel::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        bool bReject  = false; // 토큰이 틀리거나 줄이 너무 길다
        bool bTooBusy = false;
        bool bTooLong = false;
        {
            ScopedLock<mutex> lock{ _mutex };
            Connection*       pConnection = nullptr;
            for ( Connection& connection : _listConnection )
            {
                if ( connection._packedHandle == handle.packed() )
                    pConnection = &connection;
            }
            if ( pConnection == nullptr )
                return;
            pConnection->_buffer.append( reinterpret_cast<const utf8*>( pData ), static_cast<size_t>( size ) );
            while ( bReject == false )
            {
                const size_t lineEnd = pConnection->_buffer.find( '\n' );
                if ( lineEnd == string::npos )
                {
                    bTooLong = static_cast<int32>( pConnection->_buffer.size() ) > _settings._maxLineBytes;
                    bReject  = bTooLong;
                    break;
                }
                const string_view line  = StringUtil::trim( string_view( pConnection->_buffer ).substr( 0, lineEnd ) );
                const size_t      space = line.find( ' ' );
                const string_view token = space == string_view::npos ? line : line.substr( 0, space );
                if ( DebugChannelInternal::equalsConstantTime( token, _settings._token ) == false )
                    bReject = true;
                else if ( static_cast<int32>( _listRequest.size() ) >= _settings._maxQueuedLines )
                    bTooBusy = true;
                else if ( space != string_view::npos )
                {
                    Request& request      = _listRequest.emplace_back();
                    request._packedHandle = handle.packed();
                    request._command      = string( StringUtil::trim( line.substr( space + 1 ) ) );
                }
                pConnection->_buffer.erase( 0, lineEnd + 1 );
                if ( bTooBusy )
                    break;
            }
        }
        if ( bReject )
        {
            // 토큰이 틀린 연결에는 아무것도 실행하지 않고 이유를 한 줄 알린 뒤 닫는다.
            sendReply( handle.packed(), false, bTooLong ? "line too long" : "unauthorized" );
            _pTransport->close( handle, StreamCloseMode::Graceful );
            SW_LOG_WARNING( "Debug channel rejected a connection (%#)", bTooLong ? "line too long" : "wrong token" );
        }
        else if ( bTooBusy )
        {
            sendReply( handle.packed(), false, "busy" );
        }
    }

    void DebugChannel::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        (void)reason;
        ScopedLock<mutex> lock{ _mutex };
        for ( size_t connectionIndex = 0; connectionIndex < _listConnection.size(); ++connectionIndex )
        {
            if ( _listConnection[connectionIndex]._packedHandle == handle.packed() )
            {
                _listConnection.erase( _listConnection.begin() + static_cast<ptrdiff_t>( connectionIndex ) );
                break;
            }
        }
        // 닫힌 연결이 남긴 요청은 답할 곳이 없다.
        for ( size_t requestIndex = _listRequest.size(); requestIndex > 0; --requestIndex )
        {
            if ( _listRequest[requestIndex - 1]._packedHandle == handle.packed() )
                _listRequest.erase( _listRequest.begin() + static_cast<ptrdiff_t>( requestIndex - 1 ) );
        }
    }
} // namespace sw

namespace sw
{
    DebugChannelClient::DebugChannelClient()
        : _transport{}
        , _token{}
        , _received{}
        , _mutex{}
        , _handle{}
        , _bOpen{ SW_FALSE }
        , _bClosed{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    DebugChannelClient::~DebugChannelClient()
    {
        disconnect();
    }

    bool DebugChannelClient::connect( const NetAddress& remote, string_view token, int32 timeoutMilli )
    {
        disconnect();
        _token     = string( token );
        _bOpen     = SW_FALSE;
        _bClosed   = SW_FALSE;
        _transport = StreamTransportFactory::createPlatformTransport();
        StreamTransportConfig settings;
        settings._idleTimeoutSeconds = 0.0;
        if ( _transport == nullptr || _transport->initialize( this, settings ) == false )
        {
            _transport.reset();
            return false;
        }
        _handle = _transport->connect( remote );
        if ( _handle.isValid() == false )
        {
            disconnect();
            return false;
        }
        const int64 deadline = DebugChannelInternal::nowMilli() + timeoutMilli;
        while ( DebugChannelInternal::nowMilli() < deadline )
        {
            if ( isConnected() )
                return true;
            {
                ScopedLock<mutex> lock{ _mutex };
                if ( _bClosed == SW_TRUE )
                    break;
            }
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + DebugChannelInternal::kPollSleepNanoseconds );
        }
        disconnect();
        return false;
    }

    void DebugChannelClient::disconnect()
    {
        if ( _transport != nullptr )
            _transport->shutdown();
        _transport.reset();
        ScopedLock<mutex> lock{ _mutex };
        _received.clear();
        _handle  = StreamConnectionHandle{};
        _bOpen   = SW_FALSE;
        _bClosed = SW_FALSE;
    }

    bool DebugChannelClient::isConnected() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _bOpen == SW_TRUE && _bClosed == SW_FALSE;
    }

    bool DebugChannelClient::request( string_view commandLine, int32 timeoutMilli, string& outBody )
    {
        outBody.clear();
        if ( _transport == nullptr || isConnected() == false )
            return false;
        {
            ScopedLock<mutex> lock{ _mutex };
            _received.clear();
        }
        string line = _token;
        line += ' ';
        line += commandLine;
        line += '\n';
        (void)_transport->send( _handle, reinterpret_cast<const uint8*>( line.data() ), static_cast<int32>( line.size() ) );
        const int64 deadline = DebugChannelInternal::nowMilli() + timeoutMilli;
        while ( DebugChannelInternal::nowMilli() < deadline )
        {
            {
                ScopedLock<mutex> lock{ _mutex };
                const size_t      headEnd = _received.find( '\n' );
                if ( headEnd != string::npos )
                {
                    const string_view head      = string_view( _received ).substr( 0, headEnd );
                    const size_t      space     = head.find( ' ' );
                    uint64            bodySize  = 0;
                    const bool        bHasSize  = space != string_view::npos && StringUtil::parseUint64( head.substr( space + 1 ), bodySize );
                    const bool        bComplete = bHasSize && _received.size() >= headEnd + 1 + bodySize;
                    if ( bComplete )
                    {
                        outBody = _received.substr( headEnd + 1, static_cast<size_t>( bodySize ) );
                        return head.substr( 0, space ) == "OK";
                    }
                }
                if ( _bClosed == SW_TRUE )
                    return false;
            }
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + DebugChannelInternal::kPollSleepNanoseconds );
        }
        return false;
    }

    void DebugChannelClient::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)handle;
        (void)remote;
        (void)bAccepted;
        ScopedLock<mutex> lock{ _mutex };
        _bOpen = SW_TRUE;
    }

    void DebugChannelClient::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        (void)handle;
        ScopedLock<mutex> lock{ _mutex };
        _received.append( reinterpret_cast<const utf8*>( pData ), static_cast<size_t>( size ) );
    }

    void DebugChannelClient::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        (void)handle;
        (void)reason;
        ScopedLock<mutex> lock{ _mutex };
        _bClosed = SW_TRUE;
    }
} // namespace sw

#endif
