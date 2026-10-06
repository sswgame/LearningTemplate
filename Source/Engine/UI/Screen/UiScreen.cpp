#include "pch.h"

#include "Engine/UI/Screen/UiScreen.h"

#include "Core/Log/Logger.h"

#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Style/UiStyleSet.h"
#include "Engine/UI/UiSystem.h"

namespace sw
{
    SW_LOG_CALLER( "UiScreen" );

    UiScreen::UiScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
        : _tree{}
        , _desc{ desc }
        , _documentPath{}
        , _listBinding{}
        , _listStyleSheet{}
        , _styleSet{}
        , _mapCommandToHandler{}
        , _pUiSystem{ nullptr }
        , _handle{ kInvalidUiScreenHandle }
        , _lastFocused{ kInvalidWidgetId }
        , _pushOrder{ 0 }
        , _bClosing{ SW_FALSE }
    {
        _tree._pScreen = this;
        _tree.setRoot( std::move( root ) );
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
        const auto iter = _mapCommandToHandler.find( command );
        if ( iter == _mapCommandToHandler.end() || iter->second.isBound() == false )
            return false;
        iter->second( command, source );
        return true;
    }

    void UiScreen::dispatchCommand( const hashed_string& command, Widget& source )
    {
        if ( command.empty() )
            return;
        if ( onCommand( command, source ) == false )
            SW_LOG_WARNING( "[Ui] Command '%#' from widget '%#' is not handled by screen %# (%#)", command.c_str(), source.getName().c_str(), _handle,
                            _documentPath.empty() ? "built in code" : _documentPath.c_str() );
    }
} // namespace sw
