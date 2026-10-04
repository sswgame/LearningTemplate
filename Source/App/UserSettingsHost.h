/**
 * @file UserSettingsHost.h
 * @brief 사용자 설정의 호스트 쪽 — 확인 카운트다운 진행, 화면 요청(창 방식 · 해상도 · VSync)을 렌더 스레드와 안전하게 적용, 명령줄 디버그 적용입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct DisplaySettingsRequest;

    class EngineLoop;
    class IWindow;

    /**
     * @class UserSettingsHost
     * @brief App 이 프레임 맨 앞(OS 메시지 처리 직후 — OS 리사이즈와 같은 자리)에서 부릅니다.
     * @details 화면 변경 순서: 렌더 스레드를 기다린다 → VSync(`IRHIDevice::setVSync`) → 창 방식 · 크기(`IWindow::setDisplayMode`). 창 크기 통보는
     *          `App::onResize` 로 가서 그곳이 다시 렌더 스레드를 기다리고 스왑체인을 바꿉니다 — 스왑체인을 바꾸는 길은 OS 리사이즈와 하나입니다.
     *          명령줄 `-gv_userSettingsApply="id=value;id=value"` 는 `-gv_userSettingsApplyFrame` 째 프레임에 메뉴와 같은 길(보류 → 적용)로 넣습니다.
     */
    class UserSettingsHost
    {
    public:
        UserSettingsHost();

        /** @brief 엔진 루프와 창을 붙입니다. */
        void initialize( EngineLoop* pEngineLoop, IWindow* pWindow );
        /** @brief 붙인 것을 뗍니다. */
        void shutdown();

        /** @brief 카운트다운을 진행하고 쌓인 화면 요청을 적용합니다. */
        void tick( float32 deltaSeconds );

    private:
        void applyCommandLineSettings();
        void applyDisplayRequest( const DisplaySettingsRequest& request );

    private:
        EngineLoop* _pEngineLoop;
        IWindow*    _pWindow;
        uint32      _frameIndex;
    };
} // namespace sw
