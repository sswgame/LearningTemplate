#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Scenario/AutomationStepRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/Foundation/Data/GameSettings.h"
#include "GameFramework/Base/Foundation/Framework/Flow/ScreenTransitionManager.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "LoadingScreen" );

    namespace
    {
        /** @brief 로딩 화면입니다 — Back 으로 닫히지 않는다(로드가 끝나야 닫힌다). */
        class LoadingScreen final : public UIScreen
        {
        public:
            LoadingScreen( const UIScreenDesc& desc, unique_ptr<Widget> root )
                : UIScreen{ desc, std::move( root ) }
            {
            }

            bool onBack() override { return true; }
        };

        /**
         * @brief 화면 페이드가 그림을 덮고 있으면 true 입니다(캡처 차단 조건). 로딩 화면이 닫힌 뒤 실제 시간으로 걷히므로 그동안 찍으면 어두운 화면이 찍힌다.
         * @details 페이드는 입력을 막지 않아 시나리오 시작은 미루지 않는다. 화면 UI 를 막은 동안(멈춘 에디터)은 그려지지 않으니 덮지 않는다.
         */
        bool isScreenFadeCoveringCapture()
        {
            const LoadingScreenController* pController = game::findSubsystem<LoadingScreenController>();
            const UISystem*                pUISystem   = pController != nullptr ? pController->getUISystem() : nullptr;
            return pUISystem != nullptr && pUISystem->isOnScreenSuppressed() == false && pController->findFadeScreen() != nullptr;
        }

        /** @brief `<Tip text="원문"/>` 들을 읽습니다. `text` 가 없거나 빈 팁은 읽기 오류입니다(조용히 건너뛰지 않는다). */
        [[nodiscard]] bool readTips( const XMLNode& root, string_view sourceName, vector<string>& outListTip )
        {
            bool bValid = true;
            for ( XMLNode node = root.findChild( "Tip" ); node; node = node.findNextSibling( "Tip" ) )
            {
                const utf8* pText = node.findAttribute( "text" );
                if ( pText == nullptr || pText[0] == '\0' )
                {
                    SW_LOG_ERROR( "%#: <Tip> needs a non-empty text", sourceName );
                    bValid = false;
                    continue;
                }
                outListTip.push_back( pText );
            }
            if ( bValid == false )
                outListTip.clear();
            return bValid;
        }
    } // namespace

    SW_AUTOMATION_CAPTURE_BLOCKER( screenFade, "ScreenFade", &isScreenFadeCoveringCapture );
} // namespace sw

namespace sw
{
    LoadingScreenController::LoadingScreenController()
        : _settings{}
        , _currentTip{}
        , _pUISystem{ nullptr }
        , _loadingScreen{ kInvalidUIScreenHandle }
        , _fadeScreen{ kInvalidUIScreenHandle }
        , _shownSeconds{ 0.0f }
        , _spinnerAngle{ 0.0f }
        , _tipRandom{ GameRandom::kDefaultSeed }
    {
    }

    SubsystemInitResult LoadingScreenController::initializeSubsystem()
    {
        // 게임 인스턴스가 초기화 앞에 gamesettings 를 게임 범위에 건다. 없으면(테스트 범위) 문서 기본값을 둔다.
        if ( const GameSettings* pSettings = game::findSubsystem<GameSettings>() )
        {
            LoadingScreenSettings settings = _settings;
            settings._documentPath         = pSettings->_loadingScreen; // 칸이 없으면 로딩 화면 없음(`_documentPath.isSet()`)
            settings._listTip.clear();
            // 팁 파일을 적었는데 읽지 못하면 기동을 멈춘다 — 적지 않은 것(팁 없음)과 깨진 것을 섞지 않는다.
            if ( pSettings->_loadingTips.isSet() && loadTipsFromResource( ResourceID{ pSettings->_loadingTips.get() }, settings._listTip ) == false )
                return SubsystemInitResult::Failed;
            setSettings( settings );
        }
        UISystem* pUISystem = game::findSubsystem<UISystem>();
        bindUISystem( pUISystem != nullptr && pUISystem->isInitialized() ? pUISystem : nullptr );
        return SubsystemInitResult::Succeeded;
    }

    LoadingScreenController::~LoadingScreenController()
    {
        closeScreens();
    }

    void LoadingScreenController::bindUISystem( UISystem* pUISystem )
    {
        if ( _pUISystem == pUISystem )
            return;
        closeScreens();
        _pUISystem = pUISystem;
    }

    void LoadingScreenController::setSettings( const LoadingScreenSettings& settings )
    {
        _settings = settings;
        _tipRandom.setSeed( settings._tipSeed );
    }

    bool LoadingScreenController::beginLoading()
    {
        _shownSeconds       = 0.0f;
        const bool bUIReady = _pUISystem != nullptr && _pUISystem->isInitialized();
        if ( bUIReady == false || _settings._documentPath.isSet() == false )
            return false; // 문서를 적지 않은 게임은 로딩 화면이 없다(gamesettings 의 `loadingScreen`)
        if ( findLoadingScreen() != nullptr )
            return true;
        _loadingScreen    = _pUISystem->openScreen<LoadingScreen>( _settings._documentPath.get() );
        UIScreen* pScreen = findLoadingScreen();
        if ( pScreen == nullptr )
        {
            SW_LOG_WARNING( "The loading screen '%#' could not be opened - loading continues without it", _settings._documentPath.c_str() );
            return false;
        }
        if ( pScreen->getDesc()._layer != UILayer::Loading )
            SW_LOG_WARNING( "The loading screen '%#' is not on the Loading layer - it does not cover the other screens", _settings._documentPath.c_str() );
        _spinnerAngle = 0.0f;
        applyTip( *pScreen );
        return true;
    }

    void LoadingScreenController::update( float32 deltaSeconds, bool bLoading, std::optional<float32> loadProgress, ScreenFade& fade )
    {
        UIScreen* pScreen = findLoadingScreen();
        if ( pScreen != nullptr )
        {
            _shownSeconds += deltaSeconds;
            applyProgress( *pScreen, loadProgress );
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
                _loadingScreen = kInvalidUIScreenHandle;
                fade.beginFadeIn( _settings._fadeInSeconds );
            }
        }
        syncFadeOverlay( fade.getOverlayAlpha() );
    }

    void LoadingScreenController::applyProgress( UIScreen& screen, std::optional<float32> loadProgress )
    {
        SliderWidget* pProgress = screen.getTree().findWidget<SliderWidget>( hashed_string( kProgressWidgetName ) );
        if ( pProgress == nullptr )
            return;
        if ( loadProgress.has_value() == false )
        {
            pProgress->setVisibility( WidgetVisibility::Collapsed );
            return;
        }
        pProgress->setRange( 0.0f, 1.0f, 0.0f );
        pProgress->setValue( MathUtil::clamp( *loadProgress, 0.0f, 1.0f ) );
        pProgress->setVisibility( WidgetVisibility::HitTestInvisible ); // 진행 막대는 입력을 받지 않는다
    }

    bool LoadingScreenController::loadTipsFromResource( const ResourceID& resourceID, vector<string>& outListTip )
    {
        outListTip.clear();
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        if ( XMLReadUtil::loadRootOrWarn( doc, resourceID, "LoadingTips", root, sourceName ) == false )
            return false;
        return readTips( root, sourceName, outListTip );
    }

    bool LoadingScreenController::loadTipsFromXMLText( string_view xmlText, string_view sourceName, vector<string>& outListTip )
    {
        outListTip.clear();
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, "LoadingTips", root ) == false )
            return false;
        return readTips( root, sourceName, outListTip );
    }

    bool LoadingScreenController::isShowing() const
    {
        return findLoadingScreen() != nullptr;
    }

    UIScreen* LoadingScreenController::findLoadingScreen() const
    {
        UIScreen* pScreen = _pUISystem != nullptr ? _pUISystem->findScreen( _loadingScreen ) : nullptr;
        return pScreen != nullptr && pScreen->isClosing() == false ? pScreen : nullptr;
    }

    UIScreen* LoadingScreenController::findFadeScreen() const
    {
        UIScreen* pScreen = _pUISystem != nullptr ? _pUISystem->findScreen( _fadeScreen ) : nullptr;
        return pScreen != nullptr && pScreen->isClosing() == false ? pScreen : nullptr;
    }

    void LoadingScreenController::closeScreens()
    {
        if ( _pUISystem != nullptr )
        {
            if ( _loadingScreen != kInvalidUIScreenHandle )
                _pUISystem->closeScreen( _loadingScreen );
            if ( _fadeScreen != kInvalidUIScreenHandle )
                _pUISystem->closeScreen( _fadeScreen );
        }
        _loadingScreen = kInvalidUIScreenHandle;
        _fadeScreen    = kInvalidUIScreenHandle;
    }

    void LoadingScreenController::syncFadeOverlay( float32 alpha )
    {
        UIScreen* pScreen = findFadeScreen();
        if ( alpha <= 0.0f )
        {
            if ( pScreen != nullptr )
                pScreen->close();
            _fadeScreen = kInvalidUIScreenHandle;
            return;
        }
        if ( pScreen == nullptr )
        {
            if ( _pUISystem == nullptr || _pUISystem->isInitialized() == false )
                return;
            // 뷰포트 전체를 덮는 검은 패널 — Overlay 층은 포커스 · 포인터를 받지 않는다(아래 화면 · 게임 입력이 그대로).
            unique_ptr<BorderPanel> panel = sw::make_unique<BorderPanel>();
            panel->setBackground( UIBrush::makeSolid( float4{ 0.0f, 0.0f, 0.0f, 1.0f } ) );
            panel->setVisibility( WidgetVisibility::HitTestInvisible );
            UIScreenDesc desc{};
            desc._layer       = UILayer::Overlay;
            desc._bTakesFocus = false;
            desc._bShowCursor = false;
            _fadeScreen       = _pUISystem->pushScreen( sw::make_unique<UIScreen>( desc, std::move( panel ) ) );
            pScreen           = findFadeScreen();
            if ( pScreen == nullptr )
                return;
        }
        Widget* pPanel = pScreen->getTree().getRoot();
        if ( pPanel != nullptr )
            pPanel->setOpacity( MathUtil::saturate( alpha ) );
    }

    void LoadingScreenController::applyTip( UIScreen& screen )
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
        const LocalizationManager* pLocalization = game::findSubsystem<LocalizationManager>();
        const utf8*                pFound        = pLocalization != nullptr ? pLocalization->findStringByText( _currentTip ) : nullptr;
        pTip->setText( pFound != nullptr ? pFound : _currentTip.c_str() );
        pTip->setVisibility( WidgetVisibility::HitTestInvisible );
    }
} // namespace sw
