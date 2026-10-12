#include "pch.h"

#include "Editor/Panels/UIPreview/UIPreviewPanel.h"

#include "Core/Container/formatString.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetNavigation.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "UIPreviewPanel" );
    SW_EDITOR_PANEL( UIPreviewPanel, "ui_preview", EditorPanelCategory::Tool, 950 );

    /** @brief 되돌리기 항목이 패널을 가리키는 길입니다. 패널이 사라지면 `_pPanel` 이 비어 항목은 아무것도 하지 않습니다. */
    struct UIPreviewPanel::EditSession
    {
        UIPreviewPanel* _pPanel{ nullptr };

        void apply( uint32 generation, uint32 ordinal, const string& propertyName, const string& text ) const
        {
            if ( _pPanel != nullptr )
                _pPanel->applyEditedText( generation, ordinal, propertyName, text );
        }
    };

    namespace
    {
        struct UIPreviewPanelInternal
        {
            static constexpr float32 kLeftWidth      = 340.0f;
            static constexpr float32 kIndentPerLevel = 12.0f;
            static constexpr float32 kTreeRatio      = 0.38f; ///< 왼쪽 칸에서 위젯 트리가 차지하는 높이 비율(나머지는 인스펙터)
            static constexpr Color   kLayoutColor{ Color::fromBytes( 90, 200, 255, 110 ) };
            static constexpr Color   kSafeZoneColor{ Color::fromBytes( 255, 80, 80, 200 ) };

            /** @brief 시나리오가 누를 이름표 `<접두어>.<접미어>` 를 남깁니다. 테스트를 켰을 때만 문자열을 만듭니다. */
            static void noteMark( const utf8* pPrefix, const utf8* pSuffix )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                fixed_string<constant::kMaxBuffer128> mark;
                formatstring( mark.data(), mark.capacity(), "%#.%#", pPrefix, pSuffix );
                EditorSelfTestMarks::note( mark.c_str() );
            }

            /** @brief 열린 패널의 탐침 값을 읽습니다. 닫혔거나 문서가 없으면 false 입니다. */
            [[nodiscard]] static const UIPreviewProbe* findProbe()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return nullptr;
                const UIPreviewPanel* pPanel = static_cast<const UIPreviewPanel*>( pContext->getPanelManager().findPanel( "ui_preview" ) );
                if ( pPanel == nullptr || pPanel->isOpen() == false || pPanel->getProbe()._bValid == false )
                    return nullptr;
                return &pPanel->getProbe();
            }

            [[nodiscard]] static bool readWidgetCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_widgetCount );
                return true;
            }

            [[nodiscard]] static bool readFirstTextBytes( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_firstTextBytes );
                return true;
            }

            [[nodiscard]] static bool readRightToLeft( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = pProbe->_bRightToLeft ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readHoveredOrdinal( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_hoveredOrdinal );
                return true;
            }

            [[nodiscard]] static bool readHoveredFocusable( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = pProbe->_bHoveredFocusable ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readFocusedOrdinal( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_focusedOrdinal );
                return true;
            }

            [[nodiscard]] static bool readSelectedOpacity( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr || pProbe->_bSelectedValid == false )
                    return false;
                outValue = static_cast<float64>( pProbe->_selectedOpacity );
                return true;
            }

            [[nodiscard]] static bool readDirty( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = pProbe->_bDirty ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readAnimationTime( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_animationTime );
                return true;
            }

            [[nodiscard]] static bool readAnimationPlaying( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = pProbe->_bAnimationPlaying ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readTextureReady( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = pProbe->_bTextureReady ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readReloadedAfterSave( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = pProbe->_bReloadedAfterSave ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readLanguageIndex( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_languageIndex );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_PROBE( editorUIPreviewWidgetCount, "Editor.UIPreviewWidgetCount", "Widgets in the UI Preview tree (no value while the panel is closed or has no document)",
                         &UIPreviewPanelInternal::readWidgetCount );
    SW_AUTOMATION_PROBE( editorUIPreviewFirstTextBytes, "Editor.UIPreviewFirstTextBytes",
                         "UTF-8 bytes of the displayed text of the first text widget in the UI Preview (longer in an expanding pseudo-language)",
                         &UIPreviewPanelInternal::readFirstTextBytes );
    SW_AUTOMATION_PROBE( editorUIPreviewRightToLeft, "Editor.UIPreviewRightToLeft", "1 when the UI Preview root lays out right to left (qps-plocm)",
                         &UIPreviewPanelInternal::readRightToLeft );
    SW_AUTOMATION_PROBE( editorUIPreviewHoveredOrdinal, "Editor.UIPreviewHoveredOrdinal",
                         "Document-order index of the widget under the simulated pointer in the UI Preview, -1 when none", &UIPreviewPanelInternal::readHoveredOrdinal );
    SW_AUTOMATION_PROBE( editorUIPreviewHoveredFocusable, "Editor.UIPreviewHoveredFocusable", "1 when the widget under the simulated pointer can take focus (a button)",
                         &UIPreviewPanelInternal::readHoveredFocusable );
    SW_AUTOMATION_PROBE( editorUIPreviewFocusedOrdinal, "Editor.UIPreviewFocusedOrdinal",
                         "Document-order index of the focused widget in the UI Preview (set by the navigation buttons), -1 when none", &UIPreviewPanelInternal::readFocusedOrdinal );
    SW_AUTOMATION_PROBE( editorUIPreviewSelectedOpacity, "Editor.UIPreviewSelectedOpacity", "Opacity of the widget selected in the UI Preview (no value without a selection)",
                         &UIPreviewPanelInternal::readSelectedOpacity );
    SW_AUTOMATION_PROBE( editorUIPreviewDirty, "Editor.UIPreviewDirty", "1 when the UI Preview has edits not yet written to the document",
                         &UIPreviewPanelInternal::readDirty );
    SW_AUTOMATION_PROBE( editorUIPreviewAnimationTime, "Editor.UIPreviewAnimationTime", "Playback time (seconds) of the animation chosen in the UI Preview, 0 when it is not playing",
                         &UIPreviewPanelInternal::readAnimationTime );
    SW_AUTOMATION_PROBE( editorUIPreviewAnimationPlaying, "Editor.UIPreviewAnimationPlaying", "1 while the animation chosen in the UI Preview is playing (a paused one counts as stopped)",
                         &UIPreviewPanelInternal::readAnimationPlaying );
    SW_AUTOMATION_PROBE( editorUIPreviewTextureReady, "Editor.UIPreviewTextureReady", "1 once the UI Preview render texture is registered with ImGui and the picture is drawn",
                         &UIPreviewPanelInternal::readTextureReady );
    SW_AUTOMATION_PROBE( editorUIPreviewReloadedAfterSave, "Editor.UIPreviewReloadedAfterSave",
                         "1 once the UI Preview rebuilt its tree from the file after the last Save (proves the write reached the document)",
                         &UIPreviewPanelInternal::readReloadedAfterSave );
    SW_AUTOMATION_PROBE( editorUIPreviewLanguageIndex, "Editor.UIPreviewLanguageIndex", "Row of the UI Preview language combo (0 = the game language)",
                         &UIPreviewPanelInternal::readLanguageIndex );

    UIPreviewPanel::UIPreviewPanel()
        : IEditorPanel( false )
        , _listRow{}
        , _listLanguage{}
        , _listBaseline{}
        , _documentPath{ "engine/ui/pause.ui.xml" }
        , _openedDocument{}
        , _targetPath{}
        , _error{}
        , _lastFocusKey{}
        , _language{}
        , _theme{}
        , _animation{}
        , _viewport{}
        , _pointer{}
        , _focus{}
        , _grid{}
        , _pLocalization{}
        , _pSession{ sw::make_shared<EditSession>() }
        , _probe{}
        , _pTextureID{ nullptr }
        , _texture{ 0 }
        , _screen{ kInvalidUIScreenHandle }
        , _selected{ kInvalidWidgetID }
        , _seenRootID{ kInvalidWidgetID }
        , _baselineWidget{ kInvalidWidgetID }
        , _selectedOrdinal{ UIPreviewLogic::kNoOrdinal }
        , _generation{ 0 }
        , _gameTextRevision{ 0 }
        , _languageIndex{ 0 }
        , _resolutionIndex{ 1 }
        , _uiScale{ 1.0f }
        , _textScale{ 1.0f }
        , _safeZone{ 0.0f }
        , _animationScrub{ 0.0f }
        , _lastPointer{}
        , _bShowLayout{ true }
        , _bInteract{ false }
        , _bNavigationMode{ false }
        , _bMutated{ false }
        , _bAnimationPaused{ false }
        , _bAnimationLoop{ false }
        , _bPointerInside{ false }
        , _bPointerPressed{ false }
        , _bApplyLocalization{ false }
        , _bAwaitReload{ false }
        , _bReloadedAfterSave{ false }
    {
        _pSession->_pPanel = this;
    }

    UIPreviewPanel::~UIPreviewPanel()
    {
        _pSession->_pPanel = nullptr; // 쌓여 있는 되돌리기 항목이 사라진 패널을 부르지 않게 한다
    }

    void UIPreviewPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        releaseTexture();
        closePreview();
        _pLocalization.reset(); // 화면을 닫은 뒤에 놓는다(화면이 관리자를 가리킨다)
        _probe = UIPreviewProbe{};
    }

    void UIPreviewPanel::releaseTexture()
    {
        if ( _pTextureID == nullptr )
            return;
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && pContext->getRendererBackend() != nullptr )
            pContext->getRendererBackend()->unregisterTexture( _pTextureID );
        _pTextureID = nullptr;
        _texture    = 0;
    }

    void UIPreviewPanel::closePreview()
    {
        UISystem* pUI = editor::getService<UISystem>();
        if ( pUI != nullptr && _screen != kInvalidUIScreenHandle )
        {
            UIScreen* pScreen = pUI->findOffscreenScreen( _screen );
            if ( pScreen != nullptr )
            {
                _pointer.forgetTree( pScreen->getTree() ); // 트리를 지우기 전에 호버와 포커스를 놓는다
                _focus.forgetTree( pScreen->getTree() );
            }
            pUI->closeOffscreenScreen( _screen );
        }
        _screen = kInvalidUIScreenHandle;
        _openedDocument.clear();
        _targetPath.clear();
        _selected           = kInvalidWidgetID;
        _selectedOrdinal    = UIPreviewLogic::kNoOrdinal;
        _seenRootID         = kInvalidWidgetID;
        _baselineWidget     = kInvalidWidgetID;
        _bMutated           = false;
        _bPointerInside     = false;
        _bPointerPressed    = false;
        _bNavigationMode    = false;
        _bApplyLocalization = true;
        ++_generation;
        _listBaseline.clear();
        clearDocumentDirty();
    }

    void UIPreviewPanel::syncFocusedDocument()
    {
        // 콘텐츠 브라우저에서 `*.ui.xml` 을 두 번 누르면(`EditorAssetCommands::openPath`) 작업공간 포커스가 그 경로가 되고 이 패널이 열린다.
        if ( EditorAssetTypeRegistry::consumeWorkspaceFocusKey( _lastFocusKey, 0 ) == false )
            return;
        const string_view focused = EditorAssetTypeRegistry::matchingFocusedPath( EditorAssetType::UIDocument );
        if ( focused.empty() || focused == string_view{ _documentPath } )
            return;
        if ( isDocumentDirty() )
            SW_LOG_WARNING( "UI Preview: unsaved edits of '%#' are discarded because '%#' was opened", _documentPath.c_str(), string{ focused }.c_str() );
        _documentPath = string{ focused };
        _error.clear();
    }

    void UIPreviewPanel::syncPreview( UISystem& ui )
    {
        _viewport               = UIPreviewLogic::makeViewport( ui.getScaleSettings(), _resolutionIndex, _uiScale, _safeZone );
        const string targetPath = UIPreviewLogic::makeTargetPath( _viewport );
        const bool   bReopen    = _screen == kInvalidUIScreenHandle || ui.findOffscreenScreen( _screen ) == nullptr || _openedDocument != _documentPath;
        if ( bReopen && _documentPath.empty() == false && _error.empty() == false && _openedDocument == _documentPath )
            return; // 같은 문서가 지난번에 짓지 못했다 — 고칠 때까지(경로를 바꾸거나 다시 열기) 프레임마다 다시 시도하지 않는다
        if ( bReopen )
        {
            const string documentPath = _documentPath;
            releaseTexture(); // 크기가 바뀌면 렌더 텍스처도 새것이다
            closePreview();   // 포인터 · 포커스를 놓고 화면을 닫는다(되돌리기 세대도 올린다)
            _screen             = documentPath.empty() ? kInvalidUIScreenHandle : ui.openOffscreenScreen( documentPath, targetPath );
            _openedDocument     = documentPath;
            _targetPath         = targetPath;
            _error              = _screen == kInvalidUIScreenHandle ? SW_LOCKEY( "EditorUIPreview", "BuildFailed" ) : "";
            _bApplyLocalization = true;
        }
        else if ( _targetPath != targetPath )
        {
            // 해상도를 바꿔도 화면(트리, 고친 값)은 그대로 두고 그릴 렌더 텍스처만 새 크기의 것으로 옮긴다.
            releaseTexture();
            ui.setOffscreenTarget( _screen, targetPath );
            _targetPath = targetPath;
        }
        if ( _screen != kInvalidUIScreenHandle )
            ui.setOffscreenView( _screen, _viewport, _textScale, _theme );
    }

    void UIPreviewPanel::syncTree( UIScreen& screen )
    {
        // 문서를 다시 읽으면(저장, 에디터 밖 편집) 핫 리로드가 트리를 새로 지어 위젯 번호가 모두 바뀐다 — 고른 위젯은 문서 순서 번호로 이어 간다.
        WidgetTree&    tree   = screen.getTree();
        const Widget*  pRoot  = tree.getRoot();
        const WidgetID rootID = pRoot != nullptr ? pRoot->getID() : kInvalidWidgetID;
        if ( rootID == _seenRootID )
            return;
        const bool bRebuilt = _seenRootID != kInvalidWidgetID;
        _seenRootID         = rootID;
        _pointer.forgetTree( tree );
        _focus.forgetTree( tree );
        _bPointerInside  = false;
        _bPointerPressed = false;
        ++_generation;
        _listBaseline.clear();
        _baselineWidget = kInvalidWidgetID;
        _selected       = _selectedOrdinal != UIPreviewLogic::kNoOrdinal ? UIPreviewLogic::findWidgetByOrdinal( tree, _selectedOrdinal ) : kInvalidWidgetID;
        if ( bRebuilt )
        {
            // 다시 지은 트리는 디스크 문서 그대로다 — 고친 값과 입력 흉내의 흔적이 모두 사라졌다.
            _bMutated = false;
            clearDocumentDirty();
            if ( _bAwaitReload )
            {
                _bAwaitReload       = false;
                _bReloadedAfterSave = true;
            }
        }
    }

    void UIPreviewPanel::syncLanguage( UISystem& ui )
    {
        LocalizationManager* pGame = editor::getService<LocalizationManager>();
        if ( _listLanguage.empty() )
        {
            UIPreviewLogic::makeLanguageChoices( pGame != nullptr ? pGame->getAvailableLanguages() : vector<string>{}, _listLanguage );
        }
        if ( _language.empty() || pGame == nullptr )
        {
            if ( _pLocalization != nullptr )
            {
                ui.setOffscreenLocalization( _screen, nullptr );
                _pLocalization.reset();
            }
            else if ( _bApplyLocalization && _screen != kInvalidUIScreenHandle )
            {
                ui.setOffscreenLocalization( _screen, nullptr );
            }
            _bApplyLocalization = false;
            return;
        }

        // 문화권이 전역이라 게임 UI 의 언어는 건드리지 않는다 — 게임 관리자의 사본을 이 화면에만 건다. 게임 텍스트가 다시 읽히면(번역 핫 리로드) 사본도 다시 만든다.
        const uint32 gameRevision = pGame->getTextRevision();
        const bool   bNeedCopy    = _pLocalization == nullptr || gameRevision != _gameTextRevision;
        if ( bNeedCopy )
        {
            if ( _pLocalization == nullptr )
                _pLocalization = sw::make_unique<LocalizationManager>();
            _pLocalization->copyStateFrom( *pGame );
            _gameTextRevision   = gameRevision;
            _bApplyLocalization = true;
        }
        if ( _pLocalization->getCurrentLanguage() != _language )
        {
            (void)_pLocalization->setCurrentLanguage( _language );
            _bApplyLocalization = true;
        }
        if ( _bApplyLocalization && _screen != kInvalidUIScreenHandle )
        {
            ui.setOffscreenLocalization( _screen, _pLocalization.get() );
            _bApplyLocalization = false;
        }
    }

    void UIPreviewPanel::syncTexture()
    {
        AssetManager*    pAssets  = editor::getService<AssetManager>();
        const Texture2D* pTexture = pAssets != nullptr && _targetPath.empty() == false ? pAssets->getAssetCache<TextureCache>().find( _targetPath ) : nullptr;
        const uint64     texture  = pTexture != nullptr && pTexture->isRHIValid() ? pTexture->getHandle() : 0;
        if ( texture == _texture && _pTextureID != nullptr )
            return;
        releaseTexture();
        EditorContext* pContext = EditorContext::get();
        if ( texture == 0 || pContext == nullptr || pContext->getRendererBackend() == nullptr )
            return; // 렌더 스레드가 아직 만들지 않았다(다음 프레임)
        _pTextureID = pContext->getRendererBackend()->registerTexture( texture );
        _texture    = _pTextureID != nullptr ? texture : 0;
    }

    bool UIPreviewPanel::canSaveDocument() const
    {
        return _bMutated == false && _screen != kInvalidUIScreenHandle && _openedDocument.empty() == false;
    }

    bool UIPreviewPanel::saveDocument()
    {
        UISystem*       pUI     = editor::getService<UISystem>();
        const UIScreen* pScreen = pUI != nullptr ? pUI->findOffscreenScreen( _screen ) : nullptr;
        if ( pScreen == nullptr )
            return false;
        string text;
        string error;
        if ( UIPreviewLogic::makeDocumentText( *pUI, *pScreen, _openedDocument, text, error ) == false )
        {
            SW_LOG_ERROR( "UI Preview: '%#' cannot be saved - %#", _openedDocument.c_str(), error.c_str() );
            return false;
        }
        const string path = ResourcePaths::getWritePath( _openedDocument );
        if ( path.empty() || FileUtil::writeTextFile( path, text ) == false )
        {
            SW_LOG_ERROR( "UI Preview: '%#' could not be written to '%#'", _openedDocument.c_str(), path.c_str() );
            return false;
        }
        SW_LOG_INFO( "UI Preview: saved '%#'", _openedDocument.c_str() );
        _bAwaitReload       = true;
        _bReloadedAfterSave = false;
        return true;
    }

    void UIPreviewPanel::revertDocument()
    {
        closePreview();
        _error.clear();
    }

    void UIPreviewPanel::markPreviewMutated()
    {
        _bMutated = true;
    }

    void UIPreviewPanel::captureBaseline( const Widget& widget )
    {
        _listBaseline.clear();
        _baselineWidget       = widget.getID();
        const TypeInfo* pType = widget.getTypeInfo();
        if ( pType == nullptr )
            return;
        for ( const PropertyInfo& prop : pType->getPropertiesWithBase() )
        {
            if ( prop._containerKind != ContainerKind::None )
                continue; // 컨테이너는 텍스트 한 줄로 되돌릴 수 없다
            BaselineValue value{};
            value._name = prop._name.c_str();
            value._text = UIPreviewLogic::formatWidgetProperty( widget, prop );
            _listBaseline.push_back( std::move( value ) );
        }
    }

    void UIPreviewPanel::onPropertyEdited( const PropertyInfo& prop )
    {
        UISystem* pUI     = editor::getService<UISystem>();
        UIScreen* pScreen = pUI != nullptr ? pUI->findOffscreenScreen( _screen ) : nullptr;
        Widget*   pWidget = pScreen != nullptr ? pScreen->getTree().findWidgetByID( _selected ) : nullptr;
        if ( pWidget == nullptr )
            return;
        markDocumentDirty();
        if ( prop._containerKind != ContainerKind::None )
        {
            captureBaseline( *pWidget ); // 컨테이너 편집은 되돌리기 항목을 남기지 못한다 — 그 뒤의 값을 기준으로 잡는다
            return;
        }
        const string name    = prop._name.c_str();
        const string newText = UIPreviewLogic::formatWidgetProperty( *pWidget, prop );
        for ( BaselineValue& baseline : _listBaseline )
        {
            if ( baseline._name != name )
                continue;
            if ( baseline._text == newText )
                return;
            const string oldText                     = baseline._text;
            baseline._text                           = newText;
            const uint32                  generation = _generation;
            const uint32                  ordinal    = _selectedOrdinal;
            const shared_ptr<EditSession> pSession   = _pSession;
            // 끌어 바꾸는 값은 프레임마다 알려 온다 — 같은 위젯 · 같은 프로퍼티의 연속 편집은 되돌리기 항목 하나로 합친다.
            const string coalesceKey = "uipreview." + to_string( ordinal ) + "." + name;
            EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, ordinal, name, oldText]()
            {
                pSession->apply( generation, ordinal, name, oldText );
            } ),
                                     SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, ordinal, name, newText]()
            {
                pSession->apply( generation, ordinal, name, newText );
            } ),
                                     "Edit UI Widget", coalesceKey );
            return;
        }
    }

    void UIPreviewPanel::applyEditedText( uint32 generation, uint32 ordinal, const string& propertyName, const string& text )
    {
        if ( generation != _generation )
            return; // 그 뒤에 트리를 다시 지었다 — 옛 항목이 새 트리에 값을 쓰지 않는다
        UISystem* pUI     = editor::getService<UISystem>();
        UIScreen* pScreen = pUI != nullptr ? pUI->findOffscreenScreen( _screen ) : nullptr;
        if ( pScreen == nullptr )
            return;
        Widget* pWidget = pScreen->getTree().findWidgetByID( UIPreviewLogic::findWidgetByOrdinal( pScreen->getTree(), ordinal ) );
        if ( pWidget == nullptr )
            return;
        const PropertyInfo* pProp = UIPreviewLogic::findWidgetProperty( *pWidget, propertyName );
        if ( pProp == nullptr || UIPreviewLogic::applyWidgetProperty( *pWidget, *pProp, text ) == false )
        {
            SW_LOG_WARNING( "UI Preview: '%#' could not be restored to '%#'", propertyName.c_str(), text.c_str() );
            return;
        }
        markDocumentDirty();
        if ( _baselineWidget == pWidget->getID() )
        {
            for ( BaselineValue& baseline : _listBaseline )
            {
                if ( baseline._name == propertyName )
                    baseline._text = text;
            }
        }
    }

    void UIPreviewPanel::drawLanguageCombo()
    {
        const string preview = _language.empty() ? string( "(game language)" ) : _language;
        ImGui::SetNextItemWidth( 150.0f * EditorThemeUtil::getDpiScale() );
        const bool bOpen = ImGui::BeginCombo( "##uipreviewlang", preview.c_str() );
        UIPreviewPanelInternal::noteMark( "uipreview", "language" );
        EditorWidgets::drawTooltip( "Language the preview text is resolved in. The game UI keeps its own language; qps-ploc and qps-plocm are pseudo-languages "
                                    "(expanded text, right-to-left layout)." );
        if ( bOpen == false )
            return;
        for ( uint32 index = 0; index < _listLanguage.size(); ++index )
        {
            const string& code  = _listLanguage[index];
            string        label = code.empty() ? string( "(game language)" ) : code;
            if ( UIPreviewLogic::isPseudoLanguage( code ) )
                label += "  [pseudo]";
            if ( ImGui::Selectable( label.c_str(), code == _language ) )
            {
                _language      = code;
                _languageIndex = index;
            }
            UIPreviewPanelInternal::noteMark( "uipreview.language", code.empty() ? "game" : code.c_str() );
        }
        ImGui::EndCombo();
    }

    void UIPreviewPanel::drawToolbar( UISystem& ui )
    {
        string path = _documentPath;
        ImGui::SetNextItemWidth( 320.0f * EditorThemeUtil::getDpiScale() );
        if ( EditorWidgets::drawTextField( "##uipreviewdoc", path ) )
        {
            if ( path != _documentPath && isDocumentDirty() )
                SW_LOG_WARNING( "UI Preview: unsaved edits of '%#' are discarded because the path changed", _documentPath.c_str() );
            _documentPath = path;
            _error.clear();
        }
        UIPreviewPanelInternal::noteMark( "uipreview", "path" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "DocumentPathTip" ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Reload" ) )
            revertDocument();
        UIPreviewPanelInternal::noteMark( "uipreview", "reload" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "ReloadTip" ) );
        ImGui::SameLine();
        const bool bCanSave = canSaveDocument() && isDocumentDirty();
        ImGui::BeginDisabled( bCanSave == false );
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 실패는 saveDocument 가 로그로 알린다
        ImGui::EndDisabled();
        UIPreviewPanelInternal::noteMark( "uipreview", "save" );
        EditorWidgets::drawTooltip( "Write the edited widget properties back to the document (UIDocumentWriter). Disabled after input simulation or animation playback "
                                    "changed the preview tree - Reload first." );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 190.0f * EditorThemeUtil::getDpiScale() );
        const bool bResolutionOpen = ImGui::BeginCombo( "##uipreviewres", UIPreviewLogic::getResolution( _resolutionIndex )._pName );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "ResolutionTip" ) );
        if ( bResolutionOpen )
        {
            for ( uint32 index = 0; index < UIPreviewLogic::getResolutionCount(); ++index )
            {
                if ( ImGui::Selectable( UIPreviewLogic::getResolution( index )._pName, index == _resolutionIndex ) )
                    _resolutionIndex = index;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 150.0f * EditorThemeUtil::getDpiScale() );
        const bool bThemeOpen = ImGui::BeginCombo( "##uipreviewtheme", _theme.empty() ? "(game theme)" : _theme.c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "ThemeTip" ) );
        if ( bThemeOpen )
        {
            if ( ImGui::Selectable( "(game theme)", _theme.empty() ) )
                _theme = hashed_string{};
            for ( const UIThemeDesc& theme : ui.getThemeCatalog()._listTheme )
            {
                if ( ImGui::Selectable( theme._name.c_str(), theme._name == _theme ) )
                    _theme = theme._name;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        drawLanguageCombo();
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SliderFloat( "UI scale", &_uiScale, 0.5f, 2.0f, "%.2f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "UIScaleTip" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SliderFloat( "Text scale", &_textScale, 0.75f, 2.0f, "%.2f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "TextScaleTip" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SliderFloat( "Safe zone", &_safeZone, 0.0f, 0.1f, "%.3f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "SafeZoneTip" ) );
        ImGui::SameLine();
        ImGui::Checkbox( "Layout rects", &_bShowLayout );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "LayoutRectsTip" ) );
    }

    bool UIPreviewPanel::navigateFocus( UIScreen& screen, uint32 direction )
    {
        UISystem* pUI = editor::getService<UISystem>();
        if ( pUI != nullptr )
        {
            _bNavigationMode = true;
            pUI->setOffscreenNavigationMode( _screen, true );
        }
        WidgetTree& tree = screen.getTree();
        if ( _focus.getFocusedTree() == &tree && tree.getFocusedWidget() != kInvalidWidgetID )
            return _focus.navigate( tree, static_cast<UINavigationDirection>( direction ) );
        // 포커스가 아직 없으면 탐색 방식은 기본 포커스부터 시작한다(게임 화면이 열릴 때와 같다) — 첫 포커스 위젯을 잡는다.
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( const Widget* pWidget : listWidget )
        {
            if ( pWidget->supportsFocus() && _focus.setFocus( tree, pWidget->getID() ) )
                return true;
        }
        return false;
    }

    void UIPreviewPanel::acceptFocus( UIScreen& screen )
    {
        WidgetTree&   tree     = screen.getTree();
        const Widget* pFocused = tree.findWidgetByID( tree.getFocusedWidget() );
        if ( pFocused == nullptr )
            return;
        const UIRect   bounds = pFocused->getGeometry().computeScreenBounds();
        UIPointerEvent event{};
        event._position = float2{ ( bounds._left + bounds._right ) * 0.5f, ( bounds._top + bounds._bottom ) * 0.5f };
        event._kind     = UIPointerEventKind::Move;
        (void)_pointer.process( tree, event );
        event._kind = UIPointerEventKind::Down;
        (void)_pointer.process( tree, event );
        event._kind = UIPointerEventKind::Up;
        (void)_pointer.process( tree, event );
        _lastPointer = event._position;
        markPreviewMutated();
    }

    void UIPreviewPanel::drawInteractionBar( UIScreen& screen )
    {
        UISystem* pUI = editor::getService<UISystem>();
        if ( ImGui::Checkbox( "Interact", &_bInteract ) && pUI != nullptr )
        {
            _bNavigationMode = false;
            pUI->setOffscreenNavigationMode( _screen, false );
            if ( _bInteract == false )
                _pointer.clearHover();
        }
        UIPreviewPanelInternal::noteMark( "uipreview", "interact" );
        EditorWidgets::drawTooltip( "Simulate the pointer over the preview (hover, press, wheel). Clicks no longer select widgets. Pressing changes widget values "
                                    "in the preview only - Reload restores the document." );
        ImGui::SameLine();
        // 탐색 방향(키보드, 패드의 방향 입력) 흉내 — 포커스를 옮기고 포커스 테두리(`:focus-visible`)를 보인다.
        struct NavigationButton
        {
            const utf8*           _pMark;
            ImGuiDir              _dir;
            UINavigationDirection _direction;
        };
        static constexpr NavigationButton kArrNavigation[] = {
            {   "up",    ImGuiDir_Up,    UINavigationDirection::Up},
            { "down",  ImGuiDir_Down,  UINavigationDirection::Down},
            { "left",  ImGuiDir_Left,  UINavigationDirection::Left},
            {"right", ImGuiDir_Right, UINavigationDirection::Right},
        };
        for ( const NavigationButton& button : kArrNavigation )
        {
            fixed_string<constant::kMaxBuffer32> id;
            formatstring( id.data(), id.capacity(), "##uipreviewnav%#", button._pMark );
            if ( ImGui::ArrowButton( id.c_str(), button._dir ) )
                (void)navigateFocus( screen, static_cast<uint32>( button._direction ) );
            UIPreviewPanelInternal::noteMark( "uipreview.nav", button._pMark );
            EditorWidgets::drawTooltip( "Move the preview focus in this direction (like the keyboard or pad). The first press focuses the first focusable widget." );
            ImGui::SameLine();
        }
        if ( ImGui::Button( "Accept" ) )
            acceptFocus( screen );
        UIPreviewPanelInternal::noteMark( "uipreview.nav", "accept" );
        EditorWidgets::drawTooltip( "Press the focused widget (UI.Accept). Its command is only logged - the preview has no screen code." );
    }

    void UIPreviewPanel::drawAnimationBar( UIScreen& screen )
    {
        UIAnimationPlayer&         player        = screen.getAnimationPlayer();
        const vector<UIAnimation>& listAnimation = player.getAnimations();
        if ( listAnimation.empty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(no animations)" );
            return;
        }
        if ( _animation.empty() || player.findAnimation( _animation ) == nullptr )
            _animation = listAnimation[0]._name;
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 120.0f * EditorThemeUtil::getDpiScale() );
        const bool bOpen = ImGui::BeginCombo( "##uipreviewanimation", _animation.c_str() );
        UIPreviewPanelInternal::noteMark( "uipreview", "animation" );
        EditorWidgets::drawTooltip( "Document animation to preview. The preview never plays Open by itself - the document values are what you see until you play one." );
        if ( bOpen )
        {
            for ( const UIAnimation& animation : listAnimation )
            {
                if ( ImGui::Selectable( animation._name.c_str(), animation._name == _animation ) )
                {
                    player.stopAll();
                    _animation = animation._name;
                }
                UIPreviewPanelInternal::noteMark( "uipreview.animation", animation._name.c_str() );
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Play" ) )
        {
            _bAnimationPaused = false;
            (void)player.play( _animation, 1.0f, _bAnimationLoop ? 0u : 1u );
            markPreviewMutated();
        }
        UIPreviewPanelInternal::noteMark( "uipreview.animation", "play" );
        EditorWidgets::drawTooltip( "Play the chosen animation from the start. The widget values stay where it leaves them; Reload restores the document." );
        ImGui::SameLine();
        if ( ImGui::Button( _bAnimationPaused ? "Resume" : "Pause" ) )
        {
            _bAnimationPaused = !_bAnimationPaused;
            player.setPaused( _animation, _bAnimationPaused );
        }
        UIPreviewPanelInternal::noteMark( "uipreview.animation", "pause" );
        ImGui::SameLine();
        if ( ImGui::Button( "Stop" ) )
        {
            player.stop( _animation );
            _bAnimationPaused = false;
        }
        UIPreviewPanelInternal::noteMark( "uipreview.animation", "stop" );
        ImGui::SameLine();
        ImGui::Checkbox( "Loop", &_bAnimationLoop );
        ImGui::SameLine();

        // 시간 막대 — 끌면 그 시각에서 멈춘 채 값을 보인다(이벤트는 보내지 않는다). 재생 중에는 지금 시각을 따라간다.
        float32            time{ 0.0f };
        float32            duration{ 0.0f };
        const bool         bActive = player.getPlaybackTime( _animation, time, duration );
        const UIAnimation* pChosen = player.findAnimation( _animation );
        if ( bActive == false )
        {
            duration = pChosen != nullptr ? pChosen->computeDuration() : 0.0f;
            time     = MathUtil::clamp( _animationScrub, 0.0f, duration );
        }
        float32 value = time;
        ImGui::SetNextItemWidth( 220.0f * EditorThemeUtil::getDpiScale() );
        if ( ImGui::SliderFloat( "##uipreviewanimationtime", &value, 0.0f, MathUtil::max( duration, 0.001f ), "%.2f s" ) )
        {
            _animationScrub   = value;
            _bAnimationPaused = true;
            (void)player.seek( _animation, value );
            markPreviewMutated();
        }
        else if ( bActive )
        {
            _animationScrub = time;
        }
        UIPreviewPanelInternal::noteMark( "uipreview", "animationtime" );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", UIPreviewLogic::makeTimeLabel( bActive ? time : value, duration ).c_str() );
    }

    void UIPreviewPanel::drawWidgetList( const UIScreen& screen )
    {
        using Internal = UIPreviewPanelInternal;
        UIPreviewLogic::collectWidgetRows( screen.getTree(), _listRow );
        for ( uint32 ordinal = 0; ordinal < _listRow.size(); ++ordinal )
        {
            const UIPreviewWidgetRow& row = _listRow[ordinal];
            ImGui::PushID( static_cast<int32>( row._widget ) );
            ImGui::Indent( Internal::kIndentPerLevel * static_cast<float32>( row._depth ) + 0.001f );
            if ( ImGui::Selectable( row._label.c_str(), row._widget == _selected ) )
            {
                _selected        = row._widget;
                _selectedOrdinal = ordinal;
            }
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::kMaxBuffer32> ordinalText;
                formatstring( ordinalText.data(), ordinalText.capacity(), "%#", ordinal );
                Internal::noteMark( "uipreview.row", ordinalText.c_str() );
                const Widget* pWidget = screen.getTree().findWidgetByID( row._widget );
                if ( pWidget != nullptr && pWidget->getName().empty() == false )
                    Internal::noteMark( "uipreview.widget", pWidget->getName().c_str() );
            }
            ImGui::Unindent( Internal::kIndentPerLevel * static_cast<float32>( row._depth ) + 0.001f );
            ImGui::PopID();
        }
    }

    void UIPreviewPanel::drawInspector( UIScreen& screen )
    {
        Widget* pWidget = screen.getTree().findWidgetByID( _selected );
        if ( pWidget == nullptr || pWidget->getTypeInfo() == nullptr )
        {
            EditorWidgets::drawEmptyHint( "Select a widget to edit its properties." );
            return;
        }
        if ( _baselineWidget != _selected )
            captureBaseline( *pWidget );
        _grid.drawSearchBar( "uipreview.inspector.search" );
        EditorPropertyGridTarget target{};
        target._pInstance = pWidget;
        target._pType     = pWidget->getTypeInfo();
        target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &UIPreviewPanel::onPropertyEdited, this );
        _grid.drawProperties( target, nullptr, vector<hashed_string>{} );
    }

    void UIPreviewPanel::feedPointer( UIScreen& screen, const float2& drawSize, const float2& origin, bool bHovered )
    {
        WidgetTree&    tree = screen.getTree();
        const ImGuiIO& io   = ImGui::GetIO();
        UIPointerEvent event{};
        event._position = UIPreviewLogic::mapImageToUI( float2{ io.MousePos.x - origin._x, io.MousePos.y - origin._y }, drawSize, _viewport );
        event._delta    = float2{ event._position._x - _lastPointer._x, event._position._y - _lastPointer._y };
        if ( bHovered )
        {
            if ( _bPointerInside == false || event._delta._x != 0.0f || event._delta._y != 0.0f )
            {
                if ( _bNavigationMode )
                {
                    UISystem* pUI    = editor::getService<UISystem>();
                    _bNavigationMode = false;
                    if ( pUI != nullptr )
                        pUI->setOffscreenNavigationMode( _screen, false ); // 마우스를 움직이면 포인터 방식으로 돌아간다(포커스 테두리를 숨긴다)
                }
                event._kind = UIPointerEventKind::Move;
                (void)_pointer.process( tree, event );
                _lastPointer = event._position;
            }
            _bPointerInside = true;
            if ( io.MouseWheel != 0.0f )
            {
                event._kind  = UIPointerEventKind::Wheel;
                event._wheel = io.MouseWheel;
                (void)_pointer.process( tree, event );
                markPreviewMutated();
            }
        }
        else if ( _bPointerInside )
        {
            if ( _bPointerPressed == false )
                _pointer.clearHover();
            _bPointerInside = false;
        }

        struct ButtonMap
        {
            int32       _imguiButton;
            MouseButton _button;
        };
        static constexpr ButtonMap kArrButton[] = {
            {0,   MouseButton::Left},
            {1,  MouseButton::Right},
            {2, MouseButton::Middle},
        };
        for ( const ButtonMap& map : kArrButton )
        {
            event._button = map._button;
            if ( bHovered && ImGui::IsMouseClicked( map._imguiButton ) )
            {
                event._kind                  = UIPointerEventKind::Down;
                const UIPointerResult result = _pointer.process( tree, event );
                if ( result._focusRequest != kInvalidWidgetID )
                    (void)_focus.setFocus( tree, result._focusRequest );
                _bPointerPressed = true;
                markPreviewMutated();
            }
            // 이미지 밖에서 뗀 버튼도 위젯이 잡고 있으면 Up 을 보낸다(슬라이더를 끌다가 바깥에서 놓는 경우).
            if ( ImGui::IsMouseReleased( map._imguiButton ) && ( bHovered || _bPointerPressed ) )
            {
                event._kind = UIPointerEventKind::Up;
                (void)_pointer.process( tree, event );
                _bPointerPressed = false;
                markPreviewMutated();
            }
        }
    }

    void UIPreviewPanel::drawImage( UIScreen& screen )
    {
        using Internal = UIPreviewPanelInternal;
        ImGui::BeginChild( "##uipreviewimage", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        const Widget* pSelected = screen.getTree().findWidgetByID( _selected );
        if ( pSelected != nullptr )
        {
            const UIRect bounds = pSelected->getGeometry().computeScreenBounds();
            ImGui::Text( "%s  (%.0f, %.0f)  %.0f x %.0f", pSelected->getName().empty() ? "(unnamed)" : pSelected->getName().c_str(),
                         static_cast<float64>( bounds._left ), static_cast<float64>( bounds._top ), static_cast<float64>( bounds._right - bounds._left ),
                         static_cast<float64>( bounds._bottom - bounds._top ) );
        }
        else
        {
            fixed_string<constant::kMaxBuffer32> width;
            fixed_string<constant::kMaxBuffer32> height;
            fixed_string<constant::kMaxBuffer32> scale;
            formatstring( width.data(), width.capacity(), "%#", Fmt( static_cast<float64>( _viewport._size._x ), Format().precision( 0 ) ) );
            formatstring( height.data(), height.capacity(), "%#", Fmt( static_cast<float64>( _viewport._size._y ), Format().precision( 0 ) ) );
            formatstring( scale.data(), scale.capacity(), "%#", Fmt( static_cast<float64>( _viewport._uiScale ), Format().precision( 2 ) ) );
            const string hint = SW_LOCKEYFORMAT( "EditorUIPreview", "SelectWidgetHint",
                                                 TextArgumentList().addText( "width", width.c_str() ).addText( "height", height.c_str() ).addText( "scale", scale.c_str() ) );
            ImGui::TextDisabled( "%s", hint.c_str() );
        }
        if ( _pTextureID == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorUIPreview", "WaitingForTexture" ) );
            ImGui::EndChild();
            return;
        }
        const ImVec2 avail    = ImGui::GetContentRegionAvail();
        const float2 drawSize = UIPreviewLogic::fitImage( _viewport._physicalSize, float2{ avail.x, avail.y } );
        const ImVec2 cursor   = ImGui::GetCursorScreenPos();
        const float2 origin{ cursor.x, cursor.y };
        ImGui::Image( reinterpret_cast<ImTextureID>( _pTextureID ), ImVec2{ drawSize._x, drawSize._y } );
        const bool bImageHovered = ImGui::IsItemHovered();
        EditorSelfTestMarks::note( "uipreview.image" );
        if ( _bInteract )
        {
            feedPointer( screen, drawSize, origin, bImageHovered );
        }
        else if ( ImGui::IsItemClicked() )
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float2 point = UIPreviewLogic::mapImageToUI( float2{ mouse.x - origin._x, mouse.y - origin._y }, drawSize, _viewport );
            _selected          = UIPreviewLogic::findWidgetAt( screen.getTree(), point );
            _selectedOrdinal   = _selected != kInvalidWidgetID ? UIPreviewLogic::findWidgetOrdinal( screen.getTree(), _selected ) : UIPreviewLogic::kNoOrdinal;
        }

        // 겹쳐 그리기 — 레이아웃 사각형 · 고른 위젯 · 안전 영역(그림 위 ImGui 선, 캔버스는 바꾸지 않는다).
        ImDrawList* pDraw   = ImGui::GetWindowDrawList();
        const auto  drawBox = [pDraw, origin]( const UIRect& rect, ImU32 color, float32 thickness )
        {
            pDraw->AddRect( ImVec2{ origin._x + rect._left, origin._y + rect._top }, ImVec2{ origin._x + rect._right, origin._y + rect._bottom }, color, 0.0f, 0, thickness );
        };
        if ( _bShowLayout )
        {
            vector<Widget*> listWidget;
            screen.getTree().collectWidgetsInDocumentOrder( listWidget );
            for ( const Widget* pWidget : listWidget )
            {
                if ( pWidget->isVisible() )
                    drawBox( UIPreviewLogic::mapUIToImage( pWidget->getGeometry().computeScreenBounds(), drawSize, _viewport ), EditorColorUtil::toImU32( Internal::kLayoutColor ), 1.0f );
            }
        }
        if ( pSelected != nullptr )
            drawBox( UIPreviewLogic::mapUIToImage( pSelected->getGeometry().computeScreenBounds(), drawSize, _viewport ), EditorColorUtil::toImU32( style::kHighlight ), 2.0f );
        const float4& insets = _viewport._safeInsets;
        if ( insets._x > 0.0f || insets._y > 0.0f || insets._z > 0.0f || insets._w > 0.0f )
        {
            const UIRect safe{ insets._x, insets._y, _viewport._size._x - insets._z, _viewport._size._y - insets._w };
            drawBox( UIPreviewLogic::mapUIToImage( safe, drawSize, _viewport ), EditorColorUtil::toImU32( Internal::kSafeZoneColor ), 1.0f );
        }
        ImGui::EndChild();
    }

    void UIPreviewPanel::updateProbe( const UIScreen* pScreen )
    {
        _probe = UIPreviewProbe{};
        if ( pScreen == nullptr )
            return;
        const WidgetTree& tree     = pScreen->getTree();
        _probe._bValid             = true;
        _probe._bDirty             = isDocumentDirty();
        _probe._bReloadedAfterSave = _bReloadedAfterSave;
        _probe._bTextureReady      = _pTextureID != nullptr;
        _probe._languageIndex      = _languageIndex;
        const Widget* pRoot        = tree.getRoot();
        _probe._bRightToLeft       = pRoot != nullptr && pRoot->isRightToLeft();
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        _probe._widgetCount = static_cast<uint32>( listWidget.size() );
        for ( const Widget* pWidget : listWidget )
        {
            const TextWidget* pText = castTo<const TextWidget>( pWidget );
            if ( pText != nullptr )
            {
                _probe._firstTextBytes = static_cast<uint32>( pText->getDisplayText().size() );
                break;
            }
        }
        if ( _pointer.getHoverTree() == &tree )
        {
            const Widget* pHovered = tree.findWidgetByID( _pointer.getHoveredWidget() );
            if ( pHovered != nullptr )
            {
                _probe._hoveredOrdinal    = static_cast<int32>( UIPreviewLogic::findWidgetOrdinal( tree, pHovered->getID() ) );
                _probe._bHoveredFocusable = pHovered->supportsFocus();
            }
        }
        if ( tree.getFocusedWidget() != kInvalidWidgetID )
        {
            const uint32 focused = UIPreviewLogic::findWidgetOrdinal( tree, tree.getFocusedWidget() );
            if ( focused != UIPreviewLogic::kNoOrdinal )
                _probe._focusedOrdinal = static_cast<int32>( focused );
        }
        const Widget* pSelected = tree.findWidgetByID( _selected );
        if ( pSelected != nullptr )
        {
            _probe._bSelectedValid  = true;
            _probe._selectedOpacity = pSelected->getOpacity();
        }
        float32 time{ 0.0f };
        float32 duration{ 0.0f };
        if ( pScreen->getAnimationPlayer().getPlaybackTime( _animation, time, duration ) )
        {
            _probe._animationTime     = time;
            _probe._bAnimationPlaying = _bAnimationPaused == false;
        }
    }

    void UIPreviewPanel::drawContent()
    {
        using Internal = UIPreviewPanelInternal;
        UISystem* pUI  = editor::getService<UISystem>();
        if ( pUI == nullptr || pUI->isInitialized() == false )
        {
            _probe = UIPreviewProbe{};
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorUIPreview", "UINotRunning" ) );
            return;
        }
        syncFocusedDocument();
        drawToolbar( *pUI );
        syncPreview( *pUI );
        syncLanguage( *pUI );
        syncTexture();
        UIScreen* pScreen = pUI->findOffscreenScreen( _screen );
        if ( pScreen != nullptr )
        {
            syncTree( *pScreen );
            drawInteractionBar( *pScreen );
            drawAnimationBar( *pScreen );
        }
        ImGui::Separator();
        if ( pScreen == nullptr )
        {
            _probe = UIPreviewProbe{};
            EditorWidgets::drawEmptyHint( _error.empty() ? SW_LOCKEY( "EditorUIPreview", "EnterPathHint" ) : _error.c_str() );
            return;
        }

        // 왼쪽 칸: 위젯 트리 위, 고른 위젯의 프로퍼티(인스펙터) 아래. 오른쪽: 미리보기 그림.
        const float32 dpi = EditorThemeUtil::getDpiScale();
        ImGui::BeginChild( "##uipreviewleft", ImVec2{ Internal::kLeftWidth * dpi, 0.0f }, ImGuiChildFlags_None );
        const float32 treeHeight = ImGui::GetContentRegionAvail().y * Internal::kTreeRatio;
        ImGui::BeginChild( "##uipreviewtree", ImVec2{ 0.0f, treeHeight }, ImGuiChildFlags_Borders );
        drawWidgetList( *pScreen );
        ImGui::EndChild();
        ImGui::BeginChild( "##uipreviewinspector", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        drawInspector( *pScreen );
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::SameLine();
        drawImage( *pScreen );
        updateProbe( pScreen );
    }
} // namespace sw::editor
