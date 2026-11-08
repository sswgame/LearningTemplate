#include "pch.h"

#include "App/UserSettingsHost.h"

#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    UserSettingsHost::UserSettingsHost()
        : _pEngine{ nullptr }
        , _pWindow{ nullptr }
    {
    }

    void UserSettingsHost::initialize( Engine* pEngine, IWindow* pWindow )
    {
        _pEngine = pEngine;
        _pWindow = pWindow;
    }

    void UserSettingsHost::shutdown()
    {
        _pEngine = nullptr;
        _pWindow = nullptr;
    }

    void UserSettingsHost::update( float32 deltaSeconds )
    {
        if ( _pEngine == nullptr || engine::areEngineServicesBound() == false )
            return;
        UserSettingsManager& settings = engine::getSubsystem<UserSettingsManager>();
        settings.update( deltaSeconds );
        DisplaySettingsRequest request;
        if ( settings.consumeDisplayRequest( request ) )
            applyDisplayRequest( request );
    }

    void UserSettingsHost::applyDisplayRequest( const DisplaySettingsRequest& request )
    {
        RHI* pRHI = _pEngine->getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
            return;

        // 렌더 스레드가 지난 프레임을 그리는 중일 수 있다 — 스왑체인 · present 값을 바꾸기 전에 비운다(Engine::onMainWindowResize 와 같은 규칙).
        RenderThread* pRenderThread = _pEngine->getRenderThread();
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle( RHIWaitIdleReason::Resize );

        IRHIDevice& device = pRHI->getDevice();
        if ( device.isVSyncEnabled() != request._bVSync )
        {
            device.setVSync( request._bVSync );
            // 백엔드를 바꿔 디바이스를 다시 만들 때도 같은 값이어야 한다.
            pRHI->setPreferredVSync( request._bVSync );
            SW_LOG_INFO_ID( "UserSettingsHost.VSync", "VSync %#", request._bVSync ? "on" : "off" );
        }

        // 창 크기가 바뀌면 이 호출 안(Win32)이나 다음 메시지 처리(X11)에서 Engine::onMainWindowResize 가 스왑체인을 맞춘다.
        if ( _pWindow != nullptr )
        {
            const bool bSameMode = _pWindow->getDisplayMode() == request._mode;
            const bool bSameSize = _pWindow->getWidth() == request._width && _pWindow->getHeight() == request._height;
            const bool bNoChange = bSameMode && ( bSameSize || request._mode != WindowDisplayMode::Windowed );
            if ( bNoChange == false && _pWindow->setDisplayMode( request._mode, request._width, request._height ) == false )
                SW_LOG_WARNING_ID( "UserSettingsHost.PlatformCannotChangeWindow", "This platform cannot change the window mode" );
        }
    }
} // namespace sw
