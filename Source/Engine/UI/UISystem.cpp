#include "pch.h"

#include "Engine/UI/UISystem.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/PackEngineSettingsSubsystem.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Text/FontService.h"
#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Accessibility/UIAccessibilityTree.h"
#include "Engine/UI/Animation/UIStyleTransition.h"
#include "Engine/UI/Binding/UIBindingSet.h"
#include "Engine/UI/Binding/UIViewModel.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/UILayoutDump.h"
#include "Engine/UI/Screen/PauseMenuScreen.h"
#include "Engine/UI/Screen/UIScreenSwitch.h"
#include "Engine/UI/Style/UIStylePass.h"
#include "Engine/UI/Style/UIStyleSet.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/World/WidgetComponent.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "UISystem" );

    /// 한 프레임에 새 글리프를 만드는 데 쓸 시간(마이크로초, 0=제한 없음)입니다. 글리프 하나가 2 ms 안팎이라(한글, FreeType sdf) 4 ms 면 두 개 안팎이 프레임마다 나옵니다.
    /// 예산이 차면 나머지 글리프는 다음 프레임에 그립니다(`GlyphCache::getOrCreateGlyph`, 글리프 래스터화 히치 G2).
    SW_GLOBAL_VARIABLE( int32, gv_uiGlyphRasterBudgetMicros, 4000, "프레임당 새 글리프 래스터화 시간 예산(마이크로초, 0=제한 없음) — 넘으면 글리프가 다음 프레임에 나타난다" );

    namespace
    {
        struct UISystemInternal
        {
            /** @brief 그리기 순서의 앞뒤를 비교합니다. 레이어가 먼저이고, 같은 레이어면 쌓인 순서입니다. */
            static bool isDrawnBefore( const UIScreen& lhs, const UIScreen& rhs )
            {
                if ( lhs.getDesc()._layer != rhs.getDesc()._layer )
                    return static_cast<uint8>( lhs.getDesc()._layer ) < static_cast<uint8>( rhs.getDesc()._layer );
                return false; // 같은 레이어면 뒤에 올린 것이 위이므로, 끼워 넣을 위치는 같은 레이어의 끝이다
            }

            /** @brief vtable 이 범위 안에 있는 화면이나 위젯이 하나라도 있으면 true 입니다. */
            /** @brief @p pType 이 @p moduleName 의 타입이면 true 입니다. */
            static bool isTypeOfModule( const TypeInfo* pType, string_view moduleName )
            {
                return pType != nullptr && string_view( pType->_moduleName.c_str() ) == moduleName;
            }

            /** @brief 화면 클래스나 그 트리의 위젯 가운데 @p moduleName 의 타입이 있으면 true 입니다. */
            static bool usesTypeOfModule( const UIScreen& screen, string_view moduleName )
            {
                if ( isTypeOfModule( screen.getTypeInfo(), moduleName ) )
                    return true;
                vector<Widget*> listWidget;
                screen.getTree().collectWidgetsInDocumentOrder( listWidget );
                for ( const Widget* pWidget : listWidget )
                {
                    if ( isTypeOfModule( pWidget->getTypeInfo(), moduleName ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UISystem::UISystem()
        : _listScreen{}
        , _offscreen{}
        , _documentCache{}
        , _styleSheetCache{}
        , _documentReloadedSubscription{}
        , _styleSheetReloadedSubscription{}
        , _theme{}
        , _listReopenDocument{}
        , _onUpdateBeforeScreens{}
        , _onUpdateAfterScreens{}
        , _actionGlyphs{}
        , _input{}
        , _worldPointer{}
        , _accessibilityBridge{}
        , _pFontService{ nullptr }
        , _pLocalization{ nullptr }
        , _pUserSettings{ nullptr }
        , _textLayout{}
        , _scaleSettings{}
        , _viewport{}
        , _paintState{}
        , _markers{}
        , _activeScreen{ kInvalidUIScreenHandle }
        , _accessibleFocusScreen{ kInvalidUIScreenHandle }
        , _accessibleFocusWidget{ kInvalidWidgetID }
        , _listWidgetComponent{}
        , _listTickScratch{}
        , _listSwitchScreen{}
        , _engineDocuments{}
        , _optionsMenuDocument{}
        , _pauseMenuDocument{}
        , _nextScreenHandle{ 1 }
        , _nextPushOrder{ 0 }
        , _textRevision{ 0 }
        , _converterRevision{ 0 }
        , _glyphStyle{ InputGlyphStyle::KeyboardMouse }
        , _bPauseRequested{ SW_FALSE }
        , _bPendingClose{ SW_FALSE }
        , _bTextRevisionKnown{ SW_FALSE }
        , _bGlyphStyleKnown{ SW_FALSE }
        , _bOnScreenSuppressed{ SW_FALSE }
    {
    }

    UISystem::~UISystem()
    {
        shutdown();
    }

    bool UISystem::initialize( InputManager& inputManager, FontService* pFontService, const AssetID& assetID )
    {
        _pFontService                   = pFontService;
        _actionGlyphs._pInput           = &inputManager;
        _actionGlyphs._pUIInputMap      = nullptr;
        _documentReloadedSubscription   = _documentCache.bindReloadedHandler( SW_DELEGATE_METHOD( UIAssetReloadedDelegate, &UISystem::onDocumentReloaded, this ) );
        _styleSheetReloadedSubscription = _styleSheetCache.bindReloadedHandler( SW_DELEGATE_METHOD( UIAssetReloadedDelegate, &UISystem::onStyleSheetReloaded, this ) );
        _textLayout                     = pFontService != nullptr ? make_unique<TextLayoutEngine>( *pFontService ) : nullptr;
        if ( _input.initialize( inputManager, assetID ) == false )
            return false; // 오류는 UIInputRouter::initialize 가 남겼다
        _actionGlyphs._pUIInputMap = _input.getUIInputMap();
        _input.syncInputLayers( getActiveScreen(), _pauseMenuDocument.empty() == false );
        return true;
    }

    void UISystem::shutdown()
    {
        _accessibilityBridge.reset();
        // 위젯 컴포넌트가 이 시스템보다 오래 남을 수 있으므로 등록과 마커를 잊게 한다(그 뒤 소멸자가 이 시스템을 부르지 않게).
        for ( WidgetComponent* pComponent : _listWidgetComponent )
        {
            _worldPointer.forgetComponent( *pComponent );
            pComponent->forgetUISystem();
        }
        _listWidgetComponent.clear();
        _markers.clear();
        _listSwitchScreen.clear();
        _input.getFocusManager().clearFocus();
        while ( _listScreen.empty() == false )
        {
            destroyScreenAt( static_cast<uint32>( _listScreen.size() ) - 1 );
        }
        _offscreen.clear();
        _bPendingClose = SW_FALSE;
        _listReopenDocument.clear();
        _documentReloadedSubscription.reset();
        _styleSheetReloadedSubscription.reset();
        refreshActiveScreen();
        _input.shutdown();
        _paintState.releasePaintScratch(); // 종료 누수 보고는 이 시스템의 소멸보다 먼저다 — 기동 뒤 자란 스크래치를 지금 놓는다
        _textLayout.reset();
        _pFontService = nullptr;
    }

    span<const utf8* const> UISystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { FontService::kSubsystemName, InputManager::kSubsystemName, PackEngineSettingsSubsystem::kSubsystemName,
                                                          UserSettingsManager::kSubsystemName, UIBindingConverterRegistry::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult UISystem::initializeSubsystem()
    {
        // UI 배율 규칙 — 팩 설정(`<Engine _uiScaleSettings>`)이 덮어쓰면 그것, 아니면 엔진 기본(engine/ui/uiscale.xml). 모르는 키는 기동 오류다.
        const EngineDefaultAssets& defaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        const PackEngineSettings&  packSettings  = engine::getSubsystem<PackEngineSettings>();
        UIScaleSettings            scaleSettings{};
        if ( scaleSettings.loadFromAsset( AssetID{ PackEngineSettings::resolve( packSettings._uiScaleSettings, defaultAssets._uiScaleSettings ) } ) == false )
            return SubsystemInitResult::Failed;
        setScaleSettings( scaleSettings );
        // UI 행동 맵(탐색 · 확인 · 뒤로)을 못 읽으면 메뉴를 패드로 다룰 수 없다 — 데이터 오류라 기동 실패다.
        if ( initialize( engine::getSubsystem<InputManager>(), &engine::getSubsystem<FontService>(), AssetID{ defaultAssets._uiInputMap.get() } ) == false )
            return SubsystemInitResult::Failed;
        // 문화권 · 설정 출처는 기동 때 한 번 받아 든다(언리얼 `InitializeDependency`) — 프레임마다 전역 표로 물러나 찾지 않는다.
        UserSettingsManager& userSettings = engine::getSubsystem<UserSettingsManager>();
        setLocalization( &engine::getSubsystem<LocalizationManager>() );
        setUserSettings( &userSettings );
        // UI 행동(일시정지 · 확인 · 뒤로 …)도 키 바인딩 설정의 대상이다 — 사용자 파일의 UI 키를 지금 넣고, 옵션 메뉴가 바꿀 수 있게 한다.
        if ( InputMap* pUIInputMap = _input.getUIInputMap(); pUIInputMap != nullptr )
            _uiKeyBindingRegistration = userSettings.addKeyBindingInputMap( *pUIInputMap );
        // UI 문서 · 스타일 시트 캐시를 에셋 캐시 레지스트리에 올린다 — 핫 리로드 · 종료 · 진단이 다른 에셋과 같은 길로 간다.
        AssetManager& assetManager   = engine::getSubsystem<AssetManager>();
        _documentCacheRegistration   = assetManager.registerAssetCache( &_documentCache, SW_ASSET_CACHE_RELOAD_FUNCTION( UIDocumentCache ) );
        _styleSheetCacheRegistration = assetManager.registerAssetCache( &_styleSheetCache, SW_ASSET_CACHE_RELOAD_FUNCTION( UIStyleSheetCache ) );
        // 테마 목록 — 팩 설정이 덮어쓰면 그것, 아니면 엔진 기본. 모르는 키는 기동 오류다(배율 규칙과 같다).
        UIThemeCatalog themes{};
        if ( themes.loadFromAsset( AssetID{ PackEngineSettings::resolve( packSettings._uiThemes, defaultAssets._uiThemes ) } ) == false )
            return SubsystemInitResult::Failed;
        setThemeCatalog( themes );
        // 엔진 UI 문서(행 견본 · 확인 창 · 알림 · 자막 · 디버그 HUD)는 엔진 기본 에셋 표가 기준이다.
        // 엔진 기본 메뉴(옵션 · 일시정지) — 팩 설정이 문서를 덮어쓴다. 일시정지 메뉴는 게임이 켤 때만.
        setEngineDocuments( UIEngineDocuments::makeFromDefaultAssets( defaultAssets ) );
        setOptionsMenuDocument( PackEngineSettings::resolve( packSettings._uiOptionsMenu, defaultAssets._uiOptionsMenu ) );
        if ( packSettings._bUIPauseMenu )
            setPauseMenuDocument( PackEngineSettings::resolve( packSettings._uiPauseMenu, defaultAssets._uiPauseMenu ) );
        // 모듈을 내리기 전에 그 모듈 타입의 화면을 닫는다(타입의 모듈 이름으로 고른다 — 코드 주소가 아니다).
        _moduleUnloadingSubscription = engine::getSubsystem<TypeRegistry>().subscribeModuleUnloading(
            TypeRegistry::ModuleUnloadingDelegate::create<&UISystem::onModuleUnloadingNotice>( this ) );
        return SubsystemInitResult::Succeeded;
    }

    void UISystem::shutdownSubsystem()
    {
        _moduleUnloadingSubscription.reset();
        _styleSheetCacheRegistration.reset();
        _documentCacheRegistration.reset();
        _uiKeyBindingRegistration.reset();
        shutdown();
        setLocalization( nullptr );
        setUserSettings( nullptr );
    }

    void UISystem::processInput( float32 deltaSeconds )
    {
        const ScopedMemoryTag uiMemoryTag{ MemoryTag::UI };
        if ( _input.getInputManager() == nullptr )
            return;
        _input.update( deltaSeconds );
        UIInputMode mode = _input.getInputMode();
        if ( _input.pollInputMode( mode ) )
            setInputMode( mode );
        if ( _bOnScreenSuppressed == SW_FALSE )
        {
            _input.processPointer( _listScreen, _viewport );
            processWorldPointer();
        }
        // 액션: 화면이 없으면 일시정지 메뉴를 먼저 연다. 활성 화면은 여기서 한 번 정하며, 방금 연 메뉴는 다음 프레임부터 액션을 받는다.
        UIScreen* pActive = getActiveScreen();
        if ( pActive == nullptr && _input.pollPauseAction( _pauseMenuDocument.empty() == false ) )
            (void)openScreen<PauseMenuScreen>( _pauseMenuDocument ); // 실패는 문서 생성 단계가 로그에 남긴다
        _input.processActions( pActive, deltaSeconds );
        applyPendingCloses();
        _input.updateKeyboardFocus();
    }

    void UISystem::update( float32 deltaSeconds, const UIViewport& viewport )
    {
        const ScopedMemoryTag uiMemoryTag{ MemoryTag::UI }; // 그리기 캐시, 그리기 목록, 텍스트 배치, 글리프 아틀라스가 프레임 안에서 커진다
        _viewport = viewport;
        syncThemeSetting();
        syncScreenSwitches();
        reopenClosedScreens();
        _onUpdateBeforeScreens.broadcast( deltaSeconds ); // 자막 · 툴팁(서브시스템 — 기동에서 건 순서)
        updateScreens( deltaSeconds );
        _onUpdateAfterScreens.broadcast( deltaSeconds ); // 알림
        applyPendingCloses();
        updateBindings();
        // 애니메이션: 문서 애니메이션과 트윈이 프로퍼티를 쓴다(스타일과 레이아웃 앞이라, 쓴 필드의 무효화를 이번 프레임에 처리한다). 실제 프레임 시간을 쓴다(일시정지 메뉴도 움직인다).
        {
            SW_PROFILE_SCOPE( "GT.UI.Animate" );
            for ( size_t index = 0; index < _listScreen.size(); ++index )
            {
                _listScreen[index]->_animationPlayer.update( deltaSeconds );
                (void)UIStyleTransition::update( _listScreen[index]->getTree(), deltaSeconds ); // 스타일 전환: 새로 바뀐 것은 아래 스타일 단계가 시작한다
            }
            _offscreen.updateAnimations( deltaSeconds ); // 에디터 미리보기가 튼 애니메이션(스택 밖 화면은 `Open` 을 틀지 않는다)
            applyPendingCloses();                        // 닫기 애니메이션이 끝난 화면을 지운다
        }
        refreshInputGlyphs();
        updateAccessibilityFocus();
        // 스타일: 스타일이 더티한 위젯만 계산된 스타일을 다시 정한다(레이아웃 앞이다. 여백과 글꼴이 크기를 바꾸기 때문이다).
        {
            SW_PROFILE_SCOPE( "GT.UI.Style" );
            const bool bNavigation   = _input.getInputMode() == UIInputMode::Navigation;
            uint32     restyledCount = 0;
            for ( const unique_ptr<UIScreen>& screen : _listScreen )
            {
                if ( screen->_styleSet != nullptr )
                    restyledCount += UIStylePass::update( screen->getTree(), *screen->_styleSet, bNavigation );
            }
            SW_PROFILE_COUNT( "UI.StyleWidgets", restyledCount );
        }
        // 화면 마커: 게임 틱과 트랜스폼 적용 뒤의 월드 점을 이번 뷰포트로 투영한다(레이아웃 앞이므로 같은 프레임에 배치된다).
        {
            SW_PROFILE_SCOPE( "GT.UI.Markers" );
            WidgetMarkerView view{}; // 카메라와 뷰 · 투영은 관리자마다 한 번 구한다
            for ( WidgetComponent* pComponent : _listWidgetComponent )
            {
                pComponent->updateScreenMarker( _viewport, view );
            }
            // GPU 투영 마커의 공용 카메라 — 켜진 마커가 있을 때만 패킷에 실린다(`getGPUMarkerCount`).
            if ( view._pGPUManager != nullptr )
            {
                CanvasMarkerView gpuView{};
                WidgetComponent::makeGPUMarkerView( view, _viewport._size, gpuView );
                _markers.setGPUMarkerView( gpuView, view._pGPUManager );
            }
        }
        // 레이아웃: 화면 트리마다 더티한 루트만 다시 측정한다. 화면마다 뷰포트 전체가 루트 사각형이다.
        {
            SW_PROFILE_SCOPE( "GT.UI.Layout" );
            const UILayoutContext context       = makeLayoutContext();
            uint32                measuredCount = 0;
            for ( const unique_ptr<UIScreen>& screen : _listScreen )
            {
                measuredCount += UILayoutPass::update( screen->getTree(), context );
            }
            SW_PROFILE_COUNT( "UI.LayoutWidgets", measuredCount );
        }
        // 그리기: 더티한 위젯만 다시 칠하고 화면마다 캐시를 이어 붙인다(스타일 순회(5-2) 전까지는 그리기가 kStyle 도 비운다).
        paintScreens();
        // 월드 공간 위젯: 컴포넌트마다 자기 트리를 렌더 텍스처 크기(배율 1)로 배치하고 칠한다.
        if ( _listWidgetComponent.empty() == false )
        {
            const UILayoutContext layout = makeLayoutContext();
            const UIPaintContext  paint  = makePaintContext();
            for ( WidgetComponent* pComponent : _listWidgetComponent )
            {
                pComponent->updateWorldCanvas( layout, paint );
            }
        }
        if ( _offscreen.isEmpty() == false )
            _offscreen.update( makeLayoutContext(), makePaintContext() );
    }

    UIScreenHandle UISystem::pushScreen( unique_ptr<UIScreen> screen )
    {
        if ( screen == nullptr )
            return kInvalidUIScreenHandle;
        SW_ASSERT( screen->_pUISystem == nullptr );
        screen->_pUISystem = this;
        rebuildStyleSet( *screen );
        screen->_handle    = _nextScreenHandle++;
        screen->_pushOrder = _nextPushOrder++;
        screen->_bClosing  = SW_FALSE;
        // 숨김 요청이 걸린 레이어에 열리는 화면도 숨긴다(포토 모드 중에 뜬 알림이 그려지지 않게).
        screen->_bHiddenByLayer = UILayerUtil::contains( _paintState.getHiddenLayers(), screen->_desc._layer ) ? SW_TRUE : SW_FALSE;
        if ( screen->_animationPlayer.findAnimation( hashed_string( UIAnimation::kOpenName ) ) != nullptr )
            (void)screen->_animationPlayer.play( hashed_string( UIAnimation::kOpenName ) );
        // 같은 레이어의 끝(그 레이어에서 맨 위)에 끼운다.
        uint32 at = static_cast<uint32>( _listScreen.size() );
        while ( at > 0 && UISystemInternal::isDrawnBefore( *screen, *_listScreen[at - 1] ) )
        {
            --at;
        }
        const UIScreenHandle handle = screen->_handle;
        _listScreen.insert( _listScreen.begin() + at, std::move( screen ) );
        refreshActiveScreen();
        return handle;
    }

    void UISystem::closeScreen( UIScreenHandle handle )
    {
        UIScreen* pScreen = findScreen( handle );
        if ( pScreen == nullptr || pScreen->_bClosing == SW_TRUE )
            return;
        pScreen->_bClosing = SW_TRUE;
        _bPendingClose     = SW_TRUE;
        // 닫기 애니메이션은 끝날 때까지 지우지 않는다(`applyPendingCloses`). 여는 애니메이션은 멈춘다.
        UIAnimationPlayer& player = pScreen->_animationPlayer;
        if ( player.findAnimation( hashed_string( UIAnimation::kCloseName ) ) != nullptr )
        {
            player.stop( hashed_string( UIAnimation::kOpenName ) );
            (void)player.play( hashed_string( UIAnimation::kCloseName ) );
        }
    }

    bool UISystem::tween( WidgetID widget, string_view propertyPath, string_view endValue, float32 duration, BlendCurve curve )
    {
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->getTree().findWidgetByID( widget ) != nullptr )
                return screen->_animationPlayer.tween( widget, propertyPath, endValue, duration, curve );
        }
        SW_LOG_WARNING( "[UI] Tween of '%#': widget %# is not in any screen", string( propertyPath ).c_str(), widget );
        return false;
    }

    unique_ptr<Widget> UISystem::instantiateDocument( string_view documentPath, UIScreenDesc& outDesc, vector<UIBindingDesc>& outListBinding,
                                                      vector<string>& outListStyleSheet )
    {
        string                                  error;
        unique_ptr<Widget>                      root;
        const shared_ptr<const UIDocumentAsset> document = _documentCache.findOrLoad( documentPath, error );
        if ( document == nullptr )
        {
            SW_LOG_ERROR( "[UI] Screen document is not loaded: %#", error.c_str() );
            return root;
        }
        root = UIDocumentLoader::instantiate( *document, _documentCache, outListBinding, error );
        if ( root == nullptr )
        {
            SW_LOG_ERROR( "[UI] Screen document cannot be built: %#", error.c_str() );
            return root;
        }
        outDesc = document->_screenDesc;
        // 스타일 시트: 문서의 것 다음에 조각의 것(너비 우선, 이미 본 문서와 시트는 건너뛴다). 조각은 생성 단계에서 이미 캐시에 올라가 있다.
        vector<string> listDocument{ document->_path };
        for ( size_t index = 0; index < listDocument.size(); ++index )
        {
            string                                  fragmentError;
            const shared_ptr<const UIDocumentAsset> current = _documentCache.findOrLoad( listDocument[index], fragmentError );
            if ( current == nullptr )
                continue;
            for ( const string& sheet : current->_listStyleSheet )
            {
                if ( VectorUtil::contains( outListStyleSheet, sheet ) == false )
                    outListStyleSheet.push_back( sheet );
            }
            for ( const string& fragment : current->_listFragment )
            {
                if ( VectorUtil::contains( listDocument, fragment ) == false )
                    listDocument.push_back( fragment );
            }
        }
        return root;
    }

    UIScreenHandle UISystem::pushDocumentScreen( unique_ptr<UIScreen> screen, string_view documentPath, vector<UIBindingDesc> listBinding, vector<string> listStyleSheet )
    {
        screen->_documentPath   = FileUtil::normalizePath( documentPath );
        screen->_listBinding    = std::move( listBinding );
        screen->_listStyleSheet = std::move( listStyleSheet );
        applyDocumentAnimations( *screen );
        return pushScreen( std::move( screen ) );
    }

    void UISystem::applyDocumentAnimations( UIScreen& screen )
    {
        // 애니메이션은 문서의 것을 쓴다(방금 만들었으므로 캐시에 이미 있다).
        screen._animationPlayer.stopAll();
        string                                  error;
        const shared_ptr<const UIDocumentAsset> document = _documentCache.findOrLoad( screen._documentPath, error );
        screen._animationPlayer.setAnimations( document != nullptr ? document->_listAnimation : vector<UIAnimation>{} );
    }

    void UISystem::onDocumentReloaded( string_view documentPath )
    {
        uint32 rebuiltCount = 0;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->_documentPath.empty() || isDocumentUsing( screen->_documentPath, documentPath ) == false )
                continue;
            if ( rebuildScreenFromDocument( *screen ) )
                ++rebuiltCount;
        }
        for ( UIOffscreenScreen& offscreen : _offscreen.getScreens() )
        {
            UIScreen& screen = *offscreen._screen;
            if ( isDocumentUsing( screen._documentPath, documentPath ) && rebuildScreenFromDocument( screen ) )
            {
                rebuildStyleSet( screen, offscreen._theme );
                ++rebuiltCount;
            }
        }
        if ( rebuiltCount > 0 )
            SW_LOG_INFO( "[UI] Reloaded %# screen(s) for %#", rebuiltCount, documentPath );
    }

    void UISystem::onStyleSheetReloaded( string_view sheetPath )
    {
        // 스타일 세트는 테마 시트와 문서 시트를 모두 가진다. 그 경로의 시트를 가진 화면만 새 시트로 다시 적용한다.
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->_styleSet == nullptr )
                continue;
            bool bUses = false;
            for ( const shared_ptr<const UIStyleSheetAsset>& sheet : screen->_styleSet->getSheets() )
            {
                bUses = bUses || ( sheet != nullptr && sheet->_path == sheetPath );
            }
            if ( bUses )
                rebuildStyleSet( *screen );
        }
        for ( UIOffscreenScreen& offscreen : _offscreen.getScreens() )
        {
            if ( offscreen._screen->_styleSet == nullptr )
                continue;
            bool bUses = false;
            for ( const shared_ptr<const UIStyleSheetAsset>& sheet : offscreen._screen->_styleSet->getSheets() )
            {
                bUses = bUses || ( sheet != nullptr && sheet->_path == sheetPath );
            }
            if ( bUses )
                rebuildStyleSet( *offscreen._screen, offscreen._theme );
        }
    }

    UIScreenHandle UISystem::openOffscreenScreen( string_view documentPath, string_view targetPath )
    {
        UIScreenDesc          desc{};
        vector<UIBindingDesc> listBinding{};
        vector<string>        listStyleSheet{};
        unique_ptr<Widget>    root = instantiateDocument( documentPath, desc, listBinding, listStyleSheet );
        if ( root == nullptr )
            return kInvalidUIScreenHandle;
        UIScreen& screen       = _offscreen.add( sw::make_unique<UIScreen>( desc, std::move( root ) ), hashed_string( targetPath ) );
        screen._documentPath   = FileUtil::normalizePath( documentPath );
        screen._listBinding    = std::move( listBinding );
        screen._listStyleSheet = std::move( listStyleSheet );
        screen._handle         = _nextScreenHandle++;
        applyDocumentAnimations( screen ); // 등록만 한다 — 스택 밖 화면은 `Open` 을 틀지 않아 문서 값 그대로다(미리보기가 고른 것만 튼다)
        rebuildStyleSet( screen );
        return screen._handle;
    }

    void UISystem::setOffscreenView( UIScreenHandle handle, const UIViewport& viewport, float32 textScale, const hashed_string& theme )
    {
        // 테마가 바뀌면 그 화면의 스타일 시트만 다시 적용한다(게임 UI 의 테마는 그대로다).
        if ( _offscreen.setView( handle, viewport, textScale, theme ) )
            rebuildStyleSet( *_offscreen.findScreen( handle ), theme );
    }

    bool UISystem::isDocumentUsing( const string& documentPath, string_view usedPath )
    {
        vector<string> listDocument{ documentPath };
        for ( size_t index = 0; index < listDocument.size(); ++index )
        {
            if ( listDocument[index] == usedPath )
                return true;
            string                                  error;
            const shared_ptr<const UIDocumentAsset> document = _documentCache.findOrLoad( listDocument[index], error );
            if ( document == nullptr )
                continue;
            for ( const string& fragment : document->_listFragment )
            {
                if ( VectorUtil::contains( listDocument, fragment ) == false )
                    listDocument.push_back( fragment );
            }
        }
        return false;
    }

    bool UISystem::rebuildScreenFromDocument( UIScreen& screen )
    {
        UIScreenDesc          desc{};
        vector<UIBindingDesc> listBinding{};
        vector<string>        listStyleSheet{};
        unique_ptr<Widget>    root = instantiateDocument( screen._documentPath, desc, listBinding, listStyleSheet );
        if ( root == nullptr )
            return false; // 기존 트리를 유지한다. 오류는 instantiateDocument 가 남겼다

        // 이름으로 이어 갈 것: 포커스 위젯, 덮였을 때의 포커스, 스크롤 오프셋.
        UIFocusManager&       focus         = _input.getFocusManager();
        WidgetTree&           tree          = screen.getTree();
        const bool            bHadFocus     = focus.getFocusedTree() == &tree;
        const Widget*         pFocused      = bHadFocus ? tree.findWidgetByID( focus.getFocusedWidget() ) : nullptr;
        const Widget*         pLastFocused  = tree.findWidgetByID( screen._lastFocused );
        const hashed_string   focusedName   = pFocused != nullptr ? pFocused->getName() : hashed_string{};
        const hashed_string   lastFocusName = pLastFocused != nullptr ? pLastFocused->getName() : hashed_string{};
        vector<Widget*>       listOldWidget;
        vector<hashed_string> listScrollName;
        vector<float2>        listScrollOffset;
        tree.collectWidgetsInDocumentOrder( listOldWidget );
        for ( const Widget* pWidget : listOldWidget )
        {
            const ScrollPanel* pScroll = castTo<const ScrollPanel>( pWidget );
            if ( pScroll == nullptr || pWidget->getName().empty() )
                continue;
            listScrollName.push_back( pWidget->getName() );
            listScrollOffset.push_back( pScroll->getScrollOffset() );
        }

        tree.setRoot( std::move( root ) );
        screen._listBinding    = std::move( listBinding );
        screen._listStyleSheet = std::move( listStyleSheet );
        screen._lastFocused    = kInvalidWidgetID;
        // 뷰모델은 화면이 그대로 가진다. 식이 새 위젯 번호를 가리키므로 다음 바인딩 단계가 다시 연결하고 모든 필드를 쓴다.
        screen._bindingSet->markRebind();
        screen.onTreeRebuilt();
        applyDocumentAnimations( screen ); // 고친 문서의 애니메이션을 등록한다. 재생 중이던 것(닫기 포함)은 멈춘다
        rebuildStyleSet( screen );
        // 스크롤 오프셋은 내용 크기 안으로 제한되므로, 새 트리를 지금 한 번 측정해 배치한 뒤에 복원한다.
        if ( screen._styleSet != nullptr )
            (void)UIStylePass::update( tree, *screen._styleSet, _input.getInputMode() == UIInputMode::Navigation );
        (void)UILayoutPass::update( tree, makeLayoutContext() );

        for ( size_t index = 0; index < listScrollName.size(); ++index )
        {
            ScrollPanel* pScroll = tree.findWidget<ScrollPanel>( listScrollName[index] );
            if ( pScroll != nullptr )
                pScroll->setScrollOffset( listScrollOffset[index] );
        }
        const Widget* pNewLast = lastFocusName.empty() ? nullptr : tree.findWidgetByName( lastFocusName );
        if ( pNewLast != nullptr )
            screen._lastFocused = pNewLast->getID();
        if ( bHadFocus )
        {
            const Widget* pNewFocus = focusedName.empty() ? nullptr : tree.findWidgetByName( focusedName );
            if ( pNewFocus == nullptr || focus.setFocus( tree, pNewFocus->getID() ) == false )
                _input.restoreFocus( screen );
        }
        return true;
    }

    void UISystem::reopenClosedScreens()
    {
        if ( _listReopenDocument.empty() )
            return;
        vector<string> listDocument;
        listDocument.swap( _listReopenDocument );
        for ( const string& documentPath : listDocument )
        {
            if ( openScreen( documentPath ) == kInvalidUIScreenHandle )
                SW_LOG_ERROR( "[UI] Screen document could not be reopened after module reload: %#", documentPath );
        }
    }

    void UISystem::setThemeCatalog( const UIThemeCatalog& catalog )
    {
        _theme.setCatalog( catalog, gv_uiTheme );
        // 사용자 설정(gv_uiTheme)의 테마가 목록에 있으면 그것을, 없으면 목록의 기본 테마를 쓴다. 테마를 두지 않은 게임 목록에서는 설정 값이 조용히 기본 테마가 된다.
        const hashed_string settingTheme = _theme.findSettingTheme();
        if ( settingTheme.empty() == false )
        {
            (void)setTheme( settingTheme );
            return;
        }
        const hashed_string& defaultTheme = _theme.getCatalog()._defaultTheme;
        if ( defaultTheme.empty() == false && setTheme( defaultTheme ) == false )
            SW_LOG_ERROR( "[UI] Default UI theme '%#' is not in the theme catalog", defaultTheme.c_str() );
    }

    bool UISystem::setTheme( const hashed_string& name )
    {
        if ( _theme.selectTheme( name ) == false )
            return false; // 경고는 selectTheme 이 남겼다
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            rebuildStyleSet( *screen );
        }
        for ( UIOffscreenScreen& offscreen : _offscreen.getScreens() )
        {
            rebuildStyleSet( *offscreen._screen, offscreen._theme );
        }
        return true;
    }

    void UISystem::rebuildStyleSet( UIScreen& screen, const hashed_string& themeName )
    {
        _theme.rebuildStyleSet( screen, _styleSheetCache, themeName );
    }

    UIScreen* UISystem::findScreen( UIScreenHandle handle ) const
    {
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->_handle == handle )
                return screen.get();
        }
        return nullptr;
    }

    string UISystem::makeLayoutDump() const
    {
        string text;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            text += "## " + ( screen->_documentPath.empty() ? string( "(code)" ) : screen->_documentPath ) + "\n";
            text += UILayoutDump::makeDump( screen->getTree(), _viewport._uiScale );
        }
        return text;
    }

    UIScreen* UISystem::getActiveScreen() const
    {
        return findScreen( _activeScreen );
    }

    bool UISystem::isGameInputBlocked() const
    {
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->isClosing() == false && screen->blocksLowerInput() )
                return true;
        }
        return false;
    }

    bool UISystem::isLoadingScreenShown() const
    {
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->isClosing() == false && screen->getDesc()._layer == UILayer::Loading )
                return true;
        }
        return false;
    }

    bool UISystem::wantsCursor() const
    {
        const UIScreen* pActive = getActiveScreen();
        return pActive != nullptr && pActive->getDesc()._bShowCursor;
    }

    void UISystem::setInputMode( UIInputMode mode )
    {
        if ( _input.setInputMode( mode ) == false )
            return;
        UIScreen*  pActive                 = getActiveScreen();
        const bool bRestoreNavigationFocus = mode == UIInputMode::Navigation && pActive != nullptr && _input.getFocusManager().getFocusedTree() != &pActive->getTree();
        if ( bRestoreNavigationFocus )
            _input.restoreFocus( *pActive );
    }

    uint32 UISystem::closeScreensOfModule( string_view moduleName )
    {
        uint32 closedCount = 0;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            if ( UISystemInternal::usesTypeOfModule( *_listScreen[index - 1], moduleName ) == false )
                continue;
            const UIScreen& screen = *_listScreen[index - 1];
            SW_LOG_WARNING_ID( "UI.ScreenClosedForModuleReload", "[UI] Screen %# closed for module reload - it holds types of module %#", screen._handle, moduleName );
            // 문서로 연 기본 화면은 문서 경로로 다시 연다(새 이미지의 위젯 타입으로). 화면 클래스가 모듈의 것이면 다시 열 수 없다.
            const bool bScreenClassInModule = UISystemInternal::isTypeOfModule( screen.getTypeInfo(), moduleName );
            if ( screen._documentPath.empty() == false && bScreenClassInModule == false )
                _listReopenDocument.push_back( screen._documentPath );
            destroyScreenAt( index - 1 );
            ++closedCount;
        }
        if ( closedCount > 0 )
            refreshActiveScreen();
        return closedCount;
    }

    void UISystem::updateBindings()
    {
        SW_PROFILE_SCOPE( "GT.UI.Bind" );
        UIBindingContext context{};
        context._pLocalization                       = findLocalization();
        const UIBindingConverterRegistry& converters = engine::getSubsystem<UIBindingConverterRegistry>();
        context._pConverters                         = &converters;
        context._pSettings                           = findUserSettings();
        // 텍스트 버전: 언어를 바꾸거나 테이블을 다시 읽으면 오른다. 정수 하나 비교라 매 프레임 확인한다(언리얼 FTextLocalizationManager 의 TextRevision 에 해당).
        const uint32 textRevision = context._pLocalization != nullptr ? context._pLocalization->getTextRevision() : 0;
        if ( _bTextRevisionKnown == SW_TRUE && textRevision != _textRevision )
        {
            if ( _pFontService != nullptr && _pFontService->isInitialized() )
                _pFontService->invalidateFaceChains(); // 문화권의 대체 폰트 패밀리가 바뀐다
            vector<Widget*> listWidget;
            for ( const unique_ptr<UIScreen>& screen : _listScreen )
            {
                listWidget.clear();
                screen->getTree().collectWidgetsInDocumentOrder( listWidget );
                for ( Widget* pWidget : listWidget )
                {
                    pWidget->onTextRevisionChanged();
                }
            }
            context._bTextRevisionChanged = true;
        }
        _textRevision       = textRevision;
        _bTextRevisionKnown = SW_TRUE;
        // 바인딩은 변환기를 값(함수 포인터)으로 복사한다 — 표가 바뀌면(등록 · 구독 해제) 모든 화면을 다시 묶는다.
        if ( converters.getRevision() != _converterRevision )
        {
            _converterRevision = converters.getRevision();
            for ( const unique_ptr<UIScreen>& screen : _listScreen )
            {
                screen->getBindingSet().markRebind();
            }
        }
        uint32 polledCount = 0;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            UIBindingSet& bindingSet = screen->getBindingSet();
            bindingSet.update( screen->getBindings(), context );
            polledCount += bindingSet.getPolledCount();
        }
        SW_PROFILE_COUNT( "UI.PollBindings", polledCount );
    }

    void UISystem::setUserSettings( UserSettingsManager* pSettings )
    {
        _pUserSettings = pSettings;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            screen->getBindingSet().markRebind();
        }
    }

    const LocalizationManager* UISystem::findLocalization() const
    {
        return _pLocalization;
    }

    void UISystem::refreshInputGlyphs()
    {
        const InputManager* pInput = _input.getInputManager();
        if ( pInput == nullptr )
            return;
        const InputGlyphStyle style = pInput->getActiveGlyphStyle();
        if ( _bGlyphStyleKnown == SW_TRUE && style != _glyphStyle )
        {
            vector<Widget*> listWidget;
            for ( const unique_ptr<UIScreen>& screen : _listScreen )
            {
                listWidget.clear();
                screen->getTree().collectWidgetsInDocumentOrder( listWidget );
                for ( Widget* pWidget : listWidget )
                {
                    pWidget->onInputGlyphsChanged();
                }
            }
        }
        _glyphStyle       = style;
        _bGlyphStyleKnown = SW_TRUE;
    }

    void UISystem::applyPendingCloses()
    {
        if ( _bPendingClose == SW_FALSE )
            return;
        _bPendingClose = SW_FALSE;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UIScreen& screen = *_listScreen[index - 1];
            if ( screen._bClosing == SW_FALSE )
                continue;
            // 닫기 애니메이션이 도는 화면은 끝날 때까지 남긴다(다음에 다시 본다).
            if ( screen._animationPlayer.isPlaying( hashed_string( UIAnimation::kCloseName ) ) )
            {
                _bPendingClose = SW_TRUE;
                continue;
            }
            destroyScreenAt( index - 1 );
        }
        refreshActiveScreen();
    }

    void UISystem::updateScreens( float32 deltaSeconds )
    {
        // 틱이 화면을 올리고 닫을 수 있으므로(확인 창, 키 바인딩 창) 번호로 돌며 매번 다시 찾는다.
        _listTickScratch.clear();
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->_bClosing == SW_FALSE )
                _listTickScratch.push_back( screen->_handle );
        }
        for ( const UIScreenHandle handle : _listTickScratch )
        {
            UIScreen* pScreen = findScreen( handle );
            if ( pScreen != nullptr && pScreen->isClosing() == false )
                pScreen->onTick( deltaSeconds );
        }
    }

    void UISystem::destroyScreenAt( uint32 index )
    {
        unique_ptr<UIScreen> screen = std::move( _listScreen[index] );
        _listScreen.erase( _listScreen.begin() + index );
        _input.forgetScreen( *screen ); // 포인터와 포커스가 트리를 놓는다(트리가 지워질 때 포커스 매니저에게 알릴 일이 없다)
        screen->_pUISystem = nullptr;
    }

    void UISystem::refreshActiveScreen()
    {
        // 활성 화면은 맨 위에서 포커스를 받는 화면이다. 입력을 막는 화면(모달, 로딩)을 지나 내려가지 않는다.
        UIScreen* pNewActive = nullptr;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UIScreen* pScreen = _listScreen[index - 1].get();
            if ( pScreen->isClosing() )
                continue;
            if ( pScreen->takesFocus() )
            {
                pNewActive = pScreen;
                break;
            }
            if ( pScreen->blocksLowerInput() )
                break;
        }

        UIScreen* pOldActive = findScreen( _activeScreen );
        if ( pNewActive != pOldActive )
        {
            UIFocusManager& focus = _input.getFocusManager();
            if ( pOldActive != nullptr && focus.getFocusedTree() == &pOldActive->getTree() )
            {
                pOldActive->_lastFocused = focus.getFocusedWidget();
                focus.clearFocus();
            }
            _activeScreen = pNewActive != nullptr ? pNewActive->_handle : kInvalidUIScreenHandle;
            if ( pNewActive != nullptr )
                _input.restoreFocus( *pNewActive );
        }

        // UI 액션은 활성 화면이 있을 때만 받는다. HUD 만 있으면 패드 A 는 게임의 것이다(UI 맵이 없으면 아무것도 하지 않는다).
        _input.syncInputLayers( pNewActive, _pauseMenuDocument.empty() == false );

        // 게임 정지: 정지 화면이 하나라도 있으면 요청을 걸어 둔다.
        bool bPause = false;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            bPause = bPause || ( screen->isClosing() == false && screen->getDesc()._bPausesGame );
        }
        if ( bPause && _bPauseRequested == SW_FALSE )
        {
            engine::getSubsystem<GameTimeScale>().addPauseRequest();
            _bPauseRequested = SW_TRUE;
        }
        else if ( bPause == false && _bPauseRequested == SW_TRUE )
        {
            engine::getSubsystem<GameTimeScale>().removePauseRequest();
            _bPauseRequested = SW_FALSE;
        }
    }

    UIPaintContext UISystem::makePaintContext() const
    {
        UIPaintContext context{};
        context._pScratch      = _paintState.getPaintScratch();
        context._pTextLayout   = _textLayout.get();
        context._uiScale       = _viewport._uiScale > 0.0f ? _viewport._uiScale : 1.0f;
        context._textScale     = gv_uiTextScale > 0.0f ? static_cast<float32>( gv_uiTextScale ) : 1.0f;
        context._pLocalization = findLocalization();
        context._textRevision  = context._pLocalization != nullptr ? context._pLocalization->getTextRevision() : 0;
        context._pActionGlyphs = &_actionGlyphs;
        if ( _pFontService != nullptr && _pFontService->isInitialized() )
        {
            context._pGlyphCache = &_pFontService->getGlyphCache();
            context._frameIndex  = context._pGlyphCache->getFrameIndex(); // 글리프 캐시의 시계다(프로파일러 프레임 수는 꺼지면 늘 0 이라 예산이 되살아나지 않는다)
            context._pGlyphCache->setRasterBudgetNanoseconds( static_cast<int64>( gv_uiGlyphRasterBudgetMicros ) * constant::kNanosecondsPerMicrosecond );
            context._atlasGeneration = context._pGlyphCache->getAtlas().getGeneration();
        }
        return context;
    }

    WidgetID UISystem::addScreenMarker( unique_ptr<Widget> widget )
    {
        if ( widget == nullptr )
            return kInvalidWidgetID;
        UIScreen* pMarkers = findScreen( _markers.getScreen() );
        if ( pMarkers == nullptr || pMarkers->isClosing() )
        {
            unique_ptr<CanvasPanel> root = make_unique<CanvasPanel>();
            root->setVisibility( WidgetVisibility::SelfHitTestInvisible ); // 빈 곳 클릭은 아래(게임)로 넘긴다
            UIScreenDesc desc{};
            desc._layer       = UILayer::HUD;
            desc._bTakesFocus = false;
            desc._bShowCursor = false;
            _markers.setScreen( pushScreen( sw::make_unique<UIScreen>( desc, std::move( root ) ) ) );
            pMarkers = findScreen( _markers.getScreen() );
        }
        PanelWidget* pRoot = castTo<PanelWidget>( pMarkers->getTree().getRoot() );
        if ( pRoot == nullptr )
            return kInvalidWidgetID;
        // 앵커 번호를 준다(뗀 마커의 번호부터). 첫 위치는 붙인 쪽이 정한다 — 그 전에는 위젯이 접혀 있다.
        widget->setPaintAnchor( _markers.allocateAnchor() );
        return pRoot->addChild( std::move( widget ) )->getID();
    }

    void UISystem::removeScreenMarker( WidgetID widget )
    {
        UIScreen*    pMarkers = findScreen( _markers.getScreen() );
        Widget*      pWidget  = pMarkers != nullptr ? pMarkers->getTree().findWidgetByID( widget ) : nullptr;
        PanelWidget* pRoot    = pWidget != nullptr ? pWidget->getParent() : nullptr;
        if ( pRoot == nullptr )
            return;
        _markers.releaseAnchor( pWidget->getPaintAnchor() );
        (void)pRoot->removeChild( pWidget ); // 부모에서 찾은 위젯이므로 항상 제거된다
        if ( pRoot->getChildCount() == 0 )
        {
            closeScreen( _markers.getScreen() );
            _markers.forgetScreen();
        }
    }

    void UISystem::setScreenMarkerAnchor( uint32 anchor, const float2& position, float32 scale )
    {
        _markers.setAnchor( anchor, position, scale );
    }

    void UISystem::setGPUScreenMarker( uint32 anchor, const CanvasMarkerRecord& record )
    {
        _markers.setGPUMarker( anchor, record );
    }

    void UISystem::clearGPUScreenMarker( uint32 anchor )
    {
        _markers.clearGPUMarker( anchor );
    }

    Widget* UISystem::findScreenMarker( WidgetID widget ) const
    {
        const UIScreen* pMarkers = findScreen( _markers.getScreen() );
        return pMarkers != nullptr ? pMarkers->getTree().findWidgetByID( widget ) : nullptr;
    }

    void UISystem::registerWidgetComponent( WidgetComponent& component )
    {
        for ( const WidgetComponent* pExisting : _listWidgetComponent )
        {
            if ( pExisting == &component )
                return;
        }
        _listWidgetComponent.push_back( &component );
    }

    void UISystem::unregisterWidgetComponent( WidgetComponent& component )
    {
        for ( size_t index = 0; index < _listWidgetComponent.size(); ++index )
        {
            if ( _listWidgetComponent[index] == &component )
            {
                _worldPointer.forgetComponent( component );
                _listWidgetComponent.erase( _listWidgetComponent.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    void UISystem::setAccessibilityBridge( unique_ptr<IUIAccessibilityBridge> bridge )
    {
        _accessibilityBridge   = std::move( bridge );
        _accessibleFocusScreen = kInvalidUIScreenHandle;
        _accessibleFocusWidget = kInvalidWidgetID;
    }

    void UISystem::announce( string_view text, UIAnnouncePriority priority )
    {
        const bool bListening = _accessibilityBridge != nullptr && _accessibilityBridge->isClientActive();
        if ( bListening && text.empty() == false )
            _accessibilityBridge->announce( text, priority );
    }

    void UISystem::appendScreenAccessibility( const UIScreen& screen, UIAccessibilitySnapshot& inoutSnapshot ) const
    {
        // 포커스는 한 화면에만 있다. 노드 번호는 스냅샷 전체의 번호라 `appendTree` 가 정한 `_focusedNode` 가 그대로 쓰인다.
        const WidgetTree* pFocusTree    = _input.getFocusManager().getFocusedTree();
        const WidgetID    focusedWidget = pFocusTree == &screen.getTree() ? _input.getFocusManager().getFocusedWidget() : kInvalidWidgetID;
        UIAccessibilityTree::appendTree( screen.getTree(), screen.getHandle(), focusedWidget, inoutSnapshot );
    }

    void UISystem::populateAccessibilitySnapshot( UIAccessibilitySnapshot& outSnapshot ) const
    {
        outSnapshot.clear();
        // 입력을 받는 화면: 위에서 아래로 읽는다. 입력을 막는 화면(모달) 아래는 읽지 않는다.
        for ( size_t index = _listScreen.size(); index > 0; --index )
        {
            const UIScreen& screen = *_listScreen[index - 1];
            if ( screen.isClosing() || screen.receivesPointer() == false )
                continue;
            appendScreenAccessibility( screen, outSnapshot );
            if ( screen.blocksLowerInput() )
                break;
        }
        // 오버레이(알림, 자막): 입력을 받지 않지만 읽을 글이다. 맨 뒤에 둔다.
        for ( size_t index = _listScreen.size(); index > 0; --index )
        {
            const UIScreen& screen = *_listScreen[index - 1];
            if ( screen.isClosing() == false && screen.receivesPointer() == false )
                appendScreenAccessibility( screen, outSnapshot );
        }
    }

    void UISystem::updateAccessibilityFocus()
    {
        if ( _accessibilityBridge != nullptr )
            _accessibilityBridge->onUIUpdated();
        const bool bListening = _accessibilityBridge != nullptr && _accessibilityBridge->isClientActive();
        if ( bListening == false )
        {
            // 붙지 않은 동안 바뀐 포커스는 다시 붙을 때 새로 알린다.
            _accessibleFocusScreen = kInvalidUIScreenHandle;
            _accessibleFocusWidget = kInvalidWidgetID;
            return;
        }
        const WidgetTree*    pTree   = _input.getFocusManager().getFocusedTree();
        const UIScreen*      pScreen = pTree != nullptr ? pTree->getScreen() : nullptr;
        const UIScreenHandle screen  = pScreen != nullptr ? pScreen->getHandle() : kInvalidUIScreenHandle;
        const WidgetID       widget  = pScreen != nullptr ? _input.getFocusManager().getFocusedWidget() : kInvalidWidgetID;
        if ( screen == _accessibleFocusScreen && widget == _accessibleFocusWidget )
            return;
        _accessibleFocusScreen = screen;
        _accessibleFocusWidget = widget;
        _accessibilityBridge->notifyFocusChanged( screen, widget );
    }

    bool UISystem::routeWorldPointer( const WorldWidgetRay& ray, const UIPointerEvent& event )
    {
        return _worldPointer.route( _listWidgetComponent, ray, event );
    }

    void UISystem::processWorldPointer()
    {
        // 입력을 켠 World 위젯이 하나도 없으면 광선을 만들 필요가 없다. 호버가 남아 있으면 푼다.
        const WidgetComponent* pAnchorComponent = nullptr;
        for ( const WidgetComponent* pComponent : _listWidgetComponent )
        {
            if ( pComponent->acceptsWorldInput() )
            {
                pAnchorComponent = pComponent;
                break;
            }
        }
        const bool bLocked     = _input.getInputManager()->isMouseLockActive();
        const bool bNoViewport = _viewport._physicalSize._y <= 0.0f;
        const bool bCannotAim  = bLocked || bNoViewport;
        if ( pAnchorComponent == nullptr || bCannotAim )
        {
            _worldPointer.releaseHover();
            return;
        }
        // 카메라는 입력을 켠 첫 위젯의 씬 것을 쓴다(한 UI 시스템은 한 게임 씬의 UI 다).
        const GameObject*        pOwner   = pAnchorComponent->getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const CameraComponent*   pCamera  = pManager != nullptr ? pManager->getCameraRegistry().selectCamera( CameraRole::Game ) : nullptr;
        if ( pCamera == nullptr )
        {
            _worldPointer.releaseHover();
            return;
        }
        const float4x4 inverseViewProjection = pCamera->getViewProjectionMatrix( _viewport._physicalSize._x / _viewport._physicalSize._y ).invert();
        for ( const UIRoutedPointerEvent& routed : _input.getFramePointerEvents() )
        {
            if ( routed._bOverScreenWidget == SW_TRUE )
            {
                _worldPointer.releaseHover(); // 화면 위젯이 World 위젯을 가린다
                continue;
            }
            WorldWidgetRay ray{};
            if ( WorldWidgetPick::makeRay( inverseViewProjection, routed._event._position, _viewport._size, ray ) == false )
                continue;
            const bool bHandled = _worldPointer.route( _listWidgetComponent, ray, routed._event );
            // 위젯이 처리한 누름 · 뗌은 게임이 받지 않게 소비한다(화면 위젯과 같은 규칙).
            const bool bButtonEvent = routed._event._kind == UIPointerEventKind::Down || routed._event._kind == UIPointerEventKind::Up;
            if ( bHandled && bButtonEvent )
                _input.getConsumption().consumeMouseButton( routed._event );
        }
    }

    void UISystem::collectWorldCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const
    {
        for ( const WidgetComponent* pComponent : _listWidgetComponent )
        {
            pComponent->appendWorldCanvas( inoutListTarget );
        }
        _offscreen.appendCanvases( inoutListTarget );
    }

    void UISystem::syncThemeSetting()
    {
        hashed_string theme;
        // 실행 중에 바꾼 값: 모르는 이름이면 setTheme 이 경고하고 지금 테마를 유지한다(게임 테마 목록에 그 이름이 없는 경우).
        if ( _theme.pollSetting( gv_uiTheme, theme ) )
            (void)setTheme( theme );
    }

    void UISystem::syncScreenSwitches()
    {
        const uint32 switchCount = UIScreenSwitches::getCount();
        for ( uint32 index = 0; index < switchCount; ++index )
        {
            const UIScreenSwitchRegistration* pRegistration = UIScreenSwitches::getAt( index );
            const string_view                 name{ pRegistration->_pName };
            size_t                            stateIndex = 0;
            while ( stateIndex < _listSwitchScreen.size() && string_view{ _listSwitchScreen[stateIndex]._name } != name )
            {
                ++stateIndex;
            }
            if ( stateIndex == _listSwitchScreen.size() )
                _listSwitchScreen.push_back( SwitchScreen{ string( name ), kInvalidUIScreenHandle } );
            // 스위치 함수가 화면을 올리고 닫아도 이 목록은 건드리지 않지만, 번호로 다시 집어 쓴다(참조를 함수 호출 너머로 들지 않는다).
            const UIScreenHandle screen           = pRegistration->_pFunction( *this, _listSwitchScreen[stateIndex]._screen );
            _listSwitchScreen[stateIndex]._screen = screen;
        }
    }

    void UISystem::setPauseMenuDocument( string_view documentPath )
    {
        _pauseMenuDocument = documentPath;
        _input.syncInputLayers( getActiveScreen(), _pauseMenuDocument.empty() == false );
    }

    UserSettingsManager* UISystem::findUserSettings() const
    {
        return _pUserSettings;
    }

    void UISystem::addLayerHideRequest( UILayerMask layers )
    {
        _paintState.addLayerHideRequest( layers );
        refreshHiddenLayers();
    }

    void UISystem::removeLayerHideRequest( UILayerMask layers )
    {
        _paintState.removeLayerHideRequest( layers );
        refreshHiddenLayers();
    }

    void UISystem::refreshHiddenLayers()
    {
        const UILayerMask hidden = _paintState.getHiddenLayers();
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            screen->_bHiddenByLayer = UILayerUtil::contains( hidden, screen->_desc._layer ) ? SW_TRUE : SW_FALSE;
        }
    }

    void UISystem::paintScreens()
    {
        SW_PROFILE_SCOPE( "GT.UI.Paint" );
        const UIPaintContext context = makePaintContext();
        // 지난 프레임과 같은 화면이 같은 순서이고 트리마다 지난 출력을 그대로 낼 수 있으며 포커스 테두리가 없으면, 목록은 지난 것과 같다 —
        // 이어 붙이기와 같은 내용 비교(사각형 수 × 144 바이트씩)를 건너뛴다. 움직이는 화면 마커만 있는 프레임이 이 길이다(앵커만 바뀐다).
        // 판정 순서(프레임 조건 → 트리마다 같은 트리인지 → `canReuseOutput`)를 바꾸지 않는다 — 재사용을 잘못 고르면 화면이 깜박인다.
        const bool bFocusRingMode = _input.getInputMode() == UIInputMode::Navigation && _input.getFocusManager().getFocusedTree() != nullptr;
        const bool bReusableFrame = bFocusRingMode == false && _bOnScreenSuppressed == SW_FALSE;
        bool       bUnchanged     = _paintState.canReuseLastCanvas( bReusableFrame, _listScreen.size(), _viewport._physicalSize );
        for ( size_t index = 0; bUnchanged && index < _listScreen.size(); ++index )
        {
            const WidgetTree& tree = _listScreen[index]->getTree();
            bUnchanged             = _paintState.isPaintedTree( index, tree ) && UIPaintPass::canReuseOutput( tree, context, _viewport._physicalSize );
        }
        if ( bUnchanged )
        {
            _paintState.markLastCanvasReused();
            SW_PROFILE_COUNT( "UI.PaintWidgets", 0 );
            SW_PROFILE_COUNT( "UI.CanvasQuads", _paintState.getCanvas()._listQuad.size() );
            _markers.publishCanvasAnchors( context._uiScale );
            return;
        }
        CanvasDrawList& target = _paintState.beginPaint( bReusableFrame, _viewport._physicalSize );
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            _paintState.addPaintedTree( screen->getTree() );
        }
        CanvasPainter painter( target, context._uiScale );
        uint32        paintedCount = 0;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( _bOnScreenSuppressed == SW_TRUE )
                break; // 그리기 목록을 비운다. 아래 같은 내용 확인이 바뀐 것으로 보고 번호를 올린다
            if ( screen->isHiddenByLayer() )
                continue; // 레이어 숨김 요청(`addLayerHideRequest`)이 가린 화면 — 포커스 테두리도 그리지 않는다
            WidgetTree& tree = screen->getTree();
            paintedCount += UIPaintPass::paint( tree, context, painter, target );
            // 포커스 테두리는 패드나 키보드로 다룰 때만 그린다(언리얼 CommonUI, 콘솔 게임과 같다). 그 화면 위, 위 화면 아래에 놓는다.
            if ( _input.getInputMode() != UIInputMode::Navigation || _input.getFocusManager().getFocusedTree() != &tree )
                continue;
            const Widget* pFocused = tree.findWidgetByID( _input.getFocusManager().getFocusedWidget() );
            if ( pFocused != nullptr && pFocused->isVisible() )
                UIPaintPass::paintFocusRing( *pFocused, painter );
        }
        SW_PROFILE_COUNT( "UI.PaintWidgets", paintedCount );
        SW_PROFILE_COUNT( "UI.CanvasQuads", target._listQuad.size() );
        _markers.publishCanvasAnchors( context._uiScale ); // 앵커는 사각형과 따로 프레임마다 낸다(움직인 마커가 그리기 목록 번호를 올리지 않는다)
        _paintState.endPaint( painter.getDeferredGlyphCount() );
    }

    UIViewport UISystem::computeViewport( const float2& physicalSize, float32 contentScale ) const
    {
        return UIScaleUtil::makeViewport( _scaleSettings, physicalSize, gv_uiScale, contentScale, gv_uiDebugSafeZone );
    }

    UILayoutContext UISystem::makeLayoutContext() const
    {
        UILayoutContext context{};
        context._pTextLayout   = _textLayout.get();
        context._safeInsets    = _viewport._safeInsets;
        context._viewportSize  = _viewport._size;
        context._uiScale       = _viewport._uiScale;
        context._textScale     = gv_uiTextScale > 0.0f ? static_cast<float32>( gv_uiTextScale ) : 1.0f;
        context._pLocalization = findLocalization();
        context._bRightToLeft  = UILayoutPass::isCultureRightToLeft( context._pLocalization ); // 운영 경로는 기동 때 받은 엔진 서비스의 문화권이다
        context._textRevision  = context._pLocalization != nullptr ? context._pLocalization->getTextRevision() : 0;
        context._pActionGlyphs = &_actionGlyphs;
        return context;
    }
} // namespace sw
