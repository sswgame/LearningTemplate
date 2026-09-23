/**
 * @file App.h
 * @brief 런타임 클라이언트 앱(얇은 런처)입니다.
 *
 * 소유권: App 은 최상위 창 콜백 · ModuleHost · EngineLoop 만 들고, 나머지 엔진 핵심 로직은 모두 EngineLoop 에 맡깁니다.
 *
 * @details App 이 직접 아는 것은 네 가지로 제한합니다. 부팅 순서, 창, 프레임 순서, 그리고 호스트 ↔ 모듈 콜백 연결입니다.
 *          시간 정책은 FrameTimeline, 백엔드 교체는 BackendSwapController, 모듈 수명은 ModuleHost 가 각자 맡습니다.
 */
#pragma once
#include "App/Frame/FrameTimeline.h"
#include "App/Rhi/BackendSwapController.h"

#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/EngineLoop.h"

namespace sw
{
    struct EngineConfig;
    struct NativeWindowEvent;

    class CommandLineManager;
    class IWindow;
    class LiveReloadManager;
    class ModuleHost;

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

    private:
        // 부팅 단계. initialize() 가 차례로 부른다.
        /** @brief 플랫폼 창을 확보합니다. EngineLoop 가 이미 만들어 두었으면 그 소유권을 넘겨받습니다. */
        bool acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager );
        /** @brief 핫 리로드 매니저입니다. Shipping 에서는 항상 nullptr 이고, 받는 쪽이 그 경우를 처리합니다. */
        LiveReloadManager* getLiveReloadManager() const;
        /** @brief 에디터 · 게임 모듈을 로드하고 ModuleHost 를 세웁니다. */
        bool startModules();
        /**
         * @brief 모듈이 모두 올라온 뒤에도 가져간 곳이 없는 `-gv_*` 인자를 한 번 경고합니다.
         * @details 파서는 모르는 `gv_` 키를 버리지 않고 보류합니다(모듈이 선언하는 변수는 파싱 시점에 아직 없습니다). 그래서 오타가
         *          바로 드러나지 않으므로 여기서 대신 알립니다.
         */
        void warnUnclaimedGlobalOverrides() const;

        /** @brief 창 콜백 · 전역 변수 훅 · Present 훅을 연결합니다. */
        void bindHostCallbacks();

        // 프레임 단계. run() 이 차례로 부른다.
        /** @brief 셸 액션을 갱신하고 리로드 단축키를 처리합니다. Shipping 에서는 아무것도 하지 않습니다. */
        void pollReloadHotkeys( float32 deltaTime );

        /** @brief 창 크기 변경 콜백입니다. */
        void onResize( const uint32 width, const uint32 height );
        /** @brief 네이티브 창 이벤트를 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief tick 안에서 필요할 때 조회하는 에디터 뷰 카메라입니다. */
        CameraComponent* getEditorViewCamera();
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
         * @details 모듈을 감시하고 갈아 끼우는 것은 런처의 일이라 여기 둡니다(예전에는 EngineLoop 가 들고 있었습니다).
         */
#if !defined( SW_SHIPPING )
        unique_ptr<LiveReloadManager> _liveReloadManager;
#endif
        unique_ptr<ModuleHost> _moduleHost;
        unique_ptr<IWindow>    _window;

        FrameTimeline         _frameTimeline;
        BackendSwapController _backendSwap;

        // 프레임마다 다시 만들 이유가 없는 델리게이트다. bindHostCallbacks 에서 한 번 묶는다.
        /** @brief 에디터 모드에서만 연결됩니다. 비어 있으면 EngineLoop 가 씬 카메라를 씁니다. */
        ViewCameraProviderDelegate _viewCameraProvider;
        /// @brief `initialize` 가 시작된 시각(마이크로초, steady_clock)입니다. 메인 루프에 들어갈 때 로그에 시작 시간을 찍습니다.
        int64 _initializeStartMicro;

        uint8                  _bEnableEditor : 1;
        [[maybe_unused]] uint8 _reserved      : 7;
    };
} // namespace sw
