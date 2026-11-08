/**
 * @file App.h
 * @brief 런타임 클라이언트 앱(얇은 런처)입니다.
 *
 * 소유권: App 은 최상위 창 콜백과 부팅 열(`HostLaunch` — Engine · 모듈 관리자 · 호스트 서비스)만 들고, 나머지 엔진 핵심 로직은 모두 Engine 에 맡깁니다.
 *
 * @details App 이 직접 아는 것은 네 가지로 제한합니다. 부팅 순서, 창, 프레임 순서, 그리고 호스트 ↔ 모듈 콜백 연결입니다.
 *          시간 정책은 FixedTimestep, 백엔드 교체는 RHIBackendSwitcher(개발 구성만 — `App/Dev/`), 모듈 수명은 EditorModuleManager(공통 GameModuleManager 위의 App 전용 층),
 *          핫 리로드 · 개발 콘솔 · 표준 입력 명령은 개발 계층(`IHostDevLayer` — Server 와 같이 쓴다, 배포에는 없다), 헤드리스 작업은 AppHeadlessTasks 가 각자 맡습니다.
 *          자동화가 찾아야 하는 호스트 객체(교체기 · 리로드 구동기)는 정적 포인터가 아니라 호스트 범위(`HostLaunch::getHostScope`)에 걸고 `engine::findSubsystem<T>()` 로 찾게 합니다.
 *          런타임 UI 에 끼우는 것(일시정지 메뉴의 "끝내기", 운영체제 클립보드, 스크린 리더)은 엔진의 `UIPlatformBinding` 이 맡습니다.
 */
#pragma once
#include "App/Dev/RHIBackendSwitcher.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/FixedTimestep.h"

#include "Engine/Engine.h"
#include "Engine/HostServiceSet.h"
#include "Engine/UI/UIPlatformBinding.h"
#include "Engine/UserSettings/UserSettingsHost.h"

#include "ModuleManager/Host/HostLaunch.h"

namespace sw
{
    class IAutomationRuntime;
    struct EngineConfig;
    struct FramePacingInputs;
    struct NativeWindowEvent;
    struct RenderFramePacket;

    class CommandLineManager;
    class EditorModuleManager;
    class FramePacer;
    class IRHIDevice;
    class IWindow;
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
         * @return 돌 루프가 있으면 true 입니다. 헤드리스 부팅 · 한 번 하고 끝나는 작업은 false 이고 그때는 `tickMainLoop` · `endMainLoop` 를 부르지 않습니다.
         */
        [[nodiscard]] bool beginMainLoop();
        /** @brief 창 메시지를 처리하고 한 프레임을 돕니다. 막지 않습니다. @return 루프를 끝낼 때면 false 입니다. */
        [[nodiscard]] bool tickMainLoop();
        /** @brief 루프가 끝났습니다. */
        void endMainLoop();
        /** @brief 루프를 끝낸 앱을 내리고 종료 코드를 돌려줍니다(`endMainLoop` → 종료 코드 → `shutdown`). 종료 코드는 내리기 전에 읽는다(시나리오 결과 — `Engine::requestQuit`). */
        [[nodiscard]] int32 finishMainLoop();
        /** @brief `initialize` 가 false 를 돌려준 뒤의 프로세스 종료 코드입니다. 이 기계 · 빌드가 그 RHI 백엔드를 못 돌리면 `kRHIUnusableHereExitCode`, 그 밖은 -1 입니다. */
        int32 getInitFailureExitCode() const;
        /** @brief 루프가 끝난 뒤 돌려줄 종료 코드입니다(`Engine::requestQuit`. 요청이 없었으면 0). */
        int32 getExitCode() const;

    private:
        /** @brief 시동 단계마다 끝난 시각입니다(마이크로초, `MonotonicClock`). 측정만 한다 — 시동 요약 로그의 재료입니다. */
        struct StartupTimings
        {
            int64 _hostScopeEndMicro{ 0 };
            int64 _engineEndMicro{ 0 };
            int64 _windowEndMicro{ 0 };
            int64 _modulesEndMicro{ 0 };
        };

        /** @brief 모듈 관리자를 이 App 이 만든 형식으로 읽습니다(`loadModuleLibraries` 가 `EditorModuleManager` 로 만든다). 호스트 로더가 돌기 전과 `shutdown` 뒤에는 nullptr 입니다. */
        EditorModuleManager* getModuleManager() const;

        // 부팅 단계. initialize() 가 차례로 부른다.
        /**
         * @brief 엔진이 선 뒤 자동화 실행기(`IAutomationRuntime`)를 찾아 시작합니다.
         * @return 실행기가 없는 구성이 `-scenario` · `-scenario-record` 를 받았으면 false(기동 오류)입니다 — 시나리오 없이 돌면 결과를 낼 수 없다.
         */
        [[nodiscard]] bool startAutomation( const CommandLineManager& commandLineManager );
        /** @brief 플랫폼 창을 확보합니다. Engine 가 이미 만들어 두었으면 그 소유권을 넘겨받습니다. */
        bool acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager );
        /**
         * @brief 기동 단계 `ModuleLoadStage` 의 호스트 로더입니다. 타입 공급자 모듈(GameFramework, 키트, SWGame)의 이미지를 로드해 그 타입을 등록합니다.
         * @details 헤드리스 작업(씬 쿠킹)도 이 단계를 지나므로 쿠킹이 게임과 키트 컴포넌트를 제 타입으로 쿠킹합니다. 인스턴스는 `startModules` 가 만듭니다.
         */
        [[nodiscard]] bool loadModuleLibraries();
        /**
         * @brief 호스트 서비스 하나를 세웁니다(`kArrClientHostService` — 순서는 `HostService` 의 의존 선언이 정한다).
         * @details `Game` 은 EditorModuleManager 로 게임과 에디터 인스턴스를 만든다(모듈 라이브러리는 `loadModuleLibraries` 가 이미 로드했다).
         */
        [[nodiscard]] bool startHostService( HostServiceID id );
        /** @brief `startHostService` 가 세운 서비스 하나를 내립니다. 호스트 서비스 집합이 역순으로 부릅니다. */
        void stopHostService( HostServiceID id );
        /** @brief 창 콜백 · 개발 도구의 프레임 시작 훅을 연결합니다(호스트 서비스가 선 뒤). */
        void bindHostCallbacks();
        /** @brief 에디터 렌더 훅 셋(준비 · 그리기 · Present 뒤)을 렌더 스레드에 걸거나(@p bBind) 뗍니다. 게임 호스트 서비스가 매니저를 세운 뒤 걸고 지우기 전에 뗀다. */
        void bindEditorRenderHooks( bool bBind );

        /** @brief 메인 루프에 들어가는 줄(시작 시간 · 파일 연 횟수)을 남깁니다. */
        void logMainLoopStart() const;
        /** @brief 시동 요약 한 줄(단계별 ms · 오래 걸린 서브시스템)을 남깁니다. 창을 띄운 뒤에 부릅니다. */
        void logStartupPhases( const StartupTimings& timings ) const;
        /** @brief 첫 프레임들이 끝난 시각을 로그로 남기고, 첫 그림이 나왔으면 스플래시를 닫고 주 창을 띄웁니다. */
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
        /** @brief 에디터 준비 훅 콜백입니다(렌더 스레드, 백버퍼 패스를 열기 전 — UI 업로드). EditorModuleManager 로 넘깁니다. */
        void onEditorPrepareRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief 에디터 렌더 훅 콜백입니다(렌더 스레드, 열린 백버퍼 패스 안 — 그리기만). EditorModuleManager 로 넘깁니다. */
        void onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief 에디터 멀티 뷰포트 · 플랫폼 창 훅 콜백입니다(Present 뒤에 실행됩니다). */
        void onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );

    private:
        /**
         * @brief 부팅 열입니다 — 호스트 범위 · 개발 계층 · 엔진 · 모듈 관리자 · 호스트 서비스의 수명과 순서를 듭니다(Server 와 같은 것, `HostLaunch`).
         * @details 맨 앞 멤버라 맨 나중에 사라진다. 호스트 서비스의 몸통이 아래 멤버를 쓰므로 `shutdown` 이 그 멤버가 사라지기 전에 호스트 서비스를 내린다.
         */
        HostLaunch _launch;
        /**
         * @brief 자동화 실행기입니다(엔진 범위에 스스로 선 서브시스템 — 빌려 쓴다). 실행기를 짓지 않은 구성은 nullptr 입니다.
         * @details 엔진이 선 뒤 `startAutomation` 이 찾고, 엔진 종료 전에 놓는다(실행기는 엔진 범위와 함께 내려간다).
         */
        IAutomationRuntime* _pAutomation;
        /** @brief 주 창입니다(관찰 포인터). 소유자는 엔진 범위의 `MainWindowSubsystem` 이라 엔진 종료 때 창이 파괴된다. `acquireMainWindow` 가 채운다. */
        IWindow* _pWindow;

        FixedTimestep _fixedTimestep;
        /**
         * @brief 백엔드 교체기입니다. 호스트 서비스 `BackendSwitcher` 가 서며 호스트 범위에 걸고 내려가며 풉니다(시나리오 단계 `SwapBackend` · 감시 값 `RHI.*` 가 찾는다).
         * @details 개발 구성에만 있다(`App/Dev/` — 배포는 짓지 않고 늘 nullptr). 생성자가 `build::kShipping` 이 아닐 때만 만든다.
         */
        unique_ptr<RHIBackendSwitcher> _pBackendSwitcher;
        UserSettingsHost               _userSettingsHost;
        /** @brief 런타임 UI 의 호스트 훅(클립보드 · 스크린 리더 · 끝내기)입니다. 호스트 서비스 `RuntimeUIHooks` 가 세우고 내린다. */
        UIPlatformBinding _uiPlatformBinding;

        // 프레임마다 다시 만들 이유가 없는 델리게이트다. bindHostCallbacks 에서 한 번 묶는다.
        /** @brief 에디터 모드에서만 연결됩니다. 비어 있으면 Engine 가 씬 카메라를 씁니다. */
        ViewCameraProviderDelegate _sceneViewCameraProvider;
        /** @brief 설정 다시 읽기 알림(`ConfigManager::onConfigReloaded`)의 구독입니다. 엔진이 설정 표를 내리면 무효가 된다. */
        Subscription _configReloadedSubscription;
        /** @brief 주 창의 메시지 처리기 칸 구독입니다(`bindHostCallbacks`). 창이 엔진 종료 때 파괴되면 무효가 된다. 크기 칸은 엔진이 쥔다(스왑체인). */
        Subscription _windowMessageSubscription;
        /// @brief 메인 루프가 쓰는 페이서입니다(`beginMainLoop` 이 주 창 서비스에서 받는다). 루프 밖이면 nullptr 입니다.
        FramePacer* _pFramePacer;
        /// @brief `initialize` 가 시작된 시각(마이크로초, `MonotonicClock`)입니다. 메인 루프에 들어갈 때 로그에 시작 시간을 찍습니다.
        int64 _initializeStartMicro;
        /// @brief 메인 루프에 들어간 시각입니다. 첫 프레임들이 끝난 시각을 이 기준으로 찍는다 — 첫 그림까지 걸린 시간(주 창은 그 뒤에 뜬다)을 잰다.
        int64                  _mainLoopStartMicro;
        uint32                 _startupFrameCount; ///< 로그로 찍은 첫 프레임 수(`kStartupFramesLogged` 까지)
        uint8                  _bEnableEditor        : 1;
        uint8                  _bQuitAfterInitialize : 1; ///< 한 번 하고 끝나는 작업(`--render-portraits`)을 마쳤다. 루프에 들어가지 않는다.
        uint8                  _bRenderPaused        : 1; ///< 지난 틱에 표면이 없거나 앱이 멈춰 프레임을 돌지 않았다. 다시 돌 때 시계를 새로 잡는다
        uint8                  _bShowWindowPending   : 1; ///< 첫 그림이 나오면 스플래시를 닫고 주 창을 띄운다(`onStartupFrameEnded`)
        uint8                  _bActiveSceneSeen     : 1; ///< 지난 프레임 끝에 활성 씬이 있었다 — 다음 프레임 끝에 그 씬의 첫 그림이 나와 있다
        [[maybe_unused]] uint8 _reserved             : 2;
    };
} // namespace sw
