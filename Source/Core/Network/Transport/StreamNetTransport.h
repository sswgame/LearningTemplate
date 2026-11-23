/**
 * @file StreamNetTransport.h
 * @brief 데이터그램을 스트림(TCP · WebSocket) 연결로 나르는 전송입니다 — UDP 소켓이 없는 브라우저 클라이언트가 게임 세션에 붙는 길.
 * @details 데이터그램 하나가 `StreamFrame` 하나(`Message`)다. 클라이언트는 이 플랫폼의 스트림 전송(웹은 `WebSocketStreamTransport`), 서버는 `WebSocketAcceptTransport` 로 감싼
 *          리슨을 준다 — 브라우저(WebSocket)와 원시 TCP 가 같은 리슨에 붙는다. 언리얼 WebSocketNetworking · 유니티 Transport `WebSocketNetworkInterface` 와 같은 자리다.
 *          UDP 와 다른 점: 잃지 않고 순서대로 온다(앞 조각이 늦으면 뒤도 늦는다). 보낼 줄이 차면 버린다 — 느린 상대에게 쌓지 않는다(엔드포인트의 재전송이 맡는다).
 *          주소: 서버는 연결의 상대 주소로, 클라이언트는 서버 주소로 연결을 찾는다. 클라이언트의 끊긴 연결은 다음 `send` 가 다시 건다(엔드포인트가 접속 요청을 되풀이한다).
 *          암호화 · 접속 티켓은 위 층(`NetEndpoint`)이 데이터그램 안에서 한다 — 이 전송은 바이트만 나른다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/StreamFrame.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/NetTransport.h"

#include <condition_variable>

namespace sw
{
    class SW_API StreamNetTransport final : public INetTransport
    {
    public:
        /** @param pStream 나를 스트림 전송(소유권을 받는다). `listen` · `connect` 가 초기화한다. */
        explicit StreamNetTransport( unique_ptr<IStreamTransport> pStream );
        ~StreamNetTransport() override;

        StreamNetTransport( const StreamNetTransport& )            = delete;
        StreamNetTransport& operator=( const StreamNetTransport& ) = delete;

        /**
         * @brief 서버 — @p bindAddress 에서 받습니다(포트 0 = 아무 포트).
         * @param maxConnectionsPerRemote 한 호스트가 열 수 있는 연결 수입니다. 넘는 연결은 바로 닫습니다(1 보다 작으면 열지 않고 false).
         */
        [[nodiscard]] bool listen( const NetAddress& bindAddress, const StreamTransportConfig& settings, int32 maxConnectionsPerRemote );
        /** @brief 클라이언트 — @p server 에 겁니다. 연결이 열리기 전의 `send` 는 버려집니다(false). */
        [[nodiscard]] bool connect( const NetAddress& server, const StreamTransportConfig& settings );
        /** @brief 연결을 모두 닫습니다. 기다리는 스레드를 멈춘 뒤에 부릅니다. */
        void close();
        /** @brief `StreamTransportConfig::_ioThreadCount` 가 0 이면 부르는 쪽이 스트림 전송을 한 번 돌립니다. */
        int32 pollIO( int32 timeoutMilli );

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        /** @brief 서버는 리슨 주소, 클라이언트는 무효 주소(포트 0)입니다 — 나가는 연결의 로컬 포트는 쓰는 곳이 없다. */
        NetAddress getLocalAddress() const override { return _localAddress; }
        bool       waitForReceive( float64 timeoutSeconds ) override;
        void       wake() override;

    private:
        /** @brief 열린 연결 하나입니다. `_decoder` · `_bRejected` 는 받기 길(한 연결에 한 스레드)만 만진다. */
        struct Peer
        {
            StreamFrameDecoder _decoder{ kNetMaxPacketSize };
            NetAddress         _address{};
            uint8              _bRejected{ SW_FALSE }; ///< 깨진 프레임으로 닫는 중이다 — 남은 바이트를 읽지 않는다
        };

        /** @brief 스트림 전송의 처리기 — 전송이 처리기를 겸하지 않게 멤버로 든다. */
        class StreamHandler final : public IStreamHandler
        {
        public:
            explicit StreamHandler( StreamNetTransport& owner )
                : _owner{ owner }
            {
            }

            void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override { _owner.handleOpened( handle, remote, bAccepted ); }
            void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override { _owner.handleReceived( handle, pData, size ); }
            void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
            {
                (void)reason; // 끊김의 까닭은 엔드포인트의 시간 초과 · 끊김 알림이 말한다
                _owner.handleClosed( handle );
            }

        private:
            StreamNetTransport& _owner;
        };

        void handleOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted );
        void handleReceived( StreamConnectionHandle handle, const uint8* pData, int32 size );
        void handleClosed( StreamConnectionHandle handle );
        /** @brief 클라이언트 — 걸려 있는 연결이 없으면 하나 겁니다. */
        void startConnect();

    private:
        StreamHandler                                                       _handler;
        mutable mutex                                                       _mutex;             ///< 아래 표 · 편지함 · 깃발을 지킨다. 쥔 채 스트림 전송을 부르지 않는다(전송이 자기 잠금을 쥐고 콜백을 부를 수 있다)
        std::condition_variable_any                                         _receivedCondition; ///< 편지함이 차거나 깨움이 왔다
        unordered_map<NetAddress, StreamConnectionHandle, NetAddress::Hash> _mapHandleByAddress;
        unordered_map<uint64, shared_ptr<Peer>>                             _mapPeer;        ///< 키 `StreamConnectionHandle::packed`
        unordered_map<NetAddress, int32, NetAddress::Hash>                  _mapCountByHost; ///< 서버 — 호스트(포트 0)마다 열린 연결 수
        deque<NetReceivedDatagram>                                          _listInbox;
        NetAddress                                                          _localAddress;
        NetAddress                                                          _serverAddress; ///< 클라이언트 — 걸 서버(서버면 무효 주소)
        int32                                                               _maxConnectionsPerRemote;
        uint8                                                               _bOpen          : 1; ///< `listen` · `connect` 뒤 `close` 전
        uint8                                                               _bConnecting    : 1; ///< 클라이언트 — 건 연결이 있다(열리는 중이거나 열렸다)
        uint8                                                               _bWakeRequested : 1;
        unique_ptr<IStreamTransport>                                        _pStream; ///< 마지막 멤버라 맨 먼저 사라진다 — 닫힘 콜백이 위 멤버를 만진다
    };
} // namespace sw
