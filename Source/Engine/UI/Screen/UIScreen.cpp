#include "pch.h"

#include "Engine/UI/Screen/UIScreen.h"

#include "Core/Container/VectorUtil.h"
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
        , _nextCommandID{ 1 }
        , _pUISystem{ nullptr }
        , _handle{ kInvalidUIScreenHandle }
        , _lastFocused{ kInvalidWidgetID }
        , _pushOrder{ 0 }
        , _bClosing{ SW_FALSE }
        , _bHiddenByLayer{ SW_FALSE }
        , _bReportsUnhandledCommands{ SW_TRUE }
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
        const size_t removeCount = VectorUtil::removeIf( _listBinding, [this]( const UIBindingDesc& binding )
        { return _tree.findWidgetByID( binding._widget ) == nullptr; } );
        if ( removeCount == 0 )
            return;
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

    Subscription UIScreen::registerCommand( const hashed_string& command, const UICommandDelegate& handler )
    {
        const uint64 id               = _nextCommandID++;
        _mapCommandToHandler[command] = CommandEntry{ handler, id };
        return _subscriptions.issue( id );
    }

    void UIScreen::releaseSubscription( uint64 id )
    {
        // 명령은 화면마다 몇 개뿐이다. 교체된 옛 구독이면 같은 번호가 없어 할 일이 없다.
        for ( auto iter = _mapCommandToHandler.begin(); iter != _mapCommandToHandler.end(); ++iter )
        {
            if ( iter->second._id != id )
                continue;
            _mapCommandToHandler.erase( iter );
            return;
        }
    }

    bool UIScreen::onCommand( const hashed_string& command, Widget& source )
    {
        const auto iter = _mapCommandToHandler.find( command );
        if ( iter == _mapCommandToHandler.end() || iter->second._handler.isBound() == false )
            return false;
        iter->second._handler( command, source );
        return true;
    }

    void UIScreen::dispatchCommand( const hashed_string& command, Widget& source )
    {
        if ( command.empty() )
            return;
        const bool bHandled = onCommand( command, source ) || ( _pUISystem != nullptr && _pUISystem->getGlobalCommands().tryExecute( command, source ) );
        if ( bHandled )
            return;
        const utf8* pOrigin = _documentPath.empty() ? "built in code" : _documentPath.c_str();
        if ( _bReportsUnhandledCommands == SW_TRUE )
            SW_LOG_WARNING( "[UI] Command '%#' from widget '%#' is not handled by screen %# (%#)", command.c_str(), source.getName().c_str(), _handle, pOrigin );
        else
            SW_LOG_INFO( "[UI] Command '%#' from widget '%#' is not wired in this preview (%#)", command.c_str(), source.getName().c_str(), pOrigin );
    }
} // namespace sw
