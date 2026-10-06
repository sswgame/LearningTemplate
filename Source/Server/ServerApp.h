/**
 * @file ServerApp.h
 * @brief 전용 서버 실행 파일의 본체입니다 — 창 · GPU 없이 엔진을 서버 역할로 세우고, 게임 모듈을 고정 틱으로 돌리고, 종료 요청에 정상 종료합니다.
 * @details App(플레이어 실행 파일)과 따로 둡니다(언리얼 `<Game>Server` · 유니티 Dedicated Server 빌드와 같은 모양). 같이 쓰는 것은 모듈 호스트와
 *          매니페스트 해석(`AppHost`)뿐입니다.
 *          기동: 엔진(`EngineHostRole::DedicatedServer` — 기동 표의 Client 단계를 건너뛴다) → 서버 설정 → 게임 인스턴스(창 · 디바이스 nullptr)
 *          → 종료 신호 처리기 → 콘솔 입력 → "Dedicated server ready" → 고정 틱 루프 → 종료 요청 → 역순 종료 → "Dedicated server shutdown complete".
 *          한 틱 = 게임 고정 스텝 하나(델타는 늘 `1 / _tickRateHz` — 서버 시뮬레이션이 벽시계 흔들림을 받지 않는다). 다음 틱 마감까지 잠자고,
 *          밀리면 `_maxCatchUpTicks` 까지 잠 없이 몰아 돌고 그 이상은 버린다(경고).
 *          운영 관측: 지표 등록부(`server_tick_seconds` · `server_dropped_ticks_total`)와 상태 확인(틱마다 박동, 종료 요청 때 비우는 중)을 늘 들고,
 *          설정의 `_opsPort` 가 0 이 아니면 운영 HTTP 끝점(`/metrics` · `/healthz` · `/readyz`, 기본 바인드 `127.0.0.1`)을 연다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/EngineLoop.h"
#include "Engine/Module/ModuleCatalog.h"

#include "Server/ServerConsole.h"

namespace sw
{
    struct ServerConfig;

    class DevConsole;
    class IStreamTransport;
    class LiveReloadManager;
    class MetricCounter;
    class MetricHistogram;
    class MetricRegistry;
    class ModuleHost;
    class OpsHttpEndpoint;
    class ServiceHealthRegistry;

    /**
     * @class ServerApp
     * @brief 전용 서버 프로세스 하나입니다. `runMain` 이 initialize → run → shutdown 을 돌리고 종료 코드를 돌려줍니다.
     */
    class ServerApp
    {
    public:
        ServerApp();
        ~ServerApp();

        ServerApp( const ServerApp& )            = delete;
        ServerApp& operator=( const ServerApp& ) = delete;

        /** @brief 프로세스 본문입니다(콘솔 실행 · Windows 서비스 공용). 정상 종료면 0 입니다. */
        static int32 runMain( int32 argc, utf8* pArgv[] );

        /** @brief 엔진 · 설정 · 모듈 · 게임을 세웁니다. 헤드리스 작업(서버 팩의 씬 쿠킹)이면 그것으로 끝납니다(`run` 이 바로 돌아온다). */
        [[nodiscard]] bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 종료 요청까지 고정 틱으로 돕니다. */
        void run();
        /** @brief 세운 역순으로 내립니다. 일부만 섰어도 됩니다. */
        void shutdown();

    private:
        /** @brief 기동 단계 `ModuleTypes` 의 호스트 로더 — 서버 대상 모듈의 이미지를 올린다(App::loadModuleImages 와 같은 자리). */
        [[nodiscard]] bool loadModuleImages();
        [[nodiscard]] bool loadServerConfig();
        /** @brief 지표 · 상태 등록부를 세우고, 설정이 켜면 운영 HTTP 끝점을 엽니다. 켰는데 열지 못하면 false(기동 실패)입니다. */
        [[nodiscard]] bool initializeObservability();
        void               shutdownObservability();
        LiveReloadManager* getLiveReloadManager() const;
        void               tickOnce( float32 deltaSeconds );
        /** @brief 콘솔 명령 한 줄을 이 스레드(게임 스레드)에서 처리합니다. */
        void executeCommand( string_view line );
        void logStatus() const;

    private:
        EngineLoop             _engineLoop;
        unique_ptr<ModuleHost> _moduleHost;
#if !defined( SW_SHIPPING )
        unique_ptr<LiveReloadManager> _liveReloadManager;
        unique_ptr<DevConsole>        _devConsole; ///< Dev 서버는 개발 콘솔과 같은 명령(개발 명령 · gv_*)을 표준 입력으로 받는다
#endif
        ModuleCatalog                     _moduleCatalog;
        ModuleResolution                  _moduleResolution;
        ServerConsole                     _console;
        unique_ptr<MetricRegistry>        _metricRegistry;
        unique_ptr<ServiceHealthRegistry> _healthRegistry;
        unique_ptr<IStreamTransport>      _opsTransport; ///< 운영 HTTP 의 스트림 전송(끝점이 빌려 쓴다 — 끝점을 내린 뒤 지운다)
        unique_ptr<OpsHttpEndpoint>       _opsEndpoint;
        vector<string>                    _listPendingCommand;  ///< 콘솔에서 꺼낸 줄(틱마다 재사용)
        const ServerConfig*               _pServerConfig;       // ConfigManager 가 소유한다
        MetricHistogram*                  _pTickHistogram;      ///< server_tick_seconds — 틱 본문 시간
        MetricCounter*                    _pDroppedTickCounter; ///< server_dropped_ticks_total
        uint64                            _tickCount;
        int64                             _startNanoseconds;
        int64                             _tickNanoseconds;          ///< 한 틱 주기
        int64                             _statusTickSumNanoseconds; ///< 틱 본문 시간 합
        int64                             _statusTickMaxNanoseconds; ///< 틱 본문 시간 최대
        uint32                            _statusTickSampleCount;
        uint32                            _droppedTickCount; ///< 밀려 버린 틱 수(누적)

        uint8                  _bReady                  : 1;
        uint8                  _bSignalHandlerInstalled : 1;
        [[maybe_unused]] uint8 _reserved                : 6;
    };
} // namespace sw
