/**
 * @file App.h
 * @brief 런타임 클라이언트 앱(얇은 런처)입니다.
 *
 * 소유권: App 은 최상위 창 콜백 · ModuleHost · EngineLoop 만 들고, 나머지 엔진 핵심 로직은 모두 EngineLoop 에 맡깁니다.
 *
 * @details App 이 직접 아는 것은 네 가지로 제한합니다. 부팅 순서, 창, 프레임 순서, 그리고 호스트 ↔ 모듈 콜백 연결입니다.
 *          시간 정책은 FixedTimestep, 백엔드 교체는 RHIBackendSwitcher, 모듈 수명은 ModuleHost 가 각자 맡습니다.
 */
#pragma once
#include "App/RHIBackendSwitcher.h"
#include "App/UserSettingsHost.h"

#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Config/FixedTimestep.h"
#include "Engine/EngineLoop.h"
#include "Engine/Module/ModuleCatalog.h"

namespace sw
{
    struct EngineConfig;
    struct NativeWindowEvent;

    class CommandLineManager;
    class DevConsoleController;
    class IWindow;
    class LiveReloadManager;
    class ModuleHost;
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
        /** @brief `initialize` 가 false 를 돌려준 뒤의 프로세스 종료 코드입니다. 이 기계 · 빌드가 그 RHI 백엔드를 못 돌리면 `kRhiUnusableHereExitCode`, 그 밖은 -1 입니다. */
        int32 getInitFailureExitCode() const;
        /** @brief 루프가 끝난 뒤 돌려줄 종료 코드입니다(`EngineLoop::requestQuit` — 요청이 없었으면 0). */
        int32 getExitCode() const;

    private:
        // 부팅 단계. initialize() 가 차례로 부른다.
        /** @brief 플랫폼 창을 확보합니다. EngineLoop 가 이미 만들어 두었으면 그 소유권을 넘겨받습니다. */
        bool acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager );
        /** @brief 핫 리로드 매니저입니다. Shipping 에서는 항상 nullptr 이고, 받는 쪽이 그 경우를 처리합니다. */
        LiveReloadManager* getLiveReloadManager() const;
        /**
         * @brief 기동 단계 `ModuleTypes` 의 호스트 로더입니다 — 타입 공급자 모듈(GameFramework · 키트 · SWGame)의 이미지를 올려 그 타입을 등록합니다.
         * @details 헤드리스 작업(씬 쿠킹)도 이 단계를 지나므로 쿠킹이 게임 · 키트 컴포넌트를 제 타입으로 쿠킹합니다. 인스턴스는 `startModules` 가 만듭니다.
         */
        [[nodiscard]] bool loadModuleImages();
        /** @brief ModuleHost 를 세워 게임 · 에디터 인스턴스를 만듭니다(모듈 이미지는 `loadModuleImages` 가 이미 올렸다). */
        bool startModules();
        /**
         * @brief 모듈이 모두 올라온 뒤에도 가져간 곳이 없는 `-gv_*` 인자를 한 번 경고합니다.
         * @details 파서는 모르는 `gv_` 키를 버리지 않고 보류합니다(모듈이 선언하는 변수는 파싱 시점에 아직 없습니다). 그래서 오타가
         *          바로 드러나지 않으므로 여기서 대신 알립니다.
         */
        void warnUnknownGlobalOverrides() const;

        /** @brief 창 콜백 · 전역 변수 훅 · Present 훅을 연결합니다. */
        void bindHostCallbacks();

        // 프레임 단계. run() 이 차례로 부른다.
        /** @brief 셸 액션을 갱신하고 리로드 단축키를 처리합니다. Shipping 에서는 아무것도 하지 않습니다. */
        void pollReloadHotkeys( float32 deltaTime );

        /** @brief 개발 콘솔 오버레이를 만들고 시작 명령(`-gv_devConsoleExec`)을 걸어 둡니다. Shipping 에서는 아무것도 하지 않습니다. */
        void startDevConsole();
        /** @brief 시작 씬이 열렸으면 걸어 둔 시작 명령을 한 번 돌립니다. 프레임마다 부릅니다. */
        void runPendingDevConsoleExec();
        /** @brief 셸 InputMap 의 이번 프레임 액션으로 개발 콘솔을 갱신합니다(에디터가 없을 때). `pollReloadHotkeys` 뒤에 부릅니다. Shipping 에서는 아무것도 하지 않습니다. */
        void updateDevConsole();

        /** @brief 창 크기 변경 콜백입니다. */
        void onResize( const uint32 width, const uint32 height );
        /** @brief 설정 파일을 다시 읽었습니다. EngineConfig 면 프레임 시간 정책(최대 델타 · 고정 스텝)을 다시 정합니다. */
        void onConfigReloaded( const hashed_string& configTypeName );
        /** @brief 네이티브 창 이벤트를 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief tick 안에서 필요할 때 조회하는 에디터 씬 뷰 카메라입니다. */
        CameraComponent* getSceneViewCamera();
        /** @brief 강제 핫 리로드 단축키 콜백입니다. */
        void onForceReload( const utf8* pModuleName );
        /** @brief 에디터 렌더 훅 콜백입니다. */
        void onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief 에디터 멀티 뷰포트 · 플랫폼 창 훅 콜백입니다(Present 뒤에 실행됩니다). */
        void onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );

    private:
        EngineLoop _engineLoop;
        /**
         * @brief 모듈 핫 리로드입니다. **Shipping 에는 없습니다.** 그 빌드에서는 이 멤버도 클래스도 컴파일되지 않습니다.
         * @details 모듈을 감시하고 갈아 끼우는 것은 런처의 일이라 여기 둡니다(EngineLoop 가 아니라).
         */
#if !defined( SW_SHIPPING )
        unique_ptr<LiveReloadManager> _liveReloadManager;
        /**
         * @brief 게임 창의 개발 콘솔(`~`)입니다. **Shipping 에는 없습니다.** 에디터가 없을 때만 입력을 받습니다(에디터는 Output Log 입력 줄이 있다).
         * @details 키는 셸 InputMap 의 콘솔 액션으로 받습니다(`pollReloadHotkeys` 뒤 `updateDevConsole`). `-gv_devConsoleExec` 의 명령을 시작 씬이 열린 뒤
         *          이 콘솔로 돌립니다(에디터가 있어도).
         */
        unique_ptr<DevConsoleController> _devConsoleController;
#endif
        unique_ptr<ModuleHost> _moduleHost;
        /** @brief 모듈 매니페스트(`Bin/Modules`)와 그 해석 — 무엇을 어떤 순서로 올릴지(Dev). */
        ModuleCatalog       _moduleCatalog;
        ModuleResolution    _moduleResolution;
        unique_ptr<IWindow> _window;

        FixedTimestep      _fixedTimestep;
        RHIBackendSwitcher _backendSwap;
        UserSettingsHost   _userSettingsHost;

        // 프레임마다 다시 만들 이유가 없는 델리게이트다. bindHostCallbacks 에서 한 번 묶는다.
        /** @brief 에디터 모드에서만 연결됩니다. 비어 있으면 EngineLoop 가 씬 카메라를 씁니다. */
        ViewCameraProviderDelegate _sceneViewCameraProvider;
        /// @brief `initialize` 가 시작된 시각(마이크로초, steady_clock)입니다. 메인 루프에 들어갈 때 로그에 시작 시간을 찍습니다.
        int64 _initializeStartMicro;

        uint8                  _bEnableEditor          : 1;
        [[maybe_unused]] uint8 _bDevConsoleExecPending : 1; ///< `-gv_devConsoleExec` 를 아직 돌리지 않았다(개발 콘솔은 Shipping 에 없어 거기서는 읽지 않는다)
        uint8                  _bQuitAfterInitialize   : 1; ///< 한 번 하고 끝나는 작업(`--render-portraits`)을 마쳤다 — 루프에 들어가지 않는다
        [[maybe_unused]] uint8 _reserved               : 5;
    };
} // namespace sw
