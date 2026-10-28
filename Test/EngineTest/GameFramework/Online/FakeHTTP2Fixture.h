/**
 * @file FakeHTTP2Fixture.h
 * @brief HTTP/2 클라이언트와 푸시 제공자(APNs) 테스트가 쓰는 가짜 HTTP/2 서버와 클라이언트 묶음입니다.
 * @details 스위트 파일이 아닙니다. 루프백 망 위에 자체 서명 인증서의 TLS(ALPN 은 컨텍스트가 정한다) 서버를 세우고, 연결 서문 · SETTINGS · HEADERS · DATA 를 손으로 읽어
 *          요청마다 `_statusCode` 응답(머리 + 본문 + 추가 머리)을 돌려줍니다. 클라이언트는 그 인증서를 믿는 `HTTP2Client` 입니다. 서버 주소는 `127.0.0.1` 이라 이름 해석이 없다.
 *          I/O 스레드가 없는 전송이라 `collect`/`pump` 가 서버와 클라이언트를 번갈아 돌린다.
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Client.h"
#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"
#include "GameFramework/Base/Online/HTTP/HTTPLink.h"
#include "GameFramework/Base/Online/HTTP/Hpack.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include <chrono>
#include <thread>

namespace test
{
    /** @brief 서버가 받은 요청 하나입니다(머리는 소문자 이름 그대로). */
    struct SeenRequest
    {
        sw::vector<sw::HpackHeader> _listHeader{};
        sw::vector<uint8>           _bodyBytes{};

        const sw::HpackHeader* find( sw::string_view name ) const
        {
            for ( const sw::HpackHeader& header : _listHeader )
            {
                if ( header._name == name )
                    return &header;
            }
            return nullptr;
        }
        sw::string valueOf( sw::string_view name ) const
        {
            const sw::HpackHeader* pHeader = find( name );
            return pHeader != nullptr ? pHeader->_value : sw::string();
        }
        sw::string getBodyText() const { return sw::string( reinterpret_cast<const utf8*>( _bodyBytes.data() ), _bodyBytes.size() ); }
    };
} // namespace test

namespace test
{
    /** @brief TLS 위에서 HTTP/2 프레임을 읽고 요청마다 `_statusCode` 응답을 돌려주는 가짜 서버입니다. */
    class FakeHTTP2Server final : public sw::IStreamHandler
    {
    public:
        struct Peer
        {
            sw::HTTPLink                           _link{};
            sw::HpackDecoder                       _decoder{};
            sw::vector<uint8>                      _plainBytes{};
            sw::vector<uint8>                      _frameBytes{};
            sw::unordered_map<uint32, SeenRequest> _mapRequest{};
            uint8                                  _bPrefaceSeen{ SW_FALSE };
        };

        explicit FakeHTTP2Server( sw::ITLSContext* pTLSContext )
            : _pTLSContext{ pTLSContext }
        {
        }

        ~FakeHTTP2Server() override
        {
            if ( _transport != nullptr )
                _transport->shutdown(); // 열린 연결이 닫히며 콜백이 오므로 멤버가 살아 있는 지금 한다
        }

        void setTLSContext( sw::ITLSContext* pTLSContext ) { _pTLSContext = pTLSContext; }

        [[nodiscard]] bool initialize( sw::unique_ptr<sw::IStreamTransport> transport )
        {
            sw::StreamTransportConfig settings;
            settings._ioThreadCount = 0;
            _transport              = std::move( transport );
            return _transport->initialize( this, settings ) && _transport->listen( sw::NetAddress::makeLoopback( 0 ) );
        }
        void   update() { (void)_transport->pollIO( 0 ); }
        uint16 getPort() const { return _transport->getListenPort(); }

        void onStreamOpened( sw::StreamConnectionHandle handle, const sw::NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            if ( bAccepted == false )
                return;
            ++_acceptedCount;
            sw::unique_ptr<Peer> peer = sw::make_unique<Peer>();
            if ( peer->_link.initialize( _pTLSContext ) == false )
                return;
            peer->_link.setHandle( handle );
            _mapPeer[handle.packed()] = std::move( peer );
        }

        void onStreamReceived( sw::StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            const auto peerIt = _mapPeer.find( handle.packed() );
            if ( peerIt == _mapPeer.end() )
                return;
            Peer& peer = *peerIt->second;
            peer._plainBytes.clear();
            if ( peer._link.readReceived( *_transport, pData, size, peer._plainBytes ) == false )
            {
                _transport->close( handle, sw::StreamCloseMode::Abort );
                return;
            }
            peer._frameBytes.insert( peer._frameBytes.end(), peer._plainBytes.begin(), peer._plainBytes.end() );
            processFrames( peer );
        }

        void onStreamClosed( sw::StreamConnectionHandle handle, sw::StreamCloseReason reason ) override
        {
            (void)reason;
            _mapPeer.erase( handle.packed() );
        }

        int32                       _acceptedCount{ 0 };
        int32                       _statusCode{ 200 };
        uint8                       _bSilent{ SW_FALSE }; ///< 켜면 요청에 답하지 않는다(시한)
        sw::string                  _responseBody{ "{\"reason\":\"ok\"}" };
        sw::vector<sw::HpackHeader> _listExtraResponseHeader{}; ///< 응답 머리에 더 싣는 것(`retry-after` …)
        sw::vector<SeenRequest>     _listSeen{};

    private:
        void processFrames( Peer& peer )
        {
            const size_t prefaceSize = sw::HTTP2FrameUtil::kConnectionPreface.size();
            if ( peer._bPrefaceSeen == SW_FALSE )
            {
                if ( peer._frameBytes.size() < prefaceSize )
                    return;
                peer._frameBytes.erase( peer._frameBytes.begin(), peer._frameBytes.begin() + static_cast<ptrdiff_t>( prefaceSize ) );
                peer._bPrefaceSeen = SW_TRUE;
                sw::vector<uint8> listSettings;
                (void)sw::HTTP2FrameUtil::writeSettings( {}, false, listSettings ); // 본문이 없어 범위 안이다
                send( peer, listSettings );
            }
            size_t offset = 0;
            for ( ;; )
            {
                sw::HTTP2FrameHeader header;
                if ( sw::HTTP2FrameUtil::readHeader( peer._frameBytes.data() + offset, peer._frameBytes.size() - offset, header ) == false )
                    break;
                if ( peer._frameBytes.size() - offset < sw::HTTP2FrameUtil::kHeaderSize + header._length )
                    break;
                const uint8* pBody = peer._frameBytes.data() + offset + sw::HTTP2FrameUtil::kHeaderSize;
                handleFrame( peer, header, pBody );
                offset += sw::HTTP2FrameUtil::kHeaderSize + header._length;
            }
            peer._frameBytes.erase( peer._frameBytes.begin(), peer._frameBytes.begin() + static_cast<ptrdiff_t>( offset ) );
        }

        void handleFrame( Peer& peer, const sw::HTTP2FrameHeader& header, const uint8* pBody )
        {
            const bool bEnd = ( header._flags & sw::HTTP2FrameUtil::kFlagEndStream ) != 0;
            if ( header._type == static_cast<uint8>( sw::HTTP2FrameType::Settings ) && ( header._flags & sw::HTTP2FrameUtil::kFlagAck ) == 0 )
            {
                sw::vector<uint8> listAck;
                (void)sw::HTTP2FrameUtil::writeSettings( {}, true, listAck ); // 본문이 없어 범위 안이다
                send( peer, listAck );
            }
            else if ( header._type == static_cast<uint8>( sw::HTTP2FrameType::Headers ) )
            {
                SeenRequest& request = peer._mapRequest[header._streamID];
                sw::string   error;
                (void)peer._decoder.decode( pBody, header._length, request._listHeader, error ); // 클라이언트 부호기는 동적 표를 쓰지 않아 한 블록에 다 있다
                if ( bEnd )
                    respond( peer, header._streamID );
            }
            else if ( header._type == static_cast<uint8>( sw::HTTP2FrameType::Data ) )
            {
                SeenRequest& request = peer._mapRequest[header._streamID];
                request._bodyBytes.insert( request._bodyBytes.end(), pBody, pBody + header._length );
                if ( bEnd )
                    respond( peer, header._streamID );
            }
        }

        void respond( Peer& peer, uint32 streamID )
        {
            _listSeen.push_back( peer._mapRequest[streamID] );
            peer._mapRequest.erase( streamID );
            if ( _bSilent == SW_TRUE )
                return;
            sw::vector<sw::HpackHeader> listHeader;
            listHeader.push_back( sw::HpackHeader{ ":status", sw::to_string( _statusCode ) } );
            listHeader.push_back( sw::HpackHeader{ "apns-id", "fake-id" } );
            for ( const sw::HpackHeader& extra : _listExtraResponseHeader )
            {
                listHeader.push_back( extra );
            }
            sw::vector<uint8> listBlock;
            (void)sw::HpackEncoder::encode( listHeader, listBlock );
            sw::vector<uint8> bytes;
            (void)sw::HTTP2FrameUtil::writeHeaders( streamID, listBlock, false, sw::HTTP2FrameUtil::kDefaultMaxFrameSize, bytes );                                                                // 작은 상수 블록이라 범위 안이다
            (void)sw::HTTP2FrameUtil::writeData( streamID, reinterpret_cast<const uint8*>( _responseBody.data() ), _responseBody.size(), true, sw::HTTP2FrameUtil::kDefaultMaxFrameSize, bytes ); // 작은 본문이라 범위 안이다
            send( peer, bytes );
        }

        void send( Peer& peer, const sw::vector<uint8>& bytes )
        {
            (void)peer._link.writePlain( *_transport, bytes.data(), bytes.size() ); // 전송이 끊기면 클라이언트가 실패로 드러낸다
        }

        sw::ITLSContext*                                _pTLSContext;
        sw::unique_ptr<sw::IStreamTransport>            _transport{};
        sw::unordered_map<uint64, sw::unique_ptr<Peer>> _mapPeer{};
    };
} // namespace test

namespace test
{
    /** @brief 가짜 HTTP/2 서버 + 클라이언트입니다. @p serverAlpn · @p clientAlpn 으로 협상을 어긋나게 만들 수 있다. */
    struct FakeHTTP2Fixture
    {
        sw::LoopbackStreamNetwork       _network;
        sw::unique_ptr<sw::ITLSContext> _serverContext;
        sw::unique_ptr<sw::ITLSContext> _clientContext;
        sw::ManualClock                 _clock; ///< HTTP 시한을 재는 단조 시계(`pump` 가 민다). 클라이언트보다 먼저 선언한다
        FakeHTTP2Server                 _server;
        sw::HTTP2Client                 _client;
        sw::UnixMilliseconds            _nowMs; ///< 제공자에 넘기는 가짜 서버 시각(`pump` 가 같은 만큼 민다)
        bool                            _bReady;

        FakeHTTP2Fixture( const sw::string& serverAlpn, const sw::string& clientAlpn )
            : _network{ 23u }
            , _serverContext{}
            , _clientContext{}
            , _clock{ sw::MonotonicMilliseconds{ 0 } }
            , _server{ nullptr }
            , _client{ _clock }
            , _nowMs{ 1000000 }
            , _bReady{ false }
        {
            sw::INetSecurityProvider& provider = sw::NetSecurity::getProvider();
            sw::string                certificatePem;
            sw::string                privateKeyPem;
            if ( provider.createSelfSignedCertificate( "localhost", 1, certificatePem, privateKeyPem ) == false )
                return;
            sw::TLSContextConfig serverTLS;
            serverTLS._role           = sw::TLSRole::Server;
            serverTLS._certificatePem = certificatePem;
            serverTLS._privateKeyPem  = privateKeyPem;
            serverTLS._alpnProtocol   = serverAlpn;
            sw::TLSContextConfig clientTLS;
            clientTLS._role         = sw::TLSRole::Client;
            clientTLS._trustPem     = certificatePem;
            clientTLS._serverName   = "localhost";
            clientTLS._alpnProtocol = clientAlpn;
            sw::string error;
            _serverContext = provider.createTLSContext( serverTLS, error );
            _clientContext = provider.createTLSContext( clientTLS, error );
            if ( _serverContext == nullptr || _clientContext == nullptr )
                return;
            _server.setTLSContext( _serverContext.get() );
            sw::StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            _bReady                          = _server.initialize( _network.createTransport() ) && _client.initialize( _network.createTransport(), transportSettings, sw::HTTP2ClientConfig{} );
            _client.registerTLSContext( "127.0.0.1", _clientContext.get() );
        }

        sw::string makeURL( const utf8* pPath ) const { return "https://127.0.0.1:" + sw::to_string( static_cast<int32>( _server.getPort() ) ) + pPath; }
        /** @brief 서버 기준 주소(경로 없음)입니다 — 제공자의 기준 주소 설정에 넣는다. */
        sw::string makeBaseURL() const { return makeURL( "" ); }

        sw::HTTPClientRequest makePost( const utf8* pPath, sw::string_view body, int64 timeoutMs ) const
        {
            sw::HTTPClientRequest request;
            request._method    = sw::HTTPMethod::Post;
            request._url       = makeURL( pPath );
            request._timeoutMs = timeoutMs;
            request._bodyBytes.assign( reinterpret_cast<const uint8*>( body.data() ), reinterpret_cast<const uint8*>( body.data() ) + body.size() );
            request._listHeader.push_back( sw::HTTPHeader{ "apns-topic", "com.example.app" } );
            return request;
        }

        /** @brief 서버를 한 번 돌리고 시계를 앞으로 보냅니다(제공자의 `update` 와 번갈아 부른다). */
        void pump( int64 advanceMs = 1 )
        {
            _server.update();
            _nowMs += advanceMs;
            _clock.advanceMilliseconds( advanceMs );
            std::this_thread::sleep_for( std::chrono::microseconds( 200 ) );
        }

        /** @brief 응답이 @p count 개 모일 때까지 서버와 클라이언트를 번갈아 돌립니다(시계는 1 ms 씩). 모이지 못하면 모은 만큼만 돌려준다. */
        sw::vector<sw::HTTPClientResponse> collect( int32 count, int32 maxRounds = 4000 )
        {
            sw::vector<sw::HTTPClientResponse> listResponse;
            for ( int32 round = 0; round < maxRounds && static_cast<int32>( listResponse.size() ) < count; ++round )
            {
                _client.update();
                (void)_client.pollResponses( listResponse );
                pump();
            }
            return listResponse;
        }
    };
} // namespace test
