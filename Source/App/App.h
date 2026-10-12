/**
 * @file App.h
 * @brief 런타임 클라이언트 앱(얇은 런처)입니다.
 *
 * 소유권: App 은 최상위 창 콜백 · EditorModuleHost · EngineLoop 만 들고, 나머지 엔진 핵심 로직은 모두 EngineLoop 에 맡깁니다.
 *
 * @details App 이 직접 아는 것은 네 가지로 제한합니다. 부팅 순서, 창, 프레임 순서, 그리고 호스트 ↔ 모듈 콜백 연결입니다.
 *          시간 정책은 FixedTimestep, 백엔드 교체는 RHIBackendSwitcher, 모듈 수명은 EditorModuleHost(공통 ModuleHost 위의 App 전용 층),
 *          핫 리로드는 ModuleReloadDriver, 개발 콘솔과 표준 입력 명령은 DevConsoleHost, 헤드리스 작업은 AppHeadlessTasks 가 각자 맡습니다.
 */
#pragma once
#include "App/DevConsoleHost.h"
#include "App/ModuleReloadDriver.h"
#include "App/RHIBackendSwitcher.h"
#include "App/UserSettingsHost.h"

#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Time/FixedTimestep.h"

#include "Engine/EngineLoop.h"
#include "Engine/Module/ModuleCatalog.h"

namespace sw
{
    struct EngineConfig;
    struct NativeWindowEvent;

    class AutomationSession;
    class CommandLineManager;
    class EditorModuleHost;
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
        bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 모든 자원을 해제합니다. */
        void shutdown();
        /** @brief 메인 루프입니다(할 일은 최소한만 합니다). */
        void run();
        /** @brief `initialize` 가 false 를 돌려준 뒤의 프로세스 종료 코드입니다. 이 기계 · 빌드가 그 RHI 백엔드를 못 돌리면 `kRHIUnusableHereExitCode`, 그 밖은 -1 입니다. */
        int32 getInitFailureExitCode() const;
        /** @brief 루프가 끝난 뒤 돌려줄 종료 코드입니다(`EngineLoop::requestQuit`. 요청이 없었으면 0). */
        int32 getExitCode() const;

    private:
        // 부팅 단계. initialize() 가 차례로 부른다.
        /** @brief 플랫폼 창을 확보합니다. EngineLoop 가 이미 만들어 두었으면 그 소유권을 넘겨받습니다. */
        bool acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager );
        /**
         * @brief 기동 단계 `ModuleTypes` 의 호스트 로더입니다. 타입 공급자 모듈(GameFramework, 키트, SWGame)의 이미지를 로드해 그 타입을 등록합니다.
         * @details 헤드리스 작업(씬 쿠킹)도 이 단계를 지나므로 쿠킹이 게임과 키트 컴포넌트를 제 타입으로 쿠킹합니다. 인스턴스는 `startModules` 가 만듭니다.
         */
        [[nodiscard]] bool loadModuleImages();
        /** @brief EditorModuleHost 를 만들어 게임과 에디터 인스턴스를 만듭니다(모듈 이미지는 `loadModuleImages` 가 이미 로드했습니다). */
        bool startModules();
        /**
         * @brief 모듈이 모두 로드된 뒤에도 가져간 곳이 없는 `-gv_*` 인자를 한 번 경고합니다.
         * @details 파서는 모르는 `gv_` 키를 버리지 않고 보류한다(모듈이 선언하는 변수는 파싱 시점에 아직 없다). 그래서 오타가
         *          바로 드러나지 않으므로 여기서 대신 알린다.
         */
        void warnUnknownGlobalOverrides() const;
        /**
         * @brief `-scenario` 가 있으면 자동화 세션을 만들어 엔진 루프에 프레임 관찰자로 붙이고 렌더 스레드의 시나리오 캡처를 켭니다.
         * @details 시나리오는 늘 고정 프레임 시간이다(세션이 단계마다 `gv_fixedFrameDelta` 를 그 시나리오 값으로 맞춘다). 여럿이면 한 프로세스에서 차례로 돈다.
         *          돌릴 시나리오가 없으면(목록이 빔) 세션을 남기지 않습니다.
         */
        void startAutomationSession( const CommandLineManager& commandLineManager );

        /** @brief 창 콜백 · 전역 변수 훅 · Present 훅을 연결합니다. */
        void bindHostCallbacks();

        /** @brief 창 크기 변경 콜백입니다. */
        void onResize( const uint32 width, const uint32 height );
        /** @brief 설정 파일을 다시 읽었습니다. EngineConfig 면 프레임 시간 정책(최대 델타 · 고정 스텝)을 다시 정합니다. */
        void onConfigReloaded( const hashed_string& configTypeName );
        /** @brief 네이티브 창 이벤트를 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief tick 안에서 필요할 때 조회하는 에디터 씬 뷰 카메라입니다. */
        CameraComponent* getSceneViewCamera();
        /** @brief 에디터 렌더 훅 콜백입니다(렌더 스레드). EditorModuleHost 로 넘깁니다. */
        void onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief 에디터 멀티 뷰포트 · 플랫폼 창 훅 콜백입니다(Present 뒤에 실행됩니다). */
        void onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );

    private:
        EngineLoop _engineLoop;
        /** @brief 모듈 핫 리로드입니다(EngineLoop 가 아니라 런처의 일). Shipping 에서는 아무것도 하지 않습니다. */
        ModuleReloadDriver _moduleReload;
        /** @brief 게임 창의 개발 콘솔(`~`)과 표준 입력 명령 채널입니다. Shipping 에서는 아무것도 하지 않습니다. */
        DevConsoleHost               _devConsole;
        unique_ptr<EditorModuleHost> _moduleHost;
        /** @brief 모듈 매니페스트(`Bin/Modules`)와 그 해석입니다. 무엇을 어떤 순서로 로드할지 정합니다(Dev). */
        ModuleCatalog       _moduleCatalog;
        ModuleResolution    _moduleResolution;
        unique_ptr<IWindow> _window;
        /**
         * @brief `-scenario=<파일>[,<파일>…]` 의 자동화 구동기입니다(`startAutomationSession`). 엔진 루프에 프레임 관찰자로 붙고, 끝나면 루프가 그 종료 코드로 끝납니다.
         * @details 엔진 루프는 이것을 모릅니다. 엔진 종료 전에 떼고 없앱니다(실행기가 로그 리스너 · 입력을 빌려 쓴다).
         */
        unique_ptr<AutomationSession> _automationSession;

        FixedTimestep      _fixedTimestep;
        RHIBackendSwitcher _backendSwap;
        UserSettingsHost   _userSettingsHost;

        // 프레임마다 다시 만들 이유가 없는 델리게이트다. bindHostCallbacks 에서 한 번 묶는다.
        /** @brief 에디터 모드에서만 연결됩니다. 비어 있으면 EngineLoop 가 씬 카메라를 씁니다. */
        ViewCameraProviderDelegate _sceneViewCameraProvider;
        /// @brief `initialize` 가 시작된 시각(마이크로초, steady_clock)입니다. 메인 루프에 들어갈 때 로그에 시작 시간을 찍습니다.
        int64 _initializeStartMicro;

        uint8                  _bEnableEditor        : 1;
        uint8                  _bQuitAfterInitialize : 1; ///< 한 번 하고 끝나는 작업(`--render-portraits`)을 마쳤다. 루프에 들어가지 않는다.
        [[maybe_unused]] uint8 _reserved             : 6;
    };
} // namespace sw
