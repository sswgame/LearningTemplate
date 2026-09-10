/**
 * @file App.h
 * @brief 런타임 클라이언트 앱 (Thin Launcher)
 *
 * 소유권: App은 최상위 윈도우 콜백과 ModuleHost, EngineLoop 만을 유지하고,
 *         나머지 엔진 코어 로직은 모두 EngineLoop에 위임합니다.
 *
 * @details App 이 직접 아는 것은 네 가지로 제한한다 — 부팅 순서, 창, 프레임 순서, 그리고
 *          호스트↔모듈 콜백 배선이다. 시간 정책은 FrameTimeline, 백엔드 교체는
 *          BackendSwapController, 모듈 수명은 ModuleHost 가 각자 안다.
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
    class ModuleHost;

    /** @brief 윈도우 OS 메시지를 엔진에 전달하고 메인 루프를 구동하는 얇은 래퍼 */
    class App
    {
    public:
        App();
        ~App();

        /** @brief 윈도우 생성 및 모듈/엔진 초기화 */
        bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 모든 리소스 해제 */
        void shutdown();
        /** @brief 메인 루프 (초경량) */
        void run();

    private:
        // 부팅 단계 — initialize() 가 순서대로 부른다.
        /** @brief 플랫폼 윈도우를 확보합니다. EngineLoop 이 이미 만들어 뒀으면 그 소유권을 넘겨받습니다. */
        bool acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager );
        /** @brief 에디터/게임 모듈을 로드하고 ModuleHost 를 세웁니다. */
        bool startModules();
        /**
         * @brief 모듈이 다 올라온 뒤에도 임자가 없는 `-gv_*` 인자를 한 번 경고합니다.
         * @details 파서는 모르는 `gv_` 키를 버리지 않고 보류한다(모듈이 선언하는 변수는 파싱 시점에
         *          아직 없다). 그래서 오타가 즉시 걸리지 않는다 — 여기서 대신 알린다.
         */
        void warnUnclaimedGlobalOverrides() const;

        /** @brief 윈도우 콜백·전역 변수 훅·프레젠트 훅을 연결합니다. */
        void bindHostCallbacks();

        // 프레임 단계 — run() 이 순서대로 부른다.
        /** @brief 셸 액션을 갱신하고 리로드 단축키를 처리합니다. Shipping 에서는 아무것도 하지 않습니다. */
        void pollReloadHotkeys( float32 deltaTime );

        /** @brief 윈도우 리사이즈 콜백 */
        void onResize( const uint32 width, const uint32 height );
        /** @brief 네이티브 윈도우 이벤트 라우팅 */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief tick 내부에서 지연 조회되는 에디터 뷰 카메라입니다. */
        CameraComponent* getEditorViewCamera();
        /** @brief 강제 핫리로드 단축키 콜백 */
        void onForceReload( const utf8* pModuleName );
        /** @brief 에디터 렌더 훅 콜백 */
        void onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief 에디터 멀티 뷰포트 / 플랫폼 윈도우 훅 콜백 (Present 이후 실행) */
        void onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );

    private:
        EngineLoop             _engineLoop;
        unique_ptr<ModuleHost> _moduleHost;
        unique_ptr<IWindow>    _window;

        FrameTimeline         _frameTimeline;
        BackendSwapController _backendSwap;

        // 매 프레임 다시 만들 이유가 없는 델리게이트 — bindHostCallbacks 에서 한 번 묶는다.
        /** @brief 에디터 모드에서만 바인딩된다. 비어 있으면 EngineLoop 이 씬 카메라를 쓴다. */
        ViewCameraProviderDelegate    _viewCameraProvider;
        Delegate<void( const utf8* )> _forceReloadHandler;

        uint8                  _bEnableEditor : 1;
        [[maybe_unused]] uint8 _reserved      : 7;
    };
} // namespace sw
