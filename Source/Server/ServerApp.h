/**
 * @file ServerApp.h
 * @brief 전용 서버 실행 파일의 본체입니다. 창과 GPU 없이 엔진을 서버 역할로 시작하고, 게임 모듈을 고정 틱으로 돌리고, 종료 요청이 오면 정상 종료합니다.
 * @details App(플레이어 실행 파일)과 따로 둡니다(언리얼의 `<Game>Server` 와 유니티 Dedicated Server 빌드와 같은 구조입니다). 같이 쓰는 것은 모듈 호스트와
 *          매니페스트 해석(`ModuleHost`)뿐입니다.
 *
 *          기동 순서는 다음과 같다. 엔진(`EngineHostRole::DedicatedServer`, 기동 테이블의 Client 단계를 건너뛴다), 서버 설정, 게임 인스턴스(창과 디바이스는 nullptr),
 *          종료 신호 처리기, 콘솔 입력, "Dedicated server ready" 로그, 고정 틱 루프, 종료 요청, 역순 종료, "Dedicated server shutdown complete" 로그다.
 *          한 틱은 게임 고정 스텝 하나이며, 델타는 항상 `1 / _tickRateHz` 다. 그래서 서버 시뮬레이션이 실제 시계의 흔들림을 받지 않는다.
 *          다음 틱 마감까지 잠을 자고, 밀리면 `_maxCatchUpTicks` 까지 잠 없이 몰아서 돌고 그 이상은 버린다(경고를 남긴다).
 *
 *          운영 관측을 위해 지표 레지스트리(`server_tick_seconds`, `server_dropped_ticks_total`)와 상태 확인(틱마다 박동하고, 종료 요청이 오면 비우는 중으로 바꾼다)을 항상 가진다.
 *          설정의 `_opsPort` 가 0 이 아니면 운영 HTTP 엔드포인트(`/metrics`, `/healthz`, `/readyz`, 기본 바인드는 `127.0.0.1`)를 연다.
 *          Dev 빌드에서는 콘솔 명령 `pause`, `resume`, `step [N]` 으로 게임 틱을 멈추고 한 틱씩 돌릴 수 있다. 디버거 없이 중단점처럼 쓰는 기능이며, 네트워크 스레드는 계속 돌아 연결이 유지된다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/StandardInputReader.h"

#include "Engine/EngineLoop.h"
#include "Engine/Module/ModuleCatalog.h"

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
    class OpsHTTPEndpoint;
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
        /** @brief 만든 역순으로 종료합니다. 일부만 만들어졌어도 됩니다. */
        void shutdown();

    private:
        /** @brief 기동 단계 `ModuleTypes` 의 호스트 로더입니다. 서버 대상 모듈의 이미지를 로드합니다(App::loadModuleImages 에 해당합니다). */
        [[nodiscard]] bool loadModuleImages();
        [[nodiscard]] bool loadServerConfig();
        /** @brief 지표와 상태 레지스트리를 만들고, 설정이 켜면 운영 HTTP 엔드포인트를 엽니다. 켰는데 열지 못하면 false 를 반환합니다(기동 실패). */
        [[nodiscard]] bool initializeObservability();
        void               shutdownObservability();
        LiveReloadManager* getLiveReloadManager() const;
        void               tickOnce( float32 deltaSeconds );
        /** @brief 콘솔 명령 한 줄을 이 스레드(게임 스레드)에서 처리합니다. */
        void executeCommand( string_view line );
        /** @brief Dev 의 일시정지 명령(`pause` · `resume` · `step [N]`)이면 처리하고 true 입니다. Shipping 은 늘 false 입니다. */
        bool executePauseCommand( string_view command );
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
        StandardInputReader               _console;
        unique_ptr<MetricRegistry>        _metricRegistry;
        unique_ptr<ServiceHealthRegistry> _healthRegistry;
        unique_ptr<IStreamTransport>      _opsTransport; ///< 운영 HTTP 의 스트림 전송(엔드포인트가 빌려 쓰므로 엔드포인트를 종료한 뒤 지운다).
        unique_ptr<OpsHTTPEndpoint>       _opsEndpoint;
        vector<string>                    _listPendingCommand;  ///< 콘솔에서 꺼낸 줄(틱마다 재사용)
        const ServerConfig*               _pServerConfig;       // ConfigManager 가 소유한다
        MetricHistogram*                  _pTickHistogram;      ///< server_tick_seconds. 틱 본문 시간.
        MetricCounter*                    _pDroppedTickCounter; ///< server_dropped_ticks_total
        uint64                            _tickCount;
        int64                             _startNanoseconds;
        int64                             _tickNanoseconds;          ///< 한 틱 주기
        int64                             _statusTickSumNanoseconds; ///< 틱 본문 시간 합
        int64                             _statusTickMaxNanoseconds; ///< 틱 본문 시간 최대
        uint32                            _statusTickSampleCount;
        uint32                            _droppedTickCount; ///< 밀려 버린 틱 수(누적)
        uint32                            _pendingStepCount; ///< 일시정지 중 더 돌 틱 수(`step N`)

        uint8                  _bReady                  : 1;
        uint8                  _bSignalHandlerInstalled : 1;
        uint8                  _bPaused                 : 1; ///< Dev `pause`. 게임 틱을 멈춘다(콘솔, 종료 신호, 네트워크 스레드는 돈다).
        [[maybe_unused]] uint8 _reserved                : 5;
    };
} // namespace sw
