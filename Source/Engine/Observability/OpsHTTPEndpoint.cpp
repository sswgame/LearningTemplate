#include "pch.h"

#include "Engine/Observability/OpsHTTPEndpoint.h"

#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/ServiceHealthRegistry.h"

#include <charconv>

namespace sw
{
    SW_LOG_CALLER( "OpsHTTPEndpoint" );

    namespace
    {
        struct OpsHTTPEndpointInternal
        {
            static constexpr const utf8* kHeadEnd           = "\r\n\r\n";
            static constexpr string_view kMetricContentType = "text/plain; version=0.0.4; charset=utf-8";
            static constexpr string_view kTextContentType   = "text/plain; charset=utf-8";

            static const utf8* findReason( int32 status )
            {
                switch ( status )
                {
                    case 200:
                        return "OK";
                    case 400:
                        return "Bad Request";
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

            static void appendNumber( string& outText, uint64 value )
            {
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), value );
                outText.append( arrBuffer, result.ptr );
            }

            static int32 writeResponse( int32 status, string_view contentType, string_view body, string& outResponse )
            {
                outResponse.clear();
                outResponse += "HTTP/1.1 ";
                appendNumber( outResponse, static_cast<uint64>( status ) );
                outResponse.push_back( ' ' );
                outResponse += findReason( status );
                outResponse += "\r\nContent-Type: ";
                outResponse += contentType;
                outResponse += "\r\nContent-Length: ";
                appendNumber( outResponse, body.size() );
                outResponse += "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
                outResponse += body;
                return status;
            }

            static int64 nowMonotonicMs() { return MonotonicClock::nowNanoseconds() / 1000000; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    OpsHTTPEndpoint::OpsHTTPEndpoint()
        : _listConnection{}
        , _settings{}
        , _mutex{}
        , _pTransport{ nullptr }
        , _pMetricRegistry{ nullptr }
        , _pHealthRegistry{ nullptr }
    {
    }

    OpsHTTPEndpoint::~OpsHTTPEndpoint() { SW_ASSERT( _pTransport == nullptr ); }

    bool OpsHTTPEndpoint::initialize( IStreamTransport* pTransport, const StreamTransportSettings& transportSettings, const OpsHTTPEndpointSettings& settings,
                                      const MetricRegistry* pMetricRegistry, const ServiceHealthRegistry* pHealthRegistry )
    {
        if ( pTransport == nullptr )
            return false;
        _settings        = settings;
        _pMetricRegistry = pMetricRegistry;
        _pHealthRegistry = pHealthRegistry;
        if ( pTransport->initialize( this, transportSettings ) == false )
        {
            SW_LOG_ERROR( "Ops HTTP endpoint could not start its stream transport" );
            return false;
        }
        _pTransport = pTransport;
        if ( _pTransport->listen( settings._bindAddress ) == false )
        {
            SW_LOG_ERROR( "Ops HTTP endpoint could not listen on %#", settings._bindAddress.toString().c_str() );
            shutdown();
            return false;
        }
        NetAddress listenAddress = settings._bindAddress;
        listenAddress._port      = _pTransport->getListenPort(); // 포트 0(아무 포트)이면 실제로 받은 포트
        SW_LOG_INFO( "Ops HTTP endpoint listening on %# (/metrics /healthz /readyz)", listenAddress.toString().c_str() );
        return true;
    }

    void OpsHTTPEndpoint::shutdown()
    {
        if ( _pTransport != nullptr )
            _pTransport->shutdown();
        _pTransport = nullptr;
        std::scoped_lock<mutex> lock{ _mutex };
        _listConnection.clear();
    }

    uint16 OpsHTTPEndpoint::getListenPort() const { return _pTransport != nullptr ? _pTransport->getListenPort() : 0; }

    int32 OpsHTTPEndpoint::buildResponse( string_view requestHead, const MetricRegistry* pMetricRegistry, const ServiceHealthRegistry* pHealthRegistry, int64 monotonicMs,
                                          string& outResponse )
    {
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
            string body;
            pHealthRegistry->writeReport( body, monotonicMs );
            const bool bReady = pHealthRegistry->isReady( monotonicMs );
            return OpsHTTPEndpointInternal::writeResponse( bReady ? 200 : 503, OpsHTTPEndpointInternal::kTextContentType, body, outResponse );
        }
        return OpsHTTPEndpointInternal::writeResponse( 404, OpsHTTPEndpointInternal::kTextContentType, "not found\n", outResponse );
    }

    void OpsHTTPEndpoint::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        if ( bAccepted == false )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        Connection&             connection = _listConnection.emplace_back();
        connection._packedHandle           = handle.packed();
    }

    void OpsHTTPEndpoint::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        string response;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            Connection*             pConnection = findConnection( handle.packed() );
            if ( pConnection == nullptr || pConnection->_bAnswered == SW_TRUE )
                return;
            pConnection->_buffer.append( reinterpret_cast<const utf8*>( pData ), static_cast<size_t>( size ) );
            const size_t headEnd = pConnection->_buffer.find( OpsHTTPEndpointInternal::kHeadEnd );
            if ( headEnd != string::npos )
            {
                (void)buildResponse( string_view( pConnection->_buffer ).substr( 0, headEnd ), _pMetricRegistry, _pHealthRegistry,
                                     OpsHTTPEndpointInternal::nowMonotonicMs(), response );
            }
            else if ( static_cast<int32>( pConnection->_buffer.size() ) > _settings._maxRequestBytes )
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
        std::scoped_lock<mutex> lock{ _mutex };
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
        for ( Connection& connection : _listConnection )
        {
            if ( connection._packedHandle == packedHandle )
                return &connection;
        }
        return nullptr;
    }
} // namespace sw
