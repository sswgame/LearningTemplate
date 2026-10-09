#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/Foundation/Framework/Flow/ScreenTransitionManager.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "LoadingScreen" );

    namespace
    {
        /** @brief 로딩 화면입니다 — Back 으로 닫히지 않는다(로드가 끝나야 닫힌다). */
        class LoadingScreen final : public UiScreen
        {
        public:
            LoadingScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
                : UiScreen{ desc, std::move( root ) }
            {
            }

            bool onBack() override { return true; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoadingScreenController::LoadingScreenController()
        : _settings{}
        , _currentTip{}
        , _pUiSystem{ nullptr }
        , _loadingScreen{ kInvalidUiScreenHandle }
        , _fadeScreen{ kInvalidUiScreenHandle }
        , _shownSeconds{ 0.0f }
        , _spinnerAngle{ 0.0f }
        , _tipRandom{ GameRandom::kDefaultSeed }
    {
    }

    LoadingScreenController::~LoadingScreenController()
    {
        closeScreens();
    }

    void LoadingScreenController::bindUiSystem( UiSystem* pUiSystem )
    {
        if ( _pUiSystem == pUiSystem )
            return;
        closeScreens();
        _pUiSystem = pUiSystem;
    }

    void LoadingScreenController::setSettings( const LoadingScreenSettings& settings )
    {
        _settings = settings;
        _tipRandom.setSeed( settings._tipSeed );
    }

    bool LoadingScreenController::beginLoading()
    {
        _shownSeconds = 0.0f;
        if ( _pUiSystem == nullptr || _pUiSystem->isInitialized() == false )
            return false;
        if ( findLoadingScreen() != nullptr )
            return true;
        _loadingScreen    = _pUiSystem->openScreen<LoadingScreen>( _settings._documentPath );
        UiScreen* pScreen = findLoadingScreen();
        if ( pScreen == nullptr )
        {
            SW_LOG_WARNING( "The loading screen '%#' could not be opened - loading continues without it", _settings._documentPath.c_str() );
            return false;
        }
        if ( pScreen->getDesc()._layer != UiLayer::Loading )
            SW_LOG_WARNING( "The loading screen '%#' is not on the Loading layer - it does not cover the other screens", _settings._documentPath.c_str() );
        _spinnerAngle = 0.0f;
        applyTip( *pScreen );
        return true;
    }

    void LoadingScreenController::update( float32 deltaSeconds, bool bLoading, ScreenFade& fade )
    {
        UiScreen* pScreen = findLoadingScreen();
        if ( pScreen != nullptr )
        {
            _shownSeconds += deltaSeconds;
            if ( bLoading || _shownSeconds < _settings._minimumSeconds )
            {
                _spinnerAngle    = MathUtil::fmod( _spinnerAngle + kSpinnerRadiansPerSecond * deltaSeconds, MathUtil::kPi * 2.0f );
                Widget* pSpinner = pScreen->getTree().findWidgetByName( hashed_string( kSpinnerWidgetName ) );
                if ( pSpinner != nullptr )
                {
                    WidgetRenderTransform transform = pSpinner->getRenderTransform();
                    transform._angle                = _spinnerAngle;
                    pSpinner->setRenderTransform( transform );
                }
            }
            else
            {
                // 닫고 검은 화면에서 씬으로 돌아온다 — 페이드는 Overlay 층이라 입력을 막지 않는다.
                pScreen->close();
                _loadingScreen = kInvalidUiScreenHandle;
                fade.beginFadeIn( _settings._fadeInSeconds );
            }
        }
        syncFadeOverlay( fade.getOverlayAlpha() );
    }

    bool LoadingScreenController::isShowing() const
    {
        return findLoadingScreen() != nullptr;
    }

    UiScreen* LoadingScreenController::findLoadingScreen() const
    {
        UiScreen* pScreen = _pUiSystem != nullptr ? _pUiSystem->findScreen( _loadingScreen ) : nullptr;
        return pScreen != nullptr && pScreen->isClosing() == false ? pScreen : nullptr;
    }

    UiScreen* LoadingScreenController::findFadeScreen() const
    {
        UiScreen* pScreen = _pUiSystem != nullptr ? _pUiSystem->findScreen( _fadeScreen ) : nullptr;
        return pScreen != nullptr && pScreen->isClosing() == false ? pScreen : nullptr;
    }

    void LoadingScreenController::closeScreens()
    {
        if ( _pUiSystem != nullptr )
        {
            if ( _loadingScreen != kInvalidUiScreenHandle )
                _pUiSystem->closeScreen( _loadingScreen );
            if ( _fadeScreen != kInvalidUiScreenHandle )
                _pUiSystem->closeScreen( _fadeScreen );
        }
        _loadingScreen = kInvalidUiScreenHandle;
        _fadeScreen    = kInvalidUiScreenHandle;
    }

    void LoadingScreenController::syncFadeOverlay( float32 alpha )
    {
        UiScreen* pScreen = findFadeScreen();
        if ( alpha <= 0.0f )
        {
            if ( pScreen != nullptr )
                pScreen->close();
            _fadeScreen = kInvalidUiScreenHandle;
            return;
        }
        if ( pScreen == nullptr )
        {
            if ( _pUiSystem == nullptr || _pUiSystem->isInitialized() == false )
                return;
            // 뷰포트 전체를 덮는 검은 패널 — Overlay 층은 포커스 · 포인터를 받지 않는다(아래 화면 · 게임 입력이 그대로).
            unique_ptr<BorderPanel> panel = sw::make_unique<BorderPanel>();
            panel->setBackground( UiBrush::makeSolid( float4{ 0.0f, 0.0f, 0.0f, 1.0f } ) );
            panel->setVisibility( WidgetVisibility::HitTestInvisible );
            UiScreenDesc desc{};
            desc._layer       = UiLayer::Overlay;
            desc._bTakesFocus = false;
            desc._bShowCursor = false;
            _fadeScreen       = _pUiSystem->pushScreen( sw::make_unique<UiScreen>( desc, std::move( panel ) ) );
            pScreen           = findFadeScreen();
            if ( pScreen == nullptr )
                return;
        }
        Widget* pPanel = pScreen->getTree().getRoot();
        if ( pPanel != nullptr )
            pPanel->setOpacity( MathUtil::saturate( alpha ) );
    }

    void LoadingScreenController::applyTip( UiScreen& screen )
    {
        TextWidget* pTip = screen.getTree().findWidget<TextWidget>( hashed_string( kTipWidgetName ) );
        _currentTip.clear();
        if ( _settings._listTip.empty() == false )
        {
            const int32 index = _tipRandom.nextInt( 0, static_cast<int32>( _settings._listTip.size() ) - 1 );
            _currentTip       = _settings._listTip[static_cast<size_t>( index )];
        }
        if ( pTip == nullptr )
            return;
        if ( _currentTip.empty() )
        {
            pTip->setVisibility( WidgetVisibility::Collapsed );
            return;
        }
        // 팁은 현지화 키(원문)다 — 지금 문화권의 글로 푼다(표에 없으면 원문 그대로).
        const LocalizationManager* pLocalization = game::getService<LocalizationManager>();
        pTip->setText( pLocalization != nullptr ? pLocalization->getStringByText( _currentTip, _currentTip.c_str() ) : _currentTip.c_str() );
        pTip->setVisibility( WidgetVisibility::HitTestInvisible );
    }
} // namespace sw
