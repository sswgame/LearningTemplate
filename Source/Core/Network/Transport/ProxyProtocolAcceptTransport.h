/**
 * @file ProxyProtocolAcceptTransport.h
 * @brief L4 프록시(AWS NLB · HAProxy) 뒤에 선 리슨의 장식자입니다. 신뢰 프록시에서 온 연결만 받아 PROXY protocol v2 머리로 실제 클라이언트 주소를 얻습니다(감사 O-17).
 * @details - 받은 연결의 주소가 신뢰 프록시 목록(설정)에 없으면 바로 닫는다 — 프록시를 건너뛴 연결이 주소 단위 제한을 비켜 가거나 머리로 주소를 속이지 못한다.
 *          - 신뢰 프록시의 연결은 머리(`ProxyHeader::parse`)가 다 올 때까지 위로 알리지 않는다. 머리를 읽으면 실제 주소로 `onStreamOpened` 를 알리고 남은 바이트를 넘긴다.
 *            머리가 틀리면 닫는다. LOCAL(프록시의 건강 검사)은 프록시 주소 그대로 연다.
 *          - 위 층은 연결이 열린 뒤에만 보내므로 보내기는 그대로 통과한다. `getRemoteAddress` 는 실제 클라이언트 주소다.
 *          머리는 TLS · WebSocket 보다 먼저 오므로 이 장식자가 플랫폼 전송 바로 위, 다른 장식자 아래에 선다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/StreamTransportDecorator.h"

namespace sw
{
    class SW_API ProxyProtocolAcceptTransport final : public StreamTransportDecorator
    {
    public:
        /**
         * @param pInner 감쌀 전송(소유권을 받는다)
         * @param listTrustedProxy 머리를 믿을 프록시의 IPv4 주소(포트는 보지 않는다). 비면 모든 연결을 닫는다 — 프록시 없는 배치는 이 장식자를 세우지 않는다.
         */
        ProxyProtocolAcceptTransport( unique_ptr<IStreamTransport> pInner, vector<uint32> listTrustedProxy );
        ~ProxyProtocolAcceptTransport() override;

        // 덮어쓰지 않은 함수(shutdown · listen · connect · send · close · pollIO · getStats …)는 기반이 안쪽 전송에 그대로 넘긴다.
        [[nodiscard]] bool initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings ) override;
        NetAddress         getRemoteAddress( StreamConnectionHandle handle ) const override;

        /** @brief @p address 가 신뢰 프록시면 true 입니다. */
        [[nodiscard]] bool isTrustedProxy( const NetAddress& address ) const;

    private:
        /** @brief 받은 연결 하나입니다. 받기 길(한 연결에 한 스레드)만 바꾼다. */
        struct Connection
        {
            vector<uint8> _header{};              ///< 머리가 다 올 때까지 받은 바이트
            NetAddress    _proxy{};               ///< 프록시 주소(안쪽 전송이 본 주소)
            NetAddress    _client{};              ///< 머리가 알려 준 실제 주소. 열기 전에는 비어 있다
            uint8         _bOpened{ SW_FALSE };   ///< 위로 연결을 알렸다
            uint8         _bRejected{ SW_FALSE }; ///< 닫기를 걸었다 — 남은 바이트 · 닫힘을 위로 알리지 않는다
        };

        class InnerHandler final : public IStreamHandler
        {
        public:
            explicit InnerHandler( ProxyProtocolAcceptTransport& owner )
                : _owner{ owner }
            {
            }

            void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override { _owner.handleOpened( handle, remote, bAccepted ); }
            void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override { _owner.handleReceived( handle, pData, size ); }
            void onStreamWritable( StreamConnectionHandle handle ) override { _owner._pHandler->onStreamWritable( handle ); }
            void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override { _owner.handleClosed( handle, reason ); }

        private:
            ProxyProtocolAcceptTransport& _owner;
        };

        void                   handleOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted );
        void                   handleReceived( StreamConnectionHandle handle, const uint8* pData, int32 size );
        void                   handleClosed( StreamConnectionHandle handle, StreamCloseReason reason );
        shared_ptr<Connection> findConnection( StreamConnectionHandle handle ) const;

    private:
        InnerHandler                                  _innerHandler;
        IStreamHandler*                               _pHandler;
        vector<uint32>                                _listTrustedProxy;
        mutable mutex                                 _mutex;         ///< `_mapConnection` 만 지킨다(안쪽 · 위를 부르는 동안 쥐지 않는다)
        unordered_map<uint64, shared_ptr<Connection>> _mapConnection; ///< 받은 연결만(키 `StreamConnectionHandle::packed`)
    };
} // namespace sw
