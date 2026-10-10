/**
 * @file Test/TestFramework/TestStreamEndpointPair.h
 * @brief 스트림 시험의 끝점 한 쌍 — 루프백 스트림 망 위에 서버 · 클라이언트 `StreamMessageEndpoint` 를 세우고 클라이언트가 서버에 연결을 겁니다.
 * @details 전송은 I/O 스레드 없이(`_ioThreadCount = 0`) 돌고 `step` 이 두 전송의 `pollIO` 와 두 끝점의 `pump` 를 차례로 부른다 — 한 스레드라 결정적이다.
 *          Core 헤더만 include 한다(CoreTest 의 엔진 금지 규칙).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

namespace test
{
    /**
     * @class StreamEndpointPair
     * @brief 서버 끝점은 `listen` 하고 클라이언트 끝점은 `_clientHandle` 로 연결을 겁니다. 소멸자가 두 전송을 내리고 남은 닫힘을 한 번 넘깁니다.
     */
    class StreamEndpointPair
    {
    public:
        /** @brief 클라이언트 전송을 감쌀 때(엿보기 · 변조) — 루프백 전송을 받아 감싼 전송을 돌려준다. */
        using ClientTransportWrapper = sw::unique_ptr<sw::IStreamTransport> ( * )( sw::unique_ptr<sw::IStreamTransport> inner );

        StreamEndpointPair( sw::IStreamEndpointListener& serverListener, sw::IStreamEndpointListener& clientListener, const sw::StreamEndpointSettings& endpointSettings,
                            const sw::LoopbackStreamConditions& conditions )
            : StreamEndpointPair( serverListener, clientListener, endpointSettings, endpointSettings, conditions, nullptr )
        {
        }

        /** @brief 서버 · 클라이언트 설정을 따로(한쪽만 TLS 등) 받고, @p wrapClientTransport 가 있으면 클라이언트 전송을 감쌉니다. */
        StreamEndpointPair( sw::IStreamEndpointListener& serverListener, sw::IStreamEndpointListener& clientListener, const sw::StreamEndpointSettings& serverSettings,
                            const sw::StreamEndpointSettings& clientSettings, const sw::LoopbackStreamConditions& conditions, ClientTransportWrapper wrapClientTransport )
            : _network{ 11u }
            , _serverTransport{ _network.createTransport() }
            , _clientTransport{ wrapClientTransport != nullptr ? wrapClientTransport( _network.createTransport() ) : _network.createTransport() }
            , _server{}
            , _client{}
            , _clientHandle{}
            , _pServerListener{ &serverListener }
            , _pClientListener{ &clientListener }
        {
            _network.setConditions( conditions );
            sw::StreamTransportSettings transportSettings;
            transportSettings._ioThreadCount = 0;
            (void)_server.initialize( _serverTransport.get(), serverSettings );
            (void)_client.initialize( _clientTransport.get(), clientSettings );
            (void)_serverTransport->initialize( &_server, transportSettings );
            (void)_clientTransport->initialize( &_client, transportSettings );
            (void)_serverTransport->listen( sw::NetAddress::makeLoopback( 0 ) );
            _clientHandle = _client.connect( sw::NetAddress::makeLoopback( _serverTransport->getListenPort() ) );
        }

        ~StreamEndpointPair()
        {
            _clientTransport->shutdown();
            _serverTransport->shutdown();
            step( 1 );
        }

        StreamEndpointPair( const StreamEndpointPair& )            = delete;
        StreamEndpointPair& operator=( const StreamEndpointPair& ) = delete;

        /** @brief 두 전송을 돌고 두 끝점을 pump 하기를 @p count 번 합니다. */
        void step( int32 count )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                (void)_serverTransport->pollIO( 0 );
                (void)_clientTransport->pollIO( 0 );
                (void)_server.pump( *_pServerListener );
                (void)_client.pump( *_pClientListener );
            }
        }

        sw::LoopbackStreamNetwork            _network;
        sw::unique_ptr<sw::IStreamTransport> _serverTransport;
        sw::unique_ptr<sw::IStreamTransport> _clientTransport;
        sw::StreamMessageEndpoint            _server;
        sw::StreamMessageEndpoint            _client;
        sw::StreamConnectionHandle           _clientHandle;

    private:
        sw::IStreamEndpointListener* _pServerListener;
        sw::IStreamEndpointListener* _pClientListener;
    };
} // namespace test
