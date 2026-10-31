/**
 * @file ServerApp.h
 * @brief 전용 서버 실행 파일의 본체입니다. 창과 GPU 없이 엔진을 서버 역할로 시작하고, 게임 모듈을 고정 틱으로 돌리고, 종료 요청이 오면 정상 종료합니다.
 * @details App(플레이어 실행 파일)과 따로 둡니다(언리얼의 `<Game>Server` 와 유니티 Dedicated Server 빌드와 같은 구조입니다). 같이 쓰는 것은 `Source/GameModuleManager` 의 것 —
 *          모듈 호스트와 매니페스트 해석, 호스트 범위 세우기(`ModuleLoaderSubsystem::initializeHostScope`), 핫 리로드 구동기(`ModuleReloadDriver` — 개발 명령 `module.reload` 와
 *          감시 값 `Module.*` 도 따라온다), 표준 입력 명령 채널(`StandardInputCommands` — 종료 낱말 `quit` · `stop`)입니다.
 *
 *          기동 순서는 다음과 같다. 호스트 범위(모듈 라이브러리 창구 · 종료 신호 처리기), 엔진(`EngineHostRole::DedicatedServer`, 기동 테이블의 Client 단계를 건너뛴다), 서버 설정,
 *          호스트 서비스(`HostService` 의 의존 선언 순서 — 관측, 크래시 묶음, 서버 문맥, 게임 인스턴스(창과 디바이스는 nullptr), 개발 콘솔, 디버그 채널, 표준 입력), 서버 서비스,
 *          "Dedicated server ready" 로그, 고정 틱 루프, 종료 요청, 서버 서비스 멈춤, 호스트 서비스 역순 종료, "Dedicated server shutdown complete" 로그, 엔진 종료, 호스트 범위 종료다.
 *          한 틱은 게임 고정 스텝 하나이며, 델타는 항상 `1 / _tickRateHz` 다. 그래서 서버 시뮬레이션이 실제 시계의 흔들림을 받지 않는다.
 *          다음 틱 마감까지 잠을 자고, 밀리면 `_maxCatchUpTicks` 까지 잠 없이 몰아서 돌고 그 이상은 버린다(경고를 남긴다).
 *
 *          운영 관측을 위해 지표 레지스트리(`server_tick_seconds`, `server_dropped_ticks_total`)와 상태 확인(틱마다 박동하고, 종료 요청이 오면 비우는 중으로 바꾼다)을 항상 가진다.
 *          설정의 `_opsPort` 가 0 이 아니면 운영 HTTP 엔드포인트(`/metrics`, `/healthz`, `/readyz`, 기본 바인드는 `127.0.0.1`)를 연다.
 *          프로세스 지표(`process_resident_memory_bytes`, `server_service_connections`)는 초에 한 번 갱신한다. 관측 전부는 협력 객체 `ServerObservability` 가 든다.
 *          게임 인스턴스를 만든 뒤 모듈이 올린 서버 서비스(`ServerServiceRegistry`, 예: 온라인 서비스 호스트)를 시작하고 서버 루프마다 돌린다. 서비스는 지표와 상태
 *          레지스트리를 `ServerServiceContext` 로 받는다. 서비스 코드는 모듈에 있으므로 모듈을 언로드하기 전(리로드, 종료)에 멈춘다.
 *          Dev 빌드에서는 `-gv_debugChannelPort` · `-gv_debugChannelToken` 으로 디버그 채널(루프백 · 토큰, 개발 명령 원격 실행)을 열고, `-gv_stateHashHistory` 로 틱별 상태 해시를 남긴다(S3 · S7).
 *          Dev 빌드에서는 콘솔 명령 `pause`, `resume`, `step [N]` 으로 게임 틱을 멈추고 한 틱씩 돌릴 수 있다. 디버거 없이 중단점처럼 쓰는 기능이며, 네트워크 스레드는 계속 돌아 연결이 유지된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Console/StandardInputCommands.h"
#include "Engine/DedicatedServer/ServerServiceRegistry.h"
#include "Engine/Engine.h"
#include "Engine/HostServices.h"
#include "Engine/Module/ModuleCatalog.h"

#include "ModuleManager/HostDevTools.h"
#include "ModuleManager/ModuleReloadDriver.h"

#include "Server/ServerAdminChannel.h"
#include "Server/ServerDebugControl.h"
#include "Server/ServerObservability.h"

namespace sw
{
    enum class ShutdownCause : uint8;

    struct ServerConfig;

    class GameModuleManager;
    class ModuleLoaderSubsystem;
    class ServerServiceManager;

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
        /** @brief 기동 단계 `ModuleLoadStage` 의 호스트 로더입니다. 서버 대상 모듈의 이미지를 로드합니다(App::loadModuleLibrarys 에 해당합니다). */
        [[nodiscard]] bool loadModuleLibrarys();
        [[nodiscard]] bool loadServerConfig();
        /** @brief 호스트 서비스 하나를 세웁니다(`kArrServerHostService` — 순서는 `HostService` 의 의존 선언이 정한다). */
        [[nodiscard]] bool startHostService( HostServiceID id );
        /** @brief `startHostService` 가 세운 서비스 하나를 내립니다. 호스트 서비스 집합이 역순으로 부릅니다. */
        void stopHostService( HostServiceID id );
        /** @brief 서버 서비스 연결 수입니다(서비스가 없으면 0). 관측 · 드레인 · 상태가 읽는다. */
        [[nodiscard]] int32 getServiceConnectionCount() const;
        /** @brief 레지스트리에 올라온 서버 서비스(온라인 서비스 호스트 등)를 시작합니다. 하나라도 실패하면 false 입니다(기동 실패). */
        [[nodiscard]] bool startServerServices();
        /**
         * @brief 지난 실행이 남긴 크래시 파일을 묶음 폴더(`Saved/CrashReports`)로 묶고 지표 `server_crash_reports` 에 남은 묶음 수를 적습니다.
         * @details 클라이언트의 `Telemetry` 기동 단계(동의는 플레이어 설정)를 서버는 돌지 않으므로 여기서 묶습니다. 동의는 `Local`(기계 안에만)입니다.
         *          동의는 서버 설정 `_crashReportConsent`(`local` 기본 · `send` — 묶자마자 보낼 줄에 선다)이고 서버에 묻는 사람이 없어 `ask` 는 오류입니다.
         *          묶음 `crash.txt` 끝에는 크래시 시점의 최근 틱 이력이 들어 있습니다(`ServerFrameHistory::writeCrashTail`). 설정이 틀리면 false 입니다.
         */
        [[nodiscard]] bool collectPreviousCrashes();
        /** @brief 종료 요청이 처음 보였을 때 드레인(서버 운영 O3)을 시작합니다. `/readyz` 를 503 으로 바꾸고 접속이 빌 때까지의 기한을 정합니다. */
        void beginDrain( ShutdownCause cause, int64 monotonicMs );
        /** @brief 드레인이 기다리는 것의 수입니다 — 서비스 연결 + 게임 UDP 접속(O3) + 키트의 진행 중인 일(`IServerService::getOpenWorkCount`). 0 이면 조용히 내려갈 수 있다. */
        [[nodiscard]] int32 countDrainBlockers() const;
        /** @brief 드레인이 끝났는지입니다 — 기다릴 이유가 없거나(접속 0 · 기다림 0 초) 기한이 지났으면 true. */
        [[nodiscard]] bool isDrainFinished( int64 monotonicMs ) const;
        /** @brief 모듈 리로드 직전입니다. 서버 서비스의 코드는 그 모듈에 있으므로 먼저 멈춥니다(게임 쪽은 모듈 호스트의 배치 콜백이 뒤이어 한다). 다음 틱에 다시 시작합니다. */
        bool onBeforeModuleReload( const vector<string>& listModuleName );
        void updateOnce( float32 deltaSeconds );
        /** @brief 콘솔 명령 한 줄(종료 낱말은 `StandardInputCommands` 가 먼저 거른다)을 이 스레드(게임 스레드)에서 처리합니다. */
        void executeCommand( string_view command );
        void logStatus() const;

    private:
        /**
         * @brief 호스트 범위입니다 — 엔진 범위의 부모이고, 엔진보다 먼저 서서 나중에 사라집니다(그래서 `_engine` 보다 앞에 선언한다).
         * @details 모듈 라이브러리 창구(`ModuleLoaderSubsystem`)와 리로드 구동기(빌려 건다), 종료 신호 처리기를 듭니다 — 엔진의 `ModuleLoadStage` · `ModuleUnloadStage` 단계가 창구를 찾아 부릅니다.
         *          종료 신호 처리기는 엔진 종료가 끝날 때까지 걸려 있어야 한다(Windows 창 닫기 처리기가 `notifyShutdownComplete` 를 기다린다).
         *          게임이 서 있는 동안은 서버 문맥(`_serverServiceContext`)도 빌려 건다.
         *          서버 서비스 관리자(`_serverServices`)는 이 범위에 넣지 않습니다. 모듈 리로드마다 멈췄다 다시 시작하는 수명이라 범위의 초기화 · 종료 한 쌍과 맞지 않기 때문입니다.
         */
        SubsystemScope _host;
        /** @brief 호스트 범위의 모듈 라이브러리 창구입니다(`_host` 소유 — 빌려 쓴다). */
        ModuleLoaderSubsystem* _pModuleImageHost;
        /** @brief 개발 도구 연결(호스트 범위의 `DevToolsHost` · 프레임 훅 — App 과 같은 것)입니다. 구현 파일은 CMake 가 빌드마다 고른다(`HostDevTools.h`). */
        HostDevTools                  _devTools;
        Engine                        _engine;
        unique_ptr<GameModuleManager> _gameModuleManager;
        /** @brief 모듈 핫 리로드입니다(App 과 같은 구동기). 자신을 `_host` 에 겁니다. Shipping 에서는 아무것도 하지 않습니다. */
        ModuleReloadDriver _moduleReload;
        /** @brief 개발 조종(개발 콘솔 · 디버그 채널 · 일시정지/한 걸음)입니다. 호스트 서비스 `DevConsole` · `DebugChannel` 이 세우고 내린다. Shipping 은 몸통이 비어 있다. */
        ServerDebugControl _debugControl;
        /** @brief 관리 채널(서버 운영 O4 — 점검 모드 · 상태 · 정상 종료)입니다. 호스트 서비스 `DebugChannel` 이 세우고 내린다. 모든 구성(Shipping 포함)이다. */
        ServerAdminChannel    _adminChannel;
        ModuleCatalog         _moduleCatalog;
        ModuleResolution      _moduleResolution;
        StandardInputCommands _console; ///< 표준 입력 명령 채널(설정 `_bConsoleInput` 이 켤 때만 시작한다)
        /** @brief 운영 관측(지표 · 상태 · 운영 HTTP · 프레임 이력 · 틱 사용률)입니다. 호스트 서비스 `Observability` 가 세우고 내린다. */
        ServerObservability              _observability;
        unique_ptr<ServerServiceManager> _serverServices;       ///< 모듈이 올린 서버 서비스(온라인 서비스 호스트 등)
        Subscription                     _reloadStopsServices;  ///< 리로드 배치 직전에 서버 서비스를 멈추는 콜백(Dev, `onBeforeModuleReload`)
        string                           _serverConfigPath;     ///< 읽은 서버 설정 파일 경로(서비스가 옆 파일을 찾는다)
        ServerServiceContext             _serverServiceContext; ///< 서버 서비스와 게임 · 키트 모듈이 받는 서버 문맥(호스트 범위에 빌려 건다 — `engine::findSubsystem<ServerServiceContext>()`)
        const ServerConfig*              _pServerConfig;        // ConfigManager 가 소유한다
        int64                            _drainDeadlineMs;      ///< 드레인 기한(단조 밀리초)
        uint64                           _tickCount;
        int64                            _startNanoseconds;
        int64                            _tickNanoseconds;          ///< 한 틱 주기
        int64                            _statusTickSumNanoseconds; ///< 틱 본문 시간 합
        int64                            _statusTickMaxNanoseconds; ///< 틱 본문 시간 최대
        uint32                           _statusTickSampleCount;
        uint32                           _droppedTickCount; ///< 밀려 버린 틱 수(누적)
        /**
         * @brief 엔진 뒤에 서고 엔진 앞에 내려가는 호스트 서비스(관측 · 크래시 묶음 · 서버 문맥 · 게임 · 표준 입력 · 개발 콘솔 · 디버그 채널)입니다.
         * @details 순서는 `HostService` 의 의존 선언이 정한다(App 과 같은 표). 서비스의 몸통이 위 멤버를 쓰므로 맨 뒤에 둔다(소멸이 먼저다).
         */
        SubsystemCollection _hostServices;

        uint8                  _bReady                 : 1;
        uint8                  _bRestartServerServices : 1; ///< 모듈 리로드로 서버 서비스를 멈췄다. 다음 틱에 다시 시작한다
        uint8                  _bDraining              : 1; ///< 종료 요청을 받아 드레인 중이다(`beginDrain`)
        [[maybe_unused]] uint8 _reserved               : 5;
    };
} // namespace sw
