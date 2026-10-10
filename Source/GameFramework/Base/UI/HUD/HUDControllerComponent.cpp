#include "pch.h"

#include "GameFramework/Base/UI/HUD/HUDControllerComponent.h"

#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "HUDController" );

    HUDControllerComponent::HUDControllerComponent()
        : Component{}
        , _pUISystem{ nullptr }
        , _screen{ kInvalidUIScreenHandle }
        , _viewModel{ make_unique<HUDViewModel>() }
        , _documentPath{}
    {
    }

    HUDControllerComponent::~HUDControllerComponent()
    {
        bindUISystem( nullptr );
    }

    void HUDControllerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        UISystem* pUI = game::getService<UISystem>();
        bindUISystem( pUI != nullptr && pUI->isInitialized() ? pUI : nullptr );
    }

    void HUDControllerComponent::onEndPlay()
    {
        bindUISystem( nullptr );
        Component::onEndPlay();
    }

    void HUDControllerComponent::bindUISystem( UISystem* pUISystem )
    {
        if ( _pUISystem == pUISystem )
            return;
        closeScreen();
        _pUISystem = pUISystem;
        openScreen();
    }

    void HUDControllerComponent::setDocumentPath( string_view documentPath )
    {
        if ( _documentPath == documentPath )
            return;
        _documentPath = string( documentPath );
        if ( _screen == kInvalidUIScreenHandle )
            return;
        closeScreen();
        openScreen();
    }

    UIScreen* HUDControllerComponent::getScreen() const
    {
        UIScreen* pScreen = _pUISystem != nullptr ? _pUISystem->findScreen( _screen ) : nullptr;
        return pScreen != nullptr && pScreen->isClosing() == false ? pScreen : nullptr;
    }

    Widget* HUDControllerComponent::findWidget( const hashed_string& name ) const
    {
        const UIScreen* pScreen = getScreen();
        return pScreen != nullptr ? pScreen->getTree().findWidgetByName( name ) : nullptr;
    }

    void HUDControllerComponent::openScreen()
    {
        if ( _pUISystem == nullptr || _documentPath.empty() || getScreen() != nullptr )
            return;
        _screen           = _pUISystem->openScreen( _documentPath );
        UIScreen* pScreen = getScreen();
        if ( pScreen != nullptr )
            pScreen->setViewModel( _viewModel.get() );
        if ( pScreen != nullptr && pScreen->getDesc()._layer != UILayer::HUD )
            SW_LOG_WARNING( "HUD document '%#' is not on the HUD layer - it takes focus and may block game input", _documentPath.c_str() );
    }

    void HUDControllerComponent::closeScreen()
    {
        // 닫기는 지연이라(닫기 애니메이션 · 다음 update) 그동안 뷰모델이 먼저 지워질 수 있다 — 지금 뗀다.
        UIScreen* pScreen = _pUISystem != nullptr ? _pUISystem->findScreen( _screen ) : nullptr;
        if ( pScreen != nullptr )
            pScreen->setViewModel( nullptr );
        if ( _pUISystem != nullptr && _screen != kInvalidUIScreenHandle )
            _pUISystem->closeScreen( _screen );
        _screen = kInvalidUIScreenHandle;
    }
} // namespace sw
