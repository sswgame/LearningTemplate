/**
 * @file App.h
 * @brief 런타임 클라이언트 앱 (Thin Launcher)
 *
 * 소유권: App은 최상위 윈도우 콜백과 ModuleHost, EngineLoop 만을 유지하고,
 *         나머지 엔진 코어 로직은 모두 EngineLoop에 위임합니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/EngineLoop.h"

namespace sw
{
    struct GlobalVariableInfo;
    struct NativeWindowEvent;

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
        /** @brief 윈도우 리사이즈 콜백 */
        void onResize( const uint32 width, const uint32 height );
        /** @brief 네이티브 윈도우 이벤트 라우팅 */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief gv_rhiBackend 변경 이벤트 훅 */
        void onRhiBackendChanged( const GlobalVariableInfo* pInfo );
        /** @brief tick 내부에서 지연 조회되는 에디터 뷰 카메라입니다. */
        CameraComponent* getEditorViewCamera();
        /** @brief 대기 중인 RHI 백엔드 변경을 적용 (ModuleHost 연계) */
        bool applyPendingBackendChange();
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

        float32 _maxFrameDeltaTime;
        bool    _bEnableEditor;
        /** @brief gv_rhiBackend 되돌림 대입이 변경 콜백을 재귀 호출하는 것을 막습니다. */
        bool _bHandlingRhiBackendChange;
    };
} // namespace sw
