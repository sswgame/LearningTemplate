#include "pch.h"

#include "Engine/Server/OpsHTTPEndpoint.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Server/ServiceHealthRegistry.h"

namespace sw
{
    namespace
    {
        struct OpsHTTPEndpointInternal
        {
            static constexpr int32       kStatusUnauthorized = 401; ///< 지표 · 준비 보고를 토큰 없이 물었다
            static constexpr const utf8* kHeadEnd            = "\r\n\r\n";
            static constexpr string_view kMetricContentType  = "text/plain; version=0.0.4; charset=utf-8";
            static constexpr string_view kTextContentType    = "text/plain; charset=utf-8";

            static const utf8* findReason( int32 status )
            {
                switch ( status )
                {
                    case 200:
                        return "OK";
                    case 400:
                        return "Bad Request";
                    case kStatusUnauthorized:
                        return "Unauthorized";
                    case 404:
                        return "Not Found";
                    case 405:
                        return "Method Not Allowed";
                    case 431:
                        return "Request Header Fields Too Large";
                    case 503:
                        return "Service Unavailable";
                    default:
                        return "Error";
                }
            }

            static int32 writeResponse( int32 status, string_view contentType, string_view body, string& outResponse )
            {
                outResponse.clear();
                outResponse += "HTTP/1.1 ";
                StringUtil::appendNumber( outResponse, static_cast<uint64>( status ) );
                outResponse.push_back( ' ' );
                outResponse += findReason( status );
                outResponse += "\r\nContent-Type: ";
                outResponse += contentType;
                outResponse += "\r\nContent-Length: ";
                StringUtil::appendNumber( outResponse, body.size() );
                outResponse += "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
                outResponse += body;
                return status;
            }

            /** @brief 요청 머리의 `Authorization: Bearer <토큰>` 이 @p bearerToken 과 같으면 true 입니다(머리 이름은 대소문자를 가리지 않는다, 값 비교는 길이에만 시간이 든다). */
            static bool hasBearerToken( string_view requestHead, string_view bearerToken )
            {
                constexpr string_view kHeader   = "authorization:";
                constexpr string_view kScheme   = "Bearer ";
                size_t                lineStart = requestHead.find( "\r\n" );
                while ( lineStart != string_view::npos )
                {
                    lineStart += 2;
                    const size_t      lineEnd = requestHead.find( "\r\n", lineStart );
                    const string_view line    = requestHead.substr( lineStart, lineEnd == string_view::npos ? string_view::npos : lineEnd - lineStart );
                    if ( StringUtil::startsWith( line, kHeader, true ) )
                    {
                        const string_view value = StringUtil::trim( line.substr( kHeader.size() ) );
                        return value.size() > kScheme.size() && StringUtil::startsWith( value, kScheme, true ) &&
                               SecureUtil::equalsConstantTime( value.substr( kScheme.size() ), bearerToken );
                    }
                    lineStart = lineEnd;
                }
                return false;
            }

            static int64 nowMonotonicMs() { return MonotonicClock::nowNanoseconds() / constant::time::kNanosecondsPerMillisecond; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    OpsHTTPEndpoint::OpsHTTPEndpoint()
        : _listConnection{}
        , _settings{}
        , _mutex{}
        , _pTransport{}
        , _pMetricRegistry{}
        , _pHealthRegistry{}
    {
    }

    OpsHTTPEndpoint::~OpsHTTPEndpoint()
    {
        SW_ASSERT( _pTransport.get() == nullptr );
    }

    bool OpsHTTPEndpoint::initialize( IStreamTransport* pTransport, const StreamTransportConfig& transportSettings, const OpsHTTPEndpointConfig& settings,
                                      const MetricRegistry* pMetricRegistry, const ServiceHealthRegistry* pHealthRegistry )
    {
        if ( pTransport == nullptr )
            return false;
        if ( settings._bearerToken.empty() && isBindAllowedWithoutToken( settings._bindAddress ) == false )
        {
            SW_LOG_ERROR_ID( "OpsHTTPEndpoint.PublicBindNeedsToken", "Ops HTTP endpoint refuses to listen on %# without a bearer token - bind to loopback or a private address, or set the token", settings._bindAddress.toString().c_str() );
            return false;
        }
        _settings.set( settings );
        _pMetricRegistry.set( pMetricRegistry );
        _pHealthRegistry.set( pHealthRegistry );
        if ( pTransport->initialize( this, transportSettings ) == false )
        {
            SW_LOG_ERROR_ID( "OpsHTTPEndpoint.OpsHTTPEndpointCould", "Ops HTTP endpoint could not start its stream transport" );
            shutdown();
            return false;
        }
        _pTransport.set( pTransport );
        if ( _pTransport->listen( settings._bindAddress ) == false )
        {
            SW_LOG_ERROR_ID( "OpsHTTPEndpoint.OpsHTTPEndpointCould2", "Ops HTTP endpoint could not listen on %#", settings._bindAddress.toString().c_str() );
            shutdown();
            return false;
        }
        NetAddress listenAddress = settings._bindAddress;
        listenAddress._port      = _pTransport->getListenPort(); // 포트 0(아무 포트)이면 실제로 받은 포트
        SW_LOG_INFO_ID( "OpsHTTPEndpoint.OpsHTTPEndpointListening", "Ops HTTP endpoint listening on %# (/metrics /healthz /readyz)", listenAddress.toString().c_str() );
        return true;
    }

    void OpsHTTPEndpoint::shutdown()
    {
        if ( _pTransport.get() != nullptr )
            _pTransport->shutdown();
        _pTransport.reset();
        // 설정 · 레지스트리는 initialize 가 한 번 정한다 — 다시 시작할 수 있게 전송을 멈춘 뒤 비운다.
        _settings.reset();
        _pMetricRegistry.reset();
        _pHealthRegistry.reset();
        ScopedLock<mutex> lock{ _mutex };
        _listConnection.clear();
    }

    uint16 OpsHTTPEndpoint::getListenPort() const
    {
        return _pTransport.get() != nullptr ? _pTransport->getListenPort() : 0;
    }

    int32 OpsHTTPEndpoint::makeResponse( string_view requestHead, const MetricRegistry* pMetricRegistry, const ServiceHealthRegistry* pHealthRegistry, int64 monotonicMs,
                                         string_view bearerToken, string& outResponse )
    {
        // 탐침(쿠버네티스 kubelet 은 비밀 머리를 실을 수 없다)은 상태 코드만 받는다 — 지표와 준비 보고 본문은 토큰이 있어야 한다.
        const bool        bAuthorized = bearerToken.empty() || OpsHTTPEndpointInternal::hasBearerToken( requestHead, bearerToken );
        const string_view requestLine = requestHead.substr( 0, requestHead.find( "\r\n" ) );
        const size_t      firstSpace  = requestLine.find( ' ' );
        const size_t      secondSpace = firstSpace == string_view::npos ? string_view::npos : requestLine.find( ' ', firstSpace + 1 );
        if ( firstSpace == string_view::npos || secondSpace == string_view::npos )
            return OpsHTTPEndpointInternal::writeResponse( 400, OpsHTTPEndpointInternal::kTextContentType, "bad request line\n", outResponse );
        const string_view method = requestLine.substr( 0, firstSpace );
        string_view       target = requestLine.substr( firstSpace + 1, secondSpace - firstSpace - 1 );
        target                   = target.substr( 0, target.find( '?' ) );
        if ( method != "GET" )
            return OpsHTTPEndpointInternal::writeResponse( 405, OpsHTTPEndpointInternal::kTextContentType, "only GET\n", outResponse );

        if ( bAuthorized == false && target != "/healthz" && target != "/readyz" )
            return OpsHTTPEndpointInternal::writeResponse( OpsHTTPEndpointInternal::kStatusUnauthorized, OpsHTTPEndpointInternal::kTextContentType, "unauthorized\n", outResponse );
        if ( target == "/metrics" && pMetricRegistry != nullptr )
        {
            string body;
            pMetricRegistry->writePrometheusText( body );
            return OpsHTTPEndpointInternal::writeResponse( 200, OpsHTTPEndpointInternal::kMetricContentType, body, outResponse );
        }
        if ( target == "/healthz" && pHealthRegistry != nullptr )
        {
            const bool bLive = pHealthRegistry->isLive( monotonicMs );
            return OpsHTTPEndpointInternal::writeResponse( bLive ? 200 : 503, OpsHTTPEndpointInternal::kTextContentType, bLive ? "ok\n" : "stalled\n", outResponse );
        }
        if ( target == "/readyz" && pHealthRegistry != nullptr )
        {
            const bool bReady = pHealthRegistry->isReady( monotonicMs );
            string     body   = bReady ? "ready\n" : "not ready\n";
            if ( bAuthorized )
            {
                body.clear();
                pHealthRegistry->writeReport( body, monotonicMs ); // 서비스별 상태는 토큰이 있을 때만 보인다
            }
            return OpsHTTPEndpointInternal::writeResponse( bReady ? 200 : 503, OpsHTTPEndpointInternal::kTextContentType, body, outResponse );
        }
        return OpsHTTPEndpointInternal::writeResponse( 404, OpsHTTPEndpointInternal::kTextContentType, "not found\n", outResponse );
    }

    void OpsHTTPEndpoint::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        if ( bAccepted == false )
            return;
        ScopedLock<mutex> lock{ _mutex };
        Connection&       connection = _listConnection.emplace_back();
        connection._packedHandle     = handle.packed();
    }

    void OpsHTTPEndpoint::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        string response;
        {
            ScopedLock<mutex> lock{ _mutex };
            Connection*       pConnection = findConnection( handle.packed() );
            if ( pConnection == nullptr || pConnection->_bAnswered == SW_TRUE )
                return;
            pConnection->_buffer.append( reinterpret_cast<const utf8*>( pData ), static_cast<size_t>( size ) );
            const size_t headEnd = pConnection->_buffer.find( OpsHTTPEndpointInternal::kHeadEnd );
            if ( headEnd != string::npos )
            {
                (void)makeResponse( string_view( pConnection->_buffer ).substr( 0, headEnd ), _pMetricRegistry.get(), _pHealthRegistry.get(),
                                    OpsHTTPEndpointInternal::nowMonotonicMs(), _settings->_bearerToken, response );
            }
            else if ( static_cast<int32>( pConnection->_buffer.size() ) > _settings->_maxRequestBytes )
            {
                // 돌려주는 것은 상태 코드다 — 응답은 response 에 담긴다
                (void)OpsHTTPEndpointInternal::writeResponse( 431, OpsHTTPEndpointInternal::kTextContentType, "request head too large\n", response );
            }
            else
            {
                return; // 머리가 아직 다 오지 않았다
            }
            pConnection->_bAnswered = SW_TRUE;
            pConnection->_buffer.clear();
        }
        (void)_pTransport->send( handle, reinterpret_cast<const uint8*>( response.data() ), static_cast<int32>( response.size() ) );
        _pTransport->close( handle, StreamCloseMode::Graceful );
    }

    void OpsHTTPEndpoint::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        (void)reason;
        ScopedLock<mutex> lock{ _mutex };
        for ( size_t connectionIndex = 0; connectionIndex < _listConnection.size(); ++connectionIndex )
        {
            if ( _listConnection[connectionIndex]._packedHandle == handle.packed() )
            {
                _listConnection.erase( _listConnection.begin() + static_cast<ptrdiff_t>( connectionIndex ) );
                return;
            }
        }
    }

    OpsHTTPEndpoint::Connection* OpsHTTPEndpoint::findConnection( uint64 packedHandle )
    {
        return VectorUtil::findBy<&Connection::_packedHandle>( _listConnection, packedHandle );
    }
} // namespace sw
