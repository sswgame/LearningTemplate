/**
 * @file ServerObservability.h
 * @brief 전용 서버의 운영 관측 — 지표 · 상태 레지스트리, 운영 HTTP 엔드포인트, 프로세스 지표, 틱 지표, 프레임 이력(O1)과 느린 틱 덤프, 틱 사용률(O5).
 * @details `ServerApp` 이 호스트 서비스 `Observability` 로 세우고 내리는 협력 객체다(언리얼 전용 서버의 FPerfCounters · CSV 프로파일러가 엔진 루프 밖 모듈인 것과 같은 자리).
 *          서버 루프는 틱마다 `recordTick`, 초마다 `updateEverySecond` 를 부르고, 드레인 · 점검 모드는 `getHealthRegistry` 로 `/readyz` 를 바꾼다.
 *          서버 서비스와 게임 · 키트는 지표와 상태 레지스트리를 서버 문맥(`ServerServiceContext`)으로 받는다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct ServerConfig;

    class IStreamTransport;
    class MetricCounter;
    class MetricGauge;
    class MetricHistogram;
    class MetricRegistry;
    class OpsHTTPEndpoint;
    class ProcessMetrics;
    class ServerFrameHistory;
    class ServiceHealthRegistry;

    /**
     * @class ServerObservability
     * @brief 전용 서버의 운영 관측입니다. 파일 머리말 참고.
     */
    class ServerObservability
    {
    public:
        ServerObservability();
        ~ServerObservability();

        ServerObservability( const ServerObservability& )            = delete;
        ServerObservability& operator=( const ServerObservability& ) = delete;

        /**
         * @brief 지표와 상태 레지스트리를 만들고, 설정이 켜면 운영 HTTP 엔드포인트를 엽니다. 켰는데 열지 못하면 false 를 반환합니다(기동 실패).
         * @param processStartNanoseconds 프로세스 기동 시각(단조 나노초) — 프로세스 지표의 시작 시각을 벽시계로 옮길 때 쓴다.
         */
        [[nodiscard]] bool initialize( const ServerConfig& config, int64 processStartNanoseconds );
        /** @brief 운영 엔드포인트를 닫고 모든 것을 지웁니다. 일부만 섰어도 됩니다. 로그를 남기므로 엔진(로거)보다 먼저 부릅니다. */
        void shutdown();

        MetricRegistry*        getMetricRegistry() const { return _metricRegistry.get(); }
        ServiceHealthRegistry* getHealthRegistry() const { return _healthRegistry.get(); }

        /** @brief 틱 사용률 창을 지금부터 셉니다(서버가 준비된 순간). */
        void beginLoadWindow( int64 monotonicMs );
        /**
         * @brief 틱 하나를 끝낸 뒤 부릅니다. 틱 지표 · 상태 박동을 남기고, 프레임 이력(O1)에 더하고, 느린 틱이면 이력을 덤프하고, 틱 사용률 창(O5)에 더합니다.
         * @param tickNanoseconds 틱 본문 시간
         * @param tickCount 끝낸 틱 수(덤프 파일 이름 · 이력 표본)
         * @param connectionCount 서버 서비스가 받고 있는 연결 수
         * @param droppedTickTotal 밀려 버린 틱 수(누적)
         * @param monotonicMs 지금(단조 밀리초)
         */
        void recordTick( int64 tickNanoseconds, uint64 tickCount, int32 connectionCount, uint32 droppedTickTotal, int64 monotonicMs );
        /** @brief 밀려 버린 틱 @p dropped 개를 `server_dropped_ticks_total` 에 더합니다. */
        void addDroppedTicks( uint32 dropped );
        /** @brief 틱을 돌지 않아도(일시정지) 서버가 살아 있다고 상태 확인에 박동합니다. */
        void markAlive( int64 monotonicMs );
        /** @brief 프로세스 지표(메모리 · 서비스 연결 수)와 틱 사용률을 초에 한 번 갱신합니다. 간격 전이면 아무 일도 하지 않습니다. */
        void updateEverySecond( int64 monotonicMs, int32 connectionCount );

    private:
        /** @brief 초마다 부릅니다 — 틱 사용률 지표 · 경고(O5)를 갱신합니다. */
        void updateLoad( int64 monotonicMs );
        /** @brief 프레임 이력과 메시지 종류별 누적 셈을 `Saved/ServerReports/frames_<세션>_<틱>_<까닭>.txt` 로 씁니다. 이력이 비었거나 폴더를 만들 수 없으면 경고만 남깁니다. */
        void dumpFrameHistory( const utf8* pReason, uint64 tickCount );

        const ServerConfig*               _pServerConfig; ///< ConfigManager 가 소유한다
        unique_ptr<MetricRegistry>        _metricRegistry;
        unique_ptr<ServiceHealthRegistry> _healthRegistry;
        unique_ptr<IStreamTransport>      _opsTransport; ///< 운영 HTTP 의 스트림 전송(엔드포인트가 빌려 쓰므로 엔드포인트를 종료한 뒤 지운다).
        unique_ptr<OpsHTTPEndpoint>       _opsEndpoint;
        unique_ptr<ProcessMetrics>        _processMetrics;
        MetricHistogram*                  _pTickHistogram;             ///< server_tick_seconds. 틱 본문 시간.
        MetricCounter*                    _pDroppedTickCounter;        ///< server_dropped_ticks_total
        MetricGauge*                      _pServiceConnectionGauge;    ///< server_service_connections. 서버 서비스가 받고 있는 연결 수
        MetricGauge*                      _pTickUtilizationGauge;      ///< server_tick_utilization_ratio. 최근 1 초 틱 본문 시간 ÷ 흐른 시간(O5)
        unique_ptr<ServerFrameHistory>    _frameHistory;               ///< 최근 N 틱 기록(O1)
        Subscription                      _crashExtraTextSubscription; ///< 크래시 문맥 파일에 `_frameHistory` 를 싣는 함수의 구독(이력보다 먼저 푼다)
        int64                             _lastFrameDumpMs;            ///< 마지막 덤프의 단조 밀리초(느린 틱 덤프의 간격 제한)
        int64                             _loadWindowStartMs;          ///< 틱 사용률 창의 시작
        int64                             _loadWindowBusyNanoseconds;  ///< 창 안의 틱 본문 시간 합
        int64                             _lastLoadWarningMs;          ///< 마지막 사용률 경고(1 분에 한 번)
        int64                             _nextProcessSampleMs;        ///< 프로세스 지표를 다시 읽을 단조 밀리초
    };
} // namespace sw
