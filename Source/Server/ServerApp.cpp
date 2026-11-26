/**
 * @file ServerApp.cpp
 * @brief 전용 서버 실행 파일의 본체와 그 진입(`ServerEntry::run`)입니다. 루프 클래스는 이 TU 안에만 있다 — 밖에 보이는 것은 `ServerEntry` 하나다. 창과 GPU 없이 엔진을 서버 역할로 시작하고, 게임 모듈을 고정 틱으로 돌리고, 종료 요청이 오면 정상 종료합니다.
 * @details App(플레이어 실행 파일)과 따로 둡니다(언리얼의 `<Game>Server` 와 유니티 Dedicated Server 빌드와 같은 구조입니다). 같이 쓰는 것은 `Source/GameModuleManager` 의 것 —
 *          모듈 호스트와 매니페스트 해석, 부팅 열(`HostLaunch` — 호스트 범위 · 엔진 · 모듈 관리자 · 호스트 서비스의 순서), 개발 계층(`IHostDevLayer` — 핫 리로드 · 개발 명령 `module.reload` 와
 *          감시 값 `Module.*` 도 따라온다), 표준 입력 명령 채널(`StandardInputCommands` — 종료 낱말 `quit` · `stop`)입니다.
 *
 *          기동 순서는 다음과 같다. 호스트 범위(모듈 라이브러리 창구 · 종료 신호 처리기), 엔진(`EngineHostRole::DedicatedServer`, 기동 테이블의 Client 단계를 건너뛴다), 서버 설정,
 *          호스트 서비스(각 서비스 형식의 의존 선언 순서 — 관측, 크래시 묶음, 서버 문맥, 게임 인스턴스(창과 디바이스는 nullptr), 개발 콘솔, 디버그 채널, 표준 입력), 서버 서비스,
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
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Log/Sink/ConsoleLogOutput.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Process/Process.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/String/fixed_string.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Config/ServerConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Console/StandardInputCommands.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Network/GameConnectSecurity.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/ProcessMetrics.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneObjectHash.h"
#include "Engine/Server/OpsHTTPEndpoint.h"
#include "Engine/Server/ServerAdminChannel.h"
#include "Engine/Server/ServerObservability.h"
#include "Engine/Server/ServerServiceManager.h"
#include "Engine/Server/ServerServiceRegistry.h"
#include "Engine/Server/ServiceHealthRegistry.h"
#include "Engine/Telemetry/CrashReportSubsystem.h"

#include "ModuleAPI/Service/IServerKitLocator.h"

#include "ModuleManager/GameModuleManager.h"
#include "ModuleManager/Host/HostDevLayer.h"
#include "ModuleManager/Host/HostLaunch.h"
#include "ModuleManager/Host/ServerDebugControl.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

#include "Server/ServerEntry.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ProcessExitCodes.gen.h"

namespace sw
{
    enum class ShutdownCause : uint8;

    struct ServerConfig;

    class ServerServiceManager;

    /**
     * @class ServerKitLocator
     * @brief 호스트가 소유한 키트 창구(`ServerKitDirectory`, Engine)를 게임 모듈이 보는 형식(`IServerKitLocator`, ModuleAPI)으로 비춥니다 — 엔진은 ModuleAPI 를 보지 않는다.
     */
    class ServerKitLocator final : public IServerKitLocator
    {
    public:
        explicit ServerKitLocator( ServerKitDirectory& directory )
            : _directory{ directory }
        {
        }

        [[nodiscard]] Subscription subscribeBeforeWithdraw( const Delegate<void()>& onBeforeWithdraw ) override { return _directory.subscribeBeforeWithdraw( onBeforeWithdraw ); }

    private:
        const ISubsystemLocator* findKitScope() const override { return _directory.findKitScope(); }

    private:
        ServerKitDirectory& _directory;
    };

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
        /** @brief 기동을 마치고 루프에 들어가기 직전에 불리는 함수입니다(서비스 호스트가 그때 RUNNING 을 알린다). 없으면 nullptr 을 넘긴다. */
        using ReadyFunction = ServerEntry::ReadyFunction;
        static int32 runMain( int32 argc, utf8* pArgv[], ReadyFunction pfnOnReady );

        /** @brief 엔진 · 설정 · 모듈 · 게임을 세웁니다. 헤드리스 작업(서버 팩의 씬 쿠킹)이면 그것으로 끝납니다(`run` 이 바로 돌아온다). */
        [[nodiscard]] bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 종료 요청까지 고정 틱으로 돕니다. */
        void run();
        /** @brief 만든 역순으로 종료합니다. 일부만 만들어졌어도 됩니다. */
        void shutdown();

    private:
        /** @brief 기동 단계 `ModuleLoadStage` 의 호스트 로더입니다. 서버 대상 모듈의 라이브러리를 로드하고 리로드 전 콜백을 겁니다(App::loadModuleLibraries 에 해당합니다). */
        [[nodiscard]] bool loadModuleLibraries();
        [[nodiscard]] bool loadServerConfig();
        /**
         * @brief 읽은 설정의 칸 사이 규칙을 서비스를 세우지 않고 봅니다(`--check-config` · 기동이 같은 검사를 지난다). 어긴 규칙마다 오류를 남기고, 하나라도 있으면 false 입니다.
         * @details 운영 포트 바인드 · 토큰, 관리 채널 토큰, 크래시 보고 동의. 배포 전에 CI · 이미지 빌드(`Scripts/package/server_image.py`)가 돌린다(언리얼 `-run=` 커맨드릿 · 서버 `-validate` 자리).
         */
        [[nodiscard]] static bool validateServerConfig( const ServerConfig& config );
        // 호스트 서비스 — 이름 · 의존 · 몸통을 한 형식에 둔다(`ServerApp.cpp`). 중첩 형식이라 ServerApp 의 멤버를 쓴다. `initialize` 가 `HostLaunch::addHostSubsystem` 로 더한다.
        class ObservabilityHostSubsystem;
        class CrashReportsHostSubsystem;
        class ServiceContextHostSubsystem;
        class GameHostSubsystem;
        class StandardInputHostSubsystem;
        class DevConsoleHostSubsystem;
        class DebugChannelHostSubsystem;
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
        /** @brief 드레인이 기다리는 것의 수입니다 — 서비스 연결 + 게임 UDP 접속(O3) + 키트의 진행 중인 일(`IServerService::getOpenJobCount`). 0 이면 조용히 내려갈 수 있다. */
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
         * @brief 부팅 열입니다 — 호스트 범위 · 개발 계층 · 엔진 · 모듈 관리자 · 호스트 서비스의 수명과 순서를 듭니다(App 과 같은 것, `HostLaunch`).
         * @details 맨 앞 멤버라 맨 나중에 사라진다. 호스트 서비스의 몸통이 아래 멤버를 쓰므로 `shutdown` 이 그 멤버가 사라지기 전에 호스트 서비스를 내린다.
         *          호스트 범위에는 종료 신호 처리기도 든다 — 엔진 종료가 끝날 때까지 걸려 있어야 한다(Windows 창 닫기 처리기가 `notifyShutdownComplete` 를 기다린다).
         *          게임이 서 있는 동안은 서버 설정(`const ServerConfig`)도 빌려 건다(네트워크 세션이 게임 포트를 찾는다).
         *          서버 서비스 관리자(`_serverServices`)는 이 범위에 넣지 않습니다. 모듈 리로드마다 멈췄다 다시 시작하는 수명이라 범위의 초기화 · 종료 한 쌍과 맞지 않기 때문입니다.
         */
        HostLaunch _launch;
        /** @brief 개발 조종(개발 콘솔 · 디버그 채널 · 일시정지/한 걸음)입니다. 호스트 서비스 `DevConsole` · `DebugChannel` 이 세우고 내린다. 배포 서버는 nullptr 이다(`build::kShipping`). */
        unique_ptr<IServerDebugControl> _pDebugControl;
        /** @brief 관리 채널(서버 운영 O4 — 점검 모드 · 상태 · 정상 종료)입니다. 호스트 서비스 `DebugChannel` 이 세우고 내린다. 모든 구성(Shipping 포함)이다. */
        ServerAdminChannel    _adminChannel;
        StandardInputCommands _console; ///< 표준 입력 명령 채널(설정 `_bConsoleInput` 이 켤 때만 시작한다)
        /** @brief 운영 관측(지표 · 상태 · 운영 HTTP · 프레임 이력 · 틱 사용률)입니다. 호스트 서비스 `Observability` 가 세우고 내린다. */
        ServerObservability              _observability;
        unique_ptr<ServerServiceManager> _serverServices;       ///< 모듈이 올린 서버 서비스(온라인 서비스 호스트 등)
        Subscription                     _reloadStopsServices;  ///< 리로드 배치 직전에 서버 서비스를 멈추는 콜백(Dev, `onBeforeModuleReload`)
        string                           _serverConfigPath;     ///< 읽은 서버 설정 파일 경로(서비스가 옆 파일을 찾는다)
        ServerServiceContext             _serverServiceContext; ///< 서버 서비스가 `initialize` 로 받는 서버 문맥(게임 쪽에는 `_pServerConfig` 와 키트 창구만 호스트 범위에 건다)
        ServerKitDirectory               _kitDirectory;         ///< 서버 키트 창구. 온라인 조립이 공개 · 거둔다(`ServerServiceContext::_pKitDirectory`)
        ServerKitLocator                 _kitLocator;           ///< `_kitDirectory` 를 `IServerKitLocator` 로 호스트 범위에 걸고 게임 모듈 범위가 찾는다
        const ServerConfig*              _pServerConfig;        // ConfigManager 가 소유한다
        int64                            _drainDeadlineMs;      ///< 드레인 기한(단조 밀리초)
        uint64                           _tickCount;
        int64                            _startNanoseconds;
        int64                            _tickNanoseconds;          ///< 한 틱 주기
        int64                            _statusTickSumNanoseconds; ///< 틱 본문 시간 합
        int64                            _statusTickMaxNanoseconds; ///< 틱 본문 시간 최대
        uint32                           _statusTickSampleCount;
        uint32                           _droppedTickCount; ///< 밀려 버린 틱 수(누적)
        uint8                            _bReady                 : 1;
        uint8                            _bRestartServerServices : 1; ///< 모듈 리로드로 서버 서비스를 멈췄다. 다음 틱에 다시 시작한다
        uint8                            _bDraining              : 1; ///< 종료 요청을 받아 드레인 중이다(`beginDrain`)
        uint8                            _bConfigCheckOnly       : 1; ///< `--check-config` — 설정을 읽고 검사한 뒤 서비스를 세우지 않고 끝낸다
        [[maybe_unused]] uint8           _reserved               : 4;
    };
} // namespace sw

namespace sw
{
    namespace
    {
        struct ServerAppInternal
        {
            /** @brief 밀림 경고를 몇 번에 한 번만 남길지 정합니다. 과부하 서버가 로그 때문에 더 느려지지 않게 하기 위해서입니다. */
            static constexpr uint32 kDroppedTickWarningInterval = 100;
            /** @brief 클라이언트 산출물에 없어야 할 표식(`BuildTargetImageTest`)입니다. 서버 실행 파일 자신도 서버 전용입니다. */
            static constexpr utf8 kArrServerImageMarker[] = "sw-server-only-module:Server";
            /** @brief 이 종료 까닭은 접속이 비기를 기다린다. 오케스트레이터의 종료 신호만 — 사람이 내린 것(콘솔 · Ctrl+C) · 테스트 틱 상한 · OS 가 시한을 주는 창 닫기 · 시스템 종료는 바로 내린다. */
            static bool shouldDrainFor( ShutdownCause cause ) { return cause == ShutdownCause::Terminate || cause == ShutdownCause::ServiceStop; }

            static int64 nowMonotonicMs() { return MonotonicClock::nowNanoseconds() / constant::time::kNanosecondsPerMillisecond; }
        };

        /** @brief 링커가 표식 문자열을 버리지 못하게 정적 초기화에서 한 번 읽습니다(서버 전용 모듈의 생성 표식과 같은 방식입니다). */
        struct ServerImageMarker
        {
            ServerImageMarker()
            {
                const volatile utf8* pMarker = ServerAppInternal::kArrServerImageMarker;
                (void)pMarker[0];
            }
        };
        const ServerImageMarker s_serverImageMarker{};

        /**
         * @brief 호스트 범위의 종료 신호 처리기(POSIX SIGTERM · SIGINT, Windows 콘솔 제어)입니다. 엔진보다 먼저 걸고 엔진 종료가 끝난 뒤 뗀다.
         * @details 걸지 못해도 서버는 선다(표준 입력 `quit` 로 끝낸다) — 로거가 서기 전이라 로그 아래 티어(`SW_LOW_LEVEL_WARNING`)로 알린다. 떼기는 건 것이 없으면 아무것도 하지 않는다.
         */
        class ShutdownSignalSubsystem final : public Subsystem
        {
        public:
            SubsystemInitResult initializeSubsystem() override
            {
                if ( ShutdownSignal::install() == false )
                    SW_LOW_LEVEL_WARNING( "Shutdown signal handlers are not installed - stop the server with the 'quit' console command" );
                return SubsystemInitResult::Succeeded;
            }
            void        shutdownSubsystem() override { ShutdownSignal::uninstall(); }
            const utf8* getSubsystemName() const override { return "ShutdownSignal"; }
        };
    } // namespace

    /** @brief 지표 · 상태 레지스트리 · 운영 HTTP · 프로세스 지표입니다. 맨 먼저 서고 맨 나중에 내려간다. */
    class ServerApp::ObservabilityHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "Observability";

        explicit ObservabilityHostSubsystem( ServerApp& server )
            : _server{ server }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            return ( _server._observability.initialize( *_server._pServerConfig, _server._startNanoseconds ) ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        void shutdownSubsystem() override
        {
            // 운영 엔드포인트는 게임을 내리는 동안에도 비우는 중(/readyz 503)을 알리다가 마지막에 닫는다. 로그를 남기므로 엔진(로거)보다 먼저 닫는다.
            _server._observability.shutdown();
        }
        const utf8* getSubsystemName() const override { return kSubsystemName; }

    private:
        ServerApp& _server;
    };

    /** @brief 지난 실행의 크래시를 묶어 지표로 냅니다(남은 묶음 수를 지표로 내므로 관측 뒤). */
    class ServerApp::CrashReportsHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "CrashReports";

        explicit CrashReportsHostSubsystem( ServerApp& server )
            : _server{ server }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            return ( _server.collectPreviousCrashes() ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        void shutdownSubsystem() override
        {
            engine::getSubsystem<CrashReportSubsystem>().shutdown(); // 묶을 폴더가 없어 초기화하지 않았어도 된다
        }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { ObservabilityHostSubsystem::kSubsystemName };
            return kArrDependency;
        }

    private:
        ServerApp& _server;
    };

    /** @brief 서버 문맥을 호스트 범위에 빌려 겁니다 — 문맥이 지표 · 상태 레지스트리를 빌려주므로 관측 뒤, 게임 · 키트가 기동하며 찾으므로 게임 앞. */
    class ServerApp::ServiceContextHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ServiceContext";

        explicit ServiceContextHostSubsystem( ServerApp& server )
            : _server{ server }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            ServerApp& server                              = _server;
            server._serverServiceContext._serverConfigPath = server._serverConfigPath;
            server._serverServiceContext._pServerConfig    = server._pServerConfig;
            server._serverServiceContext._pMetricRegistry  = server._observability.getMetricRegistry();
            server._serverServiceContext._pHealthRegistry  = server._observability.getHealthRegistry();
            server._serverServiceContext._pKitDirectory    = &server._kitDirectory;
            // 게임 쪽(네트워크 세션)에는 공유 형식인 설정과 지표 레지스트리만 건다 — 서버 서비스 문맥은 `Engine/Server/` 형식이라 클라이언트와 함께 짓는 코드가 알면 안 된다.
            engine::getSubsystem<GameNetSession>().bindMetricRegistry( server._observability.getMetricRegistry() ); // 게임 포트 지표(감사 N-7) — 게임이 포트를 열 때 샤드마다 올린다
            // 키트 창구는 게임 모듈 범위에도 걸린다(`GameModuleManager::fillModuleSubsystemScope`) — 게임 서버 코드가 백필 같은 키트 인터페이스를 찾는다.
            // 설정은 `engine::findSubsystem<const ServerConfig>()` 로 찾는다.
            return ( server._launch.getHostScope().bind( server._pServerConfig ) && server._launch.getHostScope().bind<IServerKitLocator>( &server._kitLocator ) ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        void shutdownSubsystem() override
        {
            _server._launch.getHostScope().unbind<const ServerConfig>(); // 게임과 모듈이 내려간 뒤 — 레지스트리를 지우기 전
            _server._launch.getHostScope().unbind<IServerKitLocator>();
            // 세션은 엔진 종료에 닫힌다(관측보다 늦다) — 지운 레지스트리에 쓰지 않게 여기서 푼다.
            engine::getSubsystem<GameNetSession>().bindMetricRegistry( nullptr );
        }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { ObservabilityHostSubsystem::kSubsystemName };
            return kArrDependency;
        }

    private:
        ServerApp& _server;
    };

    /** @brief 게임 인스턴스 — 모듈 호스트입니다(창과 디바이스는 없다). 게임과 키트가 기동하며 서버 지표 · 상태 레지스트리에 자기 것을 올리고 게임 포트를 열므로 서버 문맥 뒤. */
    class ServerApp::GameHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "Game";

        explicit GameHostSubsystem( ServerApp& server )
            : _server{ server }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            const bool bStarted = _server._launch.getModuleManager()->initializeDedicatedServer( _server._launch.getLiveReloadManager() );
            if ( bStarted == false )
                SW_LOG_ERROR_ID( "Server.GameModuleCouldNot", "The game module could not be started on the dedicated server" );
            return bStarted ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        void shutdownSubsystem() override
        {
            _server._launch.getModuleManager()->shutdown();
            _server._launch.releaseModuleManager();
        }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { ServiceContextHostSubsystem::kSubsystemName };
            return kArrDependency;
        }

    private:
        ServerApp& _server;
    };

    /** @brief 표준 입력 명령 채널입니다. */
    class ServerApp::StandardInputHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "StandardInput";

        explicit StandardInputHostSubsystem( ServerApp& server )
            : _server{ server }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            if ( _server._pServerConfig->_bConsoleInput )
                _server._console.start();
            return SubsystemInitResult::Succeeded;
        }
        void shutdownSubsystem() override
        {
            _server._console.stop(); // 읽기 스레드를 멈추고, 줄 버퍼도 엔진 종료의 누수 보고 전에 해제한다
        }
        const utf8* getSubsystemName() const override { return kSubsystemName; }

    private:
        ServerApp& _server;
    };

    /** @brief 개발 콘솔(명령 처리기)입니다. 명령이 게임이 올린 개발 명령 · 변수를 부른다 — 게임 뒤에 서고 게임보다 먼저 내려간다. */
    class ServerApp::DevConsoleHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "DevConsole";

        explicit DevConsoleHostSubsystem( ServerApp& server )
            : _server{ server }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            if ( _server._pDebugControl != nullptr )
                _server._pDebugControl->startConsole();
            return SubsystemInitResult::Succeeded;
        }
        void shutdownSubsystem() override
        {
            if ( _server._pDebugControl != nullptr )
                _server._pDebugControl->stopConsole();
        }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { GameHostSubsystem::kSubsystemName };
            return kArrDependency;
        }

    private:
        ServerApp& _server;
    };

    /** @brief 원격 개발 명령 채널(개발 구성)과 관리 채널입니다. 원격 줄을 개발 콘솔로 돌리므로 콘솔 뒤. */
    class ServerApp::DebugChannelHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "DebugChannel";

        explicit DebugChannelHostSubsystem( ServerApp& server )
            : _server{ server }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            ServerApp& server = _server;
            if ( server._pDebugControl != nullptr && server._pDebugControl->startChannel() == false )
                return SubsystemInitResult::Failed;
            // 관리 채널은 Shipping 도 여는 길이다(기본 꺼짐, 켜면 토큰 필수). 상태는 서버 루프가 답한다.
            const bool bStarted = server._adminChannel.start( *server._pServerConfig, *server._observability.getHealthRegistry(), SW_DELEGATE_LAMBDA( ServerAdminStatusDelegate, [pServer = &server]()
            { return ServerAdminStatus{ pServer->_tickCount, pServer->getServiceConnectionCount(), pServer->_bDraining == SW_TRUE }; } ) );
            return bStarted ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        void shutdownSubsystem() override
        {
            if ( _server._pDebugControl != nullptr )
                _server._pDebugControl->stopChannel();
            _server._adminChannel.stop();
        }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { DevConsoleHostSubsystem::kSubsystemName };
            return kArrDependency;
        }

    private:
        ServerApp& _server;
    };

    ServerApp::ServerApp()
        : _launch{}
        , _pDebugControl{ nullptr }
        , _console{}
        , _observability{}
        , _serverServices{ nullptr }
        , _reloadStopsServices{}
        , _serverConfigPath{}
        , _serverServiceContext{}
        , _kitDirectory{}
        , _kitLocator{ _kitDirectory }
        , _pServerConfig{ nullptr }
        , _drainDeadlineMs{ 0 }
        , _tickCount{ 0 }
        , _startNanoseconds{ 0 }
        , _tickNanoseconds{ 0 }
        , _statusTickSumNanoseconds{ 0 }
        , _statusTickMaxNanoseconds{ 0 }
        , _statusTickSampleCount{ 0 }
        , _droppedTickCount{ 0 }
        , _bReady{ SW_FALSE }
        , _bRestartServerServices{ SW_FALSE }
        , _bDraining{ SW_FALSE }
        , _bConfigCheckOnly{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ServerApp::~ServerApp() = default;

    int32 ServerApp::runMain( int32 argc, utf8* pArgv[], ReadyFunction pfnOnReady )
    {
        // 줄마다 내보낸다. 테스트, journald, docker logs 가 "ready" 줄을 바로 볼 수 있다. 엔진(로거)을 시작하기 전에 켠다.
        ConsoleLogOutput::setFlushEveryLine( true );
        ServerApp  server{};
        const bool bStarted   = server.initialize( argc, pArgv );
        const bool bCheckOnly = server._bConfigCheckOnly == SW_TRUE;
        if ( bStarted && bCheckOnly == false )
        {
            if ( pfnOnReady != nullptr )
                pfnOnReady();
            server.run();
        }
        server.shutdown();
        if ( bCheckOnly ) // 설정 검사만: 잘못된 설정은 로드 오류 코드(종료 코드 표 `LoadError`) — 스크립트가 크래시 · 기동 실패와 가른다
            return static_cast<int32>( bStarted ? ProcessExitCode::Passed : ProcessExitCode::LoadError );
        return bStarted ? 0 : 1;
    }

    bool ServerApp::initialize( int32 argc, utf8* pArgv[] )
    {
        _startNanoseconds = MonotonicClock::nowNanoseconds();
        // 호스트 범위를 먼저 세운다 — 엔진 범위의 부모다. 엔진의 `ModuleLoadStage` 단계가 여기서 모듈 라이브러리 창구를 찾아 서버 대상 모듈을 올린다(`loadModuleLibraries`).
        // 종료 신호 처리기도 이 범위에 둔다 — 엔진 종료가 끝날 때까지 걸려 있어야 하고, 기동 중에 온 신호는 깃발로 남아 루프 첫 바퀴가 정상 종료한다.
        _launch.getHostScope().emplaceSubsystem<ShutdownSignalSubsystem>();
        // 개발 조종 · 개발 계층(핫 리로드 · 개발 도구)은 DevTools.dll 이 구현한다 — ModuleManager 는 DevTools 를 보지 않아 호스트가 만들어 넘긴다. 배포는 짓지 않는다.
        unique_ptr<IHostDevLayer> pDevLayer;
        if constexpr ( build::kShipping == false )
        {
            _pDebugControl = makeServerDebugControl( SW_DELEGATE_LAMBDA( ServerTickCountDelegate, [this]()
            { return _tickCount; } ) );
            pDevLayer      = makeHostDevLayer();
        }
        if ( _launch.initializeHostScope( SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &ServerApp::loadModuleLibraries, this ), "ServerApp", std::move( pDevLayer ) ) == false )
            return false;
        // 헤드리스 작업(서버 팩의 씬 쿠킹)은 엔진 기동 안에서 끝났다. 실패는 종료 코드로 알린다. 빌드(CookAssets 타깃) · `pack.py` 가 이 코드를 본다.
        const HostEngineStart engineStart = _launch.initializeEngine( argc, pArgv, EngineHostRole::DedicatedServer );
        if ( engineStart != HostEngineStart::Ready )
            return engineStart == HostEngineStart::Headless;

        if ( loadServerConfig() == false )
            return false;
        const bool bConfigValid = validateServerConfig( *_pServerConfig );
        if ( bConfigValid == false || _bConfigCheckOnly == SW_TRUE )
            return bConfigValid; // 검사만이면 여기서 끝난다(서비스 · 포트를 열지 않는다)
        _tickNanoseconds = constant::time::kNanosecondsPerSecond / static_cast<int64>( _pServerConfig->_tickRateHz );
        // 호스트 서브시스템(관측 · 크래시 묶음 · 서버 문맥 · 게임 · 개발 콘솔 · 디버그 채널 · 표준 입력)를 의존 순서로 세운다. 순서는 각 서비스 형식의 의존 선언이 정하고,
        // 내리는 것은 `shutdown` 의 역순 한 줄이다.
        _launch.addHostSubsystem<ObservabilityHostSubsystem>( *this );
        _launch.addHostSubsystem<CrashReportsHostSubsystem>( *this );
        _launch.addHostSubsystem<ServiceContextHostSubsystem>( *this );
        _launch.addHostSubsystem<GameHostSubsystem>( *this );
        _launch.addHostSubsystem<StandardInputHostSubsystem>( *this );
        _launch.addHostSubsystem<DevConsoleHostSubsystem>( *this );
        _launch.addHostSubsystem<DebugChannelHostSubsystem>( *this );
        if ( _launch.startHostSubsystems() == false )
            return false;
        // 서버 서비스는 호스트 서브시스템이 아니다 — 모듈 리로드마다 멈췄다 다시 서는 수명이다. 게임 뒤에 시작해 서비스가 게임이 정한 상태를 보게 하고,
        // `shutdown` 이 호스트 서브시스템보다 먼저 멈춘다.
        if ( startServerServices() == false )
            return false;
        // 게임 포트가 접속 티켓을 요구하는데 확인할 조립이 서지 않았으면 모든 접속을 거절하는 서버가 된다 — 그런 서버로 뜨지 않는다.
        if ( _pServerConfig->_bGameConnectTicket && engine::getSubsystem<GameNetSession>().getConnectSecurity().isAuthenticatorBound() == false )
        {
            SW_LOG_ERROR_ID( "Server.ServerConfigBGameConnectTicketBut", "Server config _bGameConnectTicket is on but no online kit bound a game connect authenticator (add the account kit 'gameConnect')" );
            return false;
        }

        _bReady = SW_TRUE;
        _observability.beginLoadWindow( ServerAppInternal::nowMonotonicMs() );
        // 여기부터 모든 줄에 `[tick=N]`이 붙는다. N 은 끝낸 틱 수이며, 지금 도는 틱의 0 기준 번호다. 멀티 프로세스 재생의 Output Log 가 서버 줄을 틱 기준으로 맞춰 본다.
        Logger::setClockNumber( LogClock::Tick, _tickCount );
        SW_LOG_INFO_ID( "Server.DedicatedServerReadyTarget", "Dedicated server ready - target %#, %# Hz, game port %#, service port %#, startup %# ms", build::kTargetName, _pServerConfig->_tickRateHz,
                        _pServerConfig->_gamePort, _pServerConfig->_servicePort, ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::time::kNanosecondsPerMillisecond );
        return true;
    }

    bool ServerApp::loadModuleLibraries()
    {
        // 전용 서버는 Server 대상 모듈만 로드한다. Game 빌드에서도 에디터와 RHI 모듈을 로드하지 않는다. 리로드 구동기(App 과 같은 것)가 매니페스트를 해석하고
        // 매니저를 만들어 자신을 호스트 범위에 걸며, 매니저 종료를 "씬은 사라졌고 서비스는 아직 있는" 구간의 훅에 건다(배포는 정적 링크라 아무것도 하지 않는다).
        if ( _launch.loadModuleLibraries( static_cast<uint8>( ModuleTarget::Server ), make_unique<GameModuleManager>() ) == false )
            return false;
        // 서버 서비스 코드가 리로드될 모듈 안에 있으므로 서비스를 먼저 멈춘다. 모듈 호스트보다 나중에 더하므로 먼저 불린다(배치 콜백은 나중 것이 먼저). 배포는 빈 구독이다.
#if SW_WITH_DYNAMIC_MODULES
        if ( _launch.getDevLayer() != nullptr )
            _reloadStopsServices = _launch.getDevLayer()->addOnBeforeReloadBatch(
                SW_DELEGATE_METHOD( BeforeReloadBatchDelegate, &ServerApp::onBeforeModuleReload, this ) );
#endif
        return true;
    }

    bool ServerApp::loadServerConfig()
    {
        ConfigManager*            pConfigManager = _launch.getEngine().getConfigManager();
        const CommandLineManager* pCommandLine   = _launch.getEngine().getCommandLineManager();
        string                    path           = GameSelection::getServerConfigPath();
        if ( pCommandLine != nullptr )
        {
            string overridePath;
            if ( pCommandLine->getArgument( CommandLineArgument::SERVER_CONFIG, overridePath ) && overridePath.empty() == false )
                path = overridePath;
            bool bCheckOnly = false;
            (void)pCommandLine->getArgument( CommandLineArgument::CHECK_CONFIG, bCheckOnly ); // 없으면 기본값(false) 그대로다
            _bConfigCheckOnly = bCheckOnly ? SW_TRUE : SW_FALSE;
        }
        if ( pConfigManager == nullptr )
            return false;
        // 모든 구성이 디스크에서 읽는다. 운영자가 고치는 파일이므로 베이크하지 않는다. 파일이 없으면 서버를 시작하지 않는다.
        // 기본 포트로 조용히 뜬 서버는 틀린 DB 와 캐시에 붙는 서버이기 때문이다(Dev 도 같다).
        _serverConfigPath = path;
        _pServerConfig    = pConfigManager->loadHostConfig<ServerConfig>( ConfigSource::disk( path ) );
        if ( _pServerConfig == nullptr )
        {
            SW_LOG_ERROR_ID( "Server.ServerConfigCouldNot", "Server config '%#' could not be read - pass -server-config=<path>", path.c_str() );
            return false;
        }
        if ( _pServerConfig->_bJSONLogs )
        {
            // 로거는 엔진 기동에서 이미 섰다. 설정을 읽은 지금부터 줄이 JSON 한 줄로 나간다(콘솔 장치는 꺼진다). 켰는데 못 켜면 수집기가 못 읽는 줄을 내는 서버가 되므로 기동을 멈춘다.
            ILogSink* pLogSink = Logger::getGlobalSink();
            if ( pLogSink == nullptr || pLogSink->enableJSONLinesOutput() == false )
            {
                SW_LOG_ERROR_ID( "Server.ServerConfigBJSONLogsBut", "Server config _bJSONLogs is on but the log sink could not add the JSON lines output" );
                return false;
            }
        }
        SW_LOG_INFO_ID( "Server.ServerConfig", "Server config: %#", path.c_str() );
        return true;
    }

    bool ServerApp::validateServerConfig( const ServerConfig& config )
    {
        bool bValid = true;
        if ( config._opsPort > 0 )
        {
            NetAddress opsAddress;
            string     opsToken;
            (void)ProcessEnvironment::tryReadNonEmpty( config._opsTokenEnvironment, opsToken ); // 없으면 인증 없이 — 그때는 루프백 · 사설 바인드만
            if ( NetAddress::parse( config._opsListenAddress, static_cast<uint16>( config._opsPort ), opsAddress ) == false )
            {
                SW_LOG_ERROR_ID( "Server.CheckOpsListenAddress", "Server config _opsListenAddress '%#' is not an IP address", config._opsListenAddress.c_str() );
                bValid = false;
            }
            else if ( opsToken.empty() && OpsHTTPEndpoint::isBindAllowedWithoutToken( opsAddress ) == false )
            {
                SW_LOG_ERROR_ID( "Server.CheckOpsBindNeedsToken", "Server config opens the ops endpoint on '%#' (not loopback or private) but the token environment variable '%#' is not set",
                                 config._opsListenAddress.c_str(), config._opsTokenEnvironment.c_str() );
                bValid = false;
            }
        }
        if ( config._adminServicePort > 0 )
        {
            NetAddress adminAddress;
            if ( NetAddress::parse( config._adminListenAddress, static_cast<uint16>( config._adminServicePort ), adminAddress ) == false || adminAddress.isLoopbackOrPrivate() == false )
            {
                SW_LOG_ERROR_ID( "Server.CheckAdminListenAddress", "Server config _adminListenAddress '%#' must be a loopback or private IP address (the GM port is never public)", config._adminListenAddress.c_str() );
                bValid = false;
            }
        }
        string adminToken;
        if ( config._adminChannelPort > 0 && ProcessEnvironment::tryReadNonEmpty( config._adminChannelTokenEnvironment, adminToken ) == false )
        {
            SW_LOG_ERROR_ID( "Server.CheckAdminChannelToken", "Server config enables the admin channel (_adminChannelPort=%#) but the token environment variable '%#' is not set",
                             config._adminChannelPort, config._adminChannelTokenEnvironment.c_str() );
            bValid = false;
        }
        CrashReportConsent consent = CrashReportConsent::Local;
        if ( parseCrashReportConsent( config._crashReportConsent, consent ) == false || consent == CrashReportConsent::Ask )
        {
            SW_LOG_ERROR_ID( "Server.CheckCrashReportConsent", "Server config _crashReportConsent '%#' must be 'local' or 'send'", config._crashReportConsent.c_str() );
            bValid = false;
        }
        SW_LOG_INFO_ID( "Server.CheckConfigResult", "Server config check: %#", bValid ? "ok" : "invalid" );
        return bValid;
    }

    bool ServerApp::startServerServices()
    {
        if ( _serverServices == nullptr )
            _serverServices = make_unique<ServerServiceManager>();
        return _serverServices->startAll( _serverServiceContext );
    }

    bool ServerApp::collectPreviousCrashes()
    {
        const AbsolutePath crashFolder = engine::getSubsystem<CrashHandler>().getReportFolder();
        if ( crashFolder.empty() || engine::areEngineSubsystemsBound() == false )
            return true;
        CrashReportSubsystem& crashReports  = engine::getSubsystem<CrashReportSubsystem>();
        const AbsolutePath    reportsFolder = crashFolder.getParent().join( CrashReportSubsystem::kReportsFolderName );
        crashReports.initialize( crashFolder, reportsFolder, ProcessSession::getID() );
        // 서버의 보내기 동의는 플레이어 설정이 아니라 운영 설정(`ServerConfig::_crashReportConsent`)의 몫이다 — 기본은 기계 안에만(`local`).
        // 서버에는 "보낼까요?" 를 물을 사람이 없어 `ask` 는 받지 않고, 모르는 값은 오류로 기동을 멈춘다(보내지 않는 쪽으로 조용히 바꾸지 않는다).
        CrashReportConsent consent{ CrashReportConsent::Local };
        if ( parseCrashReportConsent( _pServerConfig->_crashReportConsent, consent ) == false || consent == CrashReportConsent::Ask )
        {
            SW_LOG_ERROR_ID( "Server.ServerConfigCrashReportConsentMust", "Server config _crashReportConsent '%#' must be 'local' or 'send'", _pServerConfig->_crashReportConsent.c_str() );
            return false;
        }
        crashReports.setConsent( consent );
        const uint32               newCount = crashReports.collectNewCrashes();
        vector<CrashReportSummary> listReport;
        crashReports.collectReports( listReport );
        // 지난 실행의 크래시를 묶는 것은 이번 실행의 이상이 아니라 정리다 — 클라이언트(TelemetrySubsystem)와 같이 정보로 남긴다. 크래시 자체는 그 실행이 이미
        // 오류로 남겼다. 경고로 두면 같은 바이너리 폴더(Bin/Saved/Logs)를 쓰는 앞선 실행의 잔재가 이번 실행의 판정(시나리오의 처리 안 된 경고)을 바꾼다.
        if ( newCount > 0 )
            SW_LOG_INFO_ID( "Server.BundledCrashReportPrevious", "Bundled %# crash report(s) from previous runs into '%#' (%# kept)", newCount, reportsFolder.c_str(), static_cast<uint32>( listReport.size() ) );
        MetricGauge* pCrashGauge = _observability.getMetricRegistry()->registerGauge( "server_crash_reports", "Crash report bundles kept on this machine (previous runs that crashed)" );
        if ( pCrashGauge != nullptr )
            pCrashGauge->set( static_cast<float64>( listReport.size() ) );
        return true;
    }

    int32 ServerApp::getServiceConnectionCount() const
    {
        return _serverServices != nullptr ? _serverServices->getConnectionCount() : 0;
    }

    int32 ServerApp::countDrainBlockers() const
    {
        // 서비스 연결(로그인 · 채팅 …) + 게임 UDP 접속(복제 키트의 플레이어) + 키트가 끝내야 하는 진행 중인 일(매치 · 거래). 셋이 모두 0 이어야 조용히 내려갈 수 있다.
        const int32           serviceConnections = getServiceConnectionCount();
        const int32           openJob            = _serverServices != nullptr ? _serverServices->getOpenJobCount() : 0;
        const GameNetSession* pNetSession        = engine::areEngineSubsystemsBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        const int32           gameConnections    = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server ? pNetSession->getConnectedCount() : 0;
        return serviceConnections + gameConnections + openJob;
    }

    void ServerApp::beginDrain( ShutdownCause cause, int64 monotonicMs )
    {
        _bDraining = SW_TRUE;
        _observability.getHealthRegistry()->setDraining( true ); // /readyz 는 503 을 반환한다. 부하 분산기가 새 접속을 끊는다(종료하는 동안에도 /healthz 는 살아 있다).
        const bool  bWait  = ServerAppInternal::shouldDrainFor( cause );
        const int64 waitMs = bWait ? static_cast<int64>( _pServerConfig->_drainWaitSeconds ) * constant::time::kMillisecondsPerSecond : 0;
        _drainDeadlineMs   = monotonicMs + waitMs;
        // 키트에 종료를 알리고(진행 중인 일 마무리) 게임 접속자에게 끊김 사유를 보낸다. 기다리지 않는 종료(콘솔 · Ctrl+C)는 사유 패킷이 나가기 전에 내려갈 수 있어 최선만 다한다.
        ServerShutdownNotice notification;
        notification._cause        = cause;
        notification._graceSeconds = static_cast<uint32>( waitMs / constant::time::kMillisecondsPerSecond );
        if ( _serverServices != nullptr )
            _serverServices->beginShutdown( notification );
        GameNetSession* pNetSession = engine::areEngineSubsystemsBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        if ( pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server )
            pNetSession->sendShutdownNotice( GameShutdownNotice{ bWait ? GameShutdownReason::ServerRestart : GameShutdownReason::ServerStopped, notification._graceSeconds } );
        if ( waitMs > 0 )
            SW_LOG_INFO_ID( "Server.DrainingWaitingUpConnections", "Draining: waiting up to %# s for %# connections / open jobs to finish (%#)", _pServerConfig->_drainWaitSeconds, countDrainBlockers(),
                            ShutdownSignal::getCauseName( cause ) );
    }

    bool ServerApp::isDrainFinished( int64 monotonicMs ) const
    {
        if ( monotonicMs >= _drainDeadlineMs )
            return true;
        return countDrainBlockers() == 0;
    }

    bool ServerApp::onBeforeModuleReload( [[maybe_unused]] const vector<string>& listModuleName )
    {
        if ( _serverServices != nullptr && _serverServices->isRunning() )
        {
            SW_LOG_INFO_ID( "Server.StoppingServerServicesModule", "Stopping %# server services for a module reload", _serverServices->getRunningCount() );
            _serverServices->stopAll();
            _bRestartServerServices = SW_TRUE;
        }
        return true; // 게임 쪽 처리는 모듈 호스트가 건 배치 콜백이 이어서 한다
    }

    void ServerApp::run()
    {
        if ( _bReady == SW_FALSE )
            return;
        const float32 deltaSeconds = 1.0f / static_cast<float32>( _pServerConfig->_tickRateHz );
        const int64   maxBehind    = _tickNanoseconds * static_cast<int64>( _pServerConfig->_maxCatchUpTicks );
        int64         deadline     = MonotonicClock::nowNanoseconds();
        while ( true )
        {
            if ( _console.poll( SW_DELEGATE_METHOD( StandardInputCommandDelegate, &ServerApp::executeCommand, this ) ) )
                ShutdownSignal::request( ShutdownCause::ConsoleCommand );
            if ( _pDebugControl != nullptr )
                _pDebugControl->poll(); // 일시정지 중에도 돈다 - 멈춘 상태의 값을 읽는 것이 쓰임이다
            _adminChannel.poll();

            const ShutdownCause cause = ShutdownSignal::getRequestedCause();
            if ( cause != ShutdownCause::None || _launch.getEngine().isQuitRequested() )
            {
                const int64 nowMs = ServerAppInternal::nowMonotonicMs();
                if ( _bDraining == SW_FALSE )
                    beginDrain( cause, nowMs ); // 처음 본 순간 /readyz 를 503 으로 바꾸고 기한을 정한다
                if ( isDrainFinished( nowMs ) )
                {
                    SW_LOG_INFO_ID( "Server.DedicatedServerShutdownRequested", "Dedicated server shutdown requested (%#) after %# ticks", ShutdownSignal::getCauseName( cause ), _tickCount );
                    break;
                }
                // 접속이 남았고 기한 전이다 — 틱을 계속 돌려 진행 중인 매치 · 거래가 끝나게 한다(아래의 일반 틱 경로).
            }

            // 서버 서비스는 게임 틱과 따로 돈다. 일시정지(Dev `pause`) 중에도 로그인이나 채팅 요청은 처리한다.
            if ( _serverServices != nullptr )
                _serverServices->update( WallClock::nowUnixMilliseconds() );
            _observability.updateEverySecond( ServerAppInternal::nowMonotonicMs(), getServiceConnectionCount() );

            if ( _pDebugControl != nullptr && _pDebugControl->isHoldingTick() )
            {
                // 일시정지에서는 게임 틱만 멈춘다. 서버는 살아 있으므로 상태 확인 박동은 계속 보내(감시자가 재시작하지 않게 한다). 재개할 때 밀린 틱을 몰아서 돌지 않게 마감을 지금으로 바꾼다.
                _observability.markAlive( ServerAppInternal::nowMonotonicMs() );
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + _tickNanoseconds );
                deadline = MonotonicClock::nowNanoseconds();
                continue;
            }

            const Scene* const pSceneBefore = engine::getSubsystem<SceneManager>().getActiveScene();
            const int64        tickStart    = MonotonicClock::nowNanoseconds();
            updateOnce( deltaSeconds );
            const int64 tickTime     = MonotonicClock::nowNanoseconds() - tickStart;
            const bool  bSceneOpened = engine::getSubsystem<SceneManager>().getActiveScene() != pSceneBefore;
            _statusTickSumNanoseconds += tickTime;
            _statusTickMaxNanoseconds = tickTime > _statusTickMaxNanoseconds ? tickTime : _statusTickMaxNanoseconds;
            ++_statusTickSampleCount;
            ++_tickCount;
            Logger::setClockNumber( LogClock::Tick, _tickCount );
            // 틱 지표 · 상태 박동 · 프레임 이력(O1) · 느린 틱 덤프 · 사용률 창(O5)은 관측이 든다.
            _observability.recordTick( tickTime, _tickCount, getServiceConnectionCount(), _droppedTickCount, ServerAppInternal::nowMonotonicMs() );
            // 이력은 `-gv_stateHashHistory=N` 이 켤 때만 남는다(배포에는 그 인자가 없어 늘 꺼져 있다).
            StateHashHistory* pStateHashHistory = engine::areEngineSubsystemsBound() ? engine::findSubsystem<StateHashHistory>() : nullptr;
            if ( pStateHashHistory != nullptr && pStateHashHistory->isRecording() )
            {
                pStateHashHistory->recordActiveScene( _tickCount );
                // 클라이언트가 같은 틱 번호로 자기 해시를 남긴다(S7) — 이력을 남기는 서버만 보낸다.
                GameNetSession* pNetSession = engine::findSubsystem<GameNetSession>();
                if ( pNetSession != nullptr )
                    pNetSession->sendServerTick( _tickCount );
            }
            if ( _pDebugControl != nullptr )
                _pDebugControl->onTickFinished(); // 틱 상한 종료(`-gv_serverExitAfterTicks`)도 개발 조종이 건다

            deadline += _tickNanoseconds;
            const int64 now = MonotonicClock::nowNanoseconds();
            if ( bSceneOpened )
            {
                // 씬을 연 틱은 월드를 세운 시간(오브젝트 시작 · 내비메시)을 안는다 — 게임이 밀린 것이 아니라 그 월드의 시계가 지금 시작한 것이다(언리얼은 `LoadMap` 뒤 첫 델타를 버린다). 첫 씬과 씬 이동이 같은 길이다.
                SW_LOG_INFO_ID( "Server.SceneOpened", "Scene opened in %# us - ticks count from here", tickTime / constant::time::kNanosecondsPerMicrosecond );
                deadline = now;
            }
            else if ( now - deadline > maxBehind )
            {
                // 따라잡을 수 없을 만큼 밀렸다. 밀린 시간을 버리고 지금부터 다시 센다(몰아서 돌다가 다음 틱을 또 밀지 않게 한다).
                const uint32 dropped = static_cast<uint32>( ( now - deadline ) / _tickNanoseconds );
                if ( _droppedTickCount == 0 ||
                     _droppedTickCount / ServerAppInternal::kDroppedTickWarningInterval != ( _droppedTickCount + dropped ) / ServerAppInternal::kDroppedTickWarningInterval )
                    SW_LOG_WARNING_ID( "Server.ServerBehindDroppedTicks", "Server is behind: dropped %# ticks (total %#), last tick %# us", dropped, _droppedTickCount + dropped, tickTime / constant::time::kNanosecondsPerMicrosecond );
                _droppedTickCount += dropped;
                _observability.addDroppedTicks( dropped );
                deadline = now;
            }
            else if ( deadline > now )
            {
                MonotonicClock::sleepUntilNanoseconds( deadline );
            }
        }
    }

    void ServerApp::updateOnce( float32 deltaSeconds )
    {
        // App::run 의 한 프레임에서 창, 에디터, 사용자 설정, 백엔드 교체를 뺀 것이다. 한 틱은 고정 스텝 하나다.
        _launch.getEngine().beginFrame( GameSeconds{ deltaSeconds }, RealSeconds{ deltaSeconds } ); // 서버에는 시간 배율이 없다 — 틱 하나가 실제 경과다.
        _launch.getModuleManager()->beginFrame();
        _launch.getModuleManager()->updateGameFrame( deltaSeconds, 1, deltaSeconds ); // 한 틱은 고정 스텝 하나다
        _launch.updateModuleReload();
        if ( _bRestartServerServices == SW_TRUE )
        {
            // 리로드가 끝났다(취소됐어도 서비스는 멈춰 있다). 새 모듈의 팩토리로 다시 시작한다.
            _bRestartServerServices = SW_FALSE;
            if ( startServerServices() == false )
                SW_LOG_ERROR_ID( "Server.ServerServicesCouldNot", "Server services could not be restarted after the module reload - fix the error and reload again" );
        }
        const ModuleFrameState& frameState = _launch.getModuleManager()->getFrameState();
        _launch.getEngine().update( deltaSeconds, frameState._bTickScene == SW_TRUE ? SceneTick::Run : SceneTick::Pause );
        _launch.getEngine().endFrame();
    }

    void ServerApp::executeCommand( string_view command )
    {
        if ( command == "status" )
        {
            logStatus();
            return;
        }
        if ( command == "help" )
        {
            SW_LOG_INFO_ID( "Server.ServerCommandsQuitStop", "Server commands: quit | stop | status | help%#", _pDebugControl != nullptr ? _pDebugControl->getHelpSuffix() : "" );
            return;
        }
        // Dev 는 일시정지 명령과 개발 콘솔의 명령(개발 명령과 gv_* 전역 변수)도 받는다. Shipping 서버는 위의 네 가지만 받는다.
        if ( _pDebugControl != nullptr && _pDebugControl->executeCommand( command ) )
            return;
        SW_LOG_WARNING_ID( "Server.UnknownServerCommandType", "Unknown server command '%#' - type help", string( command ).c_str() );
    }

    void ServerApp::logStatus() const
    {
        const int64 uptimeSeconds   = ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::time::kNanosecondsPerSecond;
        const int64 averageMicro    = _statusTickSampleCount > 0 ? _statusTickSumNanoseconds / _statusTickSampleCount / constant::time::kNanosecondsPerMicrosecond : 0;
        const int32 serviceCount    = _serverServices != nullptr ? _serverServices->getRunningCount() : 0;
        const int32 connectionCount = getServiceConnectionCount();
        uint64      residentBytes   = 0;
        uint64      peakBytes       = 0;
        (void)ProcessMetrics::tryGetResidentBytes( residentBytes, peakBytes ); // 답하지 못하면 0 을 적는다
        SW_LOG_INFO_ID( "Server.DedicatedServerStatusUptime", "Dedicated server status - uptime %# s, tick %#%#, tick time avg %# us / max %# us (budget %# us), dropped ticks %#, services %# (connections %#), "
                                                              "resident %# MiB",
                        uptimeSeconds, _tickCount, ( _pDebugControl != nullptr && _pDebugControl->isPaused() ) ? " (paused)" : "", averageMicro, _statusTickMaxNanoseconds / constant::time::kNanosecondsPerMicrosecond, _tickNanoseconds / constant::time::kNanosecondsPerMicrosecond,
                        _droppedTickCount, serviceCount, connectionCount, residentBytes / static_cast<uint64>( constant::memory::kBytesPerMegabyte ) );
    }

    void ServerApp::shutdown()
    {
        // 서버 서비스는 모듈 코드이고 호스트 서브시스템이 아니다 — 호스트 서브시스템(게임 · 모듈)이 내려가기 전에 멈춘다.
        if ( _serverServices != nullptr )
        {
            _serverServices->stopAll();
            _serverServices.reset();
        }
        _reloadStopsServices.reset();
        // 호스트 서브시스템을 선 순서의 역순으로 내린다 — 표준 입력 · 디버그 채널 · 개발 콘솔, 게임(엔진보다 먼저), 서버 문맥, 크래시 묶음, 마지막에 관측.
        // 순서는 각 서비스 형식의 의존 선언이 정한다.
        _launch.stopHostSubsystems();
        // 호스트 서브시스템이 모두 내려갔다 — 기동 뒤에 잡은 호스트 값(설정 경로 · 서버 문맥)을 엔진 종료(누수 보고)보다 먼저 놓는다.
        _serverServiceContext = ServerServiceContext{};
        _serverServiceContext._serverConfigPath.shrink_to_fit(); // 이동 대입은 libstdc++ 에서 버퍼를 남긴다(짧은 글)
        _serverConfigPath.clear();
        _serverConfigPath.shrink_to_fit();
        // 로거는 엔진 종료와 함께 종료된다. 마지막 줄은 그 앞에 남긴다(테스트와 운영 도구가 이 줄로 정상 종료를 확인한다).
        SW_LOG_INFO_ID( "Server.DedicatedServerShutdownComplete", "Dedicated server shutdown complete" );
        // 호스트 범위가 비며 모듈 라이브러리 창구가 사라지고 종료 신호 처리기가 떨어진다.
        _launch.shutdownEngine();
        Logger::clearClockNumber();
        // Windows 창 닫기와 시스템 종료 처리기가 이것을 기다린다.
        ShutdownSignal::notifyShutdownComplete();
    }
} // namespace sw

namespace sw
{
    int32 ServerEntry::run( int32 argc, utf8* pArgv[], ReadyFunction pfnOnReady )
    {
        return ServerApp::runMain( argc, pArgv, pfnOnReady );
    }
} // namespace sw
