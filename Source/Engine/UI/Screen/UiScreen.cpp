#include "pch.h"

#include "Engine/UI/Screen/UiScreen.h"

#include "Core/Log/Logger.h"

#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Binding/UiBindingSet.h"
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
        , _bindingSet{}
        , _animationPlayer{ _tree }
        , _mapCommandToHandler{}
        , _pUiSystem{ nullptr }
        , _handle{ kInvalidUiScreenHandle }
        , _lastFocused{ kInvalidWidgetID }
        , _pushOrder{ 0 }
        , _bClosing{ SW_FALSE }
    {
        _tree._pScreen = this;
        _tree.setRoot( std::move( root ) );
        _bindingSet = make_unique<UiBindingSet>( *this );
    }

    UiScreen::~UiScreen() = default;

    bool UiScreen::takesFocus() const
    {
        return _desc._bTakesFocus && _desc._layer != UiLayer::HUD && _desc._layer != UiLayer::Overlay;
    }

    void UiScreen::close()
    {
        if ( _pUiSystem != nullptr )
            _pUiSystem->closeScreen( _handle );
        else
            _bClosing = SW_TRUE;
    }

    void UiScreen::addBinding( const UiBindingDesc& binding )
    {
        _listBinding.push_back( binding );
        _bindingSet->markRebind();
    }

    void UiScreen::removeOrphanBindings()
    {
        const auto newEnd = std::remove_if( _listBinding.begin(), _listBinding.end(),
                                            [this]( const UiBindingDesc& binding )
        { return _tree.findWidgetByID( binding._widget ) == nullptr; } );
        if ( newEnd == _listBinding.end() )
            return;
        _listBinding.erase( newEnd, _listBinding.end() );
        _bindingSet->markRebind();
    }

    void UiScreen::setViewModel( UiViewModel* pViewModel )
    {
        _bindingSet->setViewModel( pViewModel );
    }

    UiViewModel* UiScreen::getViewModel() const
    {
        return _bindingSet->getViewModel();
    }

    void UiScreen::onWidgetValueEdited( Widget& widget, const hashed_string& propertyName )
    {
        _bindingSet->onWidgetValueEdited( widget, propertyName );
    }

    bool UiScreen::onBack()
    {
        close();
        return true;
    }

    bool UiScreen::onUnhandledAction( const hashed_string& action )
    {
        (void)action;
        return false;
    }

    void UiScreen::onTick( float32 deltaSeconds )
    {
        (void)deltaSeconds;
    }

    void UiScreen::onTreeRebuilt()
    {
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
