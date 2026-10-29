#include "pch.h"

#include "App/RuntimeUIHost.h"

#include "Core/Log/Logger.h"

#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"
#include "Engine/UI/Accessibility/UIAccessibilityService.h"
#include "Engine/UI/Input/UIClipboardService.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/PlatformClipboard.h"

namespace sw
{
    SW_LOG_CALLER( "App" );

    RuntimeUIHost::RuntimeUIHost()
        : _pEngine{ nullptr }
        , _quitCommandRegistration{}
    {
    }

    void RuntimeUIHost::install( Engine& engine, IWindow* pWindow, bool bEditor )
    {
        UISystem* pUI = engine::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return;
        _pEngine = &engine;
        if ( PlatformClipboard::isAvailable() )
        {
            UIClipboardAPI api{};
            api._pRead  = &PlatformClipboard::readText;
            api._pWrite = &PlatformClipboard::writeText;
            engine::getSubsystem<UIClipboardService>().setAPI( api );
        }
        if ( bEditor )
            return; // 에디터는 창을 ImGui 가 쓴다 — 게임 UI 만 스크린 리더에 내보이지 않는다
        // 스크린 리더: 창의 접근성 요청(Windows `WM_GETOBJECT`)에 런타임 UI 로 답한다. 이 플랫폼에 연결이 없으면 nullptr 이고 UI 는 그대로 동작한다.
        if ( pWindow != nullptr )
            engine::getSubsystem<UIAccessibilityService>().setBridge( UIAccessibilityBridgeFactory::createForWindow( *pUI, *pWindow ) );
        _quitCommandRegistration = engine::getSubsystem<UIGlobalCommandRegistry>().registerCommand( hashed_string( UIGlobalCommandRegistry::kQuitGame ), SW_DELEGATE_METHOD( UICommandDelegate, &RuntimeUIHost::onQuitGameCommand, this ) );
    }

    void RuntimeUIHost::remove()
    {
        UISystem* pUI = engine::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return;
        engine::getSubsystem<UIClipboardService>().setAPI( UIClipboardAPI{} );
        engine::getSubsystem<UIAccessibilityService>().setBridge( nullptr ); // 창이 사라지기 전에 창의 접근성 훅을 푼다
        _quitCommandRegistration.reset();
        _pEngine = nullptr;
    }

    void RuntimeUIHost::onQuitGameCommand( [[maybe_unused]] const hashed_string& command, [[maybe_unused]] Widget& source )
    {
        SW_LOG_INFO( "Quit requested from the pause menu" );
        _pEngine->requestQuit( 0 );
    }
} // namespace sw
