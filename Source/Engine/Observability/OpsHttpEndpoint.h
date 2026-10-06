/**
 * @file OpsHttpEndpoint.h
 * @brief 운영 HTTP 끝점 — 스트림 전송 위의 아주 작은 HTTP/1.1(GET 만, 요청 하나 뒤 닫기): `/metrics`(Prometheus 텍스트) · `/healthz` · `/readyz`.
 * @details - 전송의 처리기가 되어 I/O 스레드에서 요청 머리를 읽고 바로 답한다(서비스 스레드를 기다리지 않는다 — 지표는 원자 값, 상태는 잠금 하나).
 *          - 머리가 `_maxRequestBytes` 를 넘으면 431, GET 이 아니면 405, 모르는 경로는 404, 요청 줄이 깨지면 400 — 모두 답한 뒤 닫는다.
 *          - 평문 · 따로 포트 · **기본 바인드는 이 기계만**(`NetAddress::makeLoopback`) — Prometheus 스크레이프 · 감시자는 같은 기계나 사설망에서 온다.
 *            다른 기계에 열려면 설정에서 사설 주소를 준다(공용 인터페이스에 열지 말 것 — 인증이 없다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    class MetricRegistry;
    class ServiceHealthRegistry;
} // namespace sw

namespace sw
{
    /** @brief 운영 HTTP 끝점 설정입니다. */
    struct OpsHttpEndpointSettings
    {
        NetAddress _bindAddress{ NetAddress::makeLoopback( 9100 ) }; ///< 받을 주소 — 기본은 이 기계만. 포트 0 = 아무 포트(시험)
        int32      _maxRequestBytes{ 8 * 1024 };                     ///< 요청 머리 상한(넘으면 431)
    };
} // namespace sw

namespace sw
{
    /** @class OpsHttpEndpoint @brief 운영 HTTP 끝점입니다. 전송은 빌려 쓴다(부르는 쪽이 만들고, 이것을 내린 뒤 지운다). */
    class SW_API OpsHttpEndpoint final : public IStreamHandler
    {
    public:
        OpsHttpEndpoint();
        ~OpsHttpEndpoint() override;

        OpsHttpEndpoint( const OpsHttpEndpoint& )            = delete;
        OpsHttpEndpoint& operator=( const OpsHttpEndpoint& ) = delete;

        /** @brief 전송의 처리기가 되어 설정의 주소에서 받습니다. 등록부 둘은 빌려 쓴다(null 이면 그 경로는 404). */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const StreamTransportSettings& transportSettings, const OpsHttpEndpointSettings& settings,
                                       const MetricRegistry* pMetricRegistry, const ServiceHealthRegistry* pHealthRegistry );
        /** @brief 전송을 내립니다(남은 연결의 `onStreamClosed` 가 여기서 끝난다). 두 번 불러도 됩니다. */
        void   shutdown();
        uint16 getListenPort() const;

        /**
         * @brief 요청 머리(빈 줄 앞까지) 하나에 대한 응답 전체를 @p outResponse 에 씁니다 — 전송 없이 시험한다.
         * @param monotonicMs 상태 판정 시각(단조 밀리초)
         * @return HTTP 상태 코드
         */
        static int32 buildResponse( string_view requestHead, const MetricRegistry* pMetricRegistry, const ServiceHealthRegistry* pHealthRegistry, int64 monotonicMs,
                                    string& outResponse );

        // IStreamHandler — I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        struct Connection
        {
            string _buffer{};
            uint64 _packedHandle{ 0 };
            uint8  _bAnswered{ SW_FALSE };
        };

        Connection* findConnection( uint64 packedHandle );

        vector<Connection>           _listConnection;
        OpsHttpEndpointSettings      _settings;
        mutex                        _mutex;
        IStreamTransport*            _pTransport;
        const MetricRegistry*        _pMetricRegistry;
        const ServiceHealthRegistry* _pHealthRegistry;
    };
} // namespace sw
