#include "pch.h"

#include "Engine/UI/UiSystem.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Animation/UiStyleTransition.h"
#include "Engine/UI/Binding/UiBindingSet.h"
#include "Engine/UI/Binding/UiViewModel.h"
#include "Engine/UI/Core/UiEventRouter.h"
#include "Engine/UI/Core/UiNavigationSolver.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Debug/UiBenchScreen.h"
#include "Engine/UI/Debug/UiDemoScreen.h"
#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Document/UiDocumentLoader.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/UiLayoutDump.h"
#include "Engine/UI/Screens/OptionsMenuScreen.h"
#include "Engine/UI/Screens/PauseMenuScreen.h"
#include "Engine/UI/Style/UiStylePass.h"
#include "Engine/UI/Style/UiStyleSet.h"
#include "Engine/UI/Style/UiStyleSheet.h"
#include "Engine/UI/World/WidgetComponent.h"
#include "Engine/UserSettings/UserSettingsVariables.h"
#include "Engine/Utility/GameTimeScale.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "UiSystem" );

    namespace
    {
        struct UiSystemInternal
        {
            /** @brief 마우스 버튼 중 UI 가 사건으로 받는 것(왼쪽 · 오른쪽 · 가운데). */
            static constexpr MouseButton kArrPointerButton[] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };
            /** @brief UI 행동 맵의 레이어 이름입니다(활성 화면이 있을 때만 켠다). */
            static constexpr utf8 kUiLayerName[]       = "UI";
            static constexpr utf8 kUiGlobalLayerName[] = "UIGlobal"; ///< 화면이 없을 때의 UI 행동(일시정지)
            /** @brief 스틱 탐색이 한 칸 옮기는 기울기 문턱입니다. */
            static constexpr float32 kStickNavigateMagnitude = 0.5f;
            /** @brief 패드 축 사건이 "패드를 쓴다" 로 세는 크기입니다(손을 떼어 둔 스틱의 떨림은 세지 않는다). */
            static constexpr float32 kGamepadAxisActivity = 0.5f;

            /** @struct NavigationAction @brief 탐색 행동 이름과 방향입니다. */
            struct NavigationAction
            {
                const utf8*           _pAction;
                UiNavigationDirection _direction;
            };
            static constexpr NavigationAction kArrNavigationAction[] = {
                {   UiActionName::kNavigateUp,       UiNavigationDirection::Up},
                { UiActionName::kNavigateDown,     UiNavigationDirection::Down},
                { UiActionName::kNavigateLeft,     UiNavigationDirection::Left},
                {UiActionName::kNavigateRight,    UiNavigationDirection::Right},
                {    UiActionName::kFocusNext,     UiNavigationDirection::Next},
                {UiActionName::kFocusPrevious, UiNavigationDirection::Previous},
            };

            /** @brief 원시 사건 하나가 가리키는 입력 방식입니다. 방식과 무관한 사건이면 false 입니다. */
            [[nodiscard]] static bool tryGetInputMode( const RawInputEvent& rawEvent, UiInputMode& outMode )
            {
                switch ( rawEvent._type )
                {
                    case RawInputEventType::KeyDown:
                    case RawInputEventType::GamepadButtonDown:
                    {
                        outMode = UiInputMode::Navigation;
                        return true;
                    }
                    case RawInputEventType::GamepadAxis:
                    {
                        if ( MathUtil::abs( rawEvent._payload._gamepadData._axisValue ) < kGamepadAxisActivity )
                            return false;
                        outMode = UiInputMode::Navigation;
                        return true;
                    }
                    case RawInputEventType::MouseMove:
                    case RawInputEventType::MouseButtonDown:
                    case RawInputEventType::MouseWheel:
                    {
                        outMode = UiInputMode::Pointer;
                        return true;
                    }
                    default:
                    {
                        return false;
                    }
                }
            }

            /** @brief 그리기 순서의 앞뒤입니다 — 층이 먼저, 같은 층이면 쌓인 순서. */
            static bool isDrawnBefore( const UiScreen& lhs, const UiScreen& rhs )
            {
                if ( lhs.getDesc()._layer != rhs.getDesc()._layer )
                    return static_cast<uint8>( lhs.getDesc()._layer ) < static_cast<uint8>( rhs.getDesc()._layer );
                return false; // 같은 층이면 뒤에 올린 것이 위 — 끼울 자리는 같은 층의 끝
            }

            /** @brief vtable 이 범위 안인 화면 · 위젯이 하나라도 있으면 true 입니다. */
            static bool usesCodeWithin( const UiScreen& screen, const void* pBegin, const void* pEnd )
            {
                if ( IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( &screen ), pBegin, pEnd ) )
                    return true;
                vector<Widget*> listWidget;
                screen.getTree().collectWidgetsInDocumentOrder( listWidget );
                for ( const Widget* pWidget : listWidget )
                {
                    if ( IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( pWidget ), pBegin, pEnd ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiSystem::UiSystem()
        : IModuleUnloadListener{}
        , _listScreen{}
        , _listOffscreen{}
        , _documentCache{}
        , _styleSheetCache{}
        , _themeCatalog{}
        , _themeName{}
        , _themeSetting{}
        , _listReopenDocument{}
        , _bindingConverters{}
        , _notifications{ *this }
        , _actionGlyphs{}
        , _focus{}
        , _pointer{}
        , _consumption{}
        , _uiInputMap{}
        , _pInput{ nullptr }
        , _pFontSystem{ nullptr }
        , _pLocalization{ nullptr }
        , _pUserSettings{ nullptr }
        , _textLayout{}
        , _scaleSettings{}
        , _subtitles{ *this }
        , _viewport{}
        , _canvas{}
        , _canvasScratch{}
        , _canvasRevision{ 1 }
        , _lastPointerPosition{}
        , _lastMousePixel{}
        , _stickRepeatSeconds{ 0.0f }
        , _inputDeltaSeconds{ 0.0f }
        , _stickDirection{ UiNavigationDirection::Next }
        , _activeScreen{ kInvalidUiScreenHandle }
        , _demoScreen{ kInvalidUiScreenHandle }
        , _benchScreen{ kInvalidUiScreenHandle }
        , _markerScreen{ kInvalidUiScreenHandle }
        , _listWidgetComponent{}
        , _listTickScratch{}
        , _optionsMenuDocument{ "engine/ui/options.ui.xml" }
        , _pauseMenuDocument{}
        , _optionsSwitchScreen{ kInvalidUiScreenHandle }
        , _nextScreenHandle{ 1 }
        , _nextPushOrder{ 0 }
        , _textRevision{ 0 }
        , _inputMode{ UiInputMode::Pointer }
        , _glyphStyle{ InputGlyphStyle::KeyboardMouse }
        , _bPauseRequested{ SW_FALSE }
        , _bPendingClose{ SW_FALSE }
        , _bPointerKnown{ SW_FALSE }
        , _bStickHeld{ SW_FALSE }
        , _bMousePixelKnown{ SW_FALSE }
        , _bTextRevisionKnown{ SW_FALSE }
        , _bGlyphStyleKnown{ SW_FALSE }
        , _bOnScreenSuppressed{ SW_FALSE }
    {
    }

    UiSystem::~UiSystem()
    {
        shutdown();
    }

    bool UiSystem::initialize( InputManager& inputManager, FontSystem* pFontSystem, string_view uiInputMapPath )
    {
        _pInput                    = &inputManager;
        _pFontSystem               = pFontSystem;
        _actionGlyphs._pInput      = &inputManager;
        _actionGlyphs._pUiInputMap = nullptr;
        _documentCache.setReloadedHandler( SW_DELEGATE_METHOD( UiAssetReloadedDelegate, &UiSystem::onDocumentReloaded, this ) );
        _styleSheetCache.setReloadedHandler( SW_DELEGATE_METHOD( UiAssetReloadedDelegate, &UiSystem::onStyleSheetReloaded, this ) );
        _textLayout    = pFontSystem != nullptr ? make_unique<TextLayoutEngine>( *pFontSystem ) : nullptr;
        _bPointerKnown = SW_FALSE;
        _consumption.clear();
        _pInput->setTextInputCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UiSystem::onTextInput, this ), InputKeyboardFocus::Ui );
        _pInput->setTextCompositionCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UiSystem::onTextComposition, this ), InputKeyboardFocus::Ui );
        if ( uiInputMapPath.empty() )
            return true;
        _uiInputMap = make_unique<InputMap>();
        // 글 입력 칸이 키보드 포커스(Ui)를 쥔 동안에도 Back · Tab 은 UI 가 받는다 — 그 밖의 행동은 UiSystem 이 거른다.
        _uiInputMap->setKeyboardFocusIgnored( true );
        _uiInputMap->setSuppressBaseActionOnChord( true ); // Shift+Tab 이 Tab 도 함께 발화하지 않게
        if ( _uiInputMap->loadFromResource( uiInputMapPath ) == false )
        {
            SW_LOG_ERROR( "[Ui] UI input map '%#' could not be loaded - UI navigation actions stay unbound", uiInputMapPath );
            _uiInputMap.reset();
            return false;
        }
        _uiInputMap->setInputManager( _pInput );
        _actionGlyphs._pUiInputMap = _uiInputMap.get();
        syncInputLayers( getActiveScreen() );
        return true;
    }

    void UiSystem::shutdown()
    {
        _subtitles.clear();
        _notifications.clear();
        // 위젯 컴포넌트가 이 시스템보다 오래 남을 수 있다 — 등록 · 마커를 잊게 한다(그 뒤 소멸자가 이 시스템을 부르지 않게).
        for ( WidgetComponent* pComponent : _listWidgetComponent )
        {
            pComponent->forgetUiSystem();
        }
        _listWidgetComponent.clear();
        _markerScreen = kInvalidUiScreenHandle;
        _demoScreen   = kInvalidUiScreenHandle;
        _benchScreen  = kInvalidUiScreenHandle;
        _focus.clearFocus();
        while ( _listScreen.empty() == false )
        {
            destroyScreenAt( static_cast<uint32>( _listScreen.size() ) - 1 );
        }
        _listOffscreen.clear();
        _bPendingClose = SW_FALSE;
        _listReopenDocument.clear();
        _documentCache.setReloadedHandler( {} );
        _styleSheetCache.setReloadedHandler( {} );
        refreshActiveScreen();
        if ( _pInput != nullptr )
        {
            if ( _pInput->getKeyboardFocus() == InputKeyboardFocus::Ui )
                _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
            _pInput->setTextInputCallback( {}, InputKeyboardFocus::Ui );
            _pInput->setTextCompositionCallback( {}, InputKeyboardFocus::Ui );
        }
        _uiInputMap.reset();
        _consumption.clear();
        _textLayout.reset();
        _pInput      = nullptr;
        _pFontSystem = nullptr;
    }

    void UiSystem::processInput( float32 deltaSeconds )
    {
        const ScopedMemoryTag uiMemoryTag{ MemoryTag::UI };
        if ( _pInput == nullptr )
            return;
        _inputDeltaSeconds = deltaSeconds;
        _consumption.update( *_pInput );
        if ( _uiInputMap != nullptr )
            _uiInputMap->update( deltaSeconds );
        updateInputMode();
        if ( _bOnScreenSuppressed == SW_FALSE )
            processPointer();
        processActions( deltaSeconds );
        applyPendingCloses();
        updateKeyboardFocus();
    }

    void UiSystem::update( float32 deltaSeconds, const UiViewport& viewport )
    {
        const ScopedMemoryTag uiMemoryTag{ MemoryTag::UI }; // 그림 캐시 · 그리기 목록 · 글 배치 · 글리프 아틀라스가 프레임 안에서 자란다
        _viewport = viewport;
        syncThemeSetting();
        syncDemoScreen();
        syncBenchScreen();
        syncOptionsMenuSwitch();
        reopenClosedScreens();
        _subtitles.update( deltaSeconds );
        tickScreens( deltaSeconds );
        _notifications.update( deltaSeconds );
        applyPendingCloses();
        updateBindings();
        // 애니메이션 — 문서 애니메이션 · 트윈이 프로퍼티를 쓴다(스타일 · 레이아웃 앞 — 쓴 칸의 무효화가 이번 프레임에 걷힌다). 실제 프레임 시간이다(정지 메뉴도 움직인다).
        {
            SW_PROFILE_SCOPE( "GT.Ui.Animate" );
            for ( size_t index = 0; index < _listScreen.size(); ++index )
            {
                _listScreen[index]->_animationPlayer.tick( deltaSeconds );
                (void)UiStyleTransition::update( _listScreen[index]->getTree(), deltaSeconds ); // 스타일 전환 — 새로 바뀐 것은 아래 스타일 단계가 시작한다
            }
            applyPendingCloses(); // 닫기 애니메이션이 끝난 화면
        }
        refreshInputGlyphs();
        // 스타일 — 스타일 더러운 위젯만 계산된 스타일을 다시 정한다(레이아웃 앞 — 여백 · 글꼴이 크기를 바꾼다).
        {
            SW_PROFILE_SCOPE( "GT.Ui.Style" );
            const bool bNavigation   = _inputMode == UiInputMode::Navigation;
            uint32     restyledCount = 0;
            for ( const unique_ptr<UiScreen>& screen : _listScreen )
            {
                if ( screen->_styleSet != nullptr )
                    restyledCount += UiStylePass::update( screen->getTree(), *screen->_styleSet, bNavigation );
            }
            SW_PROFILE_COUNT( "Ui.StyleWidgets", restyledCount );
        }
        // 화면 마커 — 게임 틱 · 트랜스폼 적용 뒤의 월드 점을 이번 뷰포트로 투영한다(레이아웃 앞 — 같은 프레임에 놓인다).
        for ( WidgetComponent* pComponent : _listWidgetComponent )
        {
            pComponent->updateScreenMarker( _viewport );
        }
        // 레이아웃 — 화면 트리마다 더러운 뿌리만 다시 잰다. 화면마다 뷰포트 전체가 루트 사각형이다.
        {
            SW_PROFILE_SCOPE( "GT.Ui.Layout" );
            const UiLayoutContext context       = makeLayoutContext();
            uint32                measuredCount = 0;
            for ( const unique_ptr<UiScreen>& screen : _listScreen )
            {
                measuredCount += UiLayoutPass::update( screen->getTree(), context );
            }
            SW_PROFILE_COUNT( "Ui.LayoutWidgets", measuredCount );
        }
        // 그리기 — 더러운 위젯만 다시 칠하고 화면마다 캐시를 이어 붙인다(스타일 걷기(5-2) 전까지 kStyle 도 그리기가 비운다).
        paintScreens();
        // 월드 공간 위젯 — 컴포넌트마다 자기 트리를 렌더 텍스처 크기(배율 1)로 놓고 칠한다.
        if ( _listWidgetComponent.empty() == false )
        {
            const UiLayoutContext layout = makeLayoutContext();
            const UiPaintContext  paint  = makePaintContext();
            for ( WidgetComponent* pComponent : _listWidgetComponent )
            {
                pComponent->updateWorldCanvas( layout, paint );
            }
        }
        updateOffscreenScreens();
    }

    UiScreenHandle UiSystem::pushScreen( unique_ptr<UiScreen> screen )
    {
        if ( screen == nullptr )
            return kInvalidUiScreenHandle;
        SW_ASSERT( screen->_pUiSystem == nullptr );
        screen->_pUiSystem = this;
        rebuildStyleSet( *screen );
        screen->_handle    = _nextScreenHandle++;
        screen->_pushOrder = _nextPushOrder++;
        screen->_bClosing  = SW_FALSE;
        if ( screen->_animationPlayer.findAnimation( hashed_string( UiAnimation::kOpenName ) ) != nullptr )
            (void)screen->_animationPlayer.play( hashed_string( UiAnimation::kOpenName ) );
        // 같은 층의 끝(그 층에서 맨 위)에 끼운다.
        uint32 at = static_cast<uint32>( _listScreen.size() );
        while ( at > 0 && UiSystemInternal::isDrawnBefore( *screen, *_listScreen[at - 1] ) )
        {
            --at;
        }
        const UiScreenHandle handle = screen->_handle;
        _listScreen.insert( _listScreen.begin() + at, std::move( screen ) );
        refreshActiveScreen();
        return handle;
    }

    void UiSystem::closeScreen( UiScreenHandle handle )
    {
        UiScreen* pScreen = findScreen( handle );
        if ( pScreen == nullptr || pScreen->_bClosing == SW_TRUE )
            return;
        pScreen->_bClosing = SW_TRUE;
        _bPendingClose     = SW_TRUE;
        // 닫기 애니메이션 — 끝날 때까지 지우지 않는다(`applyPendingCloses`). 여는 애니메이션은 멈춘다.
        UiAnimationPlayer& player = pScreen->_animationPlayer;
        if ( player.findAnimation( hashed_string( UiAnimation::kCloseName ) ) != nullptr )
        {
            player.stop( hashed_string( UiAnimation::kOpenName ) );
            (void)player.play( hashed_string( UiAnimation::kCloseName ) );
        }
    }

    bool UiSystem::tween( WidgetId widget, string_view propertyPath, string_view endValue, float32 duration, BlendCurve curve )
    {
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->getTree().findWidgetById( widget ) != nullptr )
                return screen->_animationPlayer.tween( widget, propertyPath, endValue, duration, curve );
        }
        SW_LOG_WARNING( "[Ui] Tween of '%#': widget %# is not in any screen", string( propertyPath ).c_str(), widget );
        return false;
    }

    unique_ptr<Widget> UiSystem::instantiateDocument( string_view documentPath, UiScreenDesc& outDesc, vector<UiBindingDesc>& outListBinding,
                                                      vector<string>& outListStyleSheet )
    {
        string                                  error;
        const shared_ptr<const UiDocumentAsset> document = _documentCache.findOrLoad( documentPath, error );
        if ( document == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Screen document is not loaded: %#", error.c_str() );
            return {};
        }
        unique_ptr<Widget> root = UiDocumentLoader::instantiate( *document, _documentCache, outListBinding, error );
        if ( root == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Screen document cannot be built: %#", error.c_str() );
            return {};
        }
        outDesc = document->_screenDesc;
        // 스타일 시트 — 문서 것 다음 조각 것(너비 우선, 이미 본 문서 · 시트는 건너뛴다). 조각은 짓기가 이미 캐시에 올렸다.
        vector<string> listDocument{ document->_path };
        for ( size_t index = 0; index < listDocument.size(); ++index )
        {
            string                                  fragmentError;
            const shared_ptr<const UiDocumentAsset> current = _documentCache.findOrLoad( listDocument[index], fragmentError );
            if ( current == nullptr )
                continue;
            for ( const string& sheet : current->_listStyleSheet )
            {
                if ( std::find( outListStyleSheet.begin(), outListStyleSheet.end(), sheet ) == outListStyleSheet.end() )
                    outListStyleSheet.push_back( sheet );
            }
            for ( const string& fragment : current->_listFragment )
            {
                if ( std::find( listDocument.begin(), listDocument.end(), fragment ) == listDocument.end() )
                    listDocument.push_back( fragment );
            }
        }
        return root;
    }

    UiScreenHandle UiSystem::pushDocumentScreen( unique_ptr<UiScreen> screen, string_view documentPath, vector<UiBindingDesc> listBinding, vector<string> listStyleSheet )
    {
        screen->_documentPath   = FileUtil::normalizePath( documentPath );
        screen->_listBinding    = std::move( listBinding );
        screen->_listStyleSheet = std::move( listStyleSheet );
        applyDocumentAnimations( *screen );
        return pushScreen( std::move( screen ) );
    }

    void UiSystem::applyDocumentAnimations( UiScreen& screen )
    {
        // 애니메이션은 문서의 것(캐시에 이미 있다 — 방금 지었다).
        screen._animationPlayer.stopAll();
        string                                  error;
        const shared_ptr<const UiDocumentAsset> document = _documentCache.findOrLoad( screen._documentPath, error );
        screen._animationPlayer.setAnimations( document != nullptr ? document->_listAnimation : vector<UiAnimation>{} );
    }

    void UiSystem::onDocumentReloaded( string_view documentPath )
    {
        uint32 rebuiltCount = 0;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->_documentPath.empty() || isDocumentUsing( screen->_documentPath, documentPath ) == false )
                continue;
            if ( rebuildScreenFromDocument( *screen ) )
                ++rebuiltCount;
        }
        for ( OffscreenScreen& offscreen : _listOffscreen )
        {
            UiScreen& screen = *offscreen._screen;
            if ( isDocumentUsing( screen._documentPath, documentPath ) && rebuildScreenFromDocument( screen ) )
            {
                rebuildStyleSet( screen, offscreen._theme );
                ++rebuiltCount;
            }
        }
        if ( rebuiltCount > 0 )
            SW_LOG_INFO( "[Ui] Reloaded %# screen(s) for %#", rebuiltCount, documentPath );
    }

    void UiSystem::onStyleSheetReloaded( string_view sheetPath )
    {
        // 묶음은 테마 시트와 문서 시트를 다 든다 — 그 경로의 시트를 든 화면만 새 시트로 다시 건다.
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->_styleSet == nullptr )
                continue;
            bool bUses = false;
            for ( const shared_ptr<const UiStyleSheetAsset>& sheet : screen->_styleSet->getSheets() )
            {
                bUses = bUses || ( sheet != nullptr && sheet->_path == sheetPath );
            }
            if ( bUses )
                rebuildStyleSet( *screen );
        }
        for ( OffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->_styleSet == nullptr )
                continue;
            bool bUses = false;
            for ( const shared_ptr<const UiStyleSheetAsset>& sheet : offscreen._screen->_styleSet->getSheets() )
            {
                bUses = bUses || ( sheet != nullptr && sheet->_path == sheetPath );
            }
            if ( bUses )
                rebuildStyleSet( *offscreen._screen, offscreen._theme );
        }
    }

    UiScreenHandle UiSystem::openOffscreenScreen( string_view documentPath, string_view targetPath )
    {
        UiScreenDesc          desc{};
        vector<UiBindingDesc> listBinding{};
        vector<string>        listStyleSheet{};
        unique_ptr<Widget>    root = instantiateDocument( documentPath, desc, listBinding, listStyleSheet );
        if ( root == nullptr )
            return kInvalidUiScreenHandle;
        OffscreenScreen& offscreen = _listOffscreen.emplace_back();
        offscreen._screen          = sw::make_unique<UiScreen>( desc, std::move( root ) );
        offscreen._targetPath      = hashed_string( targetPath );
        UiScreen& screen           = *offscreen._screen;
        screen._documentPath       = FileUtil::normalizePath( documentPath );
        screen._listBinding        = std::move( listBinding );
        screen._listStyleSheet     = std::move( listStyleSheet );
        screen._handle             = _nextScreenHandle++;
        rebuildStyleSet( screen );
        return screen._handle;
    }

    void UiSystem::closeOffscreenScreen( UiScreenHandle handle )
    {
        for ( size_t index = 0; index < _listOffscreen.size(); ++index )
        {
            if ( _listOffscreen[index]._screen->_handle != handle )
                continue;
            _listOffscreen.erase( _listOffscreen.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    UiScreen* UiSystem::findOffscreenScreen( UiScreenHandle handle ) const
    {
        for ( const OffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->_handle == handle )
                return offscreen._screen.get();
        }
        return nullptr;
    }

    void UiSystem::setOffscreenView( UiScreenHandle handle, const UiViewport& viewport, float32 textScale, const hashed_string& theme )
    {
        for ( OffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->_handle != handle )
                continue;
            offscreen._viewport  = viewport;
            offscreen._textScale = textScale > 0.0f ? textScale : 1.0f;
            if ( offscreen._theme != theme )
            {
                offscreen._theme = theme;
                rebuildStyleSet( *offscreen._screen, theme );
            }
            return;
        }
    }

    void UiSystem::updateOffscreenScreens()
    {
        for ( OffscreenScreen& offscreen : _listOffscreen )
        {
            const UiViewport& viewport = offscreen._viewport;
            if ( viewport._physicalSize._x <= 0.0f || viewport._physicalSize._y <= 0.0f )
                continue;
            UiScreen&   screen = *offscreen._screen;
            WidgetTree& tree   = screen.getTree();
            if ( screen._styleSet != nullptr )
                (void)UiStylePass::update( tree, *screen._styleSet, false );
            // 미리보기는 애니메이션 단계를 돌지 않는다 — 방금 시작한 스타일 전환을 끝 값으로 바로 맞춘다(옛 값에 멈추지 않게).
            (void)UiStyleTransition::update( tree, MathUtil::kMaxFloat );
            // 화면 UI 와 같은 문맥에 이 화면의 뷰포트 · 글자 배율만 바꿔 쓴다(배율 · 글자 배율이 바뀌면 루트부터 다시 잰다 — 트리가 지난 값을 든다).
            UiLayoutContext layout = makeLayoutContext();
            layout._viewportSize   = viewport._size;
            layout._safeInsets     = viewport._safeInsets;
            layout._uiScale        = viewport._uiScale;
            layout._textScale      = offscreen._textScale;
            (void)UiLayoutPass::update( tree, layout );
            UiPaintContext paint = makePaintContext();
            paint._uiScale       = viewport._uiScale;
            paint._textScale     = offscreen._textScale;
            offscreen._scratch.clear();
            offscreen._scratch._targetSize = viewport._physicalSize;
            CanvasPainter painter( offscreen._scratch, viewport._uiScale );
            (void)UiPaintPass::paint( tree, paint, painter, offscreen._scratch );
            if ( offscreen._scratch.isSameContent( offscreen._canvas ) && offscreen._scratch._targetSize == offscreen._canvas._targetSize )
                continue;
            std::swap( offscreen._canvas, offscreen._scratch );
            ++offscreen._revision;
        }
    }

    bool UiSystem::isDocumentUsing( const string& documentPath, string_view usedPath )
    {
        vector<string> listDocument{ documentPath };
        for ( size_t index = 0; index < listDocument.size(); ++index )
        {
            if ( listDocument[index] == usedPath )
                return true;
            string                                  error;
            const shared_ptr<const UiDocumentAsset> document = _documentCache.findOrLoad( listDocument[index], error );
            if ( document == nullptr )
                continue;
            for ( const string& fragment : document->_listFragment )
            {
                if ( std::find( listDocument.begin(), listDocument.end(), fragment ) == listDocument.end() )
                    listDocument.push_back( fragment );
            }
        }
        return false;
    }

    bool UiSystem::rebuildScreenFromDocument( UiScreen& screen )
    {
        UiScreenDesc          desc{};
        vector<UiBindingDesc> listBinding{};
        vector<string>        listStyleSheet{};
        unique_ptr<Widget>    root = instantiateDocument( screen._documentPath, desc, listBinding, listStyleSheet );
        if ( root == nullptr )
            return false; // 옛 트리를 둔다 — 오류는 instantiateDocument 가 남겼다

        // 이름으로 이어 갈 것 — 포커스 위젯 · 덮였을 때의 포커스 · 스크롤 오프셋.
        WidgetTree&           tree          = screen.getTree();
        const bool            bHadFocus     = _focus.getFocusedTree() == &tree;
        const Widget*         pFocused      = bHadFocus ? tree.findWidgetById( _focus.getFocusedWidget() ) : nullptr;
        const Widget*         pLastFocused  = tree.findWidgetById( screen._lastFocused );
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
        screen._lastFocused    = kInvalidWidgetId;
        // 뷰모델은 화면이 그대로 든다 — 식이 새 위젯 번호를 가리키니 다음 바인딩 단계가 다시 걸고 모든 칸을 쓴다.
        screen._bindingSet->markRebind();
        screen.onTreeRebuilt();
        applyDocumentAnimations( screen ); // 고친 문서의 애니메이션 — 재생 중이던 것(닫기 포함)은 멈춘다
        rebuildStyleSet( screen );
        // 스크롤 오프셋은 내용 크기 안으로 묶이므로 새 트리를 지금 한 번 맞추고 재 둔 뒤에 돌려준다.
        if ( screen._styleSet != nullptr )
            (void)UiStylePass::update( tree, *screen._styleSet, _inputMode == UiInputMode::Navigation );
        (void)UiLayoutPass::update( tree, makeLayoutContext() );

        for ( size_t index = 0; index < listScrollName.size(); ++index )
        {
            ScrollPanel* pScroll = tree.findWidget<ScrollPanel>( listScrollName[index] );
            if ( pScroll != nullptr )
                pScroll->setScrollOffset( listScrollOffset[index] );
        }
        const Widget* pNewLast = lastFocusName.empty() ? nullptr : tree.findWidgetByName( lastFocusName );
        if ( pNewLast != nullptr )
            screen._lastFocused = pNewLast->getId();
        if ( bHadFocus )
        {
            const Widget* pNewFocus = focusedName.empty() ? nullptr : tree.findWidgetByName( focusedName );
            if ( pNewFocus == nullptr || _focus.setFocus( tree, pNewFocus->getId() ) == false )
                restoreFocus( screen );
        }
        return true;
    }

    void UiSystem::reopenClosedScreens()
    {
        if ( _listReopenDocument.empty() )
            return;
        vector<string> listDocument;
        listDocument.swap( _listReopenDocument );
        for ( const string& documentPath : listDocument )
        {
            if ( openScreen( documentPath ) == kInvalidUiScreenHandle )
                SW_LOG_ERROR( "[Ui] Screen document could not be reopened after module reload: %#", documentPath );
        }
    }

    void UiSystem::setThemeCatalog( const UiThemeCatalog& catalog )
    {
        _themeCatalog = catalog;
        // 사용자 설정(gv_uiTheme)의 테마가 목록에 있으면 그것, 없으면 목록의 기본 — 테마를 두지 않은 게임 목록에서 설정 값은 조용히 기본이 된다.
        _themeSetting = gv_uiTheme;
        if ( _themeSetting.empty() == false && _themeCatalog.findTheme( hashed_string( _themeSetting ) ) != nullptr )
        {
            (void)setTheme( hashed_string( _themeSetting ) );
            return;
        }
        if ( _themeCatalog._defaultTheme.empty() == false && setTheme( _themeCatalog._defaultTheme ) == false )
            SW_LOG_ERROR( "[Ui] Default UI theme '%#' is not in the theme catalog", _themeCatalog._defaultTheme.c_str() );
    }

    bool UiSystem::setTheme( const hashed_string& name )
    {
        if ( _themeCatalog.findTheme( name ) == nullptr )
        {
            SW_LOG_WARNING( "[Ui] Unknown UI theme '%#'", name.c_str() );
            return false;
        }
        _themeName = name;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            rebuildStyleSet( *screen );
        }
        for ( OffscreenScreen& offscreen : _listOffscreen )
        {
            rebuildStyleSet( *offscreen._screen, offscreen._theme );
        }
        return true;
    }

    void UiSystem::appendStyleSheets( const vector<string>& listPath, vector<shared_ptr<const UiStyleSheetAsset>>& inoutListSheet )
    {
        for ( const string& path : listPath )
        {
            string                                    error;
            const shared_ptr<const UiStyleSheetAsset> sheet = _styleSheetCache.findOrLoad( path, error );
            if ( sheet == nullptr )
            {
                SW_LOG_ERROR( "[Ui] Style sheet is not loaded: %#", error.c_str() );
                continue;
            }
            inoutListSheet.push_back( sheet );
        }
    }

    void UiSystem::rebuildStyleSet( UiScreen& screen, const hashed_string& themeName )
    {
        vector<shared_ptr<const UiStyleSheetAsset>> listSheet;
        const hashed_string&                        theme  = themeName.empty() ? _themeName : themeName;
        const UiThemeDesc*                          pTheme = theme.empty() ? nullptr : _themeCatalog.findTheme( theme );
        if ( pTheme != nullptr )
            appendStyleSheets( pTheme->_listStyleSheet, listSheet );
        appendStyleSheets( screen._listStyleSheet, listSheet );
        if ( screen._styleSet == nullptr )
            screen._styleSet = make_unique<UiStyleSet>();
        screen._styleSet->setSheets( std::move( listSheet ) );
        // 트리 전체를 다시 맞춘다 — 루트의 kStyle 은 계산이 바뀐 만큼 자손으로 내려간다. 규칙이 바뀌었으니 자손도 모두 표시한다.
        vector<Widget*> listWidget;
        screen.getTree().collectWidgetsInDocumentOrder( listWidget );
        for ( Widget* pWidget : listWidget )
        {
            pWidget->invalidate( WidgetDirty::kStyle );
        }
    }

    UiScreen* UiSystem::findScreen( UiScreenHandle handle ) const
    {
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->_handle == handle )
                return screen.get();
        }
        return nullptr;
    }

    string UiSystem::makeLayoutDump() const
    {
        string text;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            text += "## " + ( screen->_documentPath.empty() ? string( "(code)" ) : screen->_documentPath ) + "\n";
            text += UiLayoutDump::makeDump( screen->getTree(), _viewport._uiScale );
        }
        return text;
    }

    UiScreen* UiSystem::getActiveScreen() const
    {
        return findScreen( _activeScreen );
    }

    bool UiSystem::isGameInputBlocked() const
    {
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->isClosing() == false && screen->blocksLowerInput() )
                return true;
        }
        return false;
    }

    bool UiSystem::isLoadingScreenShown() const
    {
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->isClosing() == false && screen->getDesc()._layer == UiLayer::Loading )
                return true;
        }
        return false;
    }

    bool UiSystem::wantsCursor() const
    {
        const UiScreen* pActive = getActiveScreen();
        return pActive != nullptr && pActive->getDesc()._bShowCursor;
    }

    bool UiSystem::isActionConsumed( const InputMap& inputMap, const hashed_string& action ) const
    {
        return _pInput != nullptr && _consumption.isActionConsumed( inputMap, *_pInput, action );
    }

    void UiSystem::consumeAction( const InputMap& inputMap, const hashed_string& action )
    {
        if ( _pInput != nullptr )
            _consumption.consumeAction( inputMap, *_pInput, action );
    }

    void UiSystem::setInputMode( UiInputMode mode )
    {
        if ( _inputMode == mode )
            return;
        _inputMode = mode;
        // :focus-visible 은 입력 방식을 따른다 — 포커스 위젯을 다시 맞춘다.
        WidgetTree* pFocusTree = _focus.getFocusedTree();
        Widget*     pFocused   = pFocusTree != nullptr ? pFocusTree->findWidgetById( _focus.getFocusedWidget() ) : nullptr;
        if ( pFocused != nullptr )
            pFocused->invalidate( WidgetDirty::kStyle );
        UiScreen* pActive = getActiveScreen();
        if ( mode == UiInputMode::Navigation && pActive != nullptr && _focus.getFocusedTree() != &pActive->getTree() )
            restoreFocus( *pActive );
    }

    uint32 UiSystem::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        uint32 closedCount = 0;
        // 내려가는 모듈의 변환기 함수 · 뷰모델은 바인딩이 놓는다(화면은 산다 — 다음 바인딩 단계가 다시 건다).
        const bool bConverterRemoved = _bindingConverters.removeCodeWithin( pBegin, pEnd ) > 0;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            const UiViewModel* pViewModel = screen->getViewModel();
            if ( pViewModel != nullptr && IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( pViewModel ), pBegin, pEnd ) )
            {
                SW_LOG_WARNING( "[Ui] Screen %# released its view model for module reload - the game sets it again after the reload", screen->_handle );
                screen->setViewModel( nullptr );
            }
            else if ( bConverterRemoved )
                screen->getBindingSet().markRebind();
        }
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            if ( UiSystemInternal::usesCodeWithin( *_listScreen[index - 1], pBegin, pEnd ) == false )
                continue;
            const UiScreen& screen = *_listScreen[index - 1];
            SW_LOG_WARNING( "[Ui] Screen %# closed for module reload - it holds widget code from the unloading module", screen._handle );
            // 문서로 연 기본 화면은 문서 경로로 다시 연다(새 이미지의 위젯 타입으로). 화면 클래스가 모듈 것이면 다시 열 길이 없다.
            const bool bScreenClassInModule = IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( &screen ), pBegin, pEnd );
            if ( screen._documentPath.empty() == false && bScreenClassInModule == false )
                _listReopenDocument.push_back( screen._documentPath );
            destroyScreenAt( index - 1 );
            ++closedCount;
        }
        if ( closedCount > 0 )
            refreshActiveScreen();
        return closedCount;
    }

    void UiSystem::updateBindings()
    {
        SW_PROFILE_SCOPE( "GT.Ui.Bind" );
        UiBindingContext context{};
        context._pLocalization = findLocalization();
        context._pConverters   = &_bindingConverters;
        context._pSettings     = findUserSettings();
        // 글 판 — 언어를 바꾸거나 표를 다시 읽으면 오른다. 정수 하나 비교라 매 프레임 본다(언리얼 FTextLocalizationManager 의 TextRevision).
        const uint32 textRevision = context._pLocalization != nullptr ? context._pLocalization->getTextRevision() : 0;
        if ( _bTextRevisionKnown == SW_TRUE && textRevision != _textRevision )
        {
            if ( _pFontSystem != nullptr && _pFontSystem->isInitialized() )
                _pFontSystem->invalidateFaceChains(); // 문화권의 대체 가족이 바뀐다
            vector<Widget*> listWidget;
            for ( const unique_ptr<UiScreen>& screen : _listScreen )
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
        uint32 polledCount  = 0;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            UiBindingSet& bindingSet = screen->getBindingSet();
            bindingSet.update( screen->getBindings(), context );
            polledCount += bindingSet.getPolledCount();
        }
        SW_PROFILE_COUNT( "Ui.PollBindings", polledCount );
    }

    void UiSystem::setUserSettings( UserSettingsManager* pSettings )
    {
        _pUserSettings = pSettings;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            screen->getBindingSet().markRebind();
        }
    }

    const LocalizationManager* UiSystem::findLocalization() const
    {
        return _pLocalization != nullptr ? _pLocalization : engine::getBoundEngineServices()._pLocalizationManager;
    }

    void UiSystem::refreshInputGlyphs()
    {
        if ( _pInput == nullptr )
            return;
        const InputGlyphStyle style = _pInput->getActiveGlyphStyle();
        if ( _bGlyphStyleKnown == SW_TRUE && style != _glyphStyle )
        {
            vector<Widget*> listWidget;
            for ( const unique_ptr<UiScreen>& screen : _listScreen )
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

    void UiSystem::processPointer()
    {
        const MouseDevice* pMouse = _pInput->getMouse();
        if ( pMouse == nullptr )
            return;
        // 게임이 포인터를 잠가 쥐고 있으면(1 인칭 시점) 커서는 화면 가운데에 묶여 있다 — UI 는 포인터를 보지 않는다.
        if ( _pInput->isMouseLockActive() )
        {
            _pointer.clearHover();
            _bPointerKnown = SW_FALSE;
            return;
        }
        // 창 픽셀 → UI 단위(지난 `update` 의 뷰포트 배율 — 입력은 이번 프레임 레이아웃 앞이라 지난 프레임에 그린 화면 기준이다).
        const int2     pixel   = pMouse->getPosition();
        const float32  uiScale = _viewport._uiScale > 0.0f ? _viewport._uiScale : 1.0f;
        const float2   position{ static_cast<float32>( pixel._x ) / uiScale, static_cast<float32>( pixel._y ) / uiScale };
        UiPointerEvent event{};
        event._position      = position;
        event._delta         = _bPointerKnown == SW_TRUE ? float2{ position._x - _lastPointerPosition._x, position._y - _lastPointerPosition._y } : float2{};
        const bool bMoved    = _bPointerKnown == SW_FALSE || event._delta._x != 0.0f || event._delta._y != 0.0f;
        _lastPointerPosition = position;
        _bPointerKnown       = SW_TRUE;
        if ( bMoved )
        {
            event._kind = UiPointerEventKind::Move;
            (void)dispatchPointerEvent( event );
        }
        for ( const MouseButton button : UiSystemInternal::kArrPointerButton )
        {
            event._button = button;
            // 위젯이 처리한 버튼은 먹는다 — 게임의 "왼쪽 클릭 = 사격" 이 메뉴 클릭을 같이 받지 않게. 빈 곳 클릭은 게임으로 간다.
            if ( pMouse->wasButtonPressed( button ) )
            {
                event._kind = UiPointerEventKind::Down;
                if ( dispatchPointerEvent( event ) )
                    _consumption.consumeMouseButton( button );
            }
            if ( pMouse->wasButtonReleased( button ) )
            {
                event._kind = UiPointerEventKind::Up;
                if ( dispatchPointerEvent( event ) )
                    _consumption.consumeMouseButton( button );
            }
        }
        const float32 wheel = pMouse->getMouseWheel();
        if ( wheel != 0.0f )
        {
            event._kind  = UiPointerEventKind::Wheel;
            event._wheel = wheel;
            (void)dispatchPointerEvent( event );
        }
    }

    bool UiSystem::dispatchPointerEvent( const UiPointerEvent& event )
    {
        // 잡은 위젯이 있으면 그 트리로, 없으면 점 아래 맨 위 화면으로.
        UiScreen* pTarget = nullptr;
        if ( _pointer.getCaptureTree() != nullptr )
        {
            for ( const unique_ptr<UiScreen>& screen : _listScreen )
            {
                if ( &screen->getTree() == _pointer.getCaptureTree() )
                    pTarget = screen.get();
            }
        }
        if ( pTarget == nullptr )
            pTarget = findPointerScreen( event._position );
        if ( pTarget == nullptr )
        {
            _pointer.clearHover();
            return false;
        }
        const UiPointerResult result = _pointer.process( pTarget->getTree(), event );
        if ( result._focusRequest != kInvalidWidgetId && pTarget->takesFocus() )
            (void)_focus.setFocus( pTarget->getTree(), result._focusRequest );
        return result._bHandled == SW_TRUE;
    }

    UiScreen* UiSystem::findPointerScreen( const float2& point ) const
    {
        UiWidgetPath path{};
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UiScreen* pScreen = _listScreen[index - 1].get();
            if ( pScreen->isClosing() || pScreen->receivesPointer() == false )
                continue;
            if ( UiEventRouter::hitTest( pScreen->getTree(), point, path ) )
                return pScreen;
            if ( pScreen->blocksLowerInput() )
                return nullptr; // 모달 아래 화면은 클릭을 받지 않는다
        }
        return nullptr;
    }

    void UiSystem::applyPendingCloses()
    {
        if ( _bPendingClose == SW_FALSE )
            return;
        _bPendingClose = SW_FALSE;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UiScreen& screen = *_listScreen[index - 1];
            if ( screen._bClosing == SW_FALSE )
                continue;
            // 닫기 애니메이션이 도는 화면은 끝날 때까지 남긴다(다음에 다시 본다).
            if ( screen._animationPlayer.isPlaying( hashed_string( UiAnimation::kCloseName ) ) )
            {
                _bPendingClose = SW_TRUE;
                continue;
            }
            destroyScreenAt( index - 1 );
        }
        refreshActiveScreen();
    }

    void UiSystem::tickScreens( float32 deltaSeconds )
    {
        // 틱이 화면을 올리고 닫을 수 있다(확인 창 · 키 바인딩 창) — 번호로 돌고 매번 다시 찾는다.
        _listTickScratch.clear();
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( screen->_bClosing == SW_FALSE )
                _listTickScratch.push_back( screen->_handle );
        }
        for ( const UiScreenHandle handle : _listTickScratch )
        {
            UiScreen* pScreen = findScreen( handle );
            if ( pScreen != nullptr && pScreen->isClosing() == false )
                pScreen->onTick( deltaSeconds );
        }
    }

    void UiSystem::destroyScreenAt( uint32 index )
    {
        unique_ptr<UiScreen> screen = std::move( _listScreen[index] );
        _listScreen.erase( _listScreen.begin() + index );
        _pointer.forgetTree( screen->getTree() );
        if ( _focus.getFocusedTree() == &screen->getTree() )
            _focus.clearFocus();
        screen->_pUiSystem = nullptr;
        // 트리가 지워질 때 포커스 관리자에게 알린다 — 위에서 이미 풀었다.
    }

    void UiSystem::refreshActiveScreen()
    {
        // 활성 화면 = 맨 위의 포커스 받는 화면. 막는 화면(모달 · 로딩)을 지나 내려가지 않는다.
        UiScreen* pNewActive = nullptr;
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UiScreen* pScreen = _listScreen[index - 1].get();
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

        UiScreen* pOldActive = findScreen( _activeScreen );
        if ( pNewActive != pOldActive )
        {
            if ( pOldActive != nullptr && _focus.getFocusedTree() == &pOldActive->getTree() )
            {
                pOldActive->_lastFocused = _focus.getFocusedWidget();
                _focus.clearFocus();
            }
            _activeScreen = pNewActive != nullptr ? pNewActive->_handle : kInvalidUiScreenHandle;
            if ( pNewActive != nullptr )
                restoreFocus( *pNewActive );
        }

        // UI 행동은 활성 화면이 있을 때만 — HUD 만 있으면 패드 A 는 게임의 것이다.
        if ( _uiInputMap != nullptr )
            syncInputLayers( pNewActive );

        // 게임 정지 — 정지 화면이 하나라도 있으면 요청을 걸어 둔다.
        bool bPause = false;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            bPause = bPause || ( screen->isClosing() == false && screen->getDesc()._bPausesGame );
        }
        if ( bPause && _bPauseRequested == SW_FALSE )
        {
            GameTimeScale::addPauseRequest();
            _bPauseRequested = SW_TRUE;
        }
        else if ( bPause == false && _bPauseRequested == SW_TRUE )
        {
            GameTimeScale::removePauseRequest();
            _bPauseRequested = SW_FALSE;
        }
    }

    void UiSystem::updateInputMode()
    {
        UiInputMode mode    = _inputMode;
        bool        bChange = false;
        for ( const RawInputEvent& rawEvent : _pInput->getLastFrameEvents() )
        {
            // 커서 이동은 실제로 자리가 바뀔 때만 포인터 방식이다 — 창이 뜰 때 · 포커스를 받을 때 OS 가 같은 자리로 보내는 이동 사건이
            // 패드로 고른 포커스를 지우지 않게(첫 사건은 자리만 기억한다).
            if ( rawEvent._type == RawInputEventType::MouseMove )
            {
                const int2 pixel{ rawEvent._payload._mouseData._x, rawEvent._payload._mouseData._y };
                const bool bMoved = _bMousePixelKnown == SW_TRUE && ( pixel._x != _lastMousePixel._x || pixel._y != _lastMousePixel._y );
                _lastMousePixel   = pixel;
                _bMousePixelKnown = SW_TRUE;
                if ( bMoved == false )
                    continue;
            }
            UiInputMode eventMode = mode;
            if ( UiSystemInternal::tryGetInputMode( rawEvent, eventMode ) )
            {
                mode    = eventMode;
                bChange = true;
            }
        }
        if ( bChange )
            setInputMode( mode );
    }

    void UiSystem::processActions( float32 deltaSeconds )
    {
        UiScreen* pActive = getActiveScreen();
        if ( pActive == nullptr )
            processPauseAction();
        if ( pActive == nullptr || _uiInputMap == nullptr || pActive->wantsUiActions() == false )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 개발 콘솔이 키보드를 쥐었으면 UI 는 행동을 받지 않는다. 글 입력 칸이 쥐었으면 Back · Tab 만.
        const InputKeyboardFocus keyboardFocus = _pInput->getKeyboardFocus();
        if ( keyboardFocus == InputKeyboardFocus::DevConsole )
            return;
        const bool      bTextFocus = keyboardFocus == InputKeyboardFocus::Ui;
        const InputMap& uiMap      = *_uiInputMap;

        for ( const UiSystemInternal::NavigationAction& entry : UiSystemInternal::kArrNavigationAction )
        {
            const hashed_string action( entry._pAction );
            const bool          bTab = entry._direction == UiNavigationDirection::Next || entry._direction == UiNavigationDirection::Previous;
            if ( ( bTextFocus && bTab == false ) || uiMap.wasActionTriggered( action ) == false )
                continue;
            if ( handleNavigation( *pActive, action, entry._direction ) )
                consumeAction( uiMap, action );
        }
        if ( bTextFocus == false )
        {
            processStickNavigation( *pActive, deltaSeconds );
            processScroll( *pActive );
        }

        const hashed_string arrRoutedAction[] = { hashed_string( UiActionName::kAccept ), hashed_string( UiActionName::kTabNext ),
                                                  hashed_string( UiActionName::kTabPrevious ) };
        for ( const hashed_string& action : arrRoutedAction )
        {
            if ( bTextFocus || uiMap.wasActionTriggered( action ) == false )
                continue;
            // 위젯이 먼저, 아무도 안 먹은 탭 행동은 화면이(옵션 메뉴의 탭 줄).
            const bool bScreenAction = action != hashed_string( UiActionName::kAccept );
            if ( routeAction( *pActive, action, float2{} ) || ( bScreenAction && pActive->onUnhandledAction( action ) ) )
                consumeAction( uiMap, action );
        }

        // 글 입력 칸 — 지우기는 칸이 키보드 포커스를 쥔 동안만(그 밖에서 Backspace 는 UI 의 것이 아니다).
        const hashed_string backspaceAction( UiActionName::kTextBackspace );
        if ( bTextFocus && uiMap.wasActionTriggered( backspaceAction ) && routeAction( *pActive, backspaceAction, float2{} ) )
            consumeAction( uiMap, backspaceAction );

        const hashed_string backAction( UiActionName::kBack );
        if ( uiMap.wasActionTriggered( backAction ) )
        {
            // 위젯이 먼저(펼친 목록을 접는 콤보 상자), 아무도 안 먹으면 화면이(기본은 닫기).
            const bool bHandled = routeAction( *pActive, backAction, float2{} ) || pActive->onBack();
            if ( bHandled )
                consumeAction( uiMap, backAction );
        }
    }

    bool UiSystem::handleNavigation( UiScreen& screen, const hashed_string& action, UiNavigationDirection direction )
    {
        WidgetTree& tree = screen.getTree();
        // 포커스가 없으면 첫 탐색 입력은 기본 포커스를 잡는 데 쓴다(어디로 갈지 모르는 채 옮기지 않는다).
        if ( _focus.getFocusedTree() != &tree || _focus.getFocusedWidget() == kInvalidWidgetId )
        {
            restoreFocus( screen );
            return _focus.getFocusedTree() == &tree;
        }
        if ( routeAction( screen, action, float2{} ) )
            return true;
        return _focus.navigate( tree, direction );
    }

    void UiSystem::processStickNavigation( UiScreen& screen, float32 deltaSeconds )
    {
        const hashed_string stickAction( UiActionName::kNavigateStick );
        const float2        value = _uiInputMap->getVector2D( stickAction );
        const float32       absX  = MathUtil::abs( value._x );
        const float32       absY  = MathUtil::abs( value._y );
        if ( MathUtil::max( absX, absY ) < UiSystemInternal::kStickNavigateMagnitude )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 큰 축 방향 한 칸 — 스틱 위(+y)는 화면 위다.
        const UiNavigationDirection direction = absX >= absY ? ( value._x > 0.0f ? UiNavigationDirection::Right : UiNavigationDirection::Left )
                                                             : ( value._y > 0.0f ? UiNavigationDirection::Up : UiNavigationDirection::Down );
        bool                        bStep     = false;
        if ( _bStickHeld == SW_FALSE || direction != _stickDirection )
        {
            _bStickHeld         = SW_TRUE;
            _stickDirection     = direction;
            _stickRepeatSeconds = _uiInputMap->getNavRepeatDelay();
            bStep               = true;
        }
        else
        {
            _stickRepeatSeconds -= deltaSeconds;
            if ( _stickRepeatSeconds <= 0.0f )
            {
                _stickRepeatSeconds += _uiInputMap->getNavRepeatRate();
                bStep = true;
            }
        }
        if ( bStep )
            (void)handleNavigation( screen, stickAction, direction );
        consumeAction( *_uiInputMap, stickAction ); // 기운 동안(반복을 기다리는 동안에도) 스틱은 UI 의 것이다
    }

    void UiSystem::processScroll( UiScreen& screen )
    {
        const hashed_string scrollAction( UiActionName::kScroll );
        const float2        value = _uiInputMap->getVector2D( scrollAction );
        if ( value._x == 0.0f && value._y == 0.0f )
            return;
        // 포커스가 이 화면에 있으면 포커스 경로(목록 안의 버튼 → 그 목록), 없으면 포인터가 올라간 경로로 보낸다.
        UiWidgetPath path{};
        _focus.makeFocusPath( screen.getTree(), path );
        if ( path.isEmpty() && _pointer.getHoverTree() == &screen.getTree() )
            path = _pointer.getHoverPath();
        if ( routeActionAlong( screen, path, scrollAction, value ) )
            consumeAction( *_uiInputMap, scrollAction );
    }

    bool UiSystem::routeAction( UiScreen& screen, const hashed_string& action, const float2& value )
    {
        UiWidgetPath path{};
        _focus.makeFocusPath( screen.getTree(), path );
        return routeActionAlong( screen, path, action, value );
    }

    bool UiSystem::routeActionAlong( UiScreen& screen, const UiWidgetPath& path, const hashed_string& action, const float2& value )
    {
        if ( path.isEmpty() )
            return false;
        UiActionEvent event{};
        event._action       = action;
        event._value        = value;
        event._deltaSeconds = _inputDeltaSeconds;
        WidgetId      handler{ kInvalidWidgetId };
        const UiReply reply = UiEventRouter::routeActionEvent( screen.getTree(), path, event, handler );
        if ( reply.isHandled() && reply._focusRequest != kInvalidWidgetId )
            (void)_focus.setFocus( screen.getTree(), reply._focusRequest );
        return reply.isHandled();
    }

    void UiSystem::updateKeyboardFocus()
    {
        const Widget* pFocused = nullptr;
        if ( _focus.getFocusedTree() != nullptr )
            pFocused = _focus.getFocusedTree()->findWidgetById( _focus.getFocusedWidget() );
        const bool               bWantsText = pFocused != nullptr && pFocused->supportsTextInput();
        const InputKeyboardFocus current    = _pInput->getKeyboardFocus();
        if ( bWantsText && current == InputKeyboardFocus::Game )
            _pInput->setKeyboardFocus( InputKeyboardFocus::Ui );
        else if ( bWantsText == false && current == InputKeyboardFocus::Ui )
            _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
    }

    void UiSystem::onTextInput( string_view text )
    {
        dispatchTextEvent( text, false );
    }

    void UiSystem::onTextComposition( string_view text )
    {
        dispatchTextEvent( text, true );
    }

    void UiSystem::dispatchTextEvent( string_view text, bool bComposition )
    {
        WidgetTree* pTree   = _focus.getFocusedTree();
        Widget*     pWidget = pTree != nullptr ? pTree->findWidgetById( _focus.getFocusedWidget() ) : nullptr;
        if ( pWidget == nullptr || pWidget->supportsTextInput() == false )
            return;
        UiTextEvent event{};
        event._text         = string{ text };
        event._bComposition = bComposition ? SW_TRUE : SW_FALSE;
        (void)pWidget->onTextEvent( event );
    }

    void UiSystem::restoreFocus( UiScreen& screen )
    {
        WidgetTree& tree = screen.getTree();
        if ( screen._lastFocused != kInvalidWidgetId && _focus.setFocus( tree, screen._lastFocused ) )
            return;
        if ( _inputMode != UiInputMode::Navigation || tree.getRoot() == nullptr )
            return;
        const Widget*  pDefault = screen.getDesc()._defaultFocus.empty() ? nullptr : tree.findWidgetByName( screen.getDesc()._defaultFocus );
        const WidgetId target   = pDefault != nullptr ? pDefault->getId() : UiNavigationSolver::findFirstFocusable( *tree.getRoot() );
        if ( target != kInvalidWidgetId )
            (void)_focus.setFocus( tree, target );
    }

    UiPaintContext UiSystem::makePaintContext() const
    {
        UiPaintContext context{};
        context._pTextLayout   = _textLayout.get();
        context._uiScale       = _viewport._uiScale > 0.0f ? _viewport._uiScale : 1.0f;
        context._textScale     = gv_uiTextScale > 0.0f ? static_cast<float32>( gv_uiTextScale ) : 1.0f;
        context._frameIndex    = engine::getFrameProfiler().getFrameCount();
        context._pLocalization = findLocalization();
        context._textRevision  = context._pLocalization != nullptr ? context._pLocalization->getTextRevision() : 0;
        context._pActionGlyphs = &_actionGlyphs;
        if ( _pFontSystem != nullptr && _pFontSystem->isInitialized() )
        {
            context._pGlyphCache     = &_pFontSystem->getGlyphCache();
            context._atlasGeneration = context._pGlyphCache->getAtlas().getGeneration();
        }
        return context;
    }

    WidgetId UiSystem::addScreenMarker( unique_ptr<Widget> widget )
    {
        if ( widget == nullptr )
            return kInvalidWidgetId;
        UiScreen* pMarkers = findScreen( _markerScreen );
        if ( pMarkers == nullptr || pMarkers->isClosing() )
        {
            unique_ptr<CanvasPanel> root = make_unique<CanvasPanel>();
            root->setVisibility( WidgetVisibility::SelfHitTestInvisible ); // 빈 곳 클릭은 아래로(게임으로)
            UiScreenDesc desc{};
            desc._layer       = UiLayer::Hud;
            desc._bTakesFocus = false;
            desc._bShowCursor = false;
            _markerScreen     = pushScreen( sw::make_unique<UiScreen>( desc, std::move( root ) ) );
            pMarkers          = findScreen( _markerScreen );
        }
        PanelWidget* pRoot = castTo<PanelWidget>( pMarkers->getTree().getRoot() );
        return pRoot != nullptr ? pRoot->addChild( std::move( widget ) )->getId() : kInvalidWidgetId;
    }

    void UiSystem::removeScreenMarker( WidgetId widget )
    {
        UiScreen*    pMarkers = findScreen( _markerScreen );
        Widget*      pWidget  = pMarkers != nullptr ? pMarkers->getTree().findWidgetById( widget ) : nullptr;
        PanelWidget* pRoot    = pWidget != nullptr ? pWidget->getParent() : nullptr;
        if ( pRoot == nullptr )
            return;
        (void)pRoot->removeChild( pWidget ); // 부모에서 찾은 위젯이라 늘 빠진다
        if ( pRoot->getChildCount() == 0 )
        {
            closeScreen( _markerScreen );
            _markerScreen = kInvalidUiScreenHandle;
        }
    }

    Widget* UiSystem::findScreenMarker( WidgetId widget ) const
    {
        const UiScreen* pMarkers = findScreen( _markerScreen );
        return pMarkers != nullptr ? pMarkers->getTree().findWidgetById( widget ) : nullptr;
    }

    void UiSystem::registerWidgetComponent( WidgetComponent& component )
    {
        for ( const WidgetComponent* pExisting : _listWidgetComponent )
        {
            if ( pExisting == &component )
                return;
        }
        _listWidgetComponent.push_back( &component );
    }

    void UiSystem::unregisterWidgetComponent( WidgetComponent& component )
    {
        for ( size_t index = 0; index < _listWidgetComponent.size(); ++index )
        {
            if ( _listWidgetComponent[index] == &component )
            {
                _listWidgetComponent.erase( _listWidgetComponent.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    void UiSystem::collectWorldCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const
    {
        for ( const WidgetComponent* pComponent : _listWidgetComponent )
        {
            pComponent->appendWorldCanvas( inoutListTarget );
        }
        // 오프스크린 화면(에디터 미리보기) — 불투명 바탕으로 지운다(ImGui 이미지가 알파를 섞지 않게).
        for ( const OffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._viewport._physicalSize._x <= 0.0f || offscreen._viewport._physicalSize._y <= 0.0f )
                continue;
            CanvasTargetDrawList& target = inoutListTarget.emplace_back();
            target._targetPath           = offscreen._targetPath;
            target._list                 = offscreen._canvas;
            target._list._targetSize     = offscreen._viewport._physicalSize;
            target._clearColor           = float4{ 0.06f, 0.07f, 0.09f, 1.0f };
            target._contentRevision      = offscreen._revision;
        }
    }

    void UiSystem::syncThemeSetting()
    {
        const string& setting = gv_uiTheme;
        if ( setting == _themeSetting )
            return;
        _themeSetting = setting;
        if ( setting.empty() || _themeCatalog._listTheme.empty() )
            return;
        // 실행 중에 바꾼 값 — 모르는 이름은 setTheme 이 경고하고 지금 테마를 둔다(게임 테마 목록이 그 이름을 두지 않았다).
        (void)setTheme( hashed_string( setting ) );
    }

    void UiSystem::syncBenchScreen()
    {
        const uint32         wanted  = gv_benchUiWidgets > 0 ? static_cast<uint32>( gv_benchUiWidgets ) : 0u;
        const UiBenchScreen* pBench  = static_cast<const UiBenchScreen*>( findScreen( _benchScreen ) );
        const uint32         current = pBench != nullptr ? pBench->getCellCount() : 0u;
        if ( wanted == current )
            return;
        if ( pBench != nullptr )
            closeScreen( _benchScreen );
        _benchScreen = wanted > 0 ? pushScreen( UiBenchScreen::create( wanted ) ) : kInvalidUiScreenHandle;
    }

    void UiSystem::syncDemoScreen()
    {
        const bool bOpen = _demoScreen != kInvalidUiScreenHandle && findScreen( _demoScreen ) != nullptr;
        if ( gv_uiDemo == bOpen )
            return;
        if ( bOpen )
        {
            closeScreen( _demoScreen );
            _demoScreen = kInvalidUiScreenHandle;
            return;
        }
        _demoScreen = pushScreen( UiDemoScreen::create() );
        setInputMode( UiInputMode::Navigation );
        // 알림 · 행동 글리프도 같이 보인다(오버레이 층 — 스크린샷 확인).
        UiNotificationDesc hint{};
        hint._text            = "[action=UI.Accept] Select";
        hint._durationSeconds = 600.0f;
        hint._kind            = UiNotificationKind::Hint;
        _notifications.post( hint );
    }

    void UiSystem::syncOptionsMenuSwitch()
    {
        const bool bOpen = _optionsSwitchScreen != kInvalidUiScreenHandle && findScreen( _optionsSwitchScreen ) != nullptr;
        if ( gv_uiOptionsMenu == bOpen )
            return;
        if ( bOpen )
        {
            closeScreen( _optionsSwitchScreen );
            _optionsSwitchScreen = kInvalidUiScreenHandle;
            return;
        }
        _optionsSwitchScreen = OptionsMenuScreen::open( *this );
        if ( _optionsSwitchScreen == kInvalidUiScreenHandle )
        {
            gv_uiOptionsMenu = false; // 열 수 없다(설정 서비스 없음) — 프레임마다 다시 시도하지 않는다
            return;
        }
        setInputMode( UiInputMode::Navigation );
    }

    void UiSystem::setPauseMenuDocument( string_view documentPath )
    {
        _pauseMenuDocument = documentPath;
        syncInputLayers( getActiveScreen() );
    }

    UserSettingsManager* UiSystem::findUserSettings() const
    {
        return _pUserSettings != nullptr ? _pUserSettings : engine::getBoundEngineServices()._pUserSettingsManager;
    }

    void UiSystem::processPauseAction()
    {
        if ( _uiInputMap == nullptr || _pauseMenuDocument.empty() || _pInput->getKeyboardFocus() == InputKeyboardFocus::DevConsole )
            return;
        const hashed_string pauseAction( UiActionName::kPause );
        if ( _uiInputMap->wasActionTriggered( pauseAction ) == false || _consumption.isActionConsumed( *_uiInputMap, *_pInput, pauseAction ) )
            return;
        // 열지 못해도 먹는다 — 일시정지를 누른 Esc 가 게임의 마우스 잠금 토글로 새지 않게(오류는 문서 짓기가 남겼다).
        consumeAction( *_uiInputMap, pauseAction );
        (void)openScreen<PauseMenuScreen>( _pauseMenuDocument ); // 실패는 문서 짓기가 남긴다(위 설명)
    }

    void UiSystem::syncInputLayers( const UiScreen* pActive )
    {
        if ( _uiInputMap == nullptr )
            return;
        _uiInputMap->setLayerEnabled( UiSystemInternal::kUiLayerName, pActive != nullptr );
        _uiInputMap->setLayerEnabled( UiSystemInternal::kUiGlobalLayerName, pActive == nullptr && _pauseMenuDocument.empty() == false );
    }

    void UiSystem::paintScreens()
    {
        SW_PROFILE_SCOPE( "GT.Ui.Paint" );
        const UiPaintContext context = makePaintContext();
        _canvasScratch.clear();
        _canvasScratch._targetSize = _viewport._physicalSize;
        CanvasPainter painter( _canvasScratch, context._uiScale );
        uint32        paintedCount = 0;
        for ( const unique_ptr<UiScreen>& screen : _listScreen )
        {
            if ( _bOnScreenSuppressed == SW_TRUE )
                break; // 그리기 목록을 비운다 — 아래 같은 내용 확인이 바뀐 것으로 보고 번호를 올린다
            WidgetTree& tree = screen->getTree();
            paintedCount += UiPaintPass::paint( tree, context, painter, _canvasScratch );
            // 포커스 테두리 — 패드 · 키보드로 다룰 때만(언리얼 CommonUI · 콘솔 게임과 같다), 그 화면 위 · 위 화면 아래.
            if ( _inputMode != UiInputMode::Navigation || _focus.getFocusedTree() != &tree )
                continue;
            const Widget* pFocused = tree.findWidgetById( _focus.getFocusedWidget() );
            if ( pFocused != nullptr && pFocused->isVisible() )
                UiPaintPass::paintFocusRing( *pFocused, painter );
        }
        SW_PROFILE_COUNT( "Ui.PaintWidgets", paintedCount );
        SW_PROFILE_COUNT( "Ui.CanvasQuads", _canvasScratch._listQuad.size() );
        if ( _canvasScratch.isSameContent( _canvas ) )
            return;
        _canvas.clear();
        _canvas.appendDrawList( _canvasScratch );
        _canvas._targetSize = _canvasScratch._targetSize;
        ++_canvasRevision;
    }

    UiViewport UiSystem::computeViewport( const float2& physicalSize, float32 contentScale ) const
    {
        return UiScaleUtil::makeViewport( _scaleSettings, physicalSize, gv_uiScale, contentScale, gv_uiDebugSafeZone );
    }

    UiLayoutContext UiSystem::makeLayoutContext() const
    {
        UiLayoutContext context{};
        context._pTextLayout   = _textLayout.get();
        context._safeInsets    = _viewport._safeInsets;
        context._viewportSize  = _viewport._size;
        context._uiScale       = _viewport._uiScale;
        context._textScale     = gv_uiTextScale > 0.0f ? static_cast<float32>( gv_uiTextScale ) : 1.0f;
        context._bRightToLeft  = UiLayoutPass::isCultureRightToLeft( _pLocalization );
        context._pLocalization = findLocalization();
        context._textRevision  = context._pLocalization != nullptr ? context._pLocalization->getTextRevision() : 0;
        context._pActionGlyphs = &_actionGlyphs;
        return context;
    }
} // namespace sw
