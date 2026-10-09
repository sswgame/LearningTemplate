#include "pch.h"

#include "App/UserSettingsHost.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/EngineLoop.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/IWindow.h"

SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_userSettingsApply, "", "사용자 설정을 메뉴와 같은 길로 바꾼다: \"id=value;id=value\" (자동화)" );
SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_userSettingsApplyFrame, 30, "gv_userSettingsApply 를 적용할 프레임" );
SW_TEST_GLOBAL_VARIABLE_SHIPPED( bool, gv_userSettingsAutoConfirm, true, "gv_userSettingsApply 의 화면 변경을 바로 확인한다 (false 면 카운트다운이 되돌린다)" );

namespace sw
{
    SW_LOG_CALLER( "UserSettingsHost" );

    UserSettingsHost::UserSettingsHost()
        : _pEngineLoop{ nullptr }
        , _pWindow{ nullptr }
        , _frameIndex{ 0 }
    {
    }

    void UserSettingsHost::initialize( EngineLoop* pEngineLoop, IWindow* pWindow )
    {
        _pEngineLoop = pEngineLoop;
        _pWindow     = pWindow;
        _frameIndex  = 0;
    }

    void UserSettingsHost::shutdown()
    {
        _pEngineLoop = nullptr;
        _pWindow     = nullptr;
    }

    void UserSettingsHost::tick( float32 deltaSeconds )
    {
        if ( _pEngineLoop == nullptr || engine::areEngineServicesBound() == false )
            return;
        ++_frameIndex;
        if ( gv_userSettingsApply.empty() == false && _frameIndex == static_cast<uint32>( gv_userSettingsApplyFrame ) )
            applyCommandLineSettings();

        UserSettingsManager& settings = engine::getUserSettingsManager();
        settings.update( deltaSeconds );
        DisplaySettingsRequest request;
        if ( settings.consumeDisplayRequest( request ) )
            applyDisplayRequest( request );
    }

    void UserSettingsHost::applyCommandLineSettings()
    {
        UserSettingsManager& settings = engine::getUserSettingsManager();
        const string_view    text( gv_userSettingsApply );
        size_t               start = 0;
        while ( start < text.size() )
        {
            size_t end = text.find( ';', start );
            if ( end == string_view::npos )
                end = text.size();
            const string_view part = StringUtil::trim( text.substr( start, end - start ) );
            start                  = end + 1;
            const size_t equalPos  = part.find( '=' );
            if ( equalPos == string_view::npos )
                continue;
            const hashed_string        settingId( StringUtil::trim( part.substr( 0, equalPos ) ) );
            const string_view          value  = StringUtil::trim( part.substr( equalPos + 1 ) );
            const UserSettingSetResult result = settings.setPendingValue( settingId, value );
            const bool                 bTaken = result == UserSettingSetResult::Accepted || result == UserSettingSetResult::Clamped || result == UserSettingSetResult::Unchanged;
            if ( bTaken == false )
                SW_LOG_WARNING( "gv_userSettingsApply: '%#' = '%#' was not taken (result %#)", settingId.c_str(), value, static_cast<uint32>( result ) );
        }

        const UserSettingsApplyResult applied = settings.applyPending();
        SW_LOG_INFO( "gv_userSettingsApply: applied %# setting(s), %# failed, restart %#, confirm %#", applied._appliedCount, applied._failedCount,
                     applied._bNeedsRestart ? "needed" : "not needed", applied._bAwaitingConfirm ? "pending" : "not needed" );
        if ( applied._bAwaitingConfirm && gv_userSettingsAutoConfirm )
            settings.confirmChanges();
    }

    void UserSettingsHost::applyDisplayRequest( const DisplaySettingsRequest& request )
    {
        RHI* pRHI = _pEngineLoop->getRhi();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
            return;

        // 렌더 스레드가 지난 프레임을 그리는 중일 수 있다 — 스왑체인 · present 값을 바꾸기 전에 비운다(App::onResize 와 같은 규칙).
        RenderThread* pRenderThread = _pEngineLoop->getRenderThread();
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle();

        IRHIDevice& device = pRHI->getDevice();
        if ( device.isVSyncEnabled() != request._bVSync )
        {
            device.setVSync( request._bVSync );
            // 백엔드를 바꿔 디바이스를 다시 만들 때도 같은 값이어야 한다.
            pRHI->setPreferredVSync( request._bVSync );
            SW_LOG_INFO( "VSync %#", request._bVSync ? "on" : "off" );
        }

        // 창 크기가 바뀌면 이 호출 안(Win32)이나 다음 메시지 처리(X11)에서 App::onResize 가 스왑체인을 맞춘다.
        if ( _pWindow != nullptr )
        {
            const bool bSameMode = _pWindow->getDisplayMode() == request._mode;
            const bool bSameSize = _pWindow->getWidth() == request._width && _pWindow->getHeight() == request._height;
            const bool bNoChange = bSameMode && ( bSameSize || request._mode != WindowDisplayMode::Windowed );
            if ( bNoChange == false && _pWindow->setDisplayMode( request._mode, request._width, request._height ) == false )
                SW_LOG_WARNING( "This platform cannot change the window mode" );
        }
    }
} // namespace sw
