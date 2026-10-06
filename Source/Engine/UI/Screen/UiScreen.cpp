#include "pch.h"

#include "Engine/UI/Screen/UiScreen.h"

#include "Core/Log/Logger.h"

#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/UiSystem.h"

namespace sw
{
    SW_LOG_CALLER( "UiScreen" );

    UiScreen::UiScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
        : _tree{}
        , _desc{ desc }
        , _documentPath{}
        , _listBinding{}
        , _mapCommandToHandler{}
        , _pUiSystem{ nullptr }
        , _handle{ kInvalidUiScreenHandle }
        , _lastFocused{ kInvalidWidgetId }
        , _pushOrder{ 0 }
        , _bClosing{ SW_FALSE }
    {
        _tree.setRoot( std::move( root ) );
        _tree.setCommandHandler( SW_DELEGATE_METHOD( UiCommandDelegate, &UiScreen::dispatchCommand, this ) );
    }

    UiScreen::~UiScreen() = default;

    bool UiScreen::takesFocus() const
    {
        return _desc._bTakesFocus && _desc._layer != UiLayer::Hud && _desc._layer != UiLayer::Overlay;
    }

    void UiScreen::close()
    {
        if ( _pUiSystem != nullptr )
            _pUiSystem->closeScreen( _handle );
        else
            _bClosing = SW_TRUE;
    }

    bool UiScreen::onBack()
    {
        close();
        return true;
    }

    void UiScreen::registerCommand( const hashed_string& command, const UiCommandDelegate& handler )
    {
        _mapCommandToHandler[command] = handler;
    }

    void UiScreen::unregisterCommand( const hashed_string& command )
    {
        _mapCommandToHandler.erase( command );
    }

    bool UiScreen::onCommand( const hashed_string& command, Widget& source )
    {
        const auto found = _mapCommandToHandler.find( command );
        if ( found == _mapCommandToHandler.end() || found->second.isBound() == false )
            return false;
        found->second( command, source );
        return true;
    }

    void UiScreen::dispatchCommand( const hashed_string& command, Widget& source )
    {
        if ( onCommand( command, source ) == false )
            SW_LOG_WARNING( "[Ui] Command '%#' from widget '%#' is not handled by screen %# (%#)", command.c_str(), source.getName().c_str(), _handle,
                            _documentPath.empty() ? "built in code" : _documentPath.c_str() );
    }
} // namespace sw
