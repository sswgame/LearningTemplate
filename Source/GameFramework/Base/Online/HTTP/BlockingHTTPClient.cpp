#include "pch.h"

#include "GameFramework/Base/Online/HTTP/BlockingHTTPClient.h"

#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    namespace
    {
        struct BlockingHTTPClientInternal
        {
            static constexpr int64 kPollIntervalNanoseconds = constant::kNanosecondsPerMillisecond; ///< 응답을 기다리는 동안 한 번 도는 간격
        };
    } // namespace
} // namespace sw

namespace sw
{
    BlockingHTTPClient::BlockingHTTPClient()
        : _sendMutex{}
        , _client{}
    {
    }

    BlockingHTTPClient::~BlockingHTTPClient()
    {
        shutdown();
    }

    bool BlockingHTTPClient::initialize( unique_ptr<IStreamTransport> transport, const StreamTransportConfig& transportSettings, const HTTPClientConfig& settings )
    {
        ScopedLock<mutex> lock{ _sendMutex };
        return _client.initialize( std::move( transport ), transportSettings, settings );
    }

    void BlockingHTTPClient::shutdown()
    {
        ScopedLock<mutex> lock{ _sendMutex };
        _client.shutdown();
    }

    void BlockingHTTPClient::registerTLSContext( string_view host, ITLSContext* pTLSContext )
    {
        _client.registerTLSContext( host, pTLSContext );
    }

    bool BlockingHTTPClient::makeClientRequest( const HTTPRequest& request, HTTPClientRequest& outRequest )
    {
        if ( StringUtil::equals( string_view( request._method ), "POST", true ) )
            outRequest._method = HTTPMethod::Post;
        else if ( StringUtil::equals( string_view( request._method ), "GET", true ) )
            outRequest._method = HTTPMethod::Get;
        else
            return false;
        outRequest._url = request._url;
        outRequest._bodyBytes.assign( reinterpret_cast<const uint8*>( request._body.data() ), reinterpret_cast<const uint8*>( request._body.data() ) + request._body.size() );
        outRequest._listHeader.clear();
        outRequest._listHeader.reserve( request._listHeader.size() );
        for ( const pair<string, string>& header : request._listHeader )
        {
            outRequest._listHeader.push_back( HTTPHeader{ header.first, header.second } );
        }
        return true;
    }

    HTTPResponse BlockingHTTPClient::send( const HTTPRequest& request )
    {
        using Internal = BlockingHTTPClientInternal;
        HTTPResponse      response;
        HTTPClientRequest clientRequest;
        if ( makeClientRequest( request, clientRequest ) == false )
        {
            response._error = "unsupported HTTP method: " + request._method;
            return response;
        }
        ScopedLock<mutex>          lock{ _sendMutex };
        const int64                startNanoseconds = MonotonicClock::nowNanoseconds();
        const uint64               requestID        = _client.submitRequest( clientRequest, 0 );
        vector<HTTPClientResponse> listResponse;
        for ( ;; )
        {
            const int64 elapsedNanoseconds = MonotonicClock::nowNanoseconds() - startNanoseconds;
            _client.update( elapsedNanoseconds / constant::kNanosecondsPerMillisecond );
            listResponse.clear();
            (void)_client.pollResponses( listResponse ); // 이 객체만 요청을 맡기므로 다른 요청의 응답은 없다
            for ( const HTTPClientResponse& clientResponse : listResponse )
            {
                if ( clientResponse._requestID != requestID )
                    continue;
                response._status = clientResponse._bTransportFailed == SW_TRUE ? 0 : clientResponse._statusCode;
                if ( clientResponse._bTransportFailed == SW_TRUE )
                    response._error = clientResponse._failureText;
                else if ( clientResponse.isSuccess() == false )
                    response._error = string( clientResponse.getBodyText() );
                return response;
            }
            // 응답은 반드시 한 번 온다 — 시한이 지나면 `update` 가 전송 실패로 끝낸다
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + Internal::kPollIntervalNanoseconds );
        }
    }
} // namespace sw
