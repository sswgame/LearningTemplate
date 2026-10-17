#include "pch.h"

#include "App/RuntimeUIHost.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Engine.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Window/PlatformClipboard.h"

namespace sw
{
    SW_LOG_CALLER( "RuntimeUIHost" );

    RuntimeUIHost::RuntimeUIHost()
        : _pEngine{ nullptr }
    {
    }

    void RuntimeUIHost::initialize( Engine* pEngine, bool bEnableEditor )
    {
        UISystem* pUI = engine::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return;
        if ( PlatformClipboard::isAvailable() )
        {
            UIClipboardAPI api{};
            api._pRead  = &PlatformClipboard::readText;
            api._pWrite = &PlatformClipboard::writeText;
            pUI->getClipboard().setAPI( api );
        }
        if ( bEnableEditor || pEngine == nullptr )
            return;
        _pEngine = pEngine;
        pUI->getGlobalCommands().registerCommand( hashed_string( UIGlobalCommandRegistry::kQuitGame ),
                                                  SW_DELEGATE_METHOD( UICommandDelegate, &RuntimeUIHost::onQuitGameCommand, this ) );
    }

    void RuntimeUIHost::shutdown()
    {
        UISystem* pUI = engine::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return;
        pUI->getClipboard().setAPI( UIClipboardAPI{} );
        if ( _pEngine == nullptr )
            return;
        pUI->getGlobalCommands().unregisterCommand( hashed_string( UIGlobalCommandRegistry::kQuitGame ),
                                                    SW_DELEGATE_METHOD( UICommandDelegate, &RuntimeUIHost::onQuitGameCommand, this ) );
        _pEngine = nullptr;
    }

    void RuntimeUIHost::onQuitGameCommand( [[maybe_unused]] const hashed_string& command, [[maybe_unused]] Widget& source )
    {
        SW_LOG_INFO( "Quit requested from the pause menu" );
        _pEngine->requestQuit( 0 );
    }
} // namespace sw
