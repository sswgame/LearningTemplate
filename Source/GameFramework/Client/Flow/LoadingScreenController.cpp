#include "pch.h"

#include "GameFramework/Client/Flow/LoadingScreenController.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"
#include "Engine/UI/Menu/ConfirmDialogScreen.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

#include "GameFramework/Runtime/Foundation/Flow/ScreenTransitionManager.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Foundation/Save/GameSaveSlots.h"
#include "GameFramework/Runtime/Foundation/Service/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 로딩 화면입니다 — Back 으로 닫히지 않고(로드가 끝나야 닫힌다), 떠 있는 동안 게임 시간을 멈춘다(문서와 상관없이 이 형식이 정한다).
         * @details 멈추지 않으면 월드가 로딩 화면 뒤에서 돌아, 화면이 걷혔을 때 월드가 몇 틱 지났는지가 로드 시간 · 최소 표시 시간에 달린다 — 같은 씬을 기동에서 열 때와
         *          실행 중에 열 때가 다르고, 디스크가 느린 PC 에서 첫 장면이 다르다. 멈추면 플레이어 · 시나리오가 보는 첫 프레임이 늘 월드의 첫 틱이다.
         */
        class LoadingScreen final : public UIScreen
        {
        public:
            LoadingScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
                : UIScreen{ makePausingDesc( desc ), std::move( root ) }
            {
            }

            bool onBack() override { return true; }

        private:
            static UIScreenDef makePausingDesc( const UIScreenDef& desc )
            {
                UIScreenDef pausing  = desc;
                pausing._bPausesGame = true;
                return pausing;
            }
        };

        /** @struct TipList @brief 팁 파일의 표 읽기 대상입니다(`XMLCatalogReadResult` — 모르는 원소도 읽기 오류다). */
        struct TipList
        {
            vector<string> _listTip;

            /** @brief `<Tip text="원문"/>` 하나를 읽습니다. `text` 가 없거나 빈 팁은 읽기 오류입니다(조용히 건너뛰지 않는다). */
            bool readTip( const XMLNode& node, string_view sourceName )
            {
                const utf8* pText = node.findAttribute( "text" );
                if ( pText == nullptr || pText[0] == '\0' )
                {
                    SW_LOG_ERROR_ID( "LoadingScreen.TipNeedsNonEmpty", "%#: <Tip> needs a non-empty text", sourceName );
                    return false;
                }
                _listTip.push_back( pText );
                return true;
            }
        };

        /** @brief 팁 파일 루트 @p root 를 읽습니다. 틀린 원소가 하나라도 있으면 목록을 비우고 false 입니다. */
        [[nodiscard]] bool readTips( const XMLNode& root, string_view sourceName, vector<string>& outListTip )
        {
            TipList list;
            using Element                          = XMLCatalogElement<TipList>;
            static constexpr Element kArrElement[] = {
                Element::of<&TipList::readTip>( "Tip" ).uncounted(), // 팁이 없는 파일도 맞다 — 팁 위젯을 접는다
            };
            if ( XMLCatalogReadResult::readInDocumentOrder( list, XMLCatalogRoot{ root }, sourceName, kArrElement ).isClean() == false )
                return false;
            outListTip = std::move( list._listTip );
            return true;
        }
    } // namespace
} // namespace sw

namespace sw
{
    LoadingScreenController::LoadingScreenController()
        : _loadingScreenPort{ *this }
        , _settings{}
        , _currentTip{}
        , _pUISystem{ nullptr }
        , _viewModel{}
        , _loadingScreen{}
        , _fadeScreen{}
        , _loadFailureDialog{}
        , _saveFailureDialog{}
        , _shownSeconds{ 0.0f }
        , _spinnerAngle{ 0.0f }
        , _sceneLoadSubscription{}
        , _saveSavedSubscription{}
        , _saveLoadedSubscription{}
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
            if ( pSettings->_loadingTips.isSet() && loadTipsFromResource( AssetID{ pSettings->_loadingTips.get() }, settings._listTip ) == false )
                return SubsystemInitResult::Failed;
            setSettings( settings );
        }
        UISystem* pUISystem = game::findSubsystem<UISystem>();
        bindUISystem( pUISystem != nullptr && pUISystem->isInitialized() ? pUISystem : nullptr );
        // 버스가 없는 범위(도구 · 테스트)는 받을 이벤트도 없다.
        EventDispatcher* const pDispatcher = game::findSubsystem<EventDispatcher>();
        if ( pDispatcher == nullptr )
            return SubsystemInitResult::Succeeded;
        _sceneLoadSubscription  = pDispatcher->subscribe<SceneLoadCompletedEvent>( gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const SceneLoadCompletedEvent& )>, &LoadingScreenController::onSceneLoadCompleted, this ) );
        _saveSavedSubscription  = pDispatcher->subscribe<SaveGameSavedEvent>( gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const SaveGameSavedEvent& )>, &LoadingScreenController::onSaveGameSaved, this ) );
        _saveLoadedSubscription = pDispatcher->subscribe<SaveGameLoadedEvent>( gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const SaveGameLoadedEvent& )>, &LoadingScreenController::onSaveGameLoaded, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void LoadingScreenController::shutdownSubsystem()
    {
        _sceneLoadSubscription.reset();
        _saveSavedSubscription.reset();
        _saveLoadedSubscription.reset();
        bindUISystem( nullptr );
    }

    void LoadingScreenController::onSceneLoadCompleted( const SceneLoadCompletedEvent& event )
    {
        _loadFailureDialog.reset();
        const SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        const bool                bSuperseded   = pSceneManager != nullptr && pSceneManager->isTransitioning(); // 뒤 요청에 밀렸다 — 그 로드의 끝이 온다
        if ( event._bSuccess || bSuperseded || _pUISystem == nullptr || _pUISystem->isInitialized() == false )
            return;
        ConfirmDialogDesc desc{};
        // 키를 넘긴다 — 바인딩이 지금 문화권으로 푼다(원문은 engine.strings.xml).
        desc._title            = SW_LOCKEYREF( "SceneLoad", "FailedTitle" );
        desc._message          = SW_LOCKEYREF( "SceneLoad", "FailedMessage" );
        desc._acceptText       = SW_LOCKEYREF( "SceneLoad", "Retry" );
        desc._cancelText       = SW_LOCKEYREF( "UIMenu", "Cancel" );
        const string levelName = event._levelName;
        _loadFailureDialog     = UIOwnedScreen( *_pUISystem, ConfirmDialogScreen::open( *_pUISystem, desc, SW_DELEGATE_LAMBDA( ConfirmDialogScreen::ResultDelegate, [levelName]( ConfirmDialogResult result )
            {
            SceneManager* const pScenes = game::findSubsystem<SceneManager>();
            if ( result == ConfirmDialogResult::Accepted && pScenes != nullptr && pScenes->requestLoadFuture( levelName, SceneLoadKind::Travel ).isValid() == false )
                SW_LOG_ERROR_ID( "LoadingScreen.RetryNotRequested", "The scene '%#' could not be requested again", levelName.c_str() );
        } ) ) );
    }

    bool LoadingScreenController::isShowingLoadFailure() const
    {
        return _loadFailureDialog.find() != nullptr;
    }

    void LoadingScreenController::onSaveGameSaved( const SaveGameSavedEvent& event )
    {
        _saveFailureDialog.reset();
        if ( event._bSuccess || _pUISystem == nullptr || _pUISystem->isInitialized() == false )
            return;
        ConfirmDialogDesc desc{};
        desc._title        = SW_LOCKEYREF( "SaveGame", "SaveFailedTitle" );
        desc._message      = SW_LOCKEYREF( "SaveGame", "SaveFailedMessage" );
        desc._acceptText   = SW_LOCKEYREF( "SceneLoad", "Retry" );
        desc._cancelText   = SW_LOCKEYREF( "UIMenu", "Cancel" );
        const string slot  = event._slot;
        _saveFailureDialog = UIOwnedScreen( *_pUISystem, ConfirmDialogScreen::open( *_pUISystem, desc, SW_DELEGATE_LAMBDA( ConfirmDialogScreen::ResultDelegate, [slot]( ConfirmDialogResult result )
        {
            GameSaveSlots* const pSlots = game::findSubsystem<GameSaveSlots>();
            if ( result == ConfirmDialogResult::Accepted && pSlots != nullptr )
                (void)pSlots->save( slot ); // 또 지면 그 이벤트가 다시 알린다
        } ) ) );
    }

    void LoadingScreenController::onSaveGameLoaded( const SaveGameLoadedEvent& event )
    {
        _saveFailureDialog.reset();
        // 세이브의 씬이 열리지 않아 진 것은 씬 로드 실패 알림이 이미 말한다.
        if ( event._bSuccess || event._bSuperseded || isShowingLoadFailure() || _pUISystem == nullptr || _pUISystem->isInitialized() == false )
            return;
        ConfirmDialogDesc desc{};
        desc._title        = SW_LOCKEYREF( "SaveGame", "LoadFailedTitle" );
        desc._message      = SW_LOCKEYREF( "SaveGame", "LoadFailedMessage" );
        desc._acceptText   = SW_LOCKEYREF( "SceneLoad", "Retry" );
        desc._cancelText   = SW_LOCKEYREF( "UIMenu", "Cancel" );
        const string slot  = event._slot;
        _saveFailureDialog = UIOwnedScreen( *_pUISystem, ConfirmDialogScreen::open( *_pUISystem, desc, SW_DELEGATE_LAMBDA( ConfirmDialogScreen::ResultDelegate, [slot]( ConfirmDialogResult result )
        {
            GameSaveSlots* const pSlots = game::findSubsystem<GameSaveSlots>();
            if ( result == ConfirmDialogResult::Accepted && pSlots != nullptr )
                (void)pSlots->load( slot );
        } ) ) );
    }

    bool LoadingScreenController::isShowingSaveFailure() const
    {
        return _saveFailureDialog.find() != nullptr;
    }

    LoadingScreenController::~LoadingScreenController() = default;

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
        _loadingScreen    = UIOwnedScreen( *_pUISystem, _pUISystem->openScreen<LoadingScreen>( _settings._documentPath.get() ) );
        UIScreen* pScreen = findLoadingScreen();
        if ( pScreen == nullptr )
        {
            SW_LOG_WARNING_ID( "LoadingScreen.LoadingScreenCouldNot", "The loading screen '%#' could not be opened - loading continues without it", _settings._documentPath.c_str() );
            return false;
        }
        if ( pScreen->getDesc()._layer != UILayer::Loading )
            SW_LOG_WARNING_ID( "LoadingScreen.LoadingScreenNotLoading", "The loading screen '%#' is not on the Loading layer - it does not cover the other screens", _settings._documentPath.c_str() );
        pScreen->setViewModel( &_viewModel );
        _viewModel.setProgress( nullopt );
        _spinnerAngle = 0.0f;
        applyTip();
        return true;
    }

    void LoadingScreenController::update( float32 deltaSeconds, bool bLoading, optional<float32> loadProgress, ScreenTransitionManager& transition )
    {
        UIScreen* pScreen = findLoadingScreen();
        if ( pScreen != nullptr )
        {
            _shownSeconds += deltaSeconds;
            _viewModel.setProgress( loadProgress );
            const bool bSkipMinimum = game::getSubsystem<DeterministicRun>().getPolicy()._bSkipLoadingScreenMinimum == SW_TRUE;
            if ( bLoading || ( bSkipMinimum == false && _shownSeconds < _settings._minimumSeconds ) )
            {
                _spinnerAngle = MathUtil::fmod( _spinnerAngle + kSpinnerRadiansPerSecond * deltaSeconds, MathUtil::kPi * 2.0f );
                static const hashed_string s_spinnerName( kSpinnerWidgetName );
                Widget*                    pSpinner = pScreen->getTree().findWidgetByName( s_spinnerName );
                if ( pSpinner != nullptr )
                {
                    WidgetRenderTransform transform = pSpinner->getRenderTransform();
                    transform._angle                = Radians::fromRadians( _spinnerAngle );
                    pSpinner->setRenderTransform( transform );
                }
            }
            else
            {
                // 닫고 검은 화면에서 씬으로 돌아온다 — 페이드는 Overlay 층이라 입력을 막지 않는다.
                _loadingScreen.reset();
                transition.fadeInFromBlack( _settings._fadeInSeconds );
            }
        }
        syncFadeOverlay( transition.fade().getOverlayAlpha() );
    }

    bool LoadingScreenController::loadTipsFromResource( const AssetID& assetID, vector<string>& outListTip )
    {
        outListTip.clear();
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        if ( XMLReadUtil::loadRootOrWarn( doc, assetID, "LoadingTips", root, sourceName ) == false )
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
        return _loadingScreen.find();
    }

    UIScreen* LoadingScreenController::findFadeScreen() const
    {
        return _fadeScreen.find();
    }

    void LoadingScreenController::closeScreens()
    {
        _loadFailureDialog.reset();
        _saveFailureDialog.reset();
        _loadingScreen.reset();
        _fadeScreen.reset();
    }

    void LoadingScreenController::syncFadeOverlay( float32 alpha )
    {
        UIScreen* pScreen = findFadeScreen();
        if ( alpha <= 0.0f )
        {
            _fadeScreen.reset();
            return;
        }
        if ( pScreen == nullptr )
        {
            if ( _pUISystem == nullptr || _pUISystem->isInitialized() == false )
                return;
            _fadeScreen = UIOwnedScreen( *_pUISystem, _pUISystem->openScreen( _pUISystem->getEngineDocuments()._screenFade ) );
            pScreen     = findFadeScreen();
            if ( pScreen == nullptr )
                return;
        }
        Widget* pPanel = pScreen->getTree().getRoot();
        if ( pPanel != nullptr )
            pPanel->setOpacity( MathUtil::saturate( alpha ) );
    }

    void LoadingScreenController::applyTip()
    {
        _currentTip.clear();
        if ( _settings._listTip.empty() == false )
        {
            const int32 index = _tipRandom.nextInt( 0, static_cast<int32>( _settings._listTip.size() ) - 1 );
            _currentTip       = _settings._listTip[static_cast<size_t>( index )];
        }
        _viewModel.setTip( _currentTip );
    }
} // namespace sw
