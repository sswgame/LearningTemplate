#include "pch.h"

#include "ModuleManager/Host/HostDevLayer.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Diagnostics/PhaseTimings.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/FileWatch/FileWatchService.h"
#include "DevTools/Host/EditorGameModuleManager.h"
#include "DevTools/Host/EditorModuleHeadlessTasks.h"
#include "DevTools/Host/HostDevTools.h"

#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Renderer/Capture/PortraitStudio.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/SplashSubsystem.h"
#include "Engine/Window/SplashWindow.h"

#include "ModuleManager/Host/HostLaunch.h"

#if SW_WITH_DYNAMIC_MODULES
    #include "DevTools/ModuleReload/LiveReload/LiveReloadManager.h"
    #include "DevTools/ModuleReload/ModuleReloadDriver.h"
#endif

namespace sw
{
    namespace
    {
        /** @brief 개발 콘솔(게임 창 콘솔과 표준 입력 채널)입니다. 명령이 게임이 올린 개발 명령 · 변수를 부른다 — 게임 뒤에 서고 게임보다 먼저 내려간다. */
        class DevConsoleHostSubsystem final : public Subsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "DevConsole";

            DevConsoleHostSubsystem( HostDevTools& devTools, const utf8* pGameServiceName, bool bEnableEditor )
                : _devTools{ devTools }
                , _arrDependency{ pGameServiceName }
                , _bEnableEditor{ bEnableEditor ? SW_TRUE : SW_FALSE }
                , _reserved{ 0 }
            {
            }

            SubsystemInitResult initializeSubsystem() override
            {
                _devTools.startConsole( _bEnableEditor == SW_TRUE );
                return SubsystemInitResult::Succeeded;
            }
            void                    shutdownSubsystem() override { _devTools.stopConsole(); }
            const utf8*             getSubsystemName() const override { return kSubsystemName; }
            span<const utf8* const> getSubsystemDependencies() const override { return _arrDependency; }

        private:
            HostDevTools&          _devTools;
            const utf8*            _arrDependency[1]; ///< 게임 호스트 서비스 이름(프로그램 내내 사는 글)
            uint8                  _bEnableEditor : 1;
            [[maybe_unused]] uint8 _reserved      : 7;
        };

        /**
         * @class HostDevLayer
         * @brief 개발 구성의 개발 계층입니다 — 개발 도구 연결(`HostDevTools`), 동적 모듈 구성이면 핫 리로드(`ModuleReloadDriver`), App 의 자동화 · 기동 측정을 듭니다.
         * @details 정적 모듈 구성(웹 · Android 개발 — `SW_STATIC_MODULES`)은 리로드 구동기(`ModuleReload/`)를 짓지 않는다 — 리로드 창구가 형식에 없다.
         *          기동 단계표는 이 계층을 만든 때(호스트 `initialize` 의 첫 줄)부터 잰다.
         */
        class HostDevLayer final : public IHostDevLayer
        {
        public:
            HostDevLayer()
                : _startMicro{ MonotonicClock::nowMicroseconds() }
            {
                _startupPhases.begin();
            }

            void addToHostScope( SubsystemScope& host, Engine& engine ) override
            {
                _pHost   = &host;
                _pEngine = &engine;
                _devTools.addToHostScope( host, engine, _fileWatch );
            }

            void pollShellActions( float32 deltaTime, bool bEnableEditor ) override
            {
                // 디버그 키(개발 콘솔 키 · 리로드 단축키)는 구성과 상관없이 이번 프레임 것으로 갱신한다 — 리로드 단축키를 요청으로 바꾸는 것만 동적 모듈 구성의 일이다.
                DebugKeyActions& debugKeys = engine::getSubsystem<DebugKeyActions>();
                debugKeys.update( engine::getSubsystem<InputManager>(), deltaTime );
#if SW_WITH_DYNAMIC_MODULES
                _moduleReload.pollReloadActions( debugKeys, bEnableEditor );
#else
                (void)bEnableEditor;
#endif
            }

#if SW_WITH_DYNAMIC_MODULES
            bool initializeModuleReload( uint8 targetMask, SubsystemScope& host, ModuleLoaderSubsystem& moduleLibraryHost, ModuleCatalog& outCatalog,
                                         ModuleResolution& outResolution ) override
            {
                return _moduleReload.initialize( targetMask, host, moduleLibraryHost, _fileWatch, outCatalog, outResolution );
            }

            ILiveReloadManager* getLiveReloadManager() const override { return _moduleReload.getLiveReloadManager(); }
            Subscription        addOnBeforeReloadBatch( BeforeReloadBatchDelegate onBeforeBatch ) override { return _moduleReload.addOnBeforeReloadBatch( onBeforeBatch ); }
            void                requestReload( const utf8* pModuleName ) override { _moduleReload.requestReload( pModuleName ); }
            void                updateModuleReload() override { _moduleReload.update(); }
#endif

            bool runEditorModuleHeadlessTasks( const CommandLineManager& commandLine ) override { return EditorModuleHeadlessTask::runRequested( commandLine ); }
            void attachFrameHooks( Engine& engine ) override { _devTools.attachFrameHooks( engine ); }
            void addConsoleHostService( HostLaunch& launch, const utf8* pGameServiceName, bool bEnableEditor ) override
            {
                launch.addHostSubsystem<DevConsoleHostSubsystem>( _devTools, pGameServiceName, bEnableEditor );
            }
            void detach() override
            {
                // 자동화 실행기는 엔진 범위와 함께 내려간다(스스로 프레임 · 호스트 범위에서 뗀다) — 빌린 포인터만 놓는다.
                _pAutomation = nullptr;
                _devTools.detach();
            }

            unique_ptr<GameModuleManager> makeEditorModuleManager() override { return make_unique<EditorGameModuleManager>( *_pEngine ); }

            bool startAutomation( const CommandLineManager& commandLine ) override
            {
                _pAutomation = engine::findSubsystem<IAutomationRuntime>();
                if ( _pAutomation != nullptr )
                    return _pAutomation->start( *_pEngine, *_pHost, commandLine );
                string     value;
                const bool bAsked = commandLine.getArgument( CommandLineArgument::SCENARIO, value ) || commandLine.getArgument( CommandLineArgument::SCENARIO_RECORD, value );
                if ( bAsked )
                    SW_LOG_ERROR_ID( "HostDevLayer.ScenarioNeedsRunner", "-scenario / -scenario-record need the automation runner, which this configuration does not build" );
                return bAsked == false;
            }
            bool                                 isAutomationBusy() const override { return _pAutomation != nullptr && _pAutomation->isBusy(); }
            unique_ptr<IAutomationShutdownWatch> watchAutomationShutdown() override { return _pAutomation != nullptr ? _pAutomation->watchShutdown() : nullptr; }

            HostTaskResult runAfterHostSubsystems( const CommandLineManager& commandLine ) override
            {
                return runPortraitBake( commandLine ); // 지금은 초상화 굽기 하나다
            }

            void markStartupPhase( const utf8* pPhaseName ) override { _startupPhases.mark( pPhaseName ); }
            void logStartupPhases() override
            {
                // 시동 요약 한 줄 — 단계별 ms 와 오래 걸린 서브시스템(`Test/Load/Baselines.json` 의 `startup_ms` 와 별개의 진단 값이다).
                SW_LOG_INFO_ID( "App.StartupPhasesHostScopeMs", "Startup phases: %# | subsystems: %#", _startupPhases.describe().c_str(), _pEngine->describeSubsystemInitTimings().c_str() );
            }
            void onMainLoopEntered() override
            {
                // 시작 시간은 개발 계층을 만든 때(호스트 `initialize` 첫 줄)부터 여기까지다. 표준 출력 로그는 에러가 아니면 버퍼에 머물러 있어 밖에서는 시각을 잴 수 없다.
                _mainLoopStartMicro = MonotonicClock::nowMicroseconds();
                SW_LOG_INFO_ID( "App.EnteringAppMainLoop", "Entering App Main Loop (Thin Launcher)... startup %# ms, %# file opens",
                                ( _mainLoopStartMicro - _startMicro ) / constant::time::kMicrosecondsPerMillisecond, PlatformFileUtil::getFileOpenCount() );
            }
            void onFrameEnded() override
            {
                // 메인 루프에 든 뒤 첫 프레임들이 끝난 시각 — 시작 씬을 받는 프레임이 길다(Debug 수 초).
                if ( _startupFrameCount >= kStartupFramesLogged )
                    return;
                ++_startupFrameCount;
                SW_LOG_INFO_ID( "App.StartupFrameEndMs", "Startup frame %# ended %# ms after entering the main loop", _startupFrameCount, getMainLoopElapsedMilliseconds() );
            }
            void onMainWindowShown() override
            {
                SW_LOG_INFO_ID( "App.MainWindowShownMs", "Main window shown %# ms after entering the main loop (first scene drawn)", getMainLoopElapsedMilliseconds() );
            }

        private:
            /** @brief 로그로 찍는 첫 프레임 수입니다. */
            static constexpr uint32 kStartupFramesLogged = 3;
            /** @brief 초상화 한 변의 기본 픽셀 수입니다(`--portrait-size` 가 없거나 0 이하일 때). */
            static constexpr int32 kDefaultPortraitSize = 256;

            int64 getMainLoopElapsedMilliseconds() const { return ( MonotonicClock::nowMicroseconds() - _mainLoopStartMicro ) / constant::time::kMicrosecondsPerMillisecond; }

            /**
             * @brief 초상화 굽기(`--render-portraits=a.prefab.xml,b.prefab.xml`)입니다. 게임 컴포넌트 타입이 로드된 뒤 그리고 끝낸다(창은 띄우지 않는다).
             * @details 스튜디오 씬 · 패킷은 이 스레드가, 그리기 · 읽기는 렌더 스레드가 명령으로 한다(`PortraitStudio`). 하나라도 실패하면 실패다.
             */
            HostTaskResult runPortraitBake( const CommandLineManager& commandLine ) const
            {
                string portraitList;
                if ( commandLine.getArgument( CommandLineArgument::RENDER_PORTRAITS, portraitList ) == false || portraitList.empty() )
                    return HostTaskResult::NotRequested;
                int32        portraitSize = kDefaultPortraitSize;
                AbsolutePath portraitDir;
                commandLine.getArgument( CommandLineArgument::PORTRAIT_SIZE, portraitSize );
                commandLine.getArgument( CommandLineArgument::PORTRAIT_DIR, portraitDir );
                // 굽는 동안 스플래시를 남기지 않는다. 렌더러 없는 구성은 스플래시 단계가 없다.
                if ( SplashSubsystem* const pSplashHost = engine::findSubsystem<SplashSubsystem>() )
                    pSplashHost->getSplash().dismiss();
                RenderThread* const pRenderThread = _pEngine->getRenderThread();
                if ( pRenderThread == nullptr || pRenderThread->getDevice() == nullptr )
                {
                    SW_LOG_ERROR_ID( "HostDevLayer.PortraitBakeNeedsRender", "Portrait bake needs a render thread with a device (--render-portraits)" );
                    return HostTaskResult::Failed;
                }
                const bool bBaked = PortraitStudio::renderPrefabList( *pRenderThread, engine::getSubsystem<SceneManager>().getSceneEngineSubsystems(), portraitList, portraitDir,
                                                                      static_cast<uint32>( portraitSize > 0 ? portraitSize : kDefaultPortraitSize ) );
                return bBaked ? HostTaskResult::Finished : HostTaskResult::Failed;
            }

        private:
            FileWatchService _fileWatch; ///< 프로세스의 파일 감시 창구 — 아래 둘이 빌려 써 맨 앞에 둔다(마지막에 사라진다)
            HostDevTools     _devTools;
#if SW_WITH_DYNAMIC_MODULES
            ModuleReloadDriver _moduleReload;
#endif
            SubsystemScope*     _pHost       = nullptr;  ///< 호스트 범위(`addToHostScope` — 호스트의 부팅 열 소유)
            Engine*             _pEngine     = nullptr;  ///< 호스트의 엔진(`addToHostScope` — 호스트의 부팅 열 소유)
            IAutomationRuntime* _pAutomation = nullptr;  ///< 자동화 실행기(엔진 범위의 서브시스템 — 빌려 쓴다). 실행기를 짓지 않은 구성은 nullptr
            PhaseTimings        _startupPhases;          ///< 기동 단계표(만든 때부터)
            int64               _startMicro;             ///< 이 계층을 만든 시각(호스트 `initialize` 첫 줄 — 시작 시간의 기준)
            int64               _mainLoopStartMicro = 0; ///< 메인 루프에 들어간 시각 — 첫 그림까지 걸린 시간의 기준
            uint32              _startupFrameCount  = 0; ///< 로그로 찍은 첫 프레임 수
        };
    } // namespace

    SW_HOST_DEV_API unique_ptr<IHostDevLayer> makeHostDevLayer()
    {
        return make_unique<HostDevLayer>();
    }
} // namespace sw
