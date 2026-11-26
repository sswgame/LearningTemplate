/**
 * @file App.cpp
 * @brief 런타임 클라이언트 앱(얇은 런처)과 그 진입(`AppEntry::run`)입니다. 루프 클래스는 이 TU 안에만 있다 — 밖에 보이는 것은 `AppEntry` 하나다(언리얼 `LaunchEngineLoop` 의 `FEngineLoop` 자리).
 *
 * 소유권: App 은 최상위 창 콜백과 부팅 열(`HostLaunch` — Engine · 모듈 관리자 · 호스트 서비스)만 들고, 나머지 엔진 핵심 로직은 모두 Engine 에 맡깁니다.
 *
 * @details App 이 직접 아는 것은 네 가지로 제한합니다. 부팅 순서, 창, 프레임 순서, 그리고 호스트 ↔ 모듈 콜백 연결입니다.
 *          시간 정책은 FixedTimestep, 장치 교체 · 다시 만들기와 모듈 수명은 모듈 관리자(`GameModuleManager` — 교체 요청은 개발 도구의 RHIBackendSwitcher)가 맡습니다.
 *          개발 전용 몫(에디터 모듈 관리자 · 자동화 실행기 · 핫 리로드 · 개발 콘솔 · 셸 액션 · 에디터 모듈 헤드리스 작업 · 초상화 굽기 · 기동 측정)은 개발 계층(`IHostDevLayer` — Server 와 같이 쓴다)이
 *          DevTools 에 든다 — 배포 App 은 그 코드를 컴파일하지 않고, 개발 전용 인자를 받으면 오류로 멈춘다. 크래시 보고 프로세스는 배포도 쓰므로 이 TU 의 `AppHeadlessTasks` 다.
 *          자동화가 찾아야 하는 호스트 객체(리로드 구동기)는 정적 포인터가 아니라 호스트 범위(`HostLaunch::getHostScope`)에 걸고 `engine::findSubsystem<T>()` 로 찾게 합니다.
 *          런타임 UI 에 끼우는 것(일시정지 메뉴의 "끝내기", 운영체제 클립보드, 스크린 리더)은 엔진의 `UIPlatformBinding` 이 맡습니다.
 */
#include "pch.h"

#include "App/AppEntry.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/UserDataPath.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/FixedTimestep.h"

#include "Engine/Audio/AudioSubsystem.h"
#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/QuitRequest.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Telemetry/CrashReportSubsystem.h"
#include "Engine/Telemetry/CrashReportUploader.h"
#include "Engine/UI/UIPlatformBinding.h"
#include "Engine/UserSettings/UserSettingsHost.h"
#include "Engine/Window/GenericWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/SplashSubsystem.h"
#include "Engine/Window/SplashWindow.h"

#include "ModuleManager/GameModuleManager.h"
#include "ModuleManager/Host/HostDevLayer.h"
#include "ModuleManager/Host/HostLaunch.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

#if defined( SW_PLATFORM_WEB )
    #include <emscripten/emscripten.h>
    #include <stdlib.h>

namespace
{
    /** @brief 웹 영속 폴더입니다 — IndexedDB 를 비추는 IDBFS 마운트(유니티 WebGL 의 /idbfs 와 같은 모양). */
    constexpr const char* kWebPersistentRoot = "/persistent";
} // namespace

// IDBFS 를 붙이고 지난 세션의 파일을 읽어 올 때까지 기다린다(JSPI) — 본문은 App/Web/WebPersistentRoot.js(Emscripten JS 라이브러리)다.
extern "C" void swMountWebPersistentRoot( const char* pRoot );
// 영속 폴더를 IndexedDB 로 내리기 시작한다(기다리지 않는다 — 페이지가 남아 있어 끝난다). 앱이 끝날 때 부른다: 숨김 · 닫힘 없이 끝난 세션의 마지막 세이브를 잃지 않게.
extern "C" void swFlushWebPersistentRoot();
// 브라우저의 다음 프레임까지 멈춘다(JSPI) — 본문은 Engine/Window/Web/WebFrame.js 다.
extern "C" void swWebFrameWait();
#endif

namespace sw
{
    struct EngineConfig;
    struct FramePacingInputs;
    struct NativeWindowEvent;

    class CommandLineManager;
    class FramePacer;
    class GameModuleManager;
    class GenericWindow;
    enum class WindowLifecycleEvent : uint8;
} // namespace sw

namespace sw
{
    /** @brief 창의 OS 메시지를 엔진에 전달하고 메인 루프를 돌리는 얇은 래퍼입니다. */
    class App
    {
    public:
        App();
        ~App();

        /** @brief 창을 만들고 모듈과 엔진을 초기화합니다. */
        [[nodiscard]] bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 모든 자원을 해제합니다. */
        void shutdown();
        /**
         * @brief 메인 루프에 들어갈 준비를 합니다. 루프는 플랫폼 진입점이 돌린다(데스크톱은 `main` 의 `while`, 웹 · 모바일은 프레임 콜백).
         * @return 돌 루프가 있으면 true 입니다. 헤드리스 부팅 · 한 번 하고 끝나는 작업은 false 이고 그때는 `updateMainLoop` · `endMainLoop` 를 부르지 않습니다.
         */
        [[nodiscard]] bool beginMainLoop();
        /** @brief 창 메시지를 처리하고 한 프레임을 돕니다. 막지 않습니다. @return 루프를 끝낼 때면 false 입니다. */
        [[nodiscard]] bool updateMainLoop();
        /** @brief 루프가 끝났습니다. */
        void endMainLoop();
        /** @brief 루프를 끝낸 앱을 내리고 종료 코드를 돌려줍니다(`endMainLoop` → 종료 코드 → `shutdown`). 종료 코드는 내리기 전에 읽는다(시나리오 결과 — `Engine::requestQuit`). */
        [[nodiscard]] int32 finishMainLoop();
        /** @brief `initialize` 가 false 를 돌려준 뒤의 프로세스 종료 코드입니다. 이 기계 · 빌드가 그 RHI 백엔드를 못 돌리면 `kRHIUnusableHereExitCode`, 그 밖은 -1 입니다. */
        int32 getInitFailureExitCode() const;
        /** @brief 루프가 끝난 뒤 돌려줄 종료 코드입니다(`Engine::requestQuit`. 요청이 없었으면 0). */
        int32 getExitCode() const;

    private:
        /** @brief 부팅 열이 소유한 모듈 관리자입니다(배포 · 에디터를 끈 실행은 `GameModuleManager`, `-EnableEditor` 는 개발 계층의 에디터 관리자). 호스트 로더가 돌기 전과 `shutdown` 뒤에는 nullptr 입니다. */
        GameModuleManager* getModuleManager() const;
        /** @brief 개발 계층에 기동 단계 하나를 적습니다(배포는 개발 계층이 없어 아무것도 하지 않는다). */
        void markStartupPhase( const utf8* pPhaseName ) const;

        // 부팅 단계. initialize() 가 차례로 부른다.
        /** @brief 플랫폼 창을 확보합니다. Engine 가 이미 만들어 두었으면 그 소유권을 넘겨받습니다. */
        bool acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager );
        /**
         * @brief 기동 단계 `ModuleLoadStage` 의 호스트 로더입니다. 타입 공급자 모듈(GameFramework, 키트, SWGame)의 이미지를 로드해 그 타입을 등록합니다.
         * @details 헤드리스 작업(씬 쿠킹)도 이 단계를 지나므로 쿠킹이 게임과 키트 컴포넌트를 제 타입으로 쿠킹합니다. 인스턴스는 게임 호스트 서비스가 만듭니다.
         *          명령줄을 처음 보는 호스트 자리라 여기서 `-EnableEditor` 를 읽고(에디터 관리자는 개발 계층이 만든다), 개발 계층이 없는 배포가 개발 전용 인자를 받았으면 오류로 멈춘다.
         */
        [[nodiscard]] bool loadModuleLibraries();
        // 호스트 서비스 — 이름 · 의존 · 몸통을 한 형식에 둔다(`App.cpp`). 중첩 형식이라 App 의 멤버를 쓴다. `initialize` 가 `HostLaunch::addHostSubsystem` 로 더한다.
        class GameHostSubsystem;
        class UserSettingsHostSubsystem;
        class RuntimeUIHooksHostSubsystem;
        /** @brief 창 콜백 · 개발 도구의 프레임 시작 훅을 연결합니다(호스트 서비스가 선 뒤). */
        void bindHostCallbacks();

        /** @brief 첫 그림이 나왔으면 스플래시를 닫고 주 창을 띄웁니다(개발 계층이 첫 프레임들의 시각을 남긴다). */
        void onStartupFrameEnded();
        /** @brief 이번 프레임의 페이싱 입력을 모읍니다. 에디터가 이번 프레임의 다시 그릴 까닭을 정한 뒤에 부릅니다. */
        [[nodiscard]] FramePacingInputs makeFramePacingInputs() const;
        /**
         * @brief 메인 루프 한 바퀴입니다. 넷 세션의 일(리슨 호스트의 `[frame=N]` · 클라이언트 상태 해시)은 여기 없다 — 세션이 엔진 프레임 훅으로 한다(`GameNetSession::makeClientHostFrameHooks`).
         * @return 종료 요청을 봐서 프레임을 돌지 않았으면 false 입니다.
         */
        [[nodiscard]] bool runFrame( FramePacer& framePacer );

        /** @brief 설정 파일을 다시 읽었습니다. EngineConfig 면 프레임 시간 정책(최대 델타 · 고정 스텝)을 다시 정합니다. */
        void onConfigReloaded( const hashed_string& configTypeName );
        /** @brief 네이티브 창 이벤트를 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief 주 창 수명 이벤트를 입력 · 오디오에 넘깁니다 — 앱이 멈추면 입력을 놓고 진동 · 소리를 멈춘다(감사 P-6). 표면은 렌더러가 따로 받는다. */
        void onAppLifecycle( WindowLifecycleEvent event );
        /** @brief 창의 모달 루프(Win32 끌기 · 크기 조절) 동안 메인 루프 대신 프레임 하나를 돌립니다. 메인 루프 밖이거나 표면이 멈췄으면 아무것도 하지 않는다. */
        void onWindowModalLoopUpdate();

    private:
        /**
         * @brief 부팅 열입니다 — 호스트 범위 · 개발 계층 · 엔진 · 모듈 관리자 · 호스트 서비스의 수명과 순서를 듭니다(Server 와 같은 것, `HostLaunch`).
         * @details 맨 앞 멤버라 맨 나중에 사라진다. 호스트 서비스의 몸통이 아래 멤버를 쓰므로 `shutdown` 이 그 멤버가 사라지기 전에 호스트 서비스를 내린다.
         */
        HostLaunch _launch;
        /** @brief 주 창입니다(관찰 포인터). 소유자는 엔진 범위의 `MainWindowSubsystem` 이라 엔진 종료 때 창이 파괴된다. `acquireMainWindow` 가 채운다. */
        GenericWindow* _pWindow;

        FixedTimestep    _fixedTimestep;
        UserSettingsHost _userSettingsHost;
        /** @brief 런타임 UI 의 호스트 훅(클립보드 · 스크린 리더 · 끝내기)입니다. 호스트 서비스 `RuntimeUIHooksHostSubsystem` 가 세우고 내린다. */
        UIPlatformBinding _uiPlatformBinding;

        // 프레임마다 다시 만들 이유가 없는 델리게이트다. bindHostCallbacks 에서 한 번 묶는다.
        /** @brief 에디터 모드에서만 연결됩니다(모듈 관리자의 `getSceneViewCamera`). 비어 있으면 Engine 가 씬 카메라를 씁니다. */
        ViewCameraProviderDelegate _sceneViewCameraProvider;
        /** @brief 설정 다시 읽기 알림(`ConfigManager::onConfigReloaded`)의 구독입니다. 엔진이 설정 표를 내리면 무효가 된다. */
        Subscription _configReloadedSubscription;
        /** @brief 주 창의 메시지 처리기 칸 구독입니다(`bindHostCallbacks`). 창이 엔진 종료 때 파괴되면 무효가 된다. 크기 칸은 엔진이 쥔다(스왑체인). */
        Subscription _windowMessageSubscription;
        Subscription _appLifecycleSubscription;    ///< 주 창 수명 구독자(`onAppLifecycle`)
        Subscription _modalLoopUpdateSubscription; ///< 주 창 모달 루프 틱 칸 구독(`onWindowModalLoopUpdate`)
        /// @brief 메인 루프가 쓰는 페이서입니다(`beginMainLoop` 이 주 창 서비스에서 받는다). 루프 밖이면 nullptr 입니다.
        FramePacer*            _pFramePacer;
        uint8                  _bEnableEditor        : 1; ///< `-EnableEditor` 를 받았다(`loadModuleLibraries` — 개발 계층의 에디터 관리자를 쓴다)
        uint8                  _bQuitAfterInitialize : 1; ///< 한 번 하고 끝나는 작업(`--render-portraits`)을 마쳤다. 루프에 들어가지 않는다.
        uint8                  _bRenderPaused        : 1; ///< 지난 틱에 표면이 없거나 앱이 멈춰 프레임을 돌지 않았다. 다시 돌 때 시계를 새로 잡는다
        uint8                  _bShowWindowPending   : 1; ///< 첫 그림이 나오면 스플래시를 닫고 주 창을 띄운다(`onStartupFrameEnded`)
        uint8                  _bActiveSceneSeen     : 1; ///< 지난 프레임 끝에 활성 씬이 있었다 — 다음 프레임 끝에 그 씬의 첫 그림이 나와 있다
        [[maybe_unused]] uint8 _reserved             : 2;
    };
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @struct AppHeadlessTasks
         * @brief 창과 RHI 없이 시작한 App 이 인자에 따라 실행하는 작업들입니다. 엔진의 `Headless` 단계가 하지 못하는 일(크래시 보고, 엔진이 선 뒤의 에디터 모듈 작업)만 둡니다.
         * @details 에디터 모듈 작업(로컬라이제이션, 임포트, 참조 검사)은 개발 계층의 표가 든다(`DevTools/Host/EditorModuleHeadlessTasks`). 여기서는 부르기만 한다.
         *          실패한 작업이 하나라도 있으면 `App::initialize` 가 false 를 반환한다(종료 코드가 0 이 아님).
         */
        struct AppHeadlessTasks
        {
            /**
             * @brief 크래시 보고 프로세스(`-crash-reporter=<폴더>`)입니다. 엔진은 명령줄 처리까지만 시작한 상태이며(서비스와 게임 모듈이 없습니다) 번들만 보내고 끝냅니다. 청해지지 않았으면 false 입니다.
             * @note App 은 GameFramework 의 HTTP 클라이언트를 링크하지 않아(층) 이 프로세스의 업로더는 보내지 않는 기본값이고 묶음을 건드리지 않습니다(`Kept`).
             *       끝점이 있는 게임은 다음 기동의 게임 프로세스가 같은 폴더를 올립니다(`TelemetryUploadLink`).
             */
            static bool runCrashReporter( const CommandLineManager& commandLine )
            {
                AbsolutePath reporterFolder;
                if ( commandLine.getArgument( CommandLineArgument::CRASH_REPORTER, reporterFolder ) == false || reporterFolder.empty() )
                    return false;
                NullCrashReportUploader       uploader;
                [[maybe_unused]] const uint32 sentCount = CrashReportSubsystem::runReporter( reporterFolder, uploader );
                SW_LOG_INFO_ID( "App.ReportSent", "[CrashReporter] %# report(s) sent from '%#'", sentCount, reporterFolder.c_str() );
                return true;
            }

            /**
             * @brief 헤드리스로 선 App 의 작업을 실행합니다 — 크래시 보고(`-crash-reporter`)면 그것만 하고, 아니면 개발 계층 @p pDevLayer 의 에디터 모듈 작업 표를 돌립니다(배포는 nullptr).
             * @return 실패한 작업이 없으면 true 입니다(명령줄이 없으면 아무것도 실행하지 않고 true).
             */
            [[nodiscard]] static bool runAll( const CommandLineManager* pCommandLine, IHostDevLayer* pDevLayer )
            {
                if ( pCommandLine == nullptr )
                    return true;
                if ( runCrashReporter( *pCommandLine ) )
                    return true; // 이 작업만 하고 끝낸다
                // 에디터 모듈 작업은 개발 계층의 표가 든다 — 배포에는 개발 계층도 그 인자도 없다.
                return pDevLayer == nullptr || pDevLayer->runEditorModuleHeadlessTasks( *pCommandLine );
            }
        };

        struct AppInternal
        {
            /**
             * @brief 개발 계층에만 있는 일(에디터 · 자동화 시나리오 · 초상화 굽기)을 청하는 인자가 있으면 true 입니다. 배포는 그 일을 조용히 건너뛰지 않고 기동 오류로 멈춘다.
             */
            static bool hasDevOnlyArgument( const CommandLineManager& commandLine )
            {
                bool   bEnableEditor = false;
                string value;
                commandLine.getArgument( CommandLineArgument::ENABLE_EDITOR, bEnableEditor );
                return bEnableEditor || commandLine.getArgument( CommandLineArgument::SCENARIO, value ) || commandLine.getArgument( CommandLineArgument::SCENARIO_RECORD, value ) ||
                       commandLine.getArgument( CommandLineArgument::RENDER_PORTRAITS, value );
            }

            /** @brief 이 기계 · 빌드가 그 백엔드를 못 돌린다는(환경) 결과면 true 입니다. 결함일 수 있는 `Failed` 는 false 입니다. */
            static constexpr bool isRHIUnusableHere( RHIInitResult result )
            {
                return result == RHIInitResult::BackendNotBuilt || result == RHIInitResult::DriverUnsupported;
            }

            /** @brief 엔진 설정에서 프레임 시간 정책 값을 꺼냅니다(값의 출처는 `Config/Engine/EngineConfig.json` 하나다). */
            static FixedTimestepParams makeFixedTimestepParams( const EngineConfig& config )
            {
                FixedTimestepParams params;
                params._maxFrameDeltaTime    = config._maxFrameDeltaTime;
                params._fixedDeltaTime       = config._fixedDeltaTime;
                params._maxFixedStepPerFrame = config._maxFixedStepPerFrame;
                return params;
            }

            /** @brief 감시 값 `Engine.FramePacingState` — 앱 루프가 이번 프레임에 고른 페이싱 상태 번호입니다. 주 창 서비스가 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readFramePacingState( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                if ( pWindowHost == nullptr )
                    return false;
                outValue = static_cast<float64>( static_cast<uint32>( pWindowHost->getFramePacer().getState() ) );
                return true;
            }

            /** @brief 표면이 없거나 앱이 멈춘 동안 창 메시지를 기다리는 최대 시간입니다. 메시지(수명 이벤트)가 오면 곧 깬다(`GenericWindow::waitForMessages`). */
            static constexpr uint32 kPausedWaitMilliseconds = 10;

            /** @brief 감시 값 `Window.RenderPausedTicks` — 표면을 잃거나 앱이 멈춰 프레임을 돌지 않은 메인 루프 틱의 누적입니다. 주 창이 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readWindowRenderPausedTicks( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GenericWindow* pWindow = MainWindowSubsystem::findMainWindow();
                if ( pWindow == nullptr )
                    return false;
                outValue = static_cast<float64>( pWindow->getRenderPausedTickCount().get() );
                return true;
            }

            /** @brief 감시 값 `Window.Minimized` — 주 창이 최소화돼 있으면 1 입니다(플랫폼 통보 기준). 주 창이 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readWindowMinimized( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GenericWindow* pWindow = MainWindowSubsystem::findMainWindow();
                if ( pWindow == nullptr )
                    return false;
                outValue = pWindow->isMinimized() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( engineFramePacingState, "Engine.FramePacingState",
                         "App loop pacing this frame: 0 Foreground, 1 Background (gv_backgroundFrameRate), 2 Minimized, 3 Automation, 4 Server, 5 FullSpeed (gv_framePacing), 6 EditorIdle",
                         &AppInternal::readFramePacingState );
    SW_AUTOMATION_WATCH( windowRenderPausedTicks, "Window.RenderPausedTicks", "Main loop ticks that ran no frame because the surface was lost or the app was suspended", &AppInternal::readWindowRenderPausedTicks );
    SW_AUTOMATION_WATCH( windowMinimized, "Window.Minimized", "1 while the main window is minimized (as the platform reported it)", &AppInternal::readWindowMinimized );
} // namespace sw

namespace sw
{
    App::App()
        : _launch{}
        , _pWindow{ nullptr }
        , _fixedTimestep{}
        , _userSettingsHost{}
        , _uiPlatformBinding{}
        , _sceneViewCameraProvider{}
        , _configReloadedSubscription{}
        , _windowMessageSubscription{}
        , _appLifecycleSubscription{}
        , _modalLoopUpdateSubscription{}
        , _pFramePacer{ nullptr }
        , _bEnableEditor{ SW_FALSE }
        , _bQuitAfterInitialize{ SW_FALSE }
        , _bRenderPaused{ SW_FALSE }
        , _bShowWindowPending{ SW_FALSE }
        , _bActiveSceneSeen{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    App::~App() = default;

    /** @brief 게임(과 에디터) 인스턴스 — 모듈 호스트입니다. 모듈 관리자가 게임(에디터 관리자면 에디터도) 인스턴스를 만든다(라이브러리는 `loadModuleLibraries` 가 이미 올렸다). */
    class App::GameHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "Game";

        explicit GameHostSubsystem( App& app )
            : _app{ app }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            const bool bStarted = _app.getModuleManager()->initializeClient( _app._launch.getLiveReloadManager(), _app._launch.getEngine().getRHI(), _app._pWindow,
                                                                             _app._launch.getEngine().getRenderDriver() );
            if ( bStarted == false )
            {
                SW_LOG_ERROR_ID( "App.GameModuleManagerInitializationFailed", "GameModuleManager initialization failed." );
                return SubsystemInitResult::Failed;
            }
            // 시나리오 체인이 단계마다 게임을 처음부터 다시 세운다(`AutomationSession::startStage` 가 `engine::findSubsystem<IGameRestarter>()` 로 찾는다).
            return _app._launch.getHostScope().bind<IGameRestarter>( _app._launch.getModuleManager() ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        void shutdownSubsystem() override
        {
            _app._launch.getHostScope().unbind<IGameRestarter>();
            // 에디터 shutdown 이 씬 뷰 · 게임 뷰 RT 를 놓을 때 렌더 스레드와 RHI 디바이스를 쓴다 — 호스트 서비스는 엔진 종료 앞에서 내려간다.
            // 에디터 렌더 훅은 에디터 관리자가 스스로 뗀다(소멸자 — 렌더 스레드가 사라진 관리자를 부르지 않는다).
            _app.getModuleManager()->shutdown();
            _app._launch.releaseModuleManager();
        }
        const utf8* getSubsystemName() const override { return kSubsystemName; }

    private:
        App& _app;
    };

    /** @brief 사용자 설정의 화면 적용입니다. */
    class App::UserSettingsHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "UserSettings";

        explicit UserSettingsHostSubsystem( App& app )
            : _app{ app }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            _app._userSettingsHost.initialize( _app._launch.getEngine().getRenderDriver(), _app._pWindow );
            return SubsystemInitResult::Succeeded;
        }
        void        shutdownSubsystem() override { _app._userSettingsHost.shutdown(); }
        const utf8* getSubsystemName() const override { return kSubsystemName; }

    private:
        App& _app;
    };

    /** @brief 런타임 UI 의 클립보드 · 접근성 · 끝내기 명령입니다. */
    class App::RuntimeUIHooksHostSubsystem final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "RuntimeUIHooks";

        explicit RuntimeUIHooksHostSubsystem( App& app )
            : _app{ app }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            _app._uiPlatformBinding.install( _app._pWindow, _app._bEnableEditor == SW_TRUE );
            return SubsystemInitResult::Succeeded;
        }
        void        shutdownSubsystem() override { _app._uiPlatformBinding.remove(); }
        const utf8* getSubsystemName() const override { return kSubsystemName; }

    private:
        App& _app;
    };

    GameModuleManager* App::getModuleManager() const
    {
        return _launch.getModuleManager();
    }

    void App::markStartupPhase( const utf8* pPhaseName ) const
    {
        if ( IHostDevLayer* const pDevLayer = _launch.getDevLayer() )
            pDevLayer->markStartupPhase( pPhaseName );
    }

    // 길이를 나누지 않는다: 호스트 범위 → 엔진 → 헤드리스 작업 → 자동화 → 설정 감시 순의 시동 열이라 순서가 곧 뜻이다.
    // `Engine::initialize` 와 같은 이유다. 기동 단계표는 개발 계층이 든다(만든 때부터 잰다 — 배포는 재지 않는다).
    bool App::initialize( int32 argc, utf8* pArgv[] )
    {
        // 리소스 루트 탐색은 Engine 가 로거를 시작한 **뒤에** 한다. 여기서 먼저 부르면 실패했을 때 로거가 없어 진단 메시지가
        // 사라지고, 반환값도 여기서는 쓸 곳이 없다.

        // 0. 호스트 범위를 먼저 세운다 — 엔진 범위의 부모다. 엔진의 `ModuleLoadStage` 기동 단계가 여기서 모듈 라이브러리 창구를 찾아 타입 공급자 모듈을 올리고
        //    (이 App 의 `loadModuleLibraries`), `ModuleUnloadStage` 종료 단계가 같은 창구로 모듈 DLL 을 내린다.
        //    개발 계층(핫 리로드 · 개발 도구)은 DevTools.dll 이 구현한다 — ModuleManager 는 DevTools 를 보지 않아 호스트가 만들어 넘긴다. 배포는 짓지 않는다.
        unique_ptr<IHostDevLayer> pDevLayer;
        if constexpr ( build::kShipping == false )
            pDevLayer = makeHostDevLayer();
        if ( _launch.initializeHostScope( SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &App::loadModuleLibraries, this ), "App", std::move( pDevLayer ) ) == false )
            return false;
        markStartupPhase( "hostScope" );

        // 1. 코어 매니저는 모두 Engine 가 초기화한다(헤드리스 작업 처리 포함).
        const HostEngineStart engineStart = _launch.initializeEngine( argc, pArgv, EngineHostRole::Client );
        if ( engineStart == HostEngineStart::Failed )
            return false;
        markStartupPhase( "engine" );
        // 헤드리스 모드(예: --cook-shaders, --cook-scenes)면 스플래시와 창 UI 를 건너뛴다.
        // App 이 맡는 헤드리스 작업은 크래시 보고와, 개발 계층의 에디터 모듈 작업 표(로컬라이제이션 도구, 원본 임포트)를 부르는 일이다(`AppHeadlessTasks`).
        if ( engineStart == HostEngineStart::Headless )
            return AppHeadlessTasks::runAll( _launch.getEngine().getCommandLineManager(), _launch.getDevLayer() );

        // 무인 실행(-unattended)은 보는 사람이 없다 — 스플래시도 메인 창도 띄우지 않고 그리기 · 캡처만 한다(언리얼 -RenderOffScreen). 화면 표시를 보는 시나리오
        // (present="true")는 그 단계에서 자동화 세션이 메인 창을 띄운다(`AutomationSession::applyPresentPolicy`).
        // 스플래시는 엔진 기동이 에셋 직후 띄웠다(`SplashSubsystem`) — 여기서는 남은 단계의 진행을 적고 닫는다.
        SplashWindow           noSplash;
        SplashSubsystem* const pSplashHost = engine::findSubsystem<SplashSubsystem>();
        SplashWindow&          splash      = pSplashHost != nullptr ? pSplashHost->getSplash() : noSplash; // 렌더러 없는 구성은 스플래시 단계가 없다

        splash.updateStatus( "Loading Configuration & Display...", 0.40f );

        ConfigManager*            pConfigManager      = _launch.getEngine().getConfigManager();
        const CommandLineManager* pCommandLineManager = _launch.getEngine().getCommandLineManager();
        if ( pConfigManager == nullptr || pCommandLineManager == nullptr )
        {
            splash.dismiss();
            return false;
        }

        const EngineConfig* pEngineConfig = pConfigManager->getConfig<EngineConfig>();
        if ( pEngineConfig == nullptr )
        {
            splash.dismiss();
            return false;
        }

        // 자동화 시나리오는 엔진 기동이 끝난 지금, 모듈을 올리기 전에 시작한다(첫 단계의 시작 조건이 모듈 · 씬이 설 때까지 기다린다). 배포는 실행기가 없다.
        IHostDevLayer* const pRunningDevLayer = _launch.getDevLayer();
        if ( pRunningDevLayer != nullptr && pRunningDevLayer->startAutomation( *pCommandLineManager ) == false )
        {
            splash.dismiss();
            return false;
        }

        _fixedTimestep.configure( AppInternal::makeFixedTimestepParams( *pEngineConfig ) );
        // 에디터가 Config/ 를 감시해 다시 읽으면(ConfigManager::reloadConfigFile) 프레임 시간 정책도 따라간다.
        _configReloadedSubscription = pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &App::onConfigReloaded, this ) );

        // 2. 창 소유권을 가져온다(초기화 중에는 숨긴 상태로 시작한다).
        splash.updateStatus( "Initializing Platform Window & Graphics...", 0.60f );
        if ( acquireMainWindow( *pEngineConfig, *pCommandLineManager ) == false )
        {
            splash.dismiss();
            return false;
        }
        markStartupPhase( "window" );

        // 3. 호스트 서브시스템(게임 · 에디터 인스턴스, 교체기, 사용자 설정, 런타임 UI 훅, 개발 콘솔)을 의존 순서로 세운다. 내리는 것은 `shutdown` 의 역순 한 줄이다.
        splash.updateStatus( "Loading Modules & Compiling Shaders...", 0.75f );
        _launch.addHostSubsystem<GameHostSubsystem>( *this );
        _launch.addHostSubsystem<UserSettingsHostSubsystem>( *this );
        _launch.addHostSubsystem<RuntimeUIHooksHostSubsystem>( *this );
        if ( pRunningDevLayer != nullptr )
            pRunningDevLayer->addConsoleHostService( _launch, GameHostSubsystem::kSubsystemName, _bEnableEditor == SW_TRUE );
        if ( _launch.startHostSubsystems() == false )
        {
            splash.dismiss();
            return false;
        }
        markStartupPhase( "modules" );

        // 모듈이 선 뒤 한 번 하고 끝나는 작업(초상화 굽기)은 개발 계층이 맡는다. 실행했으면 창을 띄우지 않고 끝낸다.
        const HostTaskResult afterServices = pRunningDevLayer != nullptr ? pRunningDevLayer->runAfterHostSubsystems( *pCommandLineManager ) : HostTaskResult::NotRequested;
        if ( afterServices != HostTaskResult::NotRequested )
        {
            splash.dismiss();
            _bQuitAfterInitialize = SW_TRUE;
            return afterServices != HostTaskResult::Failed;
        }

        // 4. 창 콜백과 이벤트 전달을 설정한다.
        splash.updateStatus( "Finalizing Setup...", 0.95f );
        bindHostCallbacks();

        // 보는 사람이 있으면(무인이 아니거나, 무인이어도 지켜보는 표시가 있으면) 창을 띄운다 — `MainWindowSubsystem::shouldRenderOffScreen`.
        // 띄우는 것은 첫 그림이 나온 뒤다(`onStartupFrameEnded`): 첫 프레임은 시작 씬을 받느라 길어(Debug 수 초) 그동안 빈 흰 창이 보였다. 그 사이는 스플래시가 진행을 보인다
        // (언리얼도 첫 맵이 설 때까지 스플래시를 둔다).
        if ( MainWindowSubsystem::shouldRenderOffScreen( *_launch.getEngine().getCommandLineManager() ) == false )
        {
            splash.updateStatus( "Loading First Scene...", 1.0f );
            _bShowWindowPending = SW_TRUE;
        }
        else
            splash.dismiss();

        if ( pRunningDevLayer != nullptr )
            pRunningDevLayer->logStartupPhases();

        return true;
    }

    bool App::acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager )
    {
        uint32 width  = engineConfig._window._width;
        uint32 height = engineConfig._window._height;
        commandLineManager.getArgument( CommandLineArgument::WIDTH, width );
        commandLineManager.getArgument( CommandLineArgument::HEIGHT, height );

        // 주 창은 엔진 범위의 서브시스템(MainWindow)이 소유한다. 엔진 기동(RHI)이 기본 창을 만들어 넘겨 두었으면 그것을 쓰고, 없으면(헤드리스 작업 등) 여기서 만들어 넘긴다.
        // App 은 관찰 포인터만 든다 — 창은 엔진 종료 때 그 서브시스템이 파괴한다.
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( pWindowHost == nullptr )
        {
            SW_LOG_ERROR_ID( "App.EngineScopeNoMainWindow", "The engine scope has no MainWindow subsystem!" );
            return false;
        }
        if ( pWindowHost->getWindow() == nullptr )
        {
            unique_ptr<GenericWindow> window = GenericWindow::createMainWindow( engine::getSubsystem<PackEngineSettings>()._windowTitle.c_str(), width, height, commandLineManager );
            if ( window == nullptr )
            {
                SW_LOG_ERROR_ID( "App.FailedCreatePlatformWindow", "Failed to create platform window!" );
                return false;
            }
            window->showWindow( false );
            if ( pWindowHost->attach( window ) == false )
            {
                SW_LOG_ERROR_ID( "App.MainWindowOwnerRefused", "The main window owner refused the platform window!" );
                return false;
            }
        }
        _pWindow = pWindowHost->getWindow();
        return true;
    }

    bool App::loadModuleLibraries()
    {
        const CommandLineManager* const pCommandLine = _launch.getEngine().getCommandLineManager();
        if ( pCommandLine == nullptr )
            return false;
        IHostDevLayer* const pDevLayer = _launch.getDevLayer();
        if ( pDevLayer == nullptr && AppInternal::hasDevOnlyArgument( *pCommandLine ) )
        {
            SW_LOG_ERROR_ID( "App.DevOnlyArgumentInShipping", "-EnableEditor / -scenario / -scenario-record / --render-portraits need a development build (Debug or Release) - Shipping has no dev layer" );
            return false;
        }
        bool bEnableEditor = false;
        pCommandLine->getArgument( CommandLineArgument::ENABLE_EDITOR, bEnableEditor );
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        // App 은 빌드가 담은 모든 대상을 로드한다(Game 은 클라이언트와 리슨 서버용 서버 모듈, Client 는 클라이언트). 전용 서버는 Server 실행 파일이 Server 대상으로 같은 해석을 한다.
        // 에디터를 켰으면 개발 계층이 에디터 관리자를 만든다 — 백엔드가 에디터를 지원하는지는 에디터가 안다(못 하면 에디터 초기화가 실패한다).
        unique_ptr<GameModuleManager> pModuleManager = _bEnableEditor == SW_TRUE ? pDevLayer->makeEditorModuleManager() : make_unique<GameModuleManager>();
        return _launch.loadModuleLibraries( ModuleCatalog::getBuildTargetMask(), std::move( pModuleManager ) );
    }

    void App::onAppLifecycle( const WindowLifecycleEvent event )
    {
        // 표면을 잃고 찾는 것(SurfaceLost · SurfaceRestored)은 렌더러 몫이다 — 앱이 멈추고 돌아오는 것만 본다.
        if ( event != WindowLifecycleEvent::AppSuspended && event != WindowLifecycleEvent::AppResumed )
            return;
        const bool bSuspended = event == WindowLifecycleEvent::AppSuspended;
        if ( bSuspended )
        {
            if ( InputManager* const pInput = engine::findSubsystem<InputManager>() ) // 선택: 헤드리스 구성은 입력이 없다
                pInput->onAppSuspended();
        }
        if ( AudioSubsystem* const pAudio = engine::findSubsystem<AudioSubsystem>() ) // 선택: 오디오 없는 구성
        {
            if ( bSuspended )
                pAudio->suspendOutput();
            else
                pAudio->resumeOutput();
        }
    }

    void App::bindHostCallbacks()
    {
        // 주 창의 처리기 칸은 주인이 하나다 — 구독을 쥐고, 창(엔진 종료 때 파괴)이 먼저 사라지면 구독이 무효가 된다.
        _windowMessageSubscription   = _pWindow->bindCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );
        _appLifecycleSubscription    = _pWindow->addLifecycleListener( SW_DELEGATE_METHOD( WindowLifecycleDelegate, &App::onAppLifecycle, this ) );
        _modalLoopUpdateSubscription = _pWindow->bindModalLoopUpdateCallback( SW_DELEGATE_METHOD( WindowModalLoopUpdateDelegate, &App::onWindowModalLoopUpdate, this ) );

        // 연결 대상이 프레임마다 바뀌지 않으므로 루프 안이 아니라 여기서 한 번만 연결한다.
        // 에디터 뷰 카메라는 에디터 모드에서만 연결한다. 비어 있다는 사실이 곧 "씬 카메라를 쓴다"는 뜻이므로 루프에서 모드를
        // 나눌 필요가 없다.
        if ( _bEnableEditor == SW_TRUE )
        {
            // 모듈 매니저는 루프가 도는 동안 산다(호스트 서비스 `GameHostSubsystem` 가 세우고 루프 뒤 `shutdown` 이 내린다) — App 이 중계하지 않고 바로 묶는다.
            _sceneViewCameraProvider = SW_DELEGATE_METHOD( ViewCameraProviderDelegate, &GameModuleManager::getSceneViewCamera, static_cast<const GameModuleManager*>( getModuleManager() ) );
            // 창 최소 크기 · 에디터 렌더 훅은 에디터 관리자가, 단언 대화상자(`AssertDialog`)는 개발 도구가 스스로 건다 — 배포는 DevTools 를 링크하지 않는다.
        }
    }

    void App::shutdown()
    {
        // 호스트 서브시스템을 선 순서의 역순으로 내린다(순서는 각 서비스 형식의 의존 선언 — 게임에 기대는 콘솔이 게임보다 먼저, 게임이 엔진보다 먼저).
        _launch.stopHostSubsystems();

        // 창은 엔진(RHI) 뒤에 파괴한다 — 소유자(MainWindow 서브시스템)가 RHI 에 기대는 순서로 내린다(제목은 창 안의 고정 버퍼라 누수 보고에 남지 않는다).
        _launch.shutdownEngine();

        // 주 창은 엔진 범위의 MainWindow 서브시스템이 소유한다 — 엔진 종료(RHI 가 내려간 뒤)가 파괴했다. 관찰 포인터만 비운다.
        _pWindow = nullptr;
    }

    void App::onStartupFrameEnded()
    {
        IHostDevLayer* const pDevLayer = _launch.getDevLayer();
        if ( pDevLayer != nullptr )
            pDevLayer->onFrameEnded();
        if ( _bShowWindowPending == SW_FALSE )
            return;
        // 활성 씬이 선 프레임 다음 프레임이 끝나면 그 씬의 첫 그림이 나와 있다(렌더 스레드가 한 프레임 늦게 낸다). 그때 스플래시를 닫고 주 창을 띄운다.
        const bool bSceneActive = engine::getSubsystem<SceneManager>().getActiveScene() != nullptr;
        if ( bSceneActive && _bActiveSceneSeen == SW_TRUE )
        {
            _bShowWindowPending = SW_FALSE;
            if ( SplashSubsystem* const pSplashHost = engine::findSubsystem<SplashSubsystem>() ) // 선택: 렌더러 없는 구성은 스플래시가 없다
                pSplashHost->getSplash().dismiss();
            _pWindow->showWindow( true );
            if ( pDevLayer != nullptr )
                pDevLayer->onMainWindowShown();
        }
        _bActiveSceneSeen = bSceneActive ? SW_TRUE : SW_FALSE;
    }

    FramePacingInputs App::makeFramePacingInputs() const
    {
        const GameNetSession*      pNetSession = engine::areEngineSubsystemsBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        FramePacingInputs          inputs;
        const IHostDevLayer* const pDevLayer = _launch.getDevLayer();
        inputs._bAutomationRun               = pDevLayer != nullptr && pDevLayer->isAutomationBusy();
        if ( inputs._bAutomationRun )
            inputs._automationFrameRate = engine::getSubsystem<DeterministicRun>().getPolicy()._realTimeFrameRate;
        inputs._bListenHost  = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server;
        inputs._bMinimized   = _pWindow->isMinimized();
        inputs._bFocused     = _pWindow->hasFocus();
        inputs._bEditorQuiet = getModuleManager()->getFrameState()._bEditorQuiet == SW_TRUE;
        return inputs;
    }

    bool App::runFrame( FramePacer& framePacer )
    {
        framePacer.markFrameStart();

        // 프로파일 실행(-gv_profileFrames=N, 개발 도구의 프레임 관찰자)과 자동화 시나리오(-scenario)는 스스로 끝난다(종료 코드는 `getExitCode`). 부모 프로세스는 표준 입력의 quit 으로 끝낸다(개발 도구 관찰자).
        if ( _launch.getEngine().isQuitRequested() )
        {
            _pWindow->requestClose();
            return false;
        }

        // 결정적 실행의 프레임 고정 시간(0 이면 벽시계) — 채우는 쪽(자동화 · 개발 스위치)이 없는 배포본은 늘 0 이다.
        const float32   fixedFrameDelta = engine::getSubsystem<DeterministicRun>().getPolicy()._fixedFrameDelta;
        const FrameTime frameTime       = _fixedTimestep.advance( engine::getSubsystem<GameTimeScale>().get(), fixedFrameDelta );

        // 사용자 설정의 화면 변경(창 방식, 해상도, VSync)은 OS 리사이즈와 같은 시점인 프레임 시작 전에 처리한다.
        _userSettingsHost.update( frameTime._deltaTime );
        // 메시지 처리 · 화면 설정이 모은 크기 변경을 여기서 마지막 크기로 한 번 적용한다(스왑체인 resize — 감사 P-2). 멈춘 틱은 이 함수에 오지 않아 표면이 없는 동안은 미룬 채 둔다.
        _pWindow->applyPendingResize();

        _launch.getEngine().beginFrame( frameTime._deltaTime, frameTime._unscaledDeltaTime ); // 정지 메뉴와 페이드는 게임이 멈춰도 돈다.
        // 에디터 Play/Pause 상태를 여기서 한 번 고정한다. 아래 고정 스텝이 여러 번 돌아도 DLL 경계를 넘어 다시 묻지 않고,
        // 모든 단계가 같은 답을 본다.
        getModuleManager()->beginFrame();

        // 셸 액션(리로드 단축키 · 개발 콘솔 키)을 이번 프레임 것으로 갱신한 뒤 읽는다(콘솔은 씬 틱 뒤 개발 도구 관찰자). Shipping 에는 둘 다 없어 셸 InputMap 도 로드하지 않는다.
        if ( _launch.getDevLayer() != nullptr )
            _launch.getDevLayer()->pollShellActions( frameTime._deltaTime, _bEnableEditor == SW_TRUE );

        getModuleManager()->updateGameFrame( frameTime._fixedDeltaTime, frameTime._fixedStepCount, frameTime._deltaTime );

        {
            // 에디터가 없으면 바로 돌아온다. 이 호출이 게임 뷰와 씬 뷰 RT, 씬 틱 여부를 확정한다.
            SW_PROFILE_SCOPE( "GT.Editor.updateUI" );
            getModuleManager()->updateHostModuleUI();
        }

        // 이번 프레임의 페이싱 상태를 정한다 — 에디터가 이번 프레임의 다시 그릴 까닭을 정한 뒤다. 실행 인자(`-gv_framePacing` · 시나리오 · 프로파일 · 리슨 포트)가
        // 고른 상태가 창 · 에디터 상태(최소화 · 유휴 · 포커스)보다 앞선다.
        framePacer.beginFrame( makeFramePacingInputs() );

        // 카메라 포인터를 미리 잡아 두면 tick 안의 씬 전환이나 핫 리로드가 그 GameObject 를 파괴한 뒤 역참조하게 된다. 그래서
        // 조회 자체를 tick 안으로 넘긴다.
        //
        // 모듈 교체는 **틱 직전**(게임 업데이트 뒤, 씬 틱 앞)에 한다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어지기 때문이다.
        _launch.updateModuleReload();

        const ModuleFrameState& frameState = getModuleManager()->getFrameState();
        _launch.getEngine().update( frameTime._deltaTime, getModuleManager()->getViews(), _sceneViewCameraProvider, frameState._bTickScene == SW_TRUE ? SceneTick::Run : SceneTick::Pause );
        getModuleManager()->endHostModuleFrame();

        // 프레임 경계의 장치 교체 · 다시 만들기 — 개발 구성의 백엔드 교체(`gv_rhiBackend`)와 배포도 쓰는 장치 분실 재생성(`RHIDeviceLostPolicy::Recreate`)이 같은 길이다.
        // 창 모달 루프 안의 프레임은 건너뛴다 — 교체가 창을 다시 만들면 아직 그 창에서 도는 모달 루프의 HWND 가 사라진다. 예약은 남아 루프가 끝난 뒤 첫 프레임이 한다.
        // 실패는 반환값으로 따로 다루지 않는다 — 장치나 모듈 인스턴스가 없으면 모듈 관리자가 종료(DeviceLost)를 이미 요청했고, 되돌린 교체는 렌더러가 센다(`RHI.SwapFailedCount`).
        if ( _pWindow->isInModalLoopUpdate() == false )
            (void)getModuleManager()->applyPendingDeviceChange( engine::getSubsystem<QuitRequest>() );

        _launch.getEngine().endFrame();
        onStartupFrameEnded();
        // 배경이면 상한 주기까지, 최소화면 창 메시지가 올 때까지 잔다. 다른 상태는 바로 다음 프레임이다.
        framePacer.endFrame( *_pWindow );
        return true;
    }

    bool App::beginMainLoop()
    {
        // 루프는 창이 있어야 돈다. 헤드리스 부팅(예: --cook-shaders)은 창을 만들지 않으므로 여기서 끝난다. 모드 플래그가 아니라
        // 실제 선행 조건으로 적는다.
        if ( _pWindow == nullptr || _bQuitAfterInitialize == SW_TRUE )
            return false;

        if ( IHostDevLayer* const pDevLayer = _launch.getDevLayer() )
            pDevLayer->onMainLoopEntered();
        _fixedTimestep.start();

        // 페이서는 주 창 서비스가 든다 — 창(`_pWindow`)을 그 서비스에서 받았으므로 여기서 없으면 결함이다.
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( pWindowHost == nullptr )
        {
            SW_LOG_ERROR_ID( "App.EngineScopeNoMainWindow2", "The engine scope has no MainWindow subsystem - the main loop cannot pace frames" );
            return false;
        }
        _pFramePacer = &pWindowHost->getFramePacer();
        return true;
    }

    bool App::updateMainLoop()
    {
        if ( _pWindow->processMessages() == false )
            return false;
        _pWindow->advanceLifecycleTick();
        // 표면이 없거나 앱이 멈췄으면 프레임을 돌지 않는다 — 프로세스는 살아 있고 게임 시간은 멈춘다(Android onPause · 언리얼 `FAppEventManager` 가 넣는 자리). 메시지만 처리하며 기다린다.
        if ( _pWindow->isRenderPaused() )
        {
            _bRenderPaused = SW_TRUE;
            // 잠들지 않고 창 메시지를 기다린다 — 웹은 메인 스레드를 재우면 바쁜 대기이고(브라우저가 숨은 탭의 프레임 콜백을 이미 멈춘다), Android 는 ALooper 에서 명령을 기다린다(감사 P-5).
            _pWindow->waitForMessages( AppInternal::kPausedWaitMilliseconds );
            return true;
        }
        // 멈춘 동안 흐른 실제 시간을 한 프레임의 델타로 받지 않는다.
        if ( _bRenderPaused == SW_TRUE )
        {
            _bRenderPaused = SW_FALSE;
            _fixedTimestep.start();
        }
        return runFrame( *_pFramePacer );
    }

    void App::onWindowModalLoopUpdate()
    {
        // 메인 루프의 `processMessages` 안이다(창 프로시저 → 모달 루프 → WM_TIMER). 루프 밖(기동 · 종료)의 메시지 처리나 멈춘 표면이면 프레임을 돌지 않는다.
        if ( _pFramePacer == nullptr || _pWindow->isRenderPaused() )
            return;
        (void)runFrame( *_pFramePacer ); // 끝내기 요청은 창 닫기를 청하고, 모달 루프가 끝난 뒤 메인 루프의 `processMessages` 가 본다
    }

    int32 App::finishMainLoop()
    {
        endMainLoop();
        const int32 exitCode = getExitCode();
        // 시나리오 실행이면 종료도 감시한다 — 감시는 이 함수 끝(엔진이 다 내려간 뒤)에 놓인다.
        IHostDevLayer* const                       pDevLayer     = _launch.getDevLayer();
        const unique_ptr<IAutomationShutdownWatch> shutdownWatch = pDevLayer != nullptr ? pDevLayer->watchAutomationShutdown() : nullptr;
        shutdown();
        return exitCode;
    }

    void App::endMainLoop()
    {
        _pFramePacer = nullptr;
        // 창이 닫혀 끝났으면 자동화 시나리오가 그 결과를 종료 코드로 정한다(시나리오가 이미 끝냈으면 아무 일도 없다).
        _launch.getEngine().onWindowClosed();
    }

    int32 App::getInitFailureExitCode() const
    {
        // 헤드리스 작업이 정한 코드(시나리오 행렬의 시나리오 결과)가 먼저다.
        if ( _launch.getEngine().isHeadless() && _launch.getEngine().didHeadlessTaskFail() && _launch.getEngine().getHeadlessFailureExitCode() != -1 )
            return _launch.getEngine().getHeadlessFailureExitCode();
        // 기동 단계가 실패하면서 종료 코드를 골랐으면 그 코드다(시나리오 실행의 사용자 데이터 뿌리 거절 — 로드 오류 11, `AutomationUserDataIsolation`).
        if ( _launch.getEngine().isQuitRequested() && _launch.getEngine().getExitCode() != ProcessExitCode::Passed )
            return static_cast<int32>( _launch.getEngine().getExitCode() );
        // 환경 탓(백엔드가 이 빌드에 없거나 드라이버가 기능을 주지 않음)은 테스트와 스크립트가 건너뜀으로 읽는 코드로 끝낸다. 그 밖은 초기화 실패다 —
        // 표에 없는 -1 로 끝내면 자동화가 크래시로 셌다(언리얼도 초기화 실패 코드를 따로 낸다). 까닭은 초기화가 오류 로그로 남겼다.
        return AppInternal::isRHIUnusableHere( _launch.getEngine().getRHIInitResult() ) ? kRHIUnusableHereExitCode : static_cast<int32>( ProcessExitCode::InitializationFailed );
    }

    int32 App::getExitCode() const
    {
        return static_cast<int32>( _launch.getEngine().getExitCode() ); // 프로세스 경계 — 엔진 안에서는 표의 열거형이다
    }

    void App::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName )
            return;
        const ConfigManager* pConfigManager = _launch.getEngine().getConfigManager();
        const EngineConfig*  pEngineConfig  = pConfigManager != nullptr ? pConfigManager->getConfig<EngineConfig>() : nullptr;
        if ( pEngineConfig != nullptr )
            _fixedTimestep.configure( AppInternal::makeFixedTimestepParams( *pEngineConfig ) );
    }

    bool App::onWindowMessage( const NativeWindowEvent& event )
    {
        // 사람 입력은 버리기 전에 센다(데모 재생의 "손대면 멈춤"). 무인 자동화는 사람의 마우스와 키보드 입력을 여기서 버린다. 에디터(ImGui)와 게임 입력 둘 다에 닿지 않는다.
        if ( engine::areEngineSubsystemsBound() )
        {
            InputManager& inputManager = engine::getSubsystem<InputManager>();
            inputManager.recordNativeEvent( event );
            if ( inputManager.shouldDropNativeEvent( event ) )
                return false;
        }

        // 이벤트를 호스트 모듈(에디터 ImGui)에 먼저 보낸다.
        GameModuleManager* const pModuleManager    = getModuleManager();
        const bool               bConsumedByEditor = ( pModuleManager != nullptr && pModuleManager->processHostModuleEvent( event ) );

        // 에디터가 가로채지 않았을 때만 게임 InputManager 로 전달한다.
        if ( bConsumedByEditor == false && engine::areEngineSubsystemsBound() )
            engine::getSubsystem<InputManager>().processNativeEvent( event );

        // Win32 OS 수준의 포커스와 활성화(DefWindowProc)가 정상적으로 동작하도록 false 를 반환한다.
        return false;
    }

} // namespace sw

namespace sw
{
    int32 AppEntry::run( int32 argc, utf8* pArgv[] )
    {
#if defined( SW_PLATFORM_WEB )
        // 세이브 · 설정 자리 — 브라우저에는 사용자 폴더가 없어 부팅이 영속 마운트를 정한다(UserDataPath).
        swMountWebPersistentRoot( kWebPersistentRoot );
        (void)::setenv( kUserDataRootVariable, kWebPersistentRoot, 1 );
        unique_ptr<App> app = make_unique<App>(); // 웹의 C 스택은 작다 — 힙에 둔다
        if ( app->initialize( argc, pArgv ) == false )
        {
            const int32 exitCode = app->getInitFailureExitCode();
            app->shutdown();
            return exitCode;
        }
        // 루프는 데스크톱처럼 여기서 돈다 — 프레임 사이를 JSPI 로 멈춰 브라우저가 그린다. `main` 은 promising 호출이라 프레임 안의 GPU 대기(`waitFrameFence`)도 멈출 수 있다.
        if ( app->beginMainLoop() )
        {
            while ( app->updateMainLoop() )
            {
                swWebFrameWait();
            }
        }
        const int32 exitCode = app->finishMainLoop();
        app.reset();
        swFlushWebPersistentRoot();        // 앱이 다 내린 뒤(세이브 · 설정 쓰기가 끝난 뒤) 한 번
        emscripten_force_exit( exitCode ); // 오디오 · 소켓이 런타임을 붙들고 있어도 끝난다
#elif defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        App app{};
        if ( app.initialize( argc, pArgv ) == false )
        {
            // 종료 코드는 내리기 전에 고른다. 일부만 초기화된 서브시스템도 정해진 순서로 내려야 렌더 스레드 비우기 · 누수 리포트를 건너뛰지 않는다.
            const int32 exitCode = app.getInitFailureExitCode();
            app.shutdown();
            return exitCode;
        }
        // 데스크톱은 프로세스가 루프를 돈다. Android 는 `android_main`(App/Android)이 이 함수를 부르고, 메시지(ALooper)는 창의 `processMessages` 가 매 틱 비운다.
        if ( app.beginMainLoop() )
        {
            while ( app.updateMainLoop() )
            {
            }
        }
        return app.finishMainLoop();
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
