#include "pch.h"

#include "Engine/UI/Screen/UIScreen.h"

#include "Core/Log/Logger.h"

#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Binding/UIBindingSet.h"
#include "Engine/UI/Style/UIStyleSet.h"
#include "Engine/UI/UISystem.h"

namespace sw
{
    SW_LOG_CALLER( "UIScreen" );

    UIScreen::UIScreen( const UIScreenDesc& desc, unique_ptr<Widget> root )
        : _tree{}
        , _desc{ desc }
        , _documentPath{}
        , _listBinding{}
        , _listStyleSheet{}
        , _styleSet{}
        , _bindingSet{}
        , _animationPlayer{ _tree }
        , _mapCommandToHandler{}
        , _pUISystem{ nullptr }
        , _handle{ kInvalidUIScreenHandle }
        , _lastFocused{ kInvalidWidgetID }
        , _pushOrder{ 0 }
        , _bClosing{ SW_FALSE }
    {
        _tree._pScreen = this;
        _tree.setRoot( std::move( root ) );
        _bindingSet = make_unique<UIBindingSet>( *this );
    }

    UIScreen::~UIScreen() = default;

    bool UIScreen::takesFocus() const
    {
        return _desc._bTakesFocus && _desc._layer != UILayer::HUD && _desc._layer != UILayer::Overlay;
    }

    void UIScreen::close()
    {
        if ( _pUISystem != nullptr )
            _pUISystem->closeScreen( _handle );
        else
            _bClosing = SW_TRUE;
    }

    void UIScreen::addBinding( const UIBindingDesc& binding )
    {
        _listBinding.push_back( binding );
        _bindingSet->markRebind();
    }

    void UIScreen::removeOrphanBindings()
    {
        const auto newEnd = std::remove_if( _listBinding.begin(), _listBinding.end(),
                                            [this]( const UIBindingDesc& binding )
        { return _tree.findWidgetByID( binding._widget ) == nullptr; } );
        if ( newEnd == _listBinding.end() )
            return;
        _listBinding.erase( newEnd, _listBinding.end() );
        _bindingSet->markRebind();
    }

    void UIScreen::setViewModel( UIViewModel* pViewModel )
    {
        _bindingSet->setViewModel( pViewModel );
    }

    UIViewModel* UIScreen::getViewModel() const
    {
        return _bindingSet->getViewModel();
    }

    void UIScreen::onWidgetValueEdited( Widget& widget, const hashed_string& propertyName )
    {
        _bindingSet->onWidgetValueEdited( widget, propertyName );
    }

    bool UIScreen::onBack()
    {
        close();
        return true;
    }

    bool UIScreen::onUnhandledAction( const hashed_string& action )
    {
        (void)action;
        return false;
    }

    void UIScreen::onTick( float32 deltaSeconds )
    {
        (void)deltaSeconds;
    }

    void UIScreen::onTreeRebuilt()
    {
    }

    void UIScreen::registerCommand( const hashed_string& command, const UICommandDelegate& handler )
    {
        _mapCommandToHandler[command] = handler;
    }

    void UIScreen::unregisterCommand( const hashed_string& command )
    {
        _mapCommandToHandler.erase( command );
    }

    bool UIScreen::onCommand( const hashed_string& command, Widget& source )
    {
        const auto iter = _mapCommandToHandler.find( command );
        if ( iter == _mapCommandToHandler.end() || iter->second.isBound() == false )
            return false;
        iter->second( command, source );
        return true;
    }

    void UIScreen::dispatchCommand( const hashed_string& command, Widget& source )
    {
        if ( command.empty() )
            return;
        if ( onCommand( command, source ) == false )
            SW_LOG_WARNING( "[UI] Command '%#' from widget '%#' is not handled by screen %# (%#)", command.c_str(), source.getName().c_str(), _handle,
                            _documentPath.empty() ? "built in code" : _documentPath.c_str() );
    }
} // namespace sw
