#include "pch.h"

#include "GameFramework/Base/UI/UI/HudControllerComponent.h"

#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/UiSystem.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "HudController" );

    HudControllerComponent::HudControllerComponent()
        : Component{}
        , _pUiSystem{ nullptr }
        , _screen{ kInvalidUiScreenHandle }
        , _viewModel{ make_unique<HudViewModel>() }
        , _documentPath{}
    {
    }

    HudControllerComponent::~HudControllerComponent()
    {
        bindUiSystem( nullptr );
    }

    void HudControllerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        UiSystem* pUi = game::getService<UiSystem>();
        bindUiSystem( pUi != nullptr && pUi->isInitialized() ? pUi : nullptr );
    }

    void HudControllerComponent::onEndPlay()
    {
        bindUiSystem( nullptr );
        Component::onEndPlay();
    }

    void HudControllerComponent::bindUiSystem( UiSystem* pUiSystem )
    {
        if ( _pUiSystem == pUiSystem )
            return;
        closeScreen();
        _pUiSystem = pUiSystem;
        openScreen();
    }

    void HudControllerComponent::setDocumentPath( string_view documentPath )
    {
        if ( _documentPath == documentPath )
            return;
        _documentPath = string( documentPath );
        if ( _screen == kInvalidUiScreenHandle )
            return;
        closeScreen();
        openScreen();
    }

    UiScreen* HudControllerComponent::getScreen() const
    {
        UiScreen* pScreen = _pUiSystem != nullptr ? _pUiSystem->findScreen( _screen ) : nullptr;
        return pScreen != nullptr && pScreen->isClosing() == false ? pScreen : nullptr;
    }

    Widget* HudControllerComponent::findWidget( const hashed_string& name ) const
    {
        const UiScreen* pScreen = getScreen();
        return pScreen != nullptr ? pScreen->getTree().findWidgetByName( name ) : nullptr;
    }

    void HudControllerComponent::openScreen()
    {
        if ( _pUiSystem == nullptr || _documentPath.empty() || getScreen() != nullptr )
            return;
        _screen           = _pUiSystem->openScreen( _documentPath );
        UiScreen* pScreen = getScreen();
        if ( pScreen != nullptr )
            pScreen->setViewModel( _viewModel.get() );
        if ( pScreen != nullptr && pScreen->getDesc()._layer != UiLayer::Hud )
            SW_LOG_WARNING( "HUD document '%#' is not on the Hud layer - it takes focus and may block game input", _documentPath.c_str() );
    }

    void HudControllerComponent::closeScreen()
    {
        // 닫기는 지연이라(닫기 애니메이션 · 다음 update) 그동안 뷰모델이 먼저 지워질 수 있다 — 지금 뗀다.
        UiScreen* pScreen = _pUiSystem != nullptr ? _pUiSystem->findScreen( _screen ) : nullptr;
        if ( pScreen != nullptr )
            pScreen->setViewModel( nullptr );
        if ( _pUiSystem != nullptr && _screen != kInvalidUiScreenHandle )
            _pUiSystem->closeScreen( _screen );
        _screen = kInvalidUiScreenHandle;
    }
} // namespace sw
