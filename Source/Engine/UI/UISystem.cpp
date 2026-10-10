#include "pch.h"

#include "Engine/UI/UISystem.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/TextLayout.h"
#include "Engine/UI/Animation/UIStyleTransition.h"
#include "Engine/UI/Base/UIEventRouter.h"
#include "Engine/UI/Base/UINavigationSolver.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Binding/UIBindingSet.h"
#include "Engine/UI/Binding/UIViewModel.h"
#include "Engine/UI/Debug/UIBenchScreen.h"
#include "Engine/UI/Debug/UIDemoScreen.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/UILayoutDump.h"
#include "Engine/UI/Screen/OptionsMenuScreen.h"
#include "Engine/UI/Screen/PauseMenuScreen.h"
#include "Engine/UI/Style/UIStylePass.h"
#include "Engine/UI/Style/UIStyleSet.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/World/WidgetComponent.h"
#include "Engine/UserSettings/UserSettingsVariables.h"
#include "Engine/Utility/GameTimeScale.h"

namespace sw
{
    SW_LOG_CALLER( "UISystem" );

    namespace
    {
        struct UISystemInternal
        {
            /** @brief 마우스 버튼 중 UI 가 사건으로 받는 것(왼쪽 · 오른쪽 · 가운데). */
            static constexpr MouseButton kArrPointerButton[] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };
            /** @brief UI 행동 맵의 레이어 이름입니다(활성 화면이 있을 때만 켠다). */
            static constexpr utf8 kUILayerName[]       = "UI";
            static constexpr utf8 kUIGlobalLayerName[] = "UIGlobal"; ///< 화면이 없을 때의 UI 행동(일시정지)
            /** @brief 스틱 탐색이 한 칸 옮기는 기울기 문턱입니다. */
            static constexpr float32 kStickNavigateMagnitude = 0.5f;
            /** @brief 패드 축 사건이 "패드를 쓴다" 로 세는 크기입니다(손을 떼어 둔 스틱의 떨림은 세지 않는다). */
            static constexpr float32 kGamepadAxisActivity = 0.5f;

            /** @struct NavigationAction @brief 탐색 행동 이름과 방향입니다. */
            struct NavigationAction
            {
                const utf8*           _pAction;
                UINavigationDirection _direction;
            };
            static constexpr NavigationAction kArrNavigationAction[] = {
                {   UIActionName::kNavigateUp,       UINavigationDirection::Up},
                { UIActionName::kNavigateDown,     UINavigationDirection::Down},
                { UIActionName::kNavigateLeft,     UINavigationDirection::Left},
                {UIActionName::kNavigateRight,    UINavigationDirection::Right},
                {    UIActionName::kFocusNext,     UINavigationDirection::Next},
                {UIActionName::kFocusPrevious, UINavigationDirection::Previous},
            };

            /** @brief 원시 사건 하나가 가리키는 입력 방식입니다. 방식과 무관한 사건이면 false 입니다. */
            [[nodiscard]] static bool tryGetInputMode( const RawInputEvent& rawEvent, UIInputMode& outMode )
            {
                switch ( rawEvent._type )
                {
                    case RawInputEventType::KeyDown:
                    case RawInputEventType::GamepadButtonDown:
                    {
                        outMode = UIInputMode::Navigation;
                        return true;
                    }
                    case RawInputEventType::GamepadAxis:
                    {
                        if ( MathUtil::abs( rawEvent._payload._gamepadData._axisValue ) < kGamepadAxisActivity )
                            return false;
                        outMode = UIInputMode::Navigation;
                        return true;
                    }
                    case RawInputEventType::MouseMove:
                    case RawInputEventType::MouseButtonDown:
                    case RawInputEventType::MouseWheel:
                    {
                        outMode = UIInputMode::Pointer;
                        return true;
                    }
                    default:
                    {
                        return false;
                    }
                }
            }

            /** @brief 그리기 순서의 앞뒤입니다 — 층이 먼저, 같은 층이면 쌓인 순서. */
            static bool isDrawnBefore( const UIScreen& lhs, const UIScreen& rhs )
            {
                if ( lhs.getDesc()._layer != rhs.getDesc()._layer )
                    return static_cast<uint8>( lhs.getDesc()._layer ) < static_cast<uint8>( rhs.getDesc()._layer );
                return false; // 같은 층이면 뒤에 올린 것이 위 — 끼울 자리는 같은 층의 끝
            }

            /** @brief vtable 이 범위 안인 화면 · 위젯이 하나라도 있으면 true 입니다. */
            static bool usesCodeWithin( const UIScreen& screen, const void* pBegin, const void* pEnd )
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
    UISystem::UISystem()
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
        , _stickDirection{ UINavigationDirection::Next }
        , _activeScreen{ kInvalidUIScreenHandle }
        , _demoScreen{ kInvalidUIScreenHandle }
        , _benchScreen{ kInvalidUIScreenHandle }
        , _markerScreen{ kInvalidUIScreenHandle }
        , _listWidgetComponent{}
        , _listTickScratch{}
        , _optionsMenuDocument{ "engine/ui/options.ui.xml" }
        , _pauseMenuDocument{}
        , _optionsSwitchScreen{ kInvalidUIScreenHandle }
        , _nextScreenHandle{ 1 }
        , _nextPushOrder{ 0 }
        , _textRevision{ 0 }
        , _inputMode{ UIInputMode::Pointer }
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

    UISystem::~UISystem()
    {
        shutdown();
    }

    bool UISystem::initialize( InputManager& inputManager, FontSystem* pFontSystem, string_view uiInputMapPath )
    {
        _pInput                    = &inputManager;
        _pFontSystem               = pFontSystem;
        _actionGlyphs._pInput      = &inputManager;
        _actionGlyphs._pUIInputMap = nullptr;
        _documentCache.setReloadedHandler( SW_DELEGATE_METHOD( UIAssetReloadedDelegate, &UISystem::onDocumentReloaded, this ) );
        _styleSheetCache.setReloadedHandler( SW_DELEGATE_METHOD( UIAssetReloadedDelegate, &UISystem::onStyleSheetReloaded, this ) );
        _textLayout    = pFontSystem != nullptr ? make_unique<TextLayoutEngine>( *pFontSystem ) : nullptr;
        _bPointerKnown = SW_FALSE;
        _consumption.clear();
        _pInput->setTextInputCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UISystem::onTextInput, this ), InputKeyboardFocus::UI );
        _pInput->setTextCompositionCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &UISystem::onTextComposition, this ), InputKeyboardFocus::UI );
        if ( uiInputMapPath.empty() )
            return true;
        _uiInputMap = make_unique<InputMap>();
        // 글 입력 칸이 키보드 포커스(UI)를 쥔 동안에도 Back · Tab 은 UI 가 받는다 — 그 밖의 행동은 UISystem 이 거른다.
        _uiInputMap->setKeyboardFocusIgnored( true );
        _uiInputMap->setSuppressBaseActionOnChord( true ); // Shift+Tab 이 Tab 도 함께 발화하지 않게
        if ( _uiInputMap->loadFromResource( uiInputMapPath ) == false )
        {
            SW_LOG_ERROR( "[UI] UI input map '%#' could not be loaded - UI navigation actions stay unbound", uiInputMapPath );
            _uiInputMap.reset();
            return false;
        }
        _uiInputMap->setInputManager( _pInput );
        _actionGlyphs._pUIInputMap = _uiInputMap.get();
        syncInputLayers( getActiveScreen() );
        return true;
    }

    void UISystem::shutdown()
    {
        _subtitles.clear();
        _notifications.clear();
        // 위젯 컴포넌트가 이 시스템보다 오래 남을 수 있다 — 등록 · 마커를 잊게 한다(그 뒤 소멸자가 이 시스템을 부르지 않게).
        for ( WidgetComponent* pComponent : _listWidgetComponent )
        {
            pComponent->forgetUISystem();
        }
        _listWidgetComponent.clear();
        _markerScreen = kInvalidUIScreenHandle;
        _demoScreen   = kInvalidUIScreenHandle;
        _benchScreen  = kInvalidUIScreenHandle;
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
            if ( _pInput->getKeyboardFocus() == InputKeyboardFocus::UI )
                _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
            _pInput->setTextInputCallback( {}, InputKeyboardFocus::UI );
            _pInput->setTextCompositionCallback( {}, InputKeyboardFocus::UI );
        }
        _uiInputMap.reset();
        _consumption.clear();
        _textLayout.reset();
        _pInput      = nullptr;
        _pFontSystem = nullptr;
    }

    void UISystem::processInput( float32 deltaSeconds )
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

    void UISystem::update( float32 deltaSeconds, const UIViewport& viewport )
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
            SW_PROFILE_SCOPE( "GT.UI.Animate" );
            for ( size_t index = 0; index < _listScreen.size(); ++index )
            {
                _listScreen[index]->_animationPlayer.tick( deltaSeconds );
                (void)UIStyleTransition::update( _listScreen[index]->getTree(), deltaSeconds ); // 스타일 전환 — 새로 바뀐 것은 아래 스타일 단계가 시작한다
            }
            applyPendingCloses(); // 닫기 애니메이션이 끝난 화면
        }
        refreshInputGlyphs();
        // 스타일 — 스타일 더러운 위젯만 계산된 스타일을 다시 정한다(레이아웃 앞 — 여백 · 글꼴이 크기를 바꾼다).
        {
            SW_PROFILE_SCOPE( "GT.UI.Style" );
            const bool bNavigation   = _inputMode == UIInputMode::Navigation;
            uint32     restyledCount = 0;
            for ( const unique_ptr<UIScreen>& screen : _listScreen )
            {
                if ( screen->_styleSet != nullptr )
                    restyledCount += UIStylePass::update( screen->getTree(), *screen->_styleSet, bNavigation );
            }
            SW_PROFILE_COUNT( "UI.StyleWidgets", restyledCount );
        }
        // 화면 마커 — 게임 틱 · 트랜스폼 적용 뒤의 월드 점을 이번 뷰포트로 투영한다(레이아웃 앞 — 같은 프레임에 놓인다).
        for ( WidgetComponent* pComponent : _listWidgetComponent )
        {
            pComponent->updateScreenMarker( _viewport );
        }
        // 레이아웃 — 화면 트리마다 더러운 뿌리만 다시 잰다. 화면마다 뷰포트 전체가 루트 사각형이다.
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
        // 그리기 — 더러운 위젯만 다시 칠하고 화면마다 캐시를 이어 붙인다(스타일 걷기(5-2) 전까지 kStyle 도 그리기가 비운다).
        paintScreens();
        // 월드 공간 위젯 — 컴포넌트마다 자기 트리를 렌더 텍스처 크기(배율 1)로 놓고 칠한다.
        if ( _listWidgetComponent.empty() == false )
        {
            const UILayoutContext layout = makeLayoutContext();
            const UIPaintContext  paint  = makePaintContext();
            for ( WidgetComponent* pComponent : _listWidgetComponent )
            {
                pComponent->updateWorldCanvas( layout, paint );
            }
        }
        updateOffscreenScreens();
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
        if ( screen->_animationPlayer.findAnimation( hashed_string( UIAnimation::kOpenName ) ) != nullptr )
            (void)screen->_animationPlayer.play( hashed_string( UIAnimation::kOpenName ) );
        // 같은 층의 끝(그 층에서 맨 위)에 끼운다.
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
        // 닫기 애니메이션 — 끝날 때까지 지우지 않는다(`applyPendingCloses`). 여는 애니메이션은 멈춘다.
        UIAnimationPlayer& player = pScreen->_animationPlayer;
        if ( player.findAnimation( hashed_string( UIAnimation::kCloseName ) ) != nullptr )
        {
            player.stop( hashed_string( UIAnimation::kOpenName ) );
            (void)player.play( hashed_string( UIAnimation::kCloseName ) );
        }
    }

    bool UISystem::tween( WidgetId widget, string_view propertyPath, string_view endValue, float32 duration, BlendCurve curve )
    {
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( screen->getTree().findWidgetById( widget ) != nullptr )
                return screen->_animationPlayer.tween( widget, propertyPath, endValue, duration, curve );
        }
        SW_LOG_WARNING( "[UI] Tween of '%#': widget %# is not in any screen", string( propertyPath ).c_str(), widget );
        return false;
    }

    unique_ptr<Widget> UISystem::instantiateDocument( string_view documentPath, UIScreenDesc& outDesc, vector<UIBindingDesc>& outListBinding,
                                                      vector<string>& outListStyleSheet )
    {
        string                                  error;
        const shared_ptr<const UIDocumentAsset> document = _documentCache.findOrLoad( documentPath, error );
        if ( document == nullptr )
        {
            SW_LOG_ERROR( "[UI] Screen document is not loaded: %#", error.c_str() );
            return {};
        }
        unique_ptr<Widget> root = UIDocumentLoader::instantiate( *document, _documentCache, outListBinding, error );
        if ( root == nullptr )
        {
            SW_LOG_ERROR( "[UI] Screen document cannot be built: %#", error.c_str() );
            return {};
        }
        outDesc = document->_screenDesc;
        // 스타일 시트 — 문서 것 다음 조각 것(너비 우선, 이미 본 문서 · 시트는 건너뛴다). 조각은 짓기가 이미 캐시에 올렸다.
        vector<string> listDocument{ document->_path };
        for ( size_t index = 0; index < listDocument.size(); ++index )
        {
            string                                  fragmentError;
            const shared_ptr<const UIDocumentAsset> current = _documentCache.findOrLoad( listDocument[index], fragmentError );
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
        // 애니메이션은 문서의 것(캐시에 이미 있다 — 방금 지었다).
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
        for ( OffscreenScreen& offscreen : _listOffscreen )
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
        // 묶음은 테마 시트와 문서 시트를 다 든다 — 그 경로의 시트를 든 화면만 새 시트로 다시 건다.
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
        for ( OffscreenScreen& offscreen : _listOffscreen )
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
        OffscreenScreen& offscreen = _listOffscreen.emplace_back();
        offscreen._screen          = sw::make_unique<UIScreen>( desc, std::move( root ) );
        offscreen._targetPath      = hashed_string( targetPath );
        UIScreen& screen           = *offscreen._screen;
        screen._documentPath       = FileUtil::normalizePath( documentPath );
        screen._listBinding        = std::move( listBinding );
        screen._listStyleSheet     = std::move( listStyleSheet );
        screen._handle             = _nextScreenHandle++;
        rebuildStyleSet( screen );
        return screen._handle;
    }

    void UISystem::closeOffscreenScreen( UIScreenHandle handle )
    {
        for ( size_t index = 0; index < _listOffscreen.size(); ++index )
        {
            if ( _listOffscreen[index]._screen->_handle != handle )
                continue;
            _listOffscreen.erase( _listOffscreen.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    UIScreen* UISystem::findOffscreenScreen( UIScreenHandle handle ) const
    {
        for ( const OffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->_handle == handle )
                return offscreen._screen.get();
        }
        return nullptr;
    }

    void UISystem::setOffscreenView( UIScreenHandle handle, const UIViewport& viewport, float32 textScale, const hashed_string& theme )
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

    void UISystem::updateOffscreenScreens()
    {
        for ( OffscreenScreen& offscreen : _listOffscreen )
        {
            const UIViewport& viewport = offscreen._viewport;
            if ( viewport._physicalSize._x <= 0.0f || viewport._physicalSize._y <= 0.0f )
                continue;
            UIScreen&   screen = *offscreen._screen;
            WidgetTree& tree   = screen.getTree();
            if ( screen._styleSet != nullptr )
                (void)UIStylePass::update( tree, *screen._styleSet, false );
            // 미리보기는 애니메이션 단계를 돌지 않는다 — 방금 시작한 스타일 전환을 끝 값으로 바로 맞춘다(옛 값에 멈추지 않게).
            (void)UIStyleTransition::update( tree, MathUtil::kMaxFloat );
            // 화면 UI 와 같은 문맥에 이 화면의 뷰포트 · 글자 배율만 바꿔 쓴다(배율 · 글자 배율이 바뀌면 루트부터 다시 잰다 — 트리가 지난 값을 든다).
            UILayoutContext layout = makeLayoutContext();
            layout._viewportSize   = viewport._size;
            layout._safeInsets     = viewport._safeInsets;
            layout._uiScale        = viewport._uiScale;
            layout._textScale      = offscreen._textScale;
            (void)UILayoutPass::update( tree, layout );
            UIPaintContext paint = makePaintContext();
            paint._uiScale       = viewport._uiScale;
            paint._textScale     = offscreen._textScale;
            offscreen._scratch.clear();
            offscreen._scratch._targetSize = viewport._physicalSize;
            CanvasPainter painter( offscreen._scratch, viewport._uiScale );
            (void)UIPaintPass::paint( tree, paint, painter, offscreen._scratch );
            if ( offscreen._scratch.isSameContent( offscreen._canvas ) && offscreen._scratch._targetSize == offscreen._canvas._targetSize )
                continue;
            std::swap( offscreen._canvas, offscreen._scratch );
            ++offscreen._revision;
        }
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
                if ( std::find( listDocument.begin(), listDocument.end(), fragment ) == listDocument.end() )
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
            (void)UIStylePass::update( tree, *screen._styleSet, _inputMode == UIInputMode::Navigation );
        (void)UILayoutPass::update( tree, makeLayoutContext() );

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
        _themeCatalog = catalog;
        // 사용자 설정(gv_uiTheme)의 테마가 목록에 있으면 그것, 없으면 목록의 기본 — 테마를 두지 않은 게임 목록에서 설정 값은 조용히 기본이 된다.
        _themeSetting = gv_uiTheme;
        if ( _themeSetting.empty() == false && _themeCatalog.findTheme( hashed_string( _themeSetting ) ) != nullptr )
        {
            (void)setTheme( hashed_string( _themeSetting ) );
            return;
        }
        if ( _themeCatalog._defaultTheme.empty() == false && setTheme( _themeCatalog._defaultTheme ) == false )
            SW_LOG_ERROR( "[UI] Default UI theme '%#' is not in the theme catalog", _themeCatalog._defaultTheme.c_str() );
    }

    bool UISystem::setTheme( const hashed_string& name )
    {
        if ( _themeCatalog.findTheme( name ) == nullptr )
        {
            SW_LOG_WARNING( "[UI] Unknown UI theme '%#'", name.c_str() );
            return false;
        }
        _themeName = name;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            rebuildStyleSet( *screen );
        }
        for ( OffscreenScreen& offscreen : _listOffscreen )
        {
            rebuildStyleSet( *offscreen._screen, offscreen._theme );
        }
        return true;
    }

    void UISystem::appendStyleSheets( const vector<string>& listPath, vector<shared_ptr<const UIStyleSheetAsset>>& inoutListSheet )
    {
        for ( const string& path : listPath )
        {
            string                                    error;
            const shared_ptr<const UIStyleSheetAsset> sheet = _styleSheetCache.findOrLoad( path, error );
            if ( sheet == nullptr )
            {
                SW_LOG_ERROR( "[UI] Style sheet is not loaded: %#", error.c_str() );
                continue;
            }
            inoutListSheet.push_back( sheet );
        }
    }

    void UISystem::rebuildStyleSet( UIScreen& screen, const hashed_string& themeName )
    {
        vector<shared_ptr<const UIStyleSheetAsset>> listSheet;
        const hashed_string&                        theme  = themeName.empty() ? _themeName : themeName;
        const UIThemeDesc*                          pTheme = theme.empty() ? nullptr : _themeCatalog.findTheme( theme );
        if ( pTheme != nullptr )
            appendStyleSheets( pTheme->_listStyleSheet, listSheet );
        appendStyleSheets( screen._listStyleSheet, listSheet );
        if ( screen._styleSet == nullptr )
            screen._styleSet = make_unique<UIStyleSet>();
        screen._styleSet->setSheets( std::move( listSheet ) );
        // 트리 전체를 다시 맞춘다 — 루트의 kStyle 은 계산이 바뀐 만큼 자손으로 내려간다. 규칙이 바뀌었으니 자손도 모두 표시한다.
        vector<Widget*> listWidget;
        screen.getTree().collectWidgetsInDocumentOrder( listWidget );
        for ( Widget* pWidget : listWidget )
        {
            pWidget->invalidate( WidgetDirty::kStyle );
        }
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

    bool UISystem::isActionConsumed( const InputMap& inputMap, const hashed_string& action ) const
    {
        return _pInput != nullptr && _consumption.isActionConsumed( inputMap, *_pInput, action );
    }

    void UISystem::consumeAction( const InputMap& inputMap, const hashed_string& action )
    {
        if ( _pInput != nullptr )
            _consumption.consumeAction( inputMap, *_pInput, action );
    }

    void UISystem::setInputMode( UIInputMode mode )
    {
        if ( _inputMode == mode )
            return;
        _inputMode = mode;
        // :focus-visible 은 입력 방식을 따른다 — 포커스 위젯을 다시 맞춘다.
        WidgetTree* pFocusTree = _focus.getFocusedTree();
        Widget*     pFocused   = pFocusTree != nullptr ? pFocusTree->findWidgetById( _focus.getFocusedWidget() ) : nullptr;
        if ( pFocused != nullptr )
            pFocused->invalidate( WidgetDirty::kStyle );
        UIScreen* pActive = getActiveScreen();
        if ( mode == UIInputMode::Navigation && pActive != nullptr && _focus.getFocusedTree() != &pActive->getTree() )
            restoreFocus( *pActive );
    }

    uint32 UISystem::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        uint32 closedCount = 0;
        // 내려가는 모듈의 변환기 함수 · 뷰모델은 바인딩이 놓는다(화면은 산다 — 다음 바인딩 단계가 다시 건다).
        const bool bConverterRemoved = _bindingConverters.removeCodeWithin( pBegin, pEnd ) > 0;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            const UIViewModel* pViewModel = screen->getViewModel();
            if ( pViewModel != nullptr && IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( pViewModel ), pBegin, pEnd ) )
            {
                SW_LOG_WARNING( "[UI] Screen %# released its view model for module reload - the game sets it again after the reload", screen->_handle );
                screen->setViewModel( nullptr );
            }
            else if ( bConverterRemoved )
                screen->getBindingSet().markRebind();
        }
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            if ( UISystemInternal::usesCodeWithin( *_listScreen[index - 1], pBegin, pEnd ) == false )
                continue;
            const UIScreen& screen = *_listScreen[index - 1];
            SW_LOG_WARNING( "[UI] Screen %# closed for module reload - it holds widget code from the unloading module", screen._handle );
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

    void UISystem::updateBindings()
    {
        SW_PROFILE_SCOPE( "GT.UI.Bind" );
        UIBindingContext context{};
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
        uint32 polledCount  = 0;
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
        return _pLocalization != nullptr ? _pLocalization : engine::getBoundEngineServices()._pLocalizationManager;
    }

    void UISystem::refreshInputGlyphs()
    {
        if ( _pInput == nullptr )
            return;
        const InputGlyphStyle style = _pInput->getActiveGlyphStyle();
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

    void UISystem::processPointer()
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
        UIPointerEvent event{};
        event._position      = position;
        event._delta         = _bPointerKnown == SW_TRUE ? float2{ position._x - _lastPointerPosition._x, position._y - _lastPointerPosition._y } : float2{};
        const bool bMoved    = _bPointerKnown == SW_FALSE || event._delta._x != 0.0f || event._delta._y != 0.0f;
        _lastPointerPosition = position;
        _bPointerKnown       = SW_TRUE;
        if ( bMoved )
        {
            event._kind = UIPointerEventKind::Move;
            (void)dispatchPointerEvent( event );
        }
        for ( const MouseButton button : UISystemInternal::kArrPointerButton )
        {
            event._button = button;
            // 위젯이 처리한 버튼은 먹는다 — 게임의 "왼쪽 클릭 = 사격" 이 메뉴 클릭을 같이 받지 않게. 빈 곳 클릭은 게임으로 간다.
            if ( pMouse->wasButtonPressed( button ) )
            {
                event._kind = UIPointerEventKind::Down;
                if ( dispatchPointerEvent( event ) )
                    _consumption.consumeMouseButton( button );
            }
            if ( pMouse->wasButtonReleased( button ) )
            {
                event._kind = UIPointerEventKind::Up;
                if ( dispatchPointerEvent( event ) )
                    _consumption.consumeMouseButton( button );
            }
        }
        const float32 wheel = pMouse->getMouseWheel();
        if ( wheel != 0.0f )
        {
            event._kind  = UIPointerEventKind::Wheel;
            event._wheel = wheel;
            (void)dispatchPointerEvent( event );
        }
    }

    bool UISystem::dispatchPointerEvent( const UIPointerEvent& event )
    {
        // 잡은 위젯이 있으면 그 트리로, 없으면 점 아래 맨 위 화면으로.
        UIScreen* pTarget = nullptr;
        if ( _pointer.getCaptureTree() != nullptr )
        {
            for ( const unique_ptr<UIScreen>& screen : _listScreen )
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
        const UIPointerResult result = _pointer.process( pTarget->getTree(), event );
        if ( result._focusRequest != kInvalidWidgetId && pTarget->takesFocus() )
            (void)_focus.setFocus( pTarget->getTree(), result._focusRequest );
        return result._bHandled == SW_TRUE;
    }

    UIScreen* UISystem::findPointerScreen( const float2& point ) const
    {
        UIWidgetPath path{};
        for ( uint32 index = static_cast<uint32>( _listScreen.size() ); index > 0; --index )
        {
            UIScreen* pScreen = _listScreen[index - 1].get();
            if ( pScreen->isClosing() || pScreen->receivesPointer() == false )
                continue;
            if ( UIEventRouter::hitTest( pScreen->getTree(), point, path ) )
                return pScreen;
            if ( pScreen->blocksLowerInput() )
                return nullptr; // 모달 아래 화면은 클릭을 받지 않는다
        }
        return nullptr;
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

    void UISystem::tickScreens( float32 deltaSeconds )
    {
        // 틱이 화면을 올리고 닫을 수 있다(확인 창 · 키 바인딩 창) — 번호로 돌고 매번 다시 찾는다.
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
        _pointer.forgetTree( screen->getTree() );
        if ( _focus.getFocusedTree() == &screen->getTree() )
            _focus.clearFocus();
        screen->_pUISystem = nullptr;
        // 트리가 지워질 때 포커스 관리자에게 알린다 — 위에서 이미 풀었다.
    }

    void UISystem::refreshActiveScreen()
    {
        // 활성 화면 = 맨 위의 포커스 받는 화면. 막는 화면(모달 · 로딩)을 지나 내려가지 않는다.
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
            if ( pOldActive != nullptr && _focus.getFocusedTree() == &pOldActive->getTree() )
            {
                pOldActive->_lastFocused = _focus.getFocusedWidget();
                _focus.clearFocus();
            }
            _activeScreen = pNewActive != nullptr ? pNewActive->_handle : kInvalidUIScreenHandle;
            if ( pNewActive != nullptr )
                restoreFocus( *pNewActive );
        }

        // UI 행동은 활성 화면이 있을 때만 — HUD 만 있으면 패드 A 는 게임의 것이다.
        if ( _uiInputMap != nullptr )
            syncInputLayers( pNewActive );

        // 게임 정지 — 정지 화면이 하나라도 있으면 요청을 걸어 둔다.
        bool bPause = false;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
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

    void UISystem::updateInputMode()
    {
        UIInputMode mode    = _inputMode;
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
            UIInputMode eventMode = mode;
            if ( UISystemInternal::tryGetInputMode( rawEvent, eventMode ) )
            {
                mode    = eventMode;
                bChange = true;
            }
        }
        if ( bChange )
            setInputMode( mode );
    }

    void UISystem::processActions( float32 deltaSeconds )
    {
        UIScreen* pActive = getActiveScreen();
        if ( pActive == nullptr )
            processPauseAction();
        if ( pActive == nullptr || _uiInputMap == nullptr || pActive->wantsUIActions() == false )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 개발 콘솔이 키보드를 쥐었으면 UI 는 행동을 받지 않는다. 글 입력 칸이 쥐었으면 Back · Tab 만.
        const InputKeyboardFocus keyboardFocus = _pInput->getKeyboardFocus();
        if ( keyboardFocus == InputKeyboardFocus::DevConsole )
            return;
        const bool      bTextFocus = keyboardFocus == InputKeyboardFocus::UI;
        const InputMap& uiMap      = *_uiInputMap;

        for ( const UISystemInternal::NavigationAction& entry : UISystemInternal::kArrNavigationAction )
        {
            const hashed_string action( entry._pAction );
            const bool          bTab = entry._direction == UINavigationDirection::Next || entry._direction == UINavigationDirection::Previous;
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

        const hashed_string arrRoutedAction[] = { hashed_string( UIActionName::kAccept ), hashed_string( UIActionName::kTabNext ),
                                                  hashed_string( UIActionName::kTabPrevious ) };
        for ( const hashed_string& action : arrRoutedAction )
        {
            if ( bTextFocus || uiMap.wasActionTriggered( action ) == false )
                continue;
            // 위젯이 먼저, 아무도 안 먹은 탭 행동은 화면이(옵션 메뉴의 탭 줄).
            const bool bScreenAction = action != hashed_string( UIActionName::kAccept );
            if ( routeAction( *pActive, action, float2{} ) || ( bScreenAction && pActive->onUnhandledAction( action ) ) )
                consumeAction( uiMap, action );
        }

        // 글 입력 칸 — 지우기는 칸이 키보드 포커스를 쥔 동안만(그 밖에서 Backspace 는 UI 의 것이 아니다).
        const hashed_string backspaceAction( UIActionName::kTextBackspace );
        if ( bTextFocus && uiMap.wasActionTriggered( backspaceAction ) && routeAction( *pActive, backspaceAction, float2{} ) )
            consumeAction( uiMap, backspaceAction );

        const hashed_string backAction( UIActionName::kBack );
        if ( uiMap.wasActionTriggered( backAction ) )
        {
            // 위젯이 먼저(펼친 목록을 접는 콤보 상자), 아무도 안 먹으면 화면이(기본은 닫기).
            const bool bHandled = routeAction( *pActive, backAction, float2{} ) || pActive->onBack();
            if ( bHandled )
                consumeAction( uiMap, backAction );
        }
    }

    bool UISystem::handleNavigation( UIScreen& screen, const hashed_string& action, UINavigationDirection direction )
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

    void UISystem::processStickNavigation( UIScreen& screen, float32 deltaSeconds )
    {
        const hashed_string stickAction( UIActionName::kNavigateStick );
        const float2        value = _uiInputMap->getVector2D( stickAction );
        const float32       absX  = MathUtil::abs( value._x );
        const float32       absY  = MathUtil::abs( value._y );
        if ( MathUtil::max( absX, absY ) < UISystemInternal::kStickNavigateMagnitude )
        {
            _bStickHeld = SW_FALSE;
            return;
        }
        // 큰 축 방향 한 칸 — 스틱 위(+y)는 화면 위다.
        const UINavigationDirection direction = absX >= absY ? ( value._x > 0.0f ? UINavigationDirection::Right : UINavigationDirection::Left )
                                                             : ( value._y > 0.0f ? UINavigationDirection::Up : UINavigationDirection::Down );
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

    void UISystem::processScroll( UIScreen& screen )
    {
        const hashed_string scrollAction( UIActionName::kScroll );
        const float2        value = _uiInputMap->getVector2D( scrollAction );
        if ( value._x == 0.0f && value._y == 0.0f )
            return;
        // 포커스가 이 화면에 있으면 포커스 경로(목록 안의 버튼 → 그 목록), 없으면 포인터가 올라간 경로로 보낸다.
        UIWidgetPath path{};
        _focus.makeFocusPath( screen.getTree(), path );
        if ( path.isEmpty() && _pointer.getHoverTree() == &screen.getTree() )
            path = _pointer.getHoverPath();
        if ( routeActionAlong( screen, path, scrollAction, value ) )
            consumeAction( *_uiInputMap, scrollAction );
    }

    bool UISystem::routeAction( UIScreen& screen, const hashed_string& action, const float2& value )
    {
        UIWidgetPath path{};
        _focus.makeFocusPath( screen.getTree(), path );
        return routeActionAlong( screen, path, action, value );
    }

    bool UISystem::routeActionAlong( UIScreen& screen, const UIWidgetPath& path, const hashed_string& action, const float2& value )
    {
        if ( path.isEmpty() )
            return false;
        UIActionEvent event{};
        event._action       = action;
        event._value        = value;
        event._deltaSeconds = _inputDeltaSeconds;
        WidgetId      handler{ kInvalidWidgetId };
        const UIReply reply = UIEventRouter::routeActionEvent( screen.getTree(), path, event, handler );
        if ( reply.isHandled() && reply._focusRequest != kInvalidWidgetId )
            (void)_focus.setFocus( screen.getTree(), reply._focusRequest );
        return reply.isHandled();
    }

    void UISystem::updateKeyboardFocus()
    {
        const Widget* pFocused = nullptr;
        if ( _focus.getFocusedTree() != nullptr )
            pFocused = _focus.getFocusedTree()->findWidgetById( _focus.getFocusedWidget() );
        const bool               bWantsText = pFocused != nullptr && pFocused->supportsTextInput();
        const InputKeyboardFocus current    = _pInput->getKeyboardFocus();
        if ( bWantsText && current == InputKeyboardFocus::Game )
            _pInput->setKeyboardFocus( InputKeyboardFocus::UI );
        else if ( bWantsText == false && current == InputKeyboardFocus::UI )
            _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
    }

    void UISystem::onTextInput( string_view text )
    {
        dispatchTextEvent( text, false );
    }

    void UISystem::onTextComposition( string_view text )
    {
        dispatchTextEvent( text, true );
    }

    void UISystem::dispatchTextEvent( string_view text, bool bComposition )
    {
        WidgetTree* pTree   = _focus.getFocusedTree();
        Widget*     pWidget = pTree != nullptr ? pTree->findWidgetById( _focus.getFocusedWidget() ) : nullptr;
        if ( pWidget == nullptr || pWidget->supportsTextInput() == false )
            return;
        UITextEvent event{};
        event._text         = string{ text };
        event._bComposition = bComposition ? SW_TRUE : SW_FALSE;
        (void)pWidget->onTextEvent( event );
    }

    void UISystem::restoreFocus( UIScreen& screen )
    {
        WidgetTree& tree = screen.getTree();
        if ( screen._lastFocused != kInvalidWidgetId && _focus.setFocus( tree, screen._lastFocused ) )
            return;
        if ( _inputMode != UIInputMode::Navigation || tree.getRoot() == nullptr )
            return;
        const Widget*  pDefault = screen.getDesc()._defaultFocus.empty() ? nullptr : tree.findWidgetByName( screen.getDesc()._defaultFocus );
        const WidgetId target   = pDefault != nullptr ? pDefault->getId() : UINavigationSolver::findFirstFocusable( *tree.getRoot() );
        if ( target != kInvalidWidgetId )
            (void)_focus.setFocus( tree, target );
    }

    UIPaintContext UISystem::makePaintContext() const
    {
        UIPaintContext context{};
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

    WidgetId UISystem::addScreenMarker( unique_ptr<Widget> widget )
    {
        if ( widget == nullptr )
            return kInvalidWidgetId;
        UIScreen* pMarkers = findScreen( _markerScreen );
        if ( pMarkers == nullptr || pMarkers->isClosing() )
        {
            unique_ptr<CanvasPanel> root = make_unique<CanvasPanel>();
            root->setVisibility( WidgetVisibility::SelfHitTestInvisible ); // 빈 곳 클릭은 아래로(게임으로)
            UIScreenDesc desc{};
            desc._layer       = UILayer::HUD;
            desc._bTakesFocus = false;
            desc._bShowCursor = false;
            _markerScreen     = pushScreen( sw::make_unique<UIScreen>( desc, std::move( root ) ) );
            pMarkers          = findScreen( _markerScreen );
        }
        PanelWidget* pRoot = castTo<PanelWidget>( pMarkers->getTree().getRoot() );
        return pRoot != nullptr ? pRoot->addChild( std::move( widget ) )->getId() : kInvalidWidgetId;
    }

    void UISystem::removeScreenMarker( WidgetId widget )
    {
        UIScreen*    pMarkers = findScreen( _markerScreen );
        Widget*      pWidget  = pMarkers != nullptr ? pMarkers->getTree().findWidgetById( widget ) : nullptr;
        PanelWidget* pRoot    = pWidget != nullptr ? pWidget->getParent() : nullptr;
        if ( pRoot == nullptr )
            return;
        (void)pRoot->removeChild( pWidget ); // 부모에서 찾은 위젯이라 늘 빠진다
        if ( pRoot->getChildCount() == 0 )
        {
            closeScreen( _markerScreen );
            _markerScreen = kInvalidUIScreenHandle;
        }
    }

    Widget* UISystem::findScreenMarker( WidgetId widget ) const
    {
        const UIScreen* pMarkers = findScreen( _markerScreen );
        return pMarkers != nullptr ? pMarkers->getTree().findWidgetById( widget ) : nullptr;
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
                _listWidgetComponent.erase( _listWidgetComponent.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    void UISystem::collectWorldCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const
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

    void UISystem::syncThemeSetting()
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

    void UISystem::syncBenchScreen()
    {
        const uint32         wanted  = gv_benchUiWidgets > 0 ? static_cast<uint32>( gv_benchUiWidgets ) : 0u;
        const UIBenchScreen* pBench  = static_cast<const UIBenchScreen*>( findScreen( _benchScreen ) );
        const uint32         current = pBench != nullptr ? pBench->getCellCount() : 0u;
        if ( wanted == current )
            return;
        if ( pBench != nullptr )
            closeScreen( _benchScreen );
        _benchScreen = wanted > 0 ? pushScreen( UIBenchScreen::create( wanted ) ) : kInvalidUIScreenHandle;
    }

    void UISystem::syncDemoScreen()
    {
        const bool bOpen = _demoScreen != kInvalidUIScreenHandle && findScreen( _demoScreen ) != nullptr;
        if ( gv_uiDemo == bOpen )
            return;
        if ( bOpen )
        {
            closeScreen( _demoScreen );
            _demoScreen = kInvalidUIScreenHandle;
            return;
        }
        _demoScreen = pushScreen( UIDemoScreen::create() );
        setInputMode( UIInputMode::Navigation );
        // 알림 · 행동 글리프도 같이 보인다(오버레이 층 — 스크린샷 확인).
        UINotificationDesc hint{};
        hint._text            = "[action=UI.Accept] Select";
        hint._durationSeconds = 600.0f;
        hint._kind            = UINotificationKind::Hint;
        _notifications.post( hint );
    }

    void UISystem::syncOptionsMenuSwitch()
    {
        const bool bOpen = _optionsSwitchScreen != kInvalidUIScreenHandle && findScreen( _optionsSwitchScreen ) != nullptr;
        if ( gv_uiOptionsMenu == bOpen )
            return;
        if ( bOpen )
        {
            closeScreen( _optionsSwitchScreen );
            _optionsSwitchScreen = kInvalidUIScreenHandle;
            return;
        }
        _optionsSwitchScreen = OptionsMenuScreen::open( *this );
        if ( _optionsSwitchScreen == kInvalidUIScreenHandle )
        {
            gv_uiOptionsMenu = false; // 열 수 없다(설정 서비스 없음) — 프레임마다 다시 시도하지 않는다
            return;
        }
        setInputMode( UIInputMode::Navigation );
    }

    void UISystem::setPauseMenuDocument( string_view documentPath )
    {
        _pauseMenuDocument = documentPath;
        syncInputLayers( getActiveScreen() );
    }

    UserSettingsManager* UISystem::findUserSettings() const
    {
        return _pUserSettings != nullptr ? _pUserSettings : engine::getBoundEngineServices()._pUserSettingsManager;
    }

    void UISystem::processPauseAction()
    {
        if ( _uiInputMap == nullptr || _pauseMenuDocument.empty() || _pInput->getKeyboardFocus() == InputKeyboardFocus::DevConsole )
            return;
        const hashed_string pauseAction( UIActionName::kPause );
        if ( _uiInputMap->wasActionTriggered( pauseAction ) == false || _consumption.isActionConsumed( *_uiInputMap, *_pInput, pauseAction ) )
            return;
        // 열지 못해도 먹는다 — 일시정지를 누른 Esc 가 게임의 마우스 잠금 토글로 새지 않게(오류는 문서 짓기가 남겼다).
        consumeAction( *_uiInputMap, pauseAction );
        (void)openScreen<PauseMenuScreen>( _pauseMenuDocument ); // 실패는 문서 짓기가 남긴다(위 설명)
    }

    void UISystem::syncInputLayers( const UIScreen* pActive )
    {
        if ( _uiInputMap == nullptr )
            return;
        _uiInputMap->setLayerEnabled( UISystemInternal::kUILayerName, pActive != nullptr );
        _uiInputMap->setLayerEnabled( UISystemInternal::kUIGlobalLayerName, pActive == nullptr && _pauseMenuDocument.empty() == false );
    }

    void UISystem::paintScreens()
    {
        SW_PROFILE_SCOPE( "GT.UI.Paint" );
        const UIPaintContext context = makePaintContext();
        _canvasScratch.clear();
        _canvasScratch._targetSize = _viewport._physicalSize;
        CanvasPainter painter( _canvasScratch, context._uiScale );
        uint32        paintedCount = 0;
        for ( const unique_ptr<UIScreen>& screen : _listScreen )
        {
            if ( _bOnScreenSuppressed == SW_TRUE )
                break; // 그리기 목록을 비운다 — 아래 같은 내용 확인이 바뀐 것으로 보고 번호를 올린다
            WidgetTree& tree = screen->getTree();
            paintedCount += UIPaintPass::paint( tree, context, painter, _canvasScratch );
            // 포커스 테두리 — 패드 · 키보드로 다룰 때만(언리얼 CommonUI · 콘솔 게임과 같다), 그 화면 위 · 위 화면 아래.
            if ( _inputMode != UIInputMode::Navigation || _focus.getFocusedTree() != &tree )
                continue;
            const Widget* pFocused = tree.findWidgetById( _focus.getFocusedWidget() );
            if ( pFocused != nullptr && pFocused->isVisible() )
                UIPaintPass::paintFocusRing( *pFocused, painter );
        }
        SW_PROFILE_COUNT( "UI.PaintWidgets", paintedCount );
        SW_PROFILE_COUNT( "UI.CanvasQuads", _canvasScratch._listQuad.size() );
        if ( _canvasScratch.isSameContent( _canvas ) )
            return;
        // 바뀐 목록은 맞바꾼다(복사하지 않는다) — 다음 프레임은 지난 목록을 비우고 칠하기 목록으로 쓴다.
        std::swap( _canvas, _canvasScratch );
        ++_canvasRevision;
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
        context._bRightToLeft  = UILayoutPass::isCultureRightToLeft( _pLocalization );
        context._pLocalization = findLocalization();
        context._textRevision  = context._pLocalization != nullptr ? context._pLocalization->getTextRevision() : 0;
        context._pActionGlyphs = &_actionGlyphs;
        return context;
    }
} // namespace sw
