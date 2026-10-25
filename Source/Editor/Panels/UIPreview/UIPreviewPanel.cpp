#include "pch.h"

#include "Editor/Panels/UIPreview/UIPreviewPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetNavigation.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/GridPanel.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "UIPreviewPanel" );
    SW_EDITOR_PANEL( UIPreviewPanel, UIPreviewPanel::kPanelID, EditorPanelCategory::Tool, 950 );

    /** @brief 되돌리기 항목이 패널을 가리키는 길입니다. 패널이 사라지면 `_pPanel` 이 비어 항목은 아무것도 하지 않습니다. */
    struct UIPreviewPanel::EditSession
    {
        UIPreviewPanel* _pPanel{ nullptr };

        void apply( uint32 generation, uint32 ordinal, const string& propertyName, const string& text ) const
        {
            if ( _pPanel != nullptr )
                _pPanel->applyEditedText( generation, ordinal, propertyName, text );
        }

        void insert( uint32 generation, const hashed_string& typeName, uint32 parentOrdinal, uint32 index ) const
        {
            if ( _pPanel != nullptr )
                _pPanel->applyInsert( generation, typeName, parentOrdinal, index );
        }

        void detach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder ) const
        {
            if ( _pPanel != nullptr )
                _pPanel->applyDetach( generation, parentOrdinal, index, pHolder );
        }

        void reattach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder ) const
        {
            if ( _pPanel != nullptr )
                _pPanel->applyReattach( generation, parentOrdinal, index, pHolder );
        }

        void move( uint32 generation, uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex ) const
        {
            if ( _pPanel != nullptr )
                _pPanel->applyMove( generation, parentOrdinal, fromIndex, toIndex );
        }
    };

    /** @brief 지운 위젯을 되돌리기 항목 둘(지우기를 되돌리기 · 다시 하기)이 나눠 보관하고 있는 상자입니다. */
    struct UIPreviewPanel::RemovedWidget
    {
        unique_ptr<Widget> _pWidget{};
    };

    namespace
    {
        struct UIPreviewPanelInternal
        {
            static constexpr float32     kLeftWidth            = 340.0f;
            static constexpr float32     kIndentPerLevel       = 12.0f;
            static constexpr float32     kTreeRatio            = 0.28f; ///< 왼쪽 영역에서 위젯 트리가 차지하는 높이 비율
            static constexpr float32     kPaletteRatio         = 0.22f; ///< 팔레트가 차지하는 높이 비율(나머지는 인스펙터)
            static constexpr float32     kHandleSize           = 9.0f;  ///< 슬롯 손잡이 한 변(96 DPI 픽셀)
            static constexpr uint32      kPaletteRefreshFrames = 180;   ///< 팔레트를 다시 모으는 간격(게임 모듈이 위젯 타입을 더하거나 뺄 수 있다)
            static constexpr const utf8* kPaletteDragPayload   = "SW_UIPREVIEW_WIDGET_TYPE";
            static constexpr Color       kLayoutColor{ Color::fromBytes( 90, 200, 255, 110 ) };
            static constexpr Color       kSafeZoneColor{ Color::fromBytes( 255, 80, 80, 200 ) };

            /** @brief 시나리오가 누를 레이블 `<접두어>.<접미어>` 를 남깁니다. 테스트를 켰을 때만 문자열을 만듭니다. */
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
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const UIPreviewPanel* pPanel            = pContext->getSubsystem<EditorPanelManager>().findPanel<UIPreviewPanel>();
                const bool            bProbeUnavailable = pPanel == nullptr || pPanel->isOpen() == false || pPanel->getProbe()._bValid == false;
                if ( bProbeUnavailable )
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

            [[nodiscard]] static bool readSlotEditable( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr || pProbe->_bSelectedValid == false )
                    return false;
                outValue = pProbe->_bSlotEditable ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readSelectedOffsetMinX( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr || pProbe->_bSelectedValid == false )
                    return false;
                outValue = static_cast<float64>( pProbe->_selectedOffsetMinX );
                return true;
            }

            [[nodiscard]] static bool readPaletteCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_paletteCount );
                return true;
            }

            [[nodiscard]] static bool readSlotEditCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UIPreviewProbe* pProbe = findProbe();
                if ( pProbe == nullptr )
                    return false;
                outValue = static_cast<float64>( pProbe->_slotEditCount );
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
    SW_AUTOMATION_PROBE( editorUIPreviewSlotEditable, "Editor.UIPreviewSlotEditable",
                         "1 when the selected widget's slot can be dragged by handles (its parent is a canvas panel); no value without a selection",
                         &UIPreviewPanelInternal::readSlotEditable );
    SW_AUTOMATION_PROBE( editorUIPreviewSelectedOffsetMinX, "Editor.UIPreviewSelectedOffsetMinX", "Slot _offsetMin._x of the selected widget (a handle drag changes it)",
                         &UIPreviewPanelInternal::readSelectedOffsetMinX );
    SW_AUTOMATION_PROBE( editorUIPreviewPaletteCount, "Editor.UIPreviewPaletteCount", "Widget types the UI Preview palette lists (every constructible Widget subclass)",
                         &UIPreviewPanelInternal::readPaletteCount );
    SW_AUTOMATION_PROBE( editorUIPreviewSlotEditCount, "Editor.UIPreviewSlotEditCount",
                         "Slot edits applied in the UI Preview (canvas handles, Slot fields, box order and grid cell handles; one undo entry each)",
                         &UIPreviewPanelInternal::readSlotEditCount );
    SW_AUTOMATION_PROBE( editorUIPreviewLanguageIndex, "Editor.UIPreviewLanguageIndex", "Row of the UI Preview language combo (0 = the game language)",
                         &UIPreviewPanelInternal::readLanguageIndex );

    UIPreviewPanel::UIPreviewPanel()
        : IEditorPanel( false )
        , _listRow{}
        , _listLanguage{}
        , _listBaseline{}
        , _listPaletteType{}
        , _paletteFilter{}
        , _slotDragBaseline{}
        , _paletteType{}
        , _documentPath{ getEditorToolDefaults()._uiPreviewDocument } // 처음 여는 문서 — 원본은 editortooldefaults.json
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
        , _slotDragOrdinal{ UIPreviewLogic::kNoOrdinal }
        , _paletteFrame{ 0 }
        , _slotEditCount{ 0 }
        , _resolutionTable{}
        , _resolutionIndex{ 0 }
        , _bResolutionTableAttempted{ false }
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
        , _bSlotDragging{ false }
        , _bLayoutDragging{ false }
    {
        _pSession->_pPanel = this;
    }

    UIPreviewPanel::~UIPreviewPanel()
    {
        _pSession->_pPanel = nullptr; // 쌓여 있는 되돌리기 항목이 사라진 패널을 부르지 않게 한다
    }

    const utf8* UIPreviewPanel::getPanelTitle() const
    {
        return EditorAssetTypeRegistry::getPanelTitle( EditorAssetType::UIDocument );
    }

    EditorAssetType UIPreviewPanel::getDocumentKind() const
    {
        return EditorAssetType::UIDocument;
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
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        if ( pContext != nullptr && pContext->getSubsystem<EditorRenderViews>().getRendererBackend() != nullptr )
            pContext->getSubsystem<EditorRenderViews>().getRendererBackend()->unregisterTexture( _pTextureID );
        _pTextureID = nullptr;
        _texture    = 0;
    }

    void UIPreviewPanel::closePreview()
    {
        // 닫힐 때는 UI 시스템이 먼저 내려갔을 수 있다(소멸자 · 종료) — 풀 화면이 이미 사라진 것이라 오류가 아니다.
        UISystem* pUI = editor::findSubsystem<UISystem>(); // 종료 순서에 따라 이미 없을 수 있다
        if ( pUI != nullptr && _screen != kInvalidUIScreenHandle )
        {
            UIScreen* pScreen = pUI->getOffscreenScreens().findScreen( _screen );
            if ( pScreen != nullptr )
            {
                _pointer.forgetTree( pScreen->getTree() ); // 트리를 지우기 전에 호버와 포커스를 놓는다
                _focus.forgetTree( pScreen->getTree() );
            }
            pUI->getOffscreenScreens().remove( _screen );
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
        if ( focused.empty() || _documentPath == focused )
            return;
        const string previous = _documentPath.get();
        if ( _documentPath.commit( focused ) == false )
            return; // 리소스 id 가 아니면 commit 이 알렸다 — 지금 문서를 그대로 둔다
        if ( isDocumentDirty() )
            SW_LOG_WARNING( "UI Preview: unsaved edits of '%#' are discarded because '%#' was opened", previous.c_str(), _documentPath.c_str() );
        _error.clear();
    }

    const UIResolutionPreset* UIPreviewPanel::findSelectedResolution()
    {
        if ( _bResolutionTableAttempted == false )
        {
            // 테이블 경로는 엔진 기본 에셋 테이블이 원본이다. 읽지 못하면(이유는 테이블이 알린다) 목록이 비어 미리보기를 세우지 않는다.
            _bResolutionTableAttempted                = true;
            const EngineDefaultAssets* pDefaultAssets = editor::findSubsystem<const EngineDefaultAssets>();
            if ( pDefaultAssets == nullptr || _resolutionTable.loadFromResource( pDefaultAssets->_uiResolutionPresets.get() ) == false )
                return nullptr;
        }
        if ( _resolutionTable._listResolution.empty() )
            return nullptr;
        if ( _resolutionIndex >= _resolutionTable._listResolution.size() )
            _resolutionIndex = 0;
        return &_resolutionTable._listResolution[_resolutionIndex];
    }

    void UIPreviewPanel::syncPreview( UISystem& ui )
    {
        const UIResolutionPreset* pResolution = findSelectedResolution();
        if ( pResolution == nullptr )
        {
            _error = "UI preview resolutions could not be read (EngineDefaultAssets _uiResolutionPresets - see the log)";
            return;
        }
        _viewport                        = UIPreviewLogic::makeViewport( ui.getScaleSettings(), *pResolution, _uiScale, _safeZone );
        const string targetPath          = UIPreviewLogic::makeTargetPath( _viewport );
        const bool   bReopen             = _screen == kInvalidUIScreenHandle || ui.getOffscreenScreens().findScreen( _screen ) == nullptr || _documentPath != _openedDocument;
        const bool   bPreviousOpenFailed = bReopen && _error.empty() == false && _documentPath == _openedDocument;
        if ( bPreviousOpenFailed )
            return; // 같은 문서가 지난번에 만들지 못했다 — 고칠 때까지(경로를 바꾸거나 다시 열기) 프레임마다 다시 시도하지 않는다
        if ( bReopen )
        {
            const string documentPath = _documentPath.get(); // 확정 경로는 늘 값이 있다(EditableResourcePath)
            releaseTexture();                                // 크기가 바뀌면 렌더 텍스처도 새것이다
            closePreview();                                  // 포인터 · 포커스를 놓고 화면을 닫는다(되돌리기 세대도 올린다)
            _screen             = ui.openOffscreenScreen( documentPath, targetPath );
            _openedDocument     = documentPath;
            _targetPath         = targetPath;
            _error              = _screen == kInvalidUIScreenHandle ? SW_LOCKEY( "EditorUIPreview", "BuildFailed" ) : "";
            _bApplyLocalization = true;
        }
        else if ( _targetPath != targetPath )
        {
            // 해상도를 바꿔도 화면(트리, 고친 값)은 그대로 두고 그릴 렌더 텍스처만 새 크기의 것으로 옮긴다.
            releaseTexture();
            ui.getOffscreenScreens().setTargetPath( _screen, hashed_string( targetPath ) );
            _targetPath = targetPath;
        }
        if ( _screen != kInvalidUIScreenHandle )
        {
            ui.setOffscreenView( _screen, _viewport, _textScale, _theme );
            if ( UIScreen* pScreen = ui.getOffscreenScreens().findScreen( _screen ) )
                pScreen->setReportsUnhandledCommands( false ); // 미리보기에는 게임 코드가 없어 명령을 받을 쪽이 없다
        }
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
            // 다시 만든 트리는 디스크 문서 그대로다 — 고친 값과 입력 흉내의 흔적이 모두 사라졌다.
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
        LocalizationManager* pGame = editor::findSubsystem<LocalizationManager>();
        if ( _listLanguage.empty() )
        {
            UIPreviewLogic::makeLanguageChoices( pGame != nullptr ? pGame->getAvailableLanguages() : vector<string>{}, _listLanguage );
        }
        if ( _language.empty() || pGame == nullptr )
        {
            if ( _pLocalization != nullptr )
            {
                ui.getOffscreenScreens().setLocalization( _screen, nullptr );
                _pLocalization.reset();
            }
            else if ( _bApplyLocalization && _screen != kInvalidUIScreenHandle )
            {
                ui.getOffscreenScreens().setLocalization( _screen, nullptr );
            }
            _bApplyLocalization = false;
            return;
        }

        // 문화권이 전역이라 게임 UI 의 언어는 건드리지 않는다 — 게임 관리자의 사본을 이 화면에만 적용한다. 게임 텍스트가 다시 읽히면(번역 핫 리로드) 사본도 다시 만든다.
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
            ui.getOffscreenScreens().setLocalization( _screen, _pLocalization.get() );
            _bApplyLocalization = false;
        }
    }

    void UIPreviewPanel::syncTexture()
    {
        AssetManager*    pAssets  = editor::findSubsystem<AssetManager>();
        const Texture2D* pTexture = pAssets != nullptr && _targetPath.empty() == false ? pAssets->getAssetCache<TextureCache>().find( _targetPath ) : nullptr;
        const uint64     texture  = pTexture != nullptr && pTexture->isRHIValid() ? pTexture->getHandle() : 0;
        if ( texture == _texture && _pTextureID != nullptr )
            return;
        releaseTexture();
        EditorRenderViews& renderViews            = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>();
        const bool         bCannotRegisterTexture = texture == 0 || renderViews.getRendererBackend() == nullptr;
        if ( bCannotRegisterTexture )
            return; // 렌더 스레드가 아직 만들지 않았다(다음 프레임)
        _pTextureID = renderViews.getRendererBackend()->registerTexture( texture );
        _texture    = _pTextureID != nullptr ? texture : 0;
    }

    bool UIPreviewPanel::canSaveDocument() const
    {
        return _bMutated == false && _screen != kInvalidUIScreenHandle && _openedDocument.empty() == false;
    }

    bool UIPreviewPanel::saveDocument()
    {
        UISystem*       pUI     = editor::findSubsystem<UISystem>();
        const UIScreen* pScreen = pUI != nullptr ? pUI->getOffscreenScreens().findScreen( _screen ) : nullptr;
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
        UISystem* pUI     = editor::findSubsystem<UISystem>();
        UIScreen* pScreen = pUI != nullptr ? pUI->getOffscreenScreens().findScreen( _screen ) : nullptr;
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
            const string oldText = baseline._text;
            baseline._text       = newText;
            // 끌어 바꾸는 값은 프레임마다 알려 온다 — 같은 위젯 · 같은 프로퍼티의 연속 편집은 되돌리기 항목 하나로 합친다.
            pushPropertyEdit( _selectedOrdinal, name, oldText, newText, "uipreview." + to_string( _selectedOrdinal ) + "." + name );
            return;
        }
    }

    void UIPreviewPanel::pushPropertyEdit( uint32 ordinal, const string& propertyName, const string& oldText, const string& newText, const string& coalesceKey )
    {
        if ( propertyName == "_slot" )
            ++_slotEditCount;
        const uint32                  generation = _generation;
        const shared_ptr<EditSession> pSession   = _pSession;
        EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, ordinal, propertyName, oldText]()
        {
            pSession->apply( generation, ordinal, propertyName, oldText );
        } ),
                                 SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, ordinal, propertyName, newText]()
        {
            pSession->apply( generation, ordinal, propertyName, newText );
        } ),
                                 "Edit UI Widget", coalesceKey );
    }

    UIScreen* UIPreviewPanel::findPreviewScreen() const
    {
        UISystem* pUI = editor::findSubsystem<UISystem>();
        return pUI != nullptr ? pUI->getOffscreenScreens().findScreen( _screen ) : nullptr;
    }

    void UIPreviewPanel::afterStructureEdit( UIScreen& screen, uint32 selectedOrdinal )
    {
        // 위젯이 붙거나 떨어졌다 — 호버 경로가 옛 위젯 번호를 보관하고 있을 수 있어 비우고, 고름과 기준 값을 새 구조에 맞춘다.
        WidgetTree& tree = screen.getTree();
        _pointer.clearHover();
        _bPointerInside  = false;
        _baselineWidget  = kInvalidWidgetID;
        _selectedOrdinal = selectedOrdinal;
        _selected        = selectedOrdinal != UIPreviewLogic::kNoOrdinal ? UIPreviewLogic::findWidgetByOrdinal( tree, selectedOrdinal ) : kInvalidWidgetID;
        markDocumentDirty();
    }

    bool UIPreviewPanel::addWidget( UIScreen& screen, const hashed_string& typeName, Widget* pTarget )
    {
        TypeRegistry*   pRegistry = editor::findSubsystem<TypeRegistry>();
        const TypeInfo* pType     = pRegistry != nullptr ? pRegistry->findType( typeName ) : nullptr;
        WidgetTree&     tree      = screen.getTree();
        uint32          index{ 0 };
        PanelWidget*    pParent = UIPreviewLogic::findDropContainer( tree, pTarget, index );
        if ( pType == nullptr || pParent == nullptr )
            return false;
        const uint32 parentOrdinal = UIPreviewLogic::findWidgetOrdinal( tree, pParent->getID() );
        Widget*      pAdded        = UIPreviewLogic::insertWidget( tree, *pType, parentOrdinal, index );
        if ( pAdded == nullptr )
        {
            SW_LOG_WARNING( "UI Preview: a '%#' cannot be added to '%#' (the panel takes no more children or the type cannot be built)", typeName.c_str(),
                            pParent->getName().c_str() );
            return false;
        }
        afterStructureEdit( screen, UIPreviewLogic::findWidgetOrdinal( tree, pAdded->getID() ) );
        const uint32                  generation = _generation;
        const shared_ptr<EditSession> pSession   = _pSession;
        EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, index]()
        {
            pSession->detach( generation, parentOrdinal, index, shared_ptr<RemovedWidget>{} );
        } ),
                                 SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, typeName, parentOrdinal, index]()
        {
            pSession->insert( generation, typeName, parentOrdinal, index );
        } ),
                                 "Add UI Widget" );
        return true;
    }

    bool UIPreviewPanel::deleteSelected( UIScreen& screen )
    {
        WidgetTree& tree    = screen.getTree();
        Widget*     pWidget = tree.findWidgetByID( _selected );
        if ( pWidget == nullptr || pWidget->getParent() == nullptr )
            return false; // 루트는 지우지 않는다(문서에는 루트가 있어야 한다)
        PanelWidget* pParent = pWidget->getParent();
        const uint32 index   = pParent->findChildIndex( pWidget );
        if ( index == invalid_index::kUint32 )
            return false;
        const uint32 parentOrdinal = UIPreviewLogic::findWidgetOrdinal( tree, pParent->getID() );
        _pointer.clearHover();
        shared_ptr<RemovedWidget> pHolder = sw::make_shared<RemovedWidget>();
        pHolder->_pWidget                 = UIPreviewLogic::detachWidget( tree, parentOrdinal, index );
        if ( pHolder->_pWidget == nullptr )
            return false;
        afterStructureEdit( screen, parentOrdinal );
        const uint32                  generation = _generation;
        const shared_ptr<EditSession> pSession   = _pSession;
        EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, index, pHolder]()
        {
            pSession->reattach( generation, parentOrdinal, index, pHolder );
        } ),
                                 SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, index, pHolder]()
        {
            pSession->detach( generation, parentOrdinal, index, pHolder );
        } ),
                                 "Delete UI Widget" );
        return true;
    }

    void UIPreviewPanel::applyInsert( uint32 generation, const hashed_string& typeName, uint32 parentOrdinal, uint32 index )
    {
        UIScreen*       pScreen   = generation == _generation ? findPreviewScreen() : nullptr;
        TypeRegistry*   pRegistry = editor::findSubsystem<TypeRegistry>();
        const TypeInfo* pType     = pRegistry != nullptr ? pRegistry->findType( typeName ) : nullptr;
        if ( pScreen == nullptr || pType == nullptr )
            return;
        Widget* pAdded = UIPreviewLogic::insertWidget( pScreen->getTree(), *pType, parentOrdinal, index );
        if ( pAdded != nullptr )
            afterStructureEdit( *pScreen, UIPreviewLogic::findWidgetOrdinal( pScreen->getTree(), pAdded->getID() ) );
    }

    void UIPreviewPanel::applyDetach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder )
    {
        UIScreen* pScreen = generation == _generation ? findPreviewScreen() : nullptr;
        if ( pScreen == nullptr )
            return;
        _pointer.clearHover();
        unique_ptr<Widget> removed = UIPreviewLogic::detachWidget( pScreen->getTree(), parentOrdinal, index );
        if ( removed == nullptr )
            return;
        if ( pHolder != nullptr )
            pHolder->_pWidget = std::move( removed ); // 지우기를 다시 하면 되돌릴 때 끼울 위젯을 다시 맡는다
        afterStructureEdit( *pScreen, parentOrdinal );
    }

    void UIPreviewPanel::applyReattach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder )
    {
        UIScreen*  pScreen             = generation == _generation ? findPreviewScreen() : nullptr;
        const bool bCannotAttachWidget = pScreen == nullptr || pHolder == nullptr || pHolder->_pWidget == nullptr;
        if ( bCannotAttachWidget )
            return;
        Widget* pAttached = UIPreviewLogic::attachWidget( pScreen->getTree(), std::move( pHolder->_pWidget ), parentOrdinal, index );
        if ( pAttached != nullptr )
            afterStructureEdit( *pScreen, UIPreviewLogic::findWidgetOrdinal( pScreen->getTree(), pAttached->getID() ) );
    }

    void UIPreviewPanel::applyEditedText( uint32 generation, uint32 ordinal, const string& propertyName, const string& text )
    {
        if ( generation != _generation )
            return; // 그 뒤에 트리를 다시 지었다 — 옛 항목이 새 트리에 값을 쓰지 않는다
        UISystem* pUI     = editor::findSubsystem<UISystem>();
        UIScreen* pScreen = pUI != nullptr ? pUI->getOffscreenScreens().findScreen( _screen ) : nullptr;
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
        const string previous = _documentPath.get();
        if ( _documentPath.drawField( "##uipreviewdoc", 320.0f * EditorThemeUtil::getDpiScale() ) )
        {
            if ( isDocumentDirty() )
                SW_LOG_WARNING( "UI Preview: unsaved edits of '%#' are discarded because the path changed", previous.c_str() );
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
        const UIResolutionPreset* pSelectedResolution = findSelectedResolution();
        const bool                bResolutionOpen     = ImGui::BeginCombo( "##uipreviewres", pSelectedResolution != nullptr ? pSelectedResolution->_name.c_str() : "-" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "ResolutionTip" ) );
        if ( bResolutionOpen )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( _resolutionTable._listResolution.size() ); ++index )
            {
                if ( ImGui::Selectable( _resolutionTable._listResolution[index]._name.c_str(), index == _resolutionIndex ) )
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
        UISystem& ui     = editor::getSubsystem<UISystem>();
        _bNavigationMode = true;
        ui.getOffscreenScreens().setNavigationMode( _screen, true );
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
        UISystem* pUI = editor::findSubsystem<UISystem>();
        if ( ImGui::Checkbox( "Interact", &_bInteract ) && pUI != nullptr )
        {
            _bNavigationMode = false;
            pUI->getOffscreenScreens().setNavigationMode( _screen, false );
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
        float32 scrubSeconds = time;
        ImGui::SetNextItemWidth( 220.0f * EditorThemeUtil::getDpiScale() );
        if ( ImGui::SliderFloat( "##uipreviewanimationtime", &scrubSeconds, 0.0f, MathUtil::max( duration, 0.001f ), "%.2f s" ) )
        {
            _animationScrub   = scrubSeconds;
            _bAnimationPaused = true;
            (void)player.seek( _animation, scrubSeconds );
            markPreviewMutated();
        }
        else if ( bActive )
        {
            _animationScrub = time;
        }
        UIPreviewPanelInternal::noteMark( "uipreview", "animationtime" );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", UIPreviewLogic::makeTimeLabel( bActive ? time : scrubSeconds, duration ).c_str() );
    }

    void UIPreviewPanel::acceptPaletteDrop( UIScreen& screen, Widget* pTarget )
    {
        if ( ImGui::BeginDragDropTarget() == false )
            return;
        const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( UIPreviewPanelInternal::kPaletteDragPayload );
        if ( pPayload != nullptr && pPayload->Data != nullptr )
            (void)addWidget( screen, hashed_string( static_cast<const utf8*>( pPayload->Data ) ), pTarget ); // 못 붙이면 addWidget 이 경고로 알린다
        ImGui::EndDragDropTarget();
    }

    void UIPreviewPanel::drawPalette( UIScreen& screen )
    {
        using Internal          = UIPreviewPanelInternal;
        TypeRegistry* pRegistry = editor::findSubsystem<TypeRegistry>();
        const uint32  frame     = static_cast<uint32>( ImGui::GetFrameCount() );
        if ( pRegistry != nullptr && ( _listPaletteType.empty() || frame - _paletteFrame > Internal::kPaletteRefreshFrames ) )
        {
            UIPreviewLogic::collectPaletteTypes( *pRegistry, _listPaletteType );
            _paletteFrame = frame;
        }

        const bool bCanAdd = _paletteType.empty() == false;
        ImGui::BeginDisabled( bCanAdd == false );
        if ( ImGui::Button( "Add" ) )
            (void)addWidget( screen, _paletteType, screen.getTree().findWidgetByID( _selected ) );
        ImGui::EndDisabled();
        Internal::noteMark( "uipreview.palette", "add" );
        EditorWidgets::drawTooltip( "Add the chosen widget type into the selected panel (after the selected widget when it is not a panel). Drag an entry onto a tree row or the picture instead to choose the place." );
        ImGui::SameLine();
        const bool bCanDelete = screen.getTree().findWidgetByID( _selected ) != nullptr && screen.getTree().findWidgetByID( _selected )->getParent() != nullptr;
        ImGui::BeginDisabled( bCanDelete == false );
        if ( ImGui::Button( "Delete" ) )
            (void)deleteSelected( screen ); // 루트나 없는 선택은 못 지우고, 그때는 단추가 꺼져 있다
        ImGui::EndDisabled();
        Internal::noteMark( "uipreview", "delete" );
        EditorWidgets::drawTooltip( "Remove the selected widget and its children (Undo puts them back). The root cannot be removed." );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.0f );
        (void)EditorWidgets::drawTextField( "##uipreviewpalettefilter", _paletteFilter );
        Internal::noteMark( "uipreview.palette", "filter" );

        ImGui::BeginChild( "##uipreviewpalettelist", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_None );
        for ( const TypeInfo* pType : _listPaletteType )
        {
            const utf8* pName = pType->_name.c_str();
            if ( _paletteFilter.empty() == false && StringUtil::stristr( pName, _paletteFilter.c_str() ) == nullptr )
                continue;
            const bool bChosen = _paletteType == pType->_name;
            if ( ImGui::Selectable( pName, bChosen, ImGuiSelectableFlags_AllowDoubleClick ) )
            {
                _paletteType = pType->_name;
                if ( ImGui::IsMouseDoubleClicked( 0 ) )
                    (void)addWidget( screen, _paletteType, screen.getTree().findWidgetByID( _selected ) );
            }
            Internal::noteMark( "uipreview.palette", pName );
            // 끌어서 트리 줄이나 이미지에 놓는다 — 놓는 쪽이 받는 패널을 고른다(`UIPreviewLogic::findDropContainer`).
            if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
            {
                ImGui::SetDragDropPayload( Internal::kPaletteDragPayload, pName, StringUtil::strlen( pName ) + 1 );
                ImGui::TextUnformatted( pName );
                ImGui::EndDragDropSource();
            }
        }
        ImGui::EndChild();
    }

    void UIPreviewPanel::drawSlotHandles( UIScreen& screen, const float2& drawSize, const float2& origin )
    {
        using Internal             = UIPreviewPanelInternal;
        Widget*    pWidget         = screen.getTree().findWidgetByID( _selected );
        const bool bCannotEditSlot = pWidget == nullptr || UIPreviewLogic::canEditSlotByHandle( *pWidget ) == false || drawSize._x <= 0.0f || drawSize._y <= 0.0f;
        if ( bCannotEditSlot )
            return;
        const PropertyInfo* pSlotProp = UIPreviewLogic::findWidgetProperty( *pWidget, "_slot" );
        if ( pSlotProp == nullptr )
            return;
        const UIRect  box  = UIPreviewLogic::mapUIToImage( pWidget->getGeometry().computeScreenBounds(), drawSize, _viewport );
        const float32 half = Internal::kHandleSize * EditorThemeUtil::getDpiScale() * 0.5f;
        const float2  center{ origin._x + ( box._left + box._right ) * 0.5f, origin._y + ( box._top + box._bottom ) * 0.5f };
        struct HandleRow
        {
            UIPreviewSlotHandle _handle;
            const utf8*         _pMark;
            float2              _center;
        };
        const HandleRow arrHandle[] = {
            {  UIPreviewSlotHandle::Move,   "move",                                       center},
            {  UIPreviewSlotHandle::Left,   "left",   float2{ origin._x + box._left, center._y }},
            {   UIPreviewSlotHandle::Top,    "top",    float2{ center._x, origin._y + box._top }},
            { UIPreviewSlotHandle::Right,  "right",  float2{ origin._x + box._right, center._y }},
            {UIPreviewSlotHandle::Bottom, "bottom", float2{ center._x, origin._y + box._bottom }},
        };
        // 마우스 이동량(화면 픽셀)을 UI 단위로 옮기는 배율 — 이미지가 뷰포트보다 작게 그려진다.
        const float32 toUIX = _viewport._size._x / drawSize._x;
        const float32 toUIY = _viewport._size._y / drawSize._y;
        for ( const HandleRow& row : arrHandle )
        {
            ImGui::SetCursorScreenPos( ImVec2{ row._center._x - half, row._center._y - half } );
            ImGui::PushID( row._pMark );
            ImGui::InvisibleButton( "##handle", ImVec2{ half * 2.0f, half * 2.0f } );
            Internal::noteMark( "uipreview.handle", row._pMark );
            ImGui::GetWindowDrawList()->AddRectFilled( ImVec2{ row._center._x - half, row._center._y - half }, ImVec2{ row._center._x + half, row._center._y + half },
                                                       EditorColorUtil::toImU32( EditorThemeUtil::getHighlightColor() ) );
            if ( ImGui::IsItemActivated() )
            {
                _bSlotDragging    = true;
                _slotDragOrdinal  = _selectedOrdinal;
                _slotDragBaseline = UIPreviewLogic::formatWidgetProperty( *pWidget, *pSlotProp );
            }
            if ( ImGui::IsItemActive() && _bSlotDragging )
            {
                const ImVec2     mouseDelta = ImGui::GetIO().MouseDelta;
                WidgetLayoutSlot slot       = pWidget->getLayoutSlot();
                if ( UIPreviewLogic::applySlotDrag( slot, row._handle, float2{ mouseDelta.x * toUIX, mouseDelta.y * toUIY } ) )
                    pWidget->setLayoutSlot( slot );
            }
            if ( ImGui::IsItemDeactivated() && _bSlotDragging )
            {
                _bSlotDragging       = false;
                const string newText = UIPreviewLogic::formatWidgetProperty( *pWidget, *pSlotProp );
                if ( newText != _slotDragBaseline )
                {
                    pushPropertyEdit( _slotDragOrdinal, "_slot", _slotDragBaseline, newText, string{} );
                    markDocumentDirty();
                    _baselineWidget = kInvalidWidgetID; // 인스펙터의 기준 값을 다시 잡는다
                }
            }
            ImGui::PopID();
        }
    }

    void UIPreviewPanel::drawLayoutHandle( UIScreen& screen, const float2& drawSize, const float2& origin )
    {
        using Internal                = UIPreviewPanelInternal;
        WidgetTree& tree              = screen.getTree();
        Widget*     pWidget           = tree.findWidgetByID( _selected );
        const bool  bCannotEditLayout = pWidget == nullptr || drawSize._x <= 0.0f || drawSize._y <= 0.0f;
        if ( bCannotEditLayout )
            return;
        PanelWidget*     pParent = pWidget->getParent();
        const BoxPanel*  pBox    = castTo<const BoxPanel>( pParent );
        const GridPanel* pGrid   = castTo<const GridPanel>( pParent );
        if ( pParent == nullptr || ( pBox == nullptr && pGrid == nullptr ) )
            return;
        const UIRect  box  = UIPreviewLogic::mapUIToImage( pWidget->getGeometry().computeScreenBounds(), drawSize, _viewport );
        const float32 half = Internal::kHandleSize * EditorThemeUtil::getDpiScale() * 0.5f;
        const ImVec2  center{ origin._x + ( box._left + box._right ) * 0.5f, origin._y + ( box._top + box._bottom ) * 0.5f };
        const ImU32   color = EditorColorUtil::toImU32( EditorThemeUtil::getHighlightColor() );
        ImGui::SetCursorScreenPos( ImVec2{ center.x - half, center.y - half } );
        ImGui::PushID( "layouthandle" );
        ImGui::InvisibleButton( "##reorder", ImVec2{ half * 2.0f, half * 2.0f } );
        Internal::noteMark( "uipreview.handle", "reorder" );
        ImGui::GetWindowDrawList()->AddCircleFilled( center, half, color );
        if ( ImGui::IsItemActivated() )
            _bLayoutDragging = true;
        if ( ImGui::IsItemActive() && _bLayoutDragging )
            ImGui::GetWindowDrawList()->AddLine( center, ImGui::GetMousePos(), color, 2.0f ); // 놓일 위치 — 놓을 때까지 트리는 그대로다
        const bool bDropped = ImGui::IsItemDeactivated() && _bLayoutDragging;
        ImGui::PopID();
        if ( bDropped == false )
            return;
        _bLayoutDragging   = false;
        const ImVec2 mouse = ImGui::GetMousePos();
        const float2 point = UIPreviewLogic::mapImageToUI( float2{ mouse.x - origin._x, mouse.y - origin._y }, drawSize, _viewport );
        if ( pBox != nullptr )
        {
            // 상자: 자식 가운데(상자 축)와 놓은 점을 비교해 사이 위치를 고르고, 분리해 다시 끼운다 — 구조 편집이라 부모 번호와 자식 번호로 되돌린다.
            const bool      bVertical = pBox->getOrientation() == UIOrientation::Vertical;
            vector<float32> listCenter;
            for ( uint32 childIndex = 0; childIndex < pParent->getChildCount(); ++childIndex )
            {
                const float2 childCenter = pParent->getChild( childIndex )->getGeometry().computeScreenBounds().getCenter();
                listCenter.push_back( bVertical ? childCenter._y : childCenter._x );
            }
            const uint32 fromIndex = pParent->findChildIndex( pWidget );
            if ( fromIndex == invalid_index::kUint32 )
                return;
            const uint32 toIndex = UIPreviewLogic::findReorderIndex( fromIndex, UIPreviewLogic::findBoxInsertIndex( listCenter, bVertical ? point._y : point._x ) );
            if ( toIndex == fromIndex )
                return;
            const uint32 parentOrdinal = UIPreviewLogic::findWidgetOrdinal( tree, pParent->getID() );
            applyMove( _generation, parentOrdinal, fromIndex, toIndex );
            ++_slotEditCount;
            const uint32                  generation = _generation;
            const shared_ptr<EditSession> pSession   = _pSession;
            EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, fromIndex, toIndex]()
            {
                pSession->move( generation, parentOrdinal, toIndex, fromIndex );
            } ),
                                     SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, fromIndex, toIndex]()
            {
                pSession->move( generation, parentOrdinal, fromIndex, toIndex );
            } ),
                                     "Reorder UI Widget" );
            return;
        }
        // 격자: 놓은 점이 속한 행 · 열(배치가 정한 줄)을 슬롯에 쓴다 — 슬롯 편집이라 캔버스 손잡이와 같이 `_slot` 텍스트 전후를 남긴다.
        const PropertyInfo* pSlotProp = UIPreviewLogic::findWidgetProperty( *pWidget, "_slot" );
        if ( pSlotProp == nullptr )
            return;
        uint32 gridRow{ 0 };
        uint32 gridColumn{ 0 };
        if ( UIPreviewLogic::findGridCell( *pGrid, point, gridRow, gridColumn ) == false )
            return;
        const uint32     row    = MathUtil::min( gridRow, 0xFFFFu );
        const uint32     column = MathUtil::min( gridColumn, 0xFFFFu );
        WidgetLayoutSlot slot   = pWidget->getLayoutSlot();
        if ( slot._row == row && slot._column == column )
            return;
        const string oldText = UIPreviewLogic::formatWidgetProperty( *pWidget, *pSlotProp );
        slot._row            = static_cast<uint16>( row );
        slot._column         = static_cast<uint16>( column );
        pWidget->setLayoutSlot( slot );
        const string newText = UIPreviewLogic::formatWidgetProperty( *pWidget, *pSlotProp );
        if ( newText == oldText )
            return;
        pushPropertyEdit( _selectedOrdinal, "_slot", oldText, newText, string{} );
        markDocumentDirty();
        _baselineWidget = kInvalidWidgetID; // 인스펙터의 기준 값을 다시 잡는다
    }

    void UIPreviewPanel::applyMove( uint32 generation, uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex )
    {
        UIScreen* pScreen = generation == _generation ? findPreviewScreen() : nullptr;
        if ( pScreen == nullptr || fromIndex == toIndex )
            return;
        // 자식을 분리하고 끼워도 부모의 문서 순서 번호는 그대로다(부모가 자식보다 앞에 온다).
        WidgetTree& tree = pScreen->getTree();
        _pointer.clearHover();
        unique_ptr<Widget> moved = UIPreviewLogic::detachWidget( tree, parentOrdinal, fromIndex );
        if ( moved == nullptr )
            return;
        Widget* pAttached = UIPreviewLogic::attachWidget( tree, std::move( moved ), parentOrdinal, toIndex );
        if ( pAttached != nullptr )
            afterStructureEdit( *pScreen, UIPreviewLogic::findWidgetOrdinal( tree, pAttached->getID() ) );
    }

    void UIPreviewPanel::finishSlotField( Widget& widget, const PropertyInfo& slotProp, const string& frameText, bool bChanged, bool bImmediate )
    {
        if ( ImGui::IsItemActivated() )
            _slotFieldBaseline = frameText;
        const bool bCommit = bImmediate ? bChanged : ImGui::IsItemDeactivatedAfterEdit();
        if ( bCommit == false )
            return;
        const string& oldText = bImmediate ? frameText : _slotFieldBaseline;
        const string  newText = UIPreviewLogic::formatWidgetProperty( widget, slotProp );
        if ( newText == oldText )
            return;
        pushPropertyEdit( _selectedOrdinal, "_slot", oldText, newText, string{} );
        markDocumentDirty();
        _baselineWidget = kInvalidWidgetID; // 인스펙터의 기준 값을 다시 잡는다
    }

    struct UIPreviewPanel::SlotFieldPass
    {
        static constexpr float32 kDragSpeed   = 0.5f;
        static constexpr float32 kAnchorSpeed = 0.005f;
        static constexpr uint16  kOneSpan     = 1;

        UIPreviewPanel&     _panel;
        Widget&             _widget;
        const PropertyInfo& _slotProp;
        EditorListFilter    _filter;
        WidgetLayoutSlot    _slot;            ///< 컨트롤 하나가 고치는 슬롯 사본. 컨트롤마다 위젯의 현재 슬롯으로 다시 채운다
        bool                _bDrawn{ false }; ///< 바로 앞 field 를 그렸다 — 그리지 않았으면 뒤의 툴팁이 남의 위젯에 붙지 않게 건너뛴다

        /** @brief 컨트롤 하나 = 슬롯 사본을 @p draw 가 고치고(고쳤으면 true), 바뀌면 바로 위젯에 넣고(레이아웃이 따라 움직인다), 편집을 맺는다. */
        template <typename Draw>
        void field( const utf8* pMark, const utf8* pLabel, bool bImmediate, Draw&& draw )
        {
            // 인스펙터 검색어가 레이블에 없으면 그리지 않는다(언리얼 Details 의 검색이 슬롯 범주의 행도 거르는 것과 같다).
            _bDrawn = _filter.matches( pLabel );
            if ( _bDrawn == false )
                return;
            const string beforeText = UIPreviewLogic::formatWidgetProperty( _widget, _slotProp );
            _slot                   = _widget.getLayoutSlot();
            const bool bChanged     = draw( _slot, pLabel );
            if ( bChanged )
                _widget.setLayoutSlot( _slot );
            UIPreviewPanelInternal::noteMark( "uipreview.slot", pMark );
            _panel.finishSlotField( _widget, _slotProp, beforeText, bChanged, bImmediate );
        }

        void tooltip( const utf8* pTooltip ) const
        {
            if ( _bDrawn )
                EditorWidgets::drawTooltip( pTooltip );
        }

        /** @brief 열거값 하나를 콤보로 고릅니다. 고르면 @p inoutValue 를 바꾸고 true 입니다. */
        template <typename Enum>
        static bool comboEnum( const utf8* pLabel, Enum& inoutValue, const utf8* const* ppArrName, int32 count )
        {
            int32 index = static_cast<int32>( inoutValue );
            if ( ImGui::Combo( pLabel, &index, ppArrName, count ) == false )
                return false;
            inoutValue = static_cast<Enum>( index );
            return true;
        }

        void drawPaddingAlignment()
        {
            static const utf8* const kArrAlignment[] = { "Fill", "Start", "Center", "End" };
            field( "padding", SW_LOCKEY( "EditorUIPreview", "SlotPadding" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat4( pLabel, &s._padding._x, kDragSpeed ); } );
            tooltip( SW_LOCKEY( "EditorUIPreview", "SlotPaddingTip" ) );
            field( "halign", SW_LOCKEY( "EditorUIPreview", "SlotHorizontalAlignment" ), true, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return comboEnum( pLabel, s._horizontalAlignment, kArrAlignment, 4 ); } );
            field( "valign", SW_LOCKEY( "EditorUIPreview", "SlotVerticalAlignment" ), true, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return comboEnum( pLabel, s._verticalAlignment, kArrAlignment, 4 ); } );
        }

        void drawBoxSize()
        {
            static const utf8* const kArrSizeRule[] = { "Auto", "Fill" };
            field( "sizerule", SW_LOCKEY( "EditorUIPreview", "SlotSizeRule" ), true, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return comboEnum( pLabel, s._sizeRule, kArrSizeRule, 2 ); } );
            tooltip( SW_LOCKEY( "EditorUIPreview", "SlotSizeRuleTip" ) );
            ImGui::BeginDisabled( _slot._sizeRule != UISizeRule::Fill );
            field( "fillweight", SW_LOCKEY( "EditorUIPreview", "SlotFillWeight" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat( pLabel, &s._fillWeight, 0.01f, 0.0f, 1000.0f ); } );
            ImGui::EndDisabled();
        }

        void drawGrid()
        {
            field( "row", SW_LOCKEY( "EditorUIPreview", "SlotRow" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragScalar( pLabel, ImGuiDataType_U16, &s._row, 0.1f ); } );
            field( "column", SW_LOCKEY( "EditorUIPreview", "SlotColumn" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragScalar( pLabel, ImGuiDataType_U16, &s._column, 0.1f ); } );
            field( "rowspan", SW_LOCKEY( "EditorUIPreview", "SlotRowSpan" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragScalar( pLabel, ImGuiDataType_U16, &s._rowSpan, 0.1f, &kOneSpan ); } );
            field( "columnspan", SW_LOCKEY( "EditorUIPreview", "SlotColumnSpan" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragScalar( pLabel, ImGuiDataType_U16, &s._columnSpan, 0.1f, &kOneSpan ); } );
        }

        /** @brief 앵커 프리셋(UMG 의 앵커 드롭다운)입니다. 앵커만 바꾸고 위젯은 제자리에 둡니다. 지금 앵커와 맞는 프리셋이 없으면 빈 항목입니다. */
        void drawAnchorPreset()
        {
            field( "anchorpreset", SW_LOCKEY( "EditorUIPreview", "SlotAnchorPreset" ), true, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            {
                const utf8* const arrPresetName[] = {
                    SW_LOCKEY( "EditorUIPreview", "AnchorTopLeft" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorTopCenter" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorTopRight" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorCenterLeft" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorCenter" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorCenterRight" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorBottomLeft" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorBottomCenter" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorBottomRight" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorStretchHorizontal" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorStretchVertical" ),
                    SW_LOCKEY( "EditorUIPreview", "AnchorStretchAll" ),
                };
                constexpr int32 kPresetCount = static_cast<int32>( UIPreviewAnchorPreset::Count );
                static_assert( sizeof( arrPresetName ) / sizeof( arrPresetName[0] ) == static_cast<size_t>( kPresetCount ), "anchor preset name count" );
                int32 picked = -1;
                for ( int32 index = 0; index < kPresetCount; ++index )
                {
                    const UIPreviewAnchorValues values               = UIPreviewLogic::getAnchorPreset( static_cast<UIPreviewAnchorPreset>( index ) );
                    const bool                  bMatchesAnchorPreset = values._anchorMin._x == s._anchorMin._x && values._anchorMin._y == s._anchorMin._y && values._anchorMax._x == s._anchorMax._x && values._anchorMax._y == s._anchorMax._y;
                    if ( bMatchesAnchorPreset )
                    {
                        picked = index;
                        break;
                    }
                }
                if ( ImGui::Combo( pLabel, &picked, arrPresetName, kPresetCount ) == false || picked < 0 )
                    return false;
                const WidgetGeometry& geometry       = _widget.getGeometry();
                const WidgetGeometry& parentGeometry = _widget.getParent()->getGeometry();
                const float32         left           = geometry._position._x - parentGeometry._position._x;
                const float32         top            = geometry._position._y - parentGeometry._position._y;
                const UIRect          localRect      = UIRect::makeFromPositionSize( left, top, geometry._size._x, geometry._size._y );
                return UIPreviewLogic::applyAnchorPreset( s, static_cast<UIPreviewAnchorPreset>( picked ), localRect, parentGeometry._size );
            } );
            tooltip( SW_LOCKEY( "EditorUIPreview", "SlotAnchorPresetTip" ) );
        }

        void drawCanvas()
        {
            static const utf8* const kArrGrow[] = { "End", "Begin", "Both" };
            drawAnchorPreset();
            field( "anchormin", SW_LOCKEY( "EditorUIPreview", "SlotAnchorMin" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat2( pLabel, &s._anchorMin._x, kAnchorSpeed, 0.0f, 1.0f ); } );
            field( "anchormax", SW_LOCKEY( "EditorUIPreview", "SlotAnchorMax" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat2( pLabel, &s._anchorMax._x, kAnchorSpeed, 0.0f, 1.0f ); } );
            field( "offsetmin", SW_LOCKEY( "EditorUIPreview", "SlotOffsetMin" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat2( pLabel, &s._offsetMin._x, kDragSpeed ); } );
            field( "offsetmax", SW_LOCKEY( "EditorUIPreview", "SlotOffsetMax" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat2( pLabel, &s._offsetMax._x, kDragSpeed ); } );
            field( "autosize", SW_LOCKEY( "EditorUIPreview", "SlotAutoSize" ), true, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::Checkbox( pLabel, &s._bAutoSize ); } );
            field( "growh", SW_LOCKEY( "EditorUIPreview", "SlotGrowHorizontal" ), true, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return comboEnum( pLabel, s._growHorizontal, kArrGrow, 3 ); } );
            field( "growv", SW_LOCKEY( "EditorUIPreview", "SlotGrowVertical" ), true, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return comboEnum( pLabel, s._growVertical, kArrGrow, 3 ); } );
            field( "zorder", SW_LOCKEY( "EditorUIPreview", "SlotZOrder" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragScalar( pLabel, ImGuiDataType_S16, &s._zOrder, 0.1f ); } );
        }

        /** @brief 크기 덮어쓰기 · 최소 · 최대입니다. 모든 패널이 읽습니다(UMG 의 SizeBox 를 슬롯 필드로 옮긴 것). */
        void drawSizeOverrides()
        {
            field( "width", SW_LOCKEY( "EditorUIPreview", "SlotWidthOverride" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat( pLabel, &s._widthOverride, kDragSpeed, 0.0f, 100000.0f ); } );
            field( "height", SW_LOCKEY( "EditorUIPreview", "SlotHeightOverride" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat( pLabel, &s._heightOverride, kDragSpeed, 0.0f, 100000.0f ); } );
            tooltip( SW_LOCKEY( "EditorUIPreview", "SlotOverrideTip" ) );
            field( "minsize", SW_LOCKEY( "EditorUIPreview", "SlotMinSize" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat2( pLabel, &s._minSize._x, kDragSpeed, 0.0f, 100000.0f ); } );
            field( "maxsize", SW_LOCKEY( "EditorUIPreview", "SlotMaxSize" ), false, [&]( WidgetLayoutSlot& s, const utf8* pLabel )
            { return ImGui::DragFloat2( pLabel, &s._maxSize._x, kDragSpeed, 0.0f, 100000.0f ); } );
            tooltip( SW_LOCKEY( "EditorUIPreview", "SlotMaxSizeTip" ) );
        }
    };

    void UIPreviewPanel::drawSlotSection( Widget& widget )
    {
        const UIPreviewSlotFields fields = UIPreviewLogic::findSlotFields( widget );
        if ( fields._bSizeOverride == false )
            return; // 루트 — 부모가 없어 슬롯을 읽는 쪽이 없다
        const PropertyInfo* pSlotProp = UIPreviewLogic::findWidgetProperty( widget, "_slot" );
        if ( pSlotProp == nullptr )
            return;
        const TypeInfo* pParentType = widget.getParent()->getTypeInfo();
        const string    header      = string{ SW_LOCKEY( "EditorUIPreview", "SlotHeader" ) } + " (" + ( pParentType != nullptr ? pParentType->_name.c_str() : "Panel" ) + ")###uipreviewslot";
        if ( ImGui::CollapsingHeader( header.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;

        SlotFieldPass pass{ *this, widget, *pSlotProp, EditorListFilter{ _grid.getFilterText() }, widget.getLayoutSlot() };
        if ( fields._bPaddingAlignment )
            pass.drawPaddingAlignment();
        if ( fields._bBoxSize )
            pass.drawBoxSize();
        if ( fields._bGrid )
            pass.drawGrid();
        if ( fields._bCanvas )
            pass.drawCanvas();
        pass.drawSizeOverrides();
        ImGui::Separator();
    }

    void UIPreviewPanel::drawWidgetList( UIScreen& screen )
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
            acceptPaletteDrop( screen, screen.getTree().findWidgetByID( row._widget ) ); // 팔레트 항목을 이 줄에 놓는다
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
        // 검색창이 맨 위다(언리얼 Details 처럼) — 슬롯 구역도 이 검색어로 거른다.
        _grid.drawSearchBar( "uipreview.inspector.search" );
        drawSlotSection( *pWidget );
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
            const bool bPointerEnteredOrMoved = _bPointerInside == false || event._delta._x != 0.0f || event._delta._y != 0.0f;
            if ( bPointerEnteredOrMoved )
            {
                if ( _bNavigationMode )
                {
                    UISystem* pUI    = editor::findSubsystem<UISystem>();
                    _bNavigationMode = false;
                    if ( pUI != nullptr )
                        pUI->getOffscreenScreens().setNavigationMode( _screen, false ); // 마우스를 움직이면 포인터 방식으로 돌아간다(포커스 테두리를 숨긴다)
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
            // 이미지 밖에서 놓은 버튼도 위젯이 잡고 있으면 Up 을 보낸다(슬라이더를 끌다가 바깥에서 놓는 경우).
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
        if ( _bInteract == false && ImGui::BeginDragDropTarget() )
        {
            // 팔레트 항목을 이미지 위에 놓는다 — 놓은 점 아래 위젯이 받는 쪽을 정한다(패널이면 그 안, 아니면 그 뒤).
            const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( Internal::kPaletteDragPayload );
            if ( pPayload != nullptr && pPayload->Data != nullptr )
            {
                const ImVec2   mouse  = ImGui::GetMousePos();
                const float2   point  = UIPreviewLogic::mapImageToUI( float2{ mouse.x - origin._x, mouse.y - origin._y }, drawSize, _viewport );
                const WidgetID target = UIPreviewLogic::findWidgetAt( screen.getTree(), point );
                (void)addWidget( screen, hashed_string( static_cast<const utf8*>( pPayload->Data ) ), screen.getTree().findWidgetByID( target ) ); // 못 붙이면 addWidget 이 경고로 알린다
            }
            ImGui::EndDragDropTarget();
        }
        // 이미지 클릭으로 고르기는 손잡이를 그린 뒤에 정한다 — 손잡이는 이미지 위에 겹쳐 있어, 여기서 바로 고르면 손잡이를 누른 클릭이 그 아래 위젯(자식)을
        // 골라 손잡이가 사라지고 끌기가 시작되지 않는다.
        const bool bImageClicked = _bInteract == false && ImGui::IsItemClicked();
        if ( _bInteract )
            feedPointer( screen, drawSize, origin, bImageHovered );

        // 겹쳐 그리기 — 레이아웃 사각형 · 고른 위젯 · 안전 영역(이미지 위 ImGui 선, 캔버스는 바꾸지 않는다).
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
            drawBox( UIPreviewLogic::mapUIToImage( pSelected->getGeometry().computeScreenBounds(), drawSize, _viewport ), EditorColorUtil::toImU32( EditorThemeUtil::getHighlightColor() ), 2.0f );
        const float4& insets         = _viewport._safeInsets;
        const bool    bHasSafeInsets = insets._x > 0.0f || insets._y > 0.0f || insets._z > 0.0f || insets._w > 0.0f;
        if ( bHasSafeInsets )
        {
            const UIRect safe{ insets._x, insets._y, _viewport._size._x - insets._z, _viewport._size._y - insets._w };
            drawBox( UIPreviewLogic::mapUIToImage( safe, drawSize, _viewport ), EditorColorUtil::toImU32( Internal::kSafeZoneColor ), 1.0f );
        }
        // 캔버스 자식의 슬롯 손잡이(옮기기 · 네 변) — 끌면 슬롯의 오프셋이 바뀌고 놓으면 되돌리기 항목 하나가 쌓인다. 입력 흉내 중에는 숨긴다(포인터를 가로채지 않게).
        if ( _bInteract == false )
        {
            drawSlotHandles( screen, drawSize, origin );
            drawLayoutHandle( screen, drawSize, origin );
        }
        const bool bSelectWidgetByClick = bImageClicked && _bSlotDragging == false && _bLayoutDragging == false;
        if ( bSelectWidgetByClick )
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float2 point = UIPreviewLogic::mapImageToUI( float2{ mouse.x - origin._x, mouse.y - origin._y }, drawSize, _viewport );
            _selected          = UIPreviewLogic::findWidgetAt( screen.getTree(), point );
            _selectedOrdinal   = _selected != kInvalidWidgetID ? UIPreviewLogic::findWidgetOrdinal( screen.getTree(), _selected ) : UIPreviewLogic::kNoOrdinal;
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
            _probe._bSelectedValid     = true;
            _probe._selectedOpacity    = pSelected->getOpacity();
            _probe._bSlotEditable      = UIPreviewLogic::canEditSlotByHandle( *pSelected );
            _probe._selectedOffsetMinX = pSelected->getLayoutSlot()._offsetMin._x;
        }
        _probe._paletteCount  = static_cast<uint32>( _listPaletteType.size() );
        _probe._slotEditCount = _slotEditCount;
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
        UISystem* pUI  = editor::findSubsystem<UISystem>();
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
        UIScreen* pScreen = pUI->getOffscreenScreens().findScreen( _screen );
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

        // 왼쪽 영역: 위젯 트리, 팔레트, 고른 위젯의 프로퍼티(인스펙터)를 위에서 아래로. 오른쪽: 미리보기 이미지.
        const float32 dpi = EditorThemeUtil::getDpiScale();
        ImGui::BeginChild( "##uipreviewleft", ImVec2{ Internal::kLeftWidth * dpi, 0.0f }, ImGuiChildFlags_None );
        const float32 treeHeight    = ImGui::GetContentRegionAvail().y * Internal::kTreeRatio;
        const float32 paletteHeight = ImGui::GetContentRegionAvail().y * Internal::kPaletteRatio;
        ImGui::BeginChild( "##uipreviewtree", ImVec2{ 0.0f, treeHeight }, ImGuiChildFlags_Borders );
        drawWidgetList( *pScreen );
        ImGui::EndChild();
        ImGui::BeginChild( "##uipreviewpalette", ImVec2{ 0.0f, paletteHeight }, ImGuiChildFlags_Borders );
        drawPalette( *pScreen );
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
