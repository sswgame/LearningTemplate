/**
 * @file Engine/UserSettings/UserSettingsHost.h
 * @brief 사용자 설정의 호스트 쪽 — 확인 카운트다운 진행, 화면 요청(창 방식 · 해상도 · VSync)을 렌더 스레드와 안전하게 적용입니다.
 * @details 엔진 일이다(예전 자리는 App): 엔진 루프(`Engine`)를 쥐지 않고 렌더 창구(`IEngineRenderDriver` — RHI · 렌더 스레드 대기)와 주 창만 받는다.
 *          언리얼 `UGameUserSettings::ApplySettings` 가 Engine 모듈에서 해상도 · VSync 를 적용하는 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct DisplaySettingsRequest;

    class IEngineRenderDriver;
    class IWindow;

    /**
     * @class UserSettingsHost
     * @brief App 이 프레임 맨 앞(OS 메시지 처리 직후 — OS 리사이즈와 같은 자리)에서 부릅니다.
     * @details 화면 변경 순서: 렌더 스레드를 기다린다(`IEngineRenderDriver::waitRenderThreadIdle`) → VSync(`IRHIDevice::setVSync`) → 창 방식 · 크기(`IWindow::setDisplayMode`). 창 크기 통보는
     *          `Engine::onMainWindowResize` 로 가서 그곳이 다시 렌더 스레드를 기다리고 스왑체인을 바꿉니다 — 스왑체인을 바꾸는 길은 OS 리사이즈와 하나입니다.
     *          자동화가 설정을 메뉴와 같은 길로 바꾸는 것은 개발 명령 `settings.apply` · `settings.confirm`(`UserSettingsManager.cpp`)이다.
     */
    class SW_API UserSettingsHost
    {
    public:
        UserSettingsHost();

        /** @brief 렌더 창구와 창을 붙입니다. 렌더 창구가 없으면(헤드리스 · 렌더러 없는 구성) 화면 요청은 적용하지 않고 카운트다운만 진행합니다. */
        void initialize( IEngineRenderDriver* pRenderDriver, IWindow* pWindow );
        /** @brief 붙인 것을 뗍니다. */
        void shutdown();

        /** @brief 카운트다운을 진행하고 쌓인 화면 요청을 적용합니다. */
        void update( float32 deltaSeconds );

    private:
        void applyDisplayRequest( const DisplaySettingsRequest& request );

    private:
        IEngineRenderDriver*   _pRenderDriver;
        IWindow*               _pWindow;
        uint8                  _bInitialized  : 1; ///< `initialize` 뒤 · `shutdown` 앞이다
        [[maybe_unused]] uint8 _reservedFlags : 7;
    };
} // namespace sw
