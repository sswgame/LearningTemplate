/**
 * @file StreamEndpointPair.h
 * @brief 스트림 테스트의 엔드포인트 한 쌍 — 루프백 스트림 망 위에 서버 · 클라이언트 `StreamMessageEndpoint` 를 세우고 클라이언트가 서버에 연결을 겁니다.
 * @details 전송은 I/O 스레드 없이(`_ioThreadCount = 0`) 돌고 `step` 이 두 전송의 `pollIO` 와 두 엔드포인트의 `pump` 를 차례로 부른다 — 한 스레드라 결정적이다.
 *          스트림 엔드포인트 · 압축 봉투 단위 테스트가 함께 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

namespace sw::test
{
    /**
     * @class StreamEndpointPair
     * @brief 서버 엔드포인트는 `listen` 하고 클라이언트 엔드포인트는 `_clientHandle` 로 연결을 겁니다. 소멸자가 두 전송을 내리고 남은 닫힘을 한 번 넘깁니다.
     */
    class StreamEndpointPair
    {
    public:
        StreamEndpointPair( IStreamEndpointListener& serverListener, IStreamEndpointListener& clientListener, const StreamEndpointConfig& endpointSettings,
                            const LoopbackStreamConditions& conditions )
            : StreamEndpointPair( serverListener, clientListener, endpointSettings, endpointSettings, conditions )
        {
        }

        /** @brief 서버 · 클라이언트 설정을 따로(한쪽만 압축 · 몸 상한 등) 받습니다. */
        StreamEndpointPair( IStreamEndpointListener& serverListener, IStreamEndpointListener& clientListener, const StreamEndpointConfig& serverSettings,
                            const StreamEndpointConfig& clientSettings, const LoopbackStreamConditions& conditions )
            : _network{ 11u }
            , _serverTransport{ _network.createTransport() }
            , _clientTransport{ _network.createTransport() }
            , _server{}
            , _client{}
            , _clientHandle{}
            , _pServerListener{ &serverListener }
            , _pClientListener{ &clientListener }
        {
            _network.setConditions( conditions );
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            _bReady                          = _server.initialize( _serverTransport.get(), serverSettings ) && _client.initialize( _clientTransport.get(), clientSettings ) &&
                      _serverTransport->initialize( &_server, transportSettings ) && _clientTransport->initialize( &_client, transportSettings ) &&
                      _serverTransport->listen( NetAddress::makeLoopback( 0 ) );
            _clientHandle = _client.connect( NetAddress::makeLoopback( _serverTransport->getListenPort() ) );
        }

        ~StreamEndpointPair()
        {
            _clientTransport->shutdown();
            _serverTransport->shutdown();
            step( 1 );
        }

        StreamEndpointPair( const StreamEndpointPair& )            = delete;
        StreamEndpointPair& operator=( const StreamEndpointPair& ) = delete;

        /** @brief 두 전송을 돌고 두 엔드포인트를 pump 하기를 @p count 번 합니다. */
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

        LoopbackStreamNetwork        _network;
        unique_ptr<IStreamTransport> _serverTransport;
        unique_ptr<IStreamTransport> _clientTransport;
        StreamMessageEndpoint        _server;
        StreamMessageEndpoint        _client;
        StreamConnectionHandle       _clientHandle;
        bool                         _bReady{ false }; ///< 준비(초기화 · listen)가 모두 됐다 — 케이스가 처음에 `SW_REQUIRE_TRUE` 로 본다

    private:
        IStreamEndpointListener* _pServerListener;
        IStreamEndpointListener* _pClientListener;
    };
} // namespace sw::test
