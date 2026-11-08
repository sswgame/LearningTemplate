#include "pch.h"

#include "Editor/Panels/UIPreview/UIPreviewPanel.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Backend/ImGuiRenderer.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorViewSurface.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"
#include "Engine/UI/Screen/UIOffscreenScreens.h"
#include "Engine/UI/Style/UIThemeController.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetNavigation.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( UIPreviewPanel, UIPreviewPanel::kPanelID, EditorPanelCategory::Tool, 950 );

    namespace
    {
        struct UIPreviewPanelInternal
        {
            static constexpr float32 kLeftColumns          = 45.0f;
            static constexpr float32 kLanguageComboColumns = 20.0f; ///< 언어 콤보 폭(글자 칸) — 줄바꿈 판단과 폭이 같은 값을 쓴다
            static constexpr float32 kIndentPerLevel       = 12.0f;
            static constexpr float32 kTreeRatio            = 0.28f; ///< 왼쪽 영역에서 위젯 트리가 차지하는 높이 비율
            static constexpr float32 kPaletteRatio         = 0.22f; ///< 팔레트가 차지하는 높이 비율(나머지는 인스펙터)
            static constexpr float32 kHandleSize           = 9.0f;  ///< 슬롯 손잡이 한 변(96 DPI 픽셀)
            /** @brief 레이아웃 사각형 · 안전 영역 선의 색입니다(테마 토큰 — 정보색 · 오류색을 반투명으로). */
            static Color getLayoutColor() { return SW_THEME_COLOR( "Signal", "Info" ).withAlpha( 0.45f ); }
            static Color getSafeZoneColor() { return SW_THEME_COLOR( "Signal", "Error" ).withAlpha( 0.8f ); }

            /** @brief 직전 항목에 팔레트 위젯 타입({WidgetType, 이름 id})이 놓였으면 그 타입 이름을 @p outTypeName 에 담고 true 입니다. */
            static bool acceptPaletteType( string& outTypeName )
            {
                EditorDragPayload payload{};
                if ( EditorInputGUI::acceptDrop( EditorPayloadKindSet{ EditorPayloadKind::WidgetType }, payload ) == false )
                    return false;
                return EditorInputGUI::findPayloadName( EditorPayloadKind::WidgetType, payload._id, outTypeName );
            }

            /** @brief 시나리오가 누를 레이블 `<접두어>.<접미어>` 를 남깁니다. 테스트를 켰을 때만 문자열을 만듭니다. */
            static void recordMark( const utf8* pPrefix, const utf8* pSuffix )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                fixed_string<constant::buffer::kMax128> mark;
                formatstring( mark.data(), mark.capacity(), "%#.%#", pPrefix, pSuffix );
                EditorSelfTestMarks::record( mark.c_str() );
            }

            /** @brief 열린 패널의 감시 값 값을 읽습니다. 닫혔거나 문서가 없으면 false 입니다. */
            [[nodiscard]] static const UIPreviewProbe* findProbe()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const UIPreviewPanel* pPanel            = pPanels->findPanel<UIPreviewPanel>();
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

    SW_AUTOMATION_WATCH( editorUIPreviewWidgetCount, "Editor.UIPreviewWidgetCount", "Widgets in the UI Preview tree (no value while the panel is closed or has no document)",
                         &UIPreviewPanelInternal::readWidgetCount );
    SW_AUTOMATION_WATCH( editorUIPreviewFirstTextBytes, "Editor.UIPreviewFirstTextBytes",
                         "UTF-8 bytes of the displayed text of the first text widget in the UI Preview (longer in an expanding pseudo-language)",
                         &UIPreviewPanelInternal::readFirstTextBytes );
    SW_AUTOMATION_WATCH( editorUIPreviewRightToLeft, "Editor.UIPreviewRightToLeft", "1 when the UI Preview root lays out right to left (qps-plocm)",
                         &UIPreviewPanelInternal::readRightToLeft );
    SW_AUTOMATION_WATCH( editorUIPreviewHoveredOrdinal, "Editor.UIPreviewHoveredOrdinal",
                         "Document-order index of the widget under the simulated pointer in the UI Preview, -1 when none", &UIPreviewPanelInternal::readHoveredOrdinal );
    SW_AUTOMATION_WATCH( editorUIPreviewHoveredFocusable, "Editor.UIPreviewHoveredFocusable", "1 when the widget under the simulated pointer can take focus (a button)",
                         &UIPreviewPanelInternal::readHoveredFocusable );
    SW_AUTOMATION_WATCH( editorUIPreviewFocusedOrdinal, "Editor.UIPreviewFocusedOrdinal",
                         "Document-order index of the focused widget in the UI Preview (set by the navigation buttons), -1 when none", &UIPreviewPanelInternal::readFocusedOrdinal );
    SW_AUTOMATION_WATCH( editorUIPreviewSelectedOpacity, "Editor.UIPreviewSelectedOpacity", "Opacity of the widget selected in the UI Preview (no value without a selection)",
                         &UIPreviewPanelInternal::readSelectedOpacity );
    SW_AUTOMATION_WATCH( editorUIPreviewDirty, "Editor.UIPreviewDirty", "1 when the UI Preview has edits not yet written to the document",
                         &UIPreviewPanelInternal::readDirty );
    SW_AUTOMATION_WATCH( editorUIPreviewAnimationTime, "Editor.UIPreviewAnimationTime", "Playback time (seconds) of the animation chosen in the UI Preview, 0 when it is not playing",
                         &UIPreviewPanelInternal::readAnimationTime );
    SW_AUTOMATION_WATCH( editorUIPreviewAnimationPlaying, "Editor.UIPreviewAnimationPlaying", "1 while the animation chosen in the UI Preview is playing (a paused one counts as stopped)",
                         &UIPreviewPanelInternal::readAnimationPlaying );
    SW_AUTOMATION_WATCH( editorUIPreviewTextureReady, "Editor.UIPreviewTextureReady", "1 once the UI Preview render texture is registered with ImGui and the picture is drawn",
                         &UIPreviewPanelInternal::readTextureReady );
    SW_AUTOMATION_WALL_WATCH( editorUIPreviewReloadedAfterSave, "Editor.UIPreviewReloadedAfterSave",
                              "1 once the UI Preview rebuilt its tree from the file after the last Save (proves the write reached the document)",
                              &UIPreviewPanelInternal::readReloadedAfterSave );
    SW_AUTOMATION_WATCH( editorUIPreviewSlotEditable, "Editor.UIPreviewSlotEditable",
                         "1 when the selected widget's slot can be dragged by handles (its parent is a canvas panel); no value without a selection",
                         &UIPreviewPanelInternal::readSlotEditable );
    SW_AUTOMATION_WATCH( editorUIPreviewSelectedOffsetMinX, "Editor.UIPreviewSelectedOffsetMinX", "Slot _offsetMin._x of the selected widget (a handle drag changes it)",
                         &UIPreviewPanelInternal::readSelectedOffsetMinX );
    SW_AUTOMATION_WATCH( editorUIPreviewPaletteCount, "Editor.UIPreviewPaletteCount", "Widget types the UI Preview palette lists (every constructible Widget subclass)",
                         &UIPreviewPanelInternal::readPaletteCount );
    SW_AUTOMATION_WATCH( editorUIPreviewSlotEditCount, "Editor.UIPreviewSlotEditCount",
                         "Slot edits applied in the UI Preview (canvas handles, Slot fields, box order and grid cell handles; one undo entry each)",
                         &UIPreviewPanelInternal::readSlotEditCount );
    SW_AUTOMATION_WATCH( editorUIPreviewLanguageIndex, "Editor.UIPreviewLanguageIndex", "Row of the UI Preview language combo (0 = the game language)",
                         &UIPreviewPanelInternal::readLanguageIndex );

    UIPreviewPanel::UIPreviewPanel()
        : IEditorDocumentPanel( false )
        , _model{ getEditorToolDefaults()._uiPreviewDocument } // 처음 여는 문서 — 원본은 editortooldefaults.json
        , _grid{}
        , _paletteFilter{}
        , _listTreeRow{}
        , _listLayoutRect{}
        , _treeFilter{}
        , _slotDragBaseline{}
        , _slotFieldBaseline{}
        , _pTextureID{ nullptr }
        , _texture{ 0 }
        , _slotDragOrdinal{ UIPreviewLogic::kNoOrdinal }
        , _bShowLayout{ true }
        , _bSlotDragging{ SW_FALSE }
        , _bLayoutDragging{ SW_FALSE }
    {
        _model.setDirtyListener( SW_DELEGATE_METHOD( Delegate<void( bool )>, &UIPreviewPanel::setDocumentDirty, this ) );
        _model.setTargetReleaseListener( SW_DELEGATE_METHOD( Delegate<void()>, &UIPreviewPanel::releaseTexture, this ) );
    }

    UIPreviewPanel::~UIPreviewPanel() = default;

    const utf8* UIPreviewPanel::getPanelTitle() const
    {
        return EditorAssetTypeRegistry::getPanelTitle( EditorAssetType{ UIDocumentAsset::kAssetType } );
    }

    EditorAssetType UIPreviewPanel::getDocumentKind() const
    {
        return EditorAssetType{ UIDocumentAsset::kAssetType };
    }

    void UIPreviewPanel::setDocumentDirty( bool bDirty )
    {
        if ( bDirty )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    void UIPreviewPanel::shutdown()
    {
        releaseTexture();
        _model.shutdown();
    }

    void UIPreviewPanel::releaseTexture()
    {
        if ( _pTextureID == nullptr )
            return;
        EditorViewSurface::unregisterImage( _pTextureID );
        _pTextureID = nullptr;
        _texture    = 0;
    }

    void UIPreviewPanel::syncTexture()
    {
        const uint64 texture = _model.findTargetTexture();
        if ( texture == _texture && _pTextureID != nullptr )
            return;
        releaseTexture();
        _pTextureID = EditorViewSurface::registerImage( texture ); // 렌더 스레드가 아직 만들지 않았으면 nullptr(다음 프레임)
        _texture    = _pTextureID != nullptr ? texture : 0;
    }

    bool UIPreviewPanel::canSaveDocument() const
    {
        return _model.canSaveDocument();
    }

    bool UIPreviewPanel::saveDocument()
    {
        return _model.saveDocument();
    }

    void UIPreviewPanel::revertDocument()
    {
        _model.revertDocument();
    }

    void UIPreviewPanel::onPropertyEdited( const PropertyInfo& prop )
    {
        _model.onPropertyEdited( prop );
    }

    void UIPreviewPanel::drawLanguageCombo()
    {
        const string& language = _model.getLanguage();
        const string  preview  = language.empty() ? string( SW_LOCKEY( "EditorUIPreview", "GameLanguage" ) ) : language;
        EditorChrome::setNextItemWidthColumns( UIPreviewPanelInternal::kLanguageComboColumns );
        const bool bOpen = ImGui::BeginCombo( "##uipreviewlang", preview.c_str() );
        UIPreviewPanelInternal::recordMark( "uipreview", "language" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PreviewLanguage" ) );
        if ( bOpen == false )
            return;
        const vector<string>& listLanguage = _model.getLanguageChoices();
        for ( uint32 index = 0; index < listLanguage.size(); ++index )
        {
            const string& code  = listLanguage[index];
            string        label = code.empty() ? string( SW_LOCKEY( "EditorUIPreview", "GameLanguage" ) ) : code;
            if ( UIPreviewLogic::isPseudoLanguage( code ) )
                label = SW_LOCKEYFORMAT( "EditorUIPreview", "PseudoLanguage", TextArgumentList().addText( "code", code ) );
            if ( ImGui::Selectable( label.c_str(), code == language ) )
                _model.selectLanguage( index );
            UIPreviewPanelInternal::recordMark( "uipreview.language", code.empty() ? "game" : code.c_str() );
        }
        ImGui::EndCombo();
    }

    void UIPreviewPanel::drawToolbar( const UIThemeController& themes )
    {
        const string previous = _model.getDocumentPath().get();
        if ( _model.getDocumentPath().drawField( "##uipreviewdoc", 320.0f * EditorThemeUtil::getDPIScale() ) )
            _model.onDocumentPathCommitted( previous, isDocumentDirty() );
        UIPreviewPanelInternal::recordMark( "uipreview", "path" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "DocumentPathTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
            revertDocument();
        UIPreviewPanelInternal::recordMark( "uipreview", "reload" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "ReloadTip" ) );
        ImGui::SameLine();
        const bool bCanSave = canSaveDocument() && isDocumentDirty();
        ImGui::BeginDisabled( bCanSave == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 실패는 saveDocument 가 로그로 알린다
        ImGui::EndDisabled();
        UIPreviewPanelInternal::recordMark( "uipreview", "save" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PreviewWriteBack" ) );
        // 도구 줄이 패널보다 길면 오른쪽 콤보(언어)가 패널 밖에 놓여 누를 수 없다 — 들어가지 않으면 줄을 바꾼다.
        EditorChrome::sameLineIfFits( EditorChrome::measureColumns( 25.0f ) );
        EditorChrome::setNextItemWidthColumns( 25.0f );
        const UIResolutionPreset* pSelectedResolution = _model.findSelectedResolution();
        const bool                bResolutionOpen     = ImGui::BeginCombo( "##uipreviewres", pSelectedResolution != nullptr ? pSelectedResolution->_name.c_str() : "-" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "ResolutionTip" ) );
        if ( bResolutionOpen )
        {
            const vector<UIResolutionPreset>& listResolution = _model.getResolutionTable()._listResolution;
            for ( uint32 index = 0; index < static_cast<uint32>( listResolution.size() ); ++index )
            {
                if ( ImGui::Selectable( listResolution[index]._name.c_str(), index == _model.getResolutionIndex() ) )
                    _model.setResolutionIndex( index );
            }
            ImGui::EndCombo();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureColumns( 20.0f ) );
        EditorChrome::setNextItemWidthColumns( 20.0f );
        const hashed_string& chosenTheme = _model.getTheme();
        const bool           bThemeOpen  = ImGui::BeginCombo( "##uipreviewtheme", chosenTheme.empty() ? SW_LOCKEY( "EditorUIPreview", "GameTheme" ) : chosenTheme.c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "ThemeTip" ) );
        if ( bThemeOpen )
        {
            if ( ImGui::Selectable( SW_LOCKEY( "EditorUIPreview", "GameTheme" ), chosenTheme.empty() ) )
                _model.setTheme( hashed_string{} );
            for ( const UIThemeDef& theme : themes.getCatalog()._listTheme )
            {
                if ( ImGui::Selectable( theme._name.c_str(), theme._name == chosenTheme ) )
                    _model.setTheme( theme._name );
            }
            ImGui::EndCombo();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureColumns( UIPreviewPanelInternal::kLanguageComboColumns ) );
        drawLanguageCombo();
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "UIScale" ) );
        EditorChrome::setNextItemWidthColumns( 19.0f );
        ImGui::SliderFloat( "##UIscale", &_model.editUIScale(), 0.5f, 2.0f, "%.2f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "UIScaleTip" ) );
        ImGui::SameLine();
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "TextScale" ) );
        EditorChrome::setNextItemWidthColumns( 19.0f );
        ImGui::SliderFloat( "##Textscale", &_model.editTextScale(), 0.75f, 2.0f, "%.2f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "TextScaleTip" ) );
        ImGui::SameLine();
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "SafeZone" ) );
        EditorChrome::setNextItemWidthColumns( 19.0f );
        ImGui::SliderFloat( "##Safezone", &_model.editSafeZone(), 0.0f, 0.1f, "%.3f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "SafeZoneTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Rotate" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Rotate" ), &_model.editRotated() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "RotateTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "LayoutRects" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "LayoutRects" ), &_bShowLayout );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorUIPreview", "LayoutRectsTip" ) );
    }

    void UIPreviewPanel::drawInteractionBar( UIScreen& screen )
    {
        bool bInteract = _model.isInteracting();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Interact" ), &bInteract ) )
            _model.setInteracting( bInteract );
        UIPreviewPanelInternal::recordMark( "uipreview", "interact" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PreviewSimulatePointer" ) );
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
            fixed_string<constant::buffer::kMax32> id;
            formatstring( id.data(), id.capacity(), "##uipreviewnav%#", button._pMark );
            if ( ImGui::ArrowButton( id.c_str(), button._dir ) )
                (void)_model.navigateFocus( screen, static_cast<uint32>( button._direction ) );
            UIPreviewPanelInternal::recordMark( "uipreview.nav", button._pMark );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "MoveThePreviewFocusInThisDirection" ) );
            ImGui::SameLine();
        }
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Accept" ) ) )
            _model.acceptFocus( screen );
        UIPreviewPanelInternal::recordMark( "uipreview.nav", "accept" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PressTheFocusedWidgetUIAcceptIts" ) );
    }

    void UIPreviewPanel::drawAnimationBar( UIScreen& screen )
    {
        UIAnimationPlayer&         player        = screen.getAnimationPlayer();
        const vector<UIAnimation>& listAnimation = player.getAnimations();
        if ( listAnimation.empty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorUIPreview", "NoAnimations" ) );
            return;
        }
        const hashed_string chosen = _model.syncAnimation( player );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 16.0f );
        const bool bOpen = ImGui::BeginCombo( "##uipreviewanimation", chosen.c_str() );
        UIPreviewPanelInternal::recordMark( "uipreview", "animation" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "DocumentAnimationToPreviewThePreviewNever" ) );
        if ( bOpen )
        {
            for ( const UIAnimation& animation : listAnimation )
            {
                if ( ImGui::Selectable( animation._name.c_str(), animation._name == chosen ) )
                    _model.chooseAnimation( player, animation._name );
                UIPreviewPanelInternal::recordMark( "uipreview.animation", animation._name.c_str() );
            }
            ImGui::EndCombo();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Play" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Play" ) ) )
            _model.playAnimation( player );
        UIPreviewPanelInternal::recordMark( "uipreview.animation", "play" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PlayTheChosenAnimationFromTheStart" ) );
        const utf8* pPauseLabel = _model.isAnimationPaused() ? SW_LOCKEY( "EdText", "Resume" ) : SW_LOCKEY( "EdButton", "Pause" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pPauseLabel ) );
        ImGui::BeginDisabled( _model.isAnimationActive( player ) == false );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( pPauseLabel, "uipreviewpause" ) ) ) // 글이 Pause · Resume 으로 바뀌어도 같은 단추다
            _model.togglePauseAnimation( player );
        ImGui::EndDisabled();
        UIPreviewPanelInternal::recordMark( "uipreview.animation", "pause" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Stop" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Stop" ) ) )
            _model.stopAnimation( player );
        UIPreviewPanelInternal::recordMark( "uipreview.animation", "stop" );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Loop" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Loop" ), &_model.editAnimationLoop() );
        ImGui::SameLine();

        // 시간 막대 — 끌면 그 시각에서 멈춘 채 값을 보인다(이벤트는 보내지 않는다). 재생 중에는 지금 시각을 따라간다.
        float32    time{ 0.0f };
        float32    duration{ 0.0f };
        const bool bActive      = _model.readAnimationTime( player, time, duration );
        float32    scrubSeconds = time;
        EditorChrome::setNextItemWidthColumns( 29.0f );
        if ( ImGui::SliderFloat( "##uipreviewanimationtime", &scrubSeconds, 0.0f, MathUtil::max( duration, 0.001f ), "%.2f s" ) )
            _model.scrubAnimation( player, scrubSeconds );
        UIPreviewPanelInternal::recordMark( "uipreview", "animationtime" );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", UIPreviewLogic::makeTimeLabel( bActive ? time : scrubSeconds, duration ).c_str() );
    }

    void UIPreviewPanel::acceptPaletteDrop( UIScreen& screen, Widget* pTarget )
    {
        string typeName;
        if ( UIPreviewPanelInternal::acceptPaletteType( typeName ) )
            (void)_model.addWidget( screen, hashed_string( typeName.c_str() ), pTarget ); // 못 붙이면 addWidget 이 경고로 알린다
    }

    void UIPreviewPanel::drawPalette( UIScreen& screen )
    {
        using Internal = UIPreviewPanelInternal;
        _model.refreshPalette();
        Widget*    pSelected = screen.getTree().findWidgetByID( _model.getSelected() );
        const bool bCanAdd   = _model.getPaletteType().empty() == false;
        ImGui::BeginDisabled( bCanAdd == false );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Add" ) ) )
            (void)_model.addWidget( screen, _model.getPaletteType(), pSelected );
        ImGui::EndDisabled();
        Internal::recordMark( "uipreview.palette", "add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "AddTheChosenWidgetTypeIntoThe" ) );
        ImGui::SameLine();
        const bool bCanDelete = _model.canDeleteSelected( screen.getTree() );
        ImGui::BeginDisabled( bCanDelete == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Delete" ) ) )
            (void)_model.deleteSelected( screen ); // 루트나 없는 선택은 못 지우고, 그때는 단추가 꺼져 있다
        ImGui::EndDisabled();
        Internal::recordMark( "uipreview", "delete" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "RemoveTheSelectedWidgetAndItsChildren" ) );
        // 형제 사이 옮기기(UMG 계층 창의 위 · 아래 옮기기) — 끌기 손잡이가 없는 부모(겹침 · 캔버스)에서도 순서를 바꾼다.
        struct MoveButton
        {
            const utf8* _pMark;
            ImGuiDir    _dir;
            int32       _delta;
            const utf8* _pTip;
        };
        const MoveButton arrMove[] = {
            {  "moveup",   ImGuiDir_Up, -1, SW_LOCKEY( "EditorUIPreview", "MoveEarlierTip" )},
            {"movedown", ImGuiDir_Down,  1, SW_LOCKEY( "EditorUIPreview",   "MoveLaterTip" )},
        };
        for ( const MoveButton& button : arrMove )
        {
            ImGui::SameLine();
            ImGui::BeginDisabled( _model.canMoveSelected( screen.getTree(), button._delta ) == false );
            ImGui::PushID( button._pMark );
            if ( ImGui::ArrowButton( "##uipreviewmove", button._dir ) )
                _model.moveSelected( screen.getTree(), button._delta );
            ImGui::PopID();
            ImGui::EndDisabled();
            Internal::recordMark( "uipreview", button._pMark );
            EditorWidgets::drawTooltip( button._pTip );
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.0f );
        (void)EditorWidgets::drawTextField( "##uipreviewpalettefilter", _paletteFilter );
        Internal::recordMark( "uipreview.palette", "filter" );

        const EditorListFilter paletteFilter{ string_view{ _paletteFilter } };
        ImGui::BeginChild( "##uipreviewpalettelist", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_None );
        uint32 shownCount{ 0 };
        for ( const TypeInfo* pType : _model.getPaletteTypes() )
        {
            const utf8* pName = pType->_name.c_str();
            if ( paletteFilter.matches( pName ) == false )
                continue;
            ++shownCount;
            const bool bChosen = _model.getPaletteType() == pType->_name;
            if ( ImGui::Selectable( pName, bChosen, ImGuiSelectableFlags_AllowDoubleClick ) )
            {
                _model.setPaletteType( pType->_name );
                if ( EditorInputGUI::wasItemDoubleClicked() )
                    (void)_model.addWidget( screen, _model.getPaletteType(), screen.getTree().findWidgetByID( _model.getSelected() ) );
            }
            Internal::recordMark( "uipreview.palette", pName );
            // 끌어서 트리 줄이나 이미지에 놓는다 — 놓는 쪽이 받는 패널을 고른다(`UIPreviewLogic::findDropContainer`). 페이로드는 {WidgetType, 타입 이름 id} 다.
            const EditorDragPayload payload{ EditorPayloadKind::WidgetType, EditorInputGUI::registerPayloadName( EditorPayloadKind::WidgetType, pName ) };
            (void)EditorInputGUI::beginDragSource( payload, pName ); // 끌기를 시작했는지는 쓰지 않는다
        }
        if ( shownCount == 0 && paletteFilter.isActive() )
            EditorWidgets::drawNoSearchResultHint( paletteFilter.getText() );
        ImGui::EndChild();
    }

    void UIPreviewPanel::drawSlotHandles( UIScreen& screen, const float2& drawSize, const float2& origin )
    {
        using Internal             = UIPreviewPanelInternal;
        Widget*    pWidget         = screen.getTree().findWidgetByID( _model.getSelected() );
        const bool bCannotEditSlot = pWidget == nullptr || UIPreviewLogic::canEditSlotByHandle( *pWidget ) == false || drawSize._x <= 0.0f || drawSize._y <= 0.0f;
        if ( bCannotEditSlot )
            return;
        const PropertyInfo* pSlotProp = UIPreviewLogic::findWidgetProperty( *pWidget, "_slot" );
        if ( pSlotProp == nullptr )
            return;
        const UIViewport& viewport = _model.getViewport();
        const UIRect      box      = UIPreviewLogic::mapUIToImage( pWidget->getGeometry().computeScreenBounds(), drawSize, viewport );
        const float32     half     = Internal::kHandleSize * EditorThemeUtil::getDPIScale() * 0.5f;
        struct HandleRow
        {
            UIPreviewSlotHandle _handle;
            const utf8*         _pMark;
        };
        static constexpr HandleRow kArrHandle[] = {
            {  UIPreviewSlotHandle::Move,   "move"},
            {  UIPreviewSlotHandle::Left,   "left"},
            {   UIPreviewSlotHandle::Top,    "top"},
            { UIPreviewSlotHandle::Right,  "right"},
            {UIPreviewSlotHandle::Bottom, "bottom"},
        };
        for ( const HandleRow& row : kArrHandle )
        {
            const float2 center = UIPreviewLogic::getSlotHandleCenter( row._handle, box );
            const ImVec2 corner{ origin._x + center._x - half, origin._y + center._y - half };
            ImGui::SetCursorScreenPos( corner );
            ImGui::PushID( row._pMark );
            ImGui::InvisibleButton( "##handle", ImVec2{ half * 2.0f, half * 2.0f } );
            Internal::recordMark( "uipreview.handle", row._pMark );
            ImGui::GetWindowDrawList()->AddRectFilled( corner, ImVec2{ corner.x + half * 2.0f, corner.y + half * 2.0f }, EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Highlight" ) ) );
            if ( ImGui::IsItemActivated() )
            {
                _bSlotDragging    = SW_TRUE;
                _slotDragOrdinal  = _model.getSelectedOrdinal();
                _slotDragBaseline = UIPreviewLogic::formatWidgetProperty( *pWidget, *pSlotProp );
            }
            if ( ImGui::IsItemActive() && _bSlotDragging )
            {
                // 마우스 이동량(화면 픽셀)을 UI 단위로 옮긴다 — 이미지가 뷰포트보다 작게 그려진다.
                WidgetLayoutSlot slot = pWidget->getLayoutSlot();
                if ( UIPreviewLogic::applySlotDrag( slot, row._handle, UIPreviewLogic::mapImageToUI( EditorInputGUI::readPointer()._delta, drawSize, viewport ) ) )
                    pWidget->setLayoutSlot( slot );
            }
            if ( ImGui::IsItemDeactivated() && _bSlotDragging )
            {
                _bSlotDragging = SW_FALSE;
                (void)_model.commitSlotEdit( _slotDragOrdinal, _slotDragBaseline, UIPreviewLogic::formatWidgetProperty( *pWidget, *pSlotProp ) ); // 같으면 남기지 않는다
            }
            ImGui::PopID();
        }
    }

    void UIPreviewPanel::drawLayoutHandle( UIScreen& screen, const float2& drawSize, const float2& origin )
    {
        using Internal                = UIPreviewPanelInternal;
        WidgetTree& tree              = screen.getTree();
        Widget*     pWidget           = tree.findWidgetByID( _model.getSelected() );
        const bool  bCannotEditLayout = pWidget == nullptr || UIPreviewLogic::canEditLayoutByHandle( *pWidget ) == false || drawSize._x <= 0.0f || drawSize._y <= 0.0f;
        if ( bCannotEditLayout )
            return;
        const UIViewport& viewport = _model.getViewport();
        const UIRect      box      = UIPreviewLogic::mapUIToImage( pWidget->getGeometry().computeScreenBounds(), drawSize, viewport );
        const float32     half     = Internal::kHandleSize * EditorThemeUtil::getDPIScale() * 0.5f;
        const ImVec2      center{ origin._x + ( box._left + box._right ) * 0.5f, origin._y + ( box._top + box._bottom ) * 0.5f };
        const ImU32       color = EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Highlight" ) );
        ImGui::SetCursorScreenPos( ImVec2{ center.x - half, center.y - half } );
        ImGui::PushID( "layouthandle" );
        ImGui::InvisibleButton( "##reorder", ImVec2{ half * 2.0f, half * 2.0f } );
        Internal::recordMark( "uipreview.handle", "reorder" );
        ImGui::GetWindowDrawList()->AddCircleFilled( center, half, color );
        if ( ImGui::IsItemActivated() )
            _bLayoutDragging = SW_TRUE;
        if ( ImGui::IsItemActive() && _bLayoutDragging )
            ImGui::GetWindowDrawList()->AddLine( center, ImGui::GetMousePos(), color, 2.0f ); // 놓일 위치 — 놓을 때까지 트리는 그대로다
        const bool bDropped = ImGui::IsItemDeactivated() && _bLayoutDragging;
        ImGui::PopID();
        if ( bDropped == false )
            return;
        _bLayoutDragging   = SW_FALSE;
        const ImVec2 mouse = ImGui::GetMousePos();
        _model.dropLayoutHandle( tree, *pWidget, UIPreviewLogic::mapImageToUI( float2{ mouse.x - origin._x, mouse.y - origin._y }, drawSize, viewport ) );
    }

    void UIPreviewPanel::finishSlotField( Widget& widget, const PropertyInfo& slotProp, const string& frameText, bool bChanged, bool bImmediate )
    {
        if ( ImGui::IsItemActivated() )
            _slotFieldBaseline = frameText;
        const bool bCommit = bImmediate ? bChanged : ImGui::IsItemDeactivatedAfterEdit();
        if ( bCommit == false )
            return;
        const string& oldText = bImmediate ? frameText : _slotFieldBaseline;
        (void)_model.commitSlotEdit( _model.getSelectedOrdinal(), oldText, UIPreviewLogic::formatWidgetProperty( widget, slotProp ) ); // 같으면 남기지 않는다
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
        bool                _bDrawn{ false }; ///< 바로 앞 field 를 그렸다 — 그리지 않았으면 뒤의 툴팁이 남의 위젯에 붙지 않게 건너뛴다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

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
            UIPreviewPanelInternal::recordMark( "uipreview.slot", pMark );
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
                const UIPreviewAnchorPreset current = UIPreviewLogic::findAnchorPreset( s );
                int32                       picked  = current == UIPreviewAnchorPreset::Count ? -1 : static_cast<int32>( current );
                if ( ImGui::Combo( pLabel, &picked, arrPresetName, kPresetCount ) == false || picked < 0 )
                    return false;
                return UIPreviewLogic::applyAnchorPreset( s, static_cast<UIPreviewAnchorPreset>( picked ), UIPreviewLogic::makeParentLocalRect( _widget ), _widget.getParent()->getGeometry()._size );
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
        const UIPreviewSlotParams params = UIPreviewLogic::findSlotFields( widget );
        if ( params._bSizeOverride == false )
            return; // 루트 — 부모가 없어 슬롯을 읽는 쪽이 없다
        const PropertyInfo* pSlotProp = UIPreviewLogic::findWidgetProperty( widget, "_slot" );
        if ( pSlotProp == nullptr )
            return;
        const TypeInfo* pParentType = widget.getParent()->getTypeInfo();
        const string    header      = string{ SW_LOCKEY( "EditorUIPreview", "SlotHeader" ) } + " (" + ( pParentType != nullptr ? pParentType->_name.c_str() : "Panel" ) + ")###uipreviewslot";
        if ( ImGui::CollapsingHeader( header.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;

        SlotFieldPass pass{ *this, widget, *pSlotProp, EditorListFilter{ _grid.getFilterText() }, widget.getLayoutSlot() };
        if ( params._bPaddingAlignment )
            pass.drawPaddingAlignment();
        if ( params._bBoxSize )
            pass.drawBoxSize();
        if ( params._bGrid )
            pass.drawGrid();
        if ( params._bCanvas )
            pass.drawCanvas();
        pass.drawSizeOverrides();
        ImGui::Separator();
    }

    void UIPreviewPanel::drawWidgetList( UIScreen& screen )
    {
        using Internal                            = UIPreviewPanelInternal;
        const vector<UIPreviewWidgetRow>& listRow = _model.getWidgetRows( screen.getTree() );
        const EditorListFilter            filter{ _treeFilter.c_str() };
        UIPreviewLogic::filterWidgetRows( listRow, filter, _listTreeRow );
        (void)EditorWidgets::drawFilterBar( "##uipreviewtreefilter", _treeFilter, nullptr, static_cast<uint32>( _listTreeRow.size() ), static_cast<uint32>( listRow.size() ), nullptr, "uipreview.tree.filter" );
        if ( _listTreeRow.empty() )
            EditorWidgets::drawNoSearchResultHint( filter.getText() );
        for ( const uint32 ordinal : _listTreeRow )
        {
            const UIPreviewWidgetRow& row = listRow[ordinal];
            ImGui::PushID( reinterpret_cast<const void*>( static_cast<uintptr_t>( row._widget.getKey() ) ) ); // 위젯 토큰 키 — 줄이 토큰을 들고 있어 유일하다
            ImGui::Indent( Internal::kIndentPerLevel * static_cast<float32>( row._depth ) + 0.001f );
            if ( ImGui::Selectable( row._label.c_str(), row._widget == _model.getSelected() ) )
                _model.select( row._widget, ordinal );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax32> ordinalText;
                formatstring( ordinalText.data(), ordinalText.capacity(), "%#", ordinal );
                Internal::recordMark( "uipreview.row", ordinalText.c_str() );
                const Widget* pWidget = screen.getTree().findWidgetByID( row._widget );
                if ( pWidget != nullptr && pWidget->getName().empty() == false )
                    Internal::recordMark( "uipreview.widget", pWidget->getName().c_str() );
            }
            acceptPaletteDrop( screen, screen.getTree().findWidgetByID( row._widget ) ); // 팔레트 항목을 이 줄에 놓는다
            ImGui::Unindent( Internal::kIndentPerLevel * static_cast<float32>( row._depth ) + 0.001f );
            ImGui::PopID();
        }
    }

    void UIPreviewPanel::drawInspector( UIScreen& screen )
    {
        Widget* pWidget = screen.getTree().findWidgetByID( _model.getSelected() );
        if ( pWidget == nullptr || pWidget->getTypeInfo() == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "SelectAWidgetToEditItsProperties" ) );
            return;
        }
        _model.syncBaseline( *pWidget );
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
        // 에디터 포인터를 UI 단위로 옮겨 모델에 넘긴다 — 모델은 에디터 입력을 모른다. 버튼 순서는 `UIPreviewPointerInput` 의 것(왼쪽 · 오른쪽 · 가운데)이다.
        static constexpr EditorMouseButton kArrButton[UIPreviewPointerInput::kButtonCount] = { EditorMouseButton::Left, EditorMouseButton::Right, EditorMouseButton::Middle };
        const EditorPointerState           pointer                                         = EditorInputGUI::readPointer();
        UIPreviewPointerInput              input{};
        input._position = UIPreviewLogic::mapImageToUI( float2{ pointer._position._x - origin._x, pointer._position._y - origin._y }, drawSize, _model.getViewport() );
        input._wheel    = pointer._wheel;
        input._bHovered = bHovered;
        for ( uint32 buttonIndex = 0; buttonIndex < UIPreviewPointerInput::kButtonCount; ++buttonIndex )
        {
            input._arrPressed[buttonIndex]  = pointer.wasPressed( kArrButton[buttonIndex] );
            input._arrReleased[buttonIndex] = pointer.wasReleased( kArrButton[buttonIndex] );
        }
        _model.feedPointer( screen, input );
    }

    void UIPreviewPanel::drawImage( UIScreen& screen )
    {
        using Internal = UIPreviewPanelInternal;
        ImGui::BeginChild( "##uipreviewimage", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        const UIViewport& viewport  = _model.getViewport();
        const bool        bInteract = _model.isInteracting();
        const Widget*     pSelected = screen.getTree().findWidgetByID( _model.getSelected() );
        if ( pSelected != nullptr )
        {
            const UIRect bounds = pSelected->getGeometry().computeScreenBounds();
            ImGui::Text( "%s  (%.0f, %.0f)  %.0f x %.0f", pSelected->getName().empty() ? SW_LOCKEY( "EditorUIPreview", "Unnamed" ) : pSelected->getName().c_str(),
                         static_cast<float64>( bounds._left ), static_cast<float64>( bounds._top ), static_cast<float64>( bounds._right - bounds._left ),
                         static_cast<float64>( bounds._bottom - bounds._top ) );
        }
        else
        {
            fixed_string<constant::buffer::kMax32> width;
            fixed_string<constant::buffer::kMax32> height;
            fixed_string<constant::buffer::kMax32> scale;
            formatstring( width.data(), width.capacity(), "%#", Fmt( static_cast<float64>( viewport._size._x ), Format().precision( 0 ) ) );
            formatstring( height.data(), height.capacity(), "%#", Fmt( static_cast<float64>( viewport._size._y ), Format().precision( 0 ) ) );
            formatstring( scale.data(), scale.capacity(), "%#", Fmt( static_cast<float64>( viewport._uiScale ), Format().precision( 2 ) ) );
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
        const float2 drawSize = UIPreviewLogic::fitImage( viewport._physicalSize, float2{ avail.x, avail.y } );
        const ImVec2 cursor   = ImGui::GetCursorScreenPos();
        const float2 origin{ cursor.x, cursor.y };
        ImGui::Image( reinterpret_cast<ImTextureID>( _pTextureID ), ImVec2{ drawSize._x, drawSize._y } );
        const bool bImageHovered = ImGui::IsItemHovered();
        EditorSelfTestMarks::record( "uipreview.image" );
        string droppedType;
        if ( bInteract == false && Internal::acceptPaletteType( droppedType ) )
        {
            // 팔레트 항목을 이미지 위에 놓는다 — 놓은 점 아래 위젯이 받는 쪽을 정한다(패널이면 그 안, 아니면 그 뒤).
            const float2   mouse  = EditorInputGUI::readPointer()._position;
            const float2   point  = UIPreviewLogic::mapImageToUI( float2{ mouse._x - origin._x, mouse._y - origin._y }, drawSize, viewport );
            const WidgetID target = UIPreviewLogic::findWidgetAt( screen.getTree(), point );
            (void)_model.addWidget( screen, hashed_string( droppedType.c_str() ), screen.getTree().findWidgetByID( target ) ); // 못 붙이면 addWidget 이 경고로 알린다
        }
        // 이미지 클릭으로 고르기는 손잡이를 그린 뒤에 정한다 — 손잡이는 이미지 위에 겹쳐 있어, 여기서 바로 고르면 손잡이를 누른 클릭이 그 아래 위젯(자식)을
        // 골라 손잡이가 사라지고 끌기가 시작되지 않는다.
        const bool bImageClicked = bInteract == false && EditorInputGUI::wasItemClicked();
        if ( bInteract )
            feedPointer( screen, drawSize, origin, bImageHovered );

        // 겹쳐 그리기 — 레이아웃 사각형 · 고른 위젯 · 안전 영역(이미지 위 ImGui 선, 캔버스는 바꾸지 않는다).
        ImDrawList* pDraw   = ImGui::GetWindowDrawList();
        const auto  drawBox = [pDraw, origin]( const UIRect& rect, ImU32 color, float32 thickness )
        {
            pDraw->AddRect( ImVec2{ origin._x + rect._left, origin._y + rect._top }, ImVec2{ origin._x + rect._right, origin._y + rect._bottom }, color, 0.0f, 0, thickness );
        };
        if ( _bShowLayout )
        {
            UIPreviewLogic::collectVisibleBounds( screen.getTree(), _listLayoutRect );
            for ( const UIRect& bounds : _listLayoutRect )
            {
                drawBox( UIPreviewLogic::mapUIToImage( bounds, drawSize, viewport ), EditorColorUtil::toImU32( Internal::getLayoutColor() ), 1.0f );
            }
        }
        if ( pSelected != nullptr )
            drawBox( UIPreviewLogic::mapUIToImage( pSelected->getGeometry().computeScreenBounds(), drawSize, viewport ), EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Highlight" ) ), 2.0f );
        UIRect safeZone{};
        if ( UIPreviewLogic::findSafeZoneRect( viewport, safeZone ) )
            drawBox( UIPreviewLogic::mapUIToImage( safeZone, drawSize, viewport ), EditorColorUtil::toImU32( Internal::getSafeZoneColor() ), 1.0f );
        // 끄는 도중 선택이 바뀌거나(Undo · 핫 리로드) 입력 흉내를 켜면 손잡이가 그려지지 않아 끝을 알 수 없다 — 깃발이 남으면 이미지 클릭 고르기가 영영 꺼진다. 아무것도 잡고 있지 않으면 내린다.
        if ( ImGui::IsAnyItemActive() == false )
        {
            _bSlotDragging   = SW_FALSE;
            _bLayoutDragging = SW_FALSE;
        }
        // 캔버스 자식의 슬롯 손잡이(옮기기 · 네 변) — 끌면 슬롯의 오프셋이 바뀌고 놓으면 되돌리기 항목 하나가 쌓인다. 입력 흉내 중에는 숨긴다(포인터를 가로채지 않게).
        if ( bInteract == false )
        {
            drawSlotHandles( screen, drawSize, origin );
            drawLayoutHandle( screen, drawSize, origin );
        }
        const bool bSelectWidgetByClick = bImageClicked && _bSlotDragging == false && _bLayoutDragging == false;
        if ( bSelectWidgetByClick )
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            _model.selectAt( screen.getTree(), UIPreviewLogic::mapImageToUI( float2{ mouse.x - origin._x, mouse.y - origin._y }, drawSize, viewport ) );
        }
        ImGui::EndChild();
    }

    void UIPreviewPanel::drawContent()
    {
        using Internal = UIPreviewPanelInternal;
        UISystem* pUI  = UIPreviewModel::findUISystem();
        if ( pUI == nullptr || pUI->isInitialized() == false )
        {
            _model.resetProbe();
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorUIPreview", "UINotRunning" ) );
            return;
        }
        _model.syncFocusedDocument( isDocumentDirty() );
        drawToolbar( UIPreviewModel::getThemeController() );
        UIOffscreenScreens& offscreen = UIPreviewModel::getOffscreenScreens();
        _model.syncPreview( *pUI, offscreen ); // 렌더 텍스처를 바꾸면 그 전에 releaseTexture 를 부른다
        _model.syncLanguage( offscreen );
        syncTexture();
        UIScreen* pScreen = _model.findPreviewScreen();
        if ( pScreen != nullptr )
        {
            _model.syncTree( *pScreen, isDocumentDirty() );
            drawInteractionBar( *pScreen );
            drawAnimationBar( *pScreen );
        }
        ImGui::Separator();
        if ( pScreen == nullptr )
        {
            _model.resetProbe();
            EditorWidgets::drawEmptyHint( _model.getError().empty() ? SW_LOCKEY( "EditorUIPreview", "EnterPathHint" ) : _model.getError().c_str() );
            return;
        }

        // 왼쪽 영역: 위젯 트리, 팔레트, 고른 위젯의 프로퍼티(인스펙터)를 위에서 아래로. 오른쪽: 미리보기 이미지.
        ImGui::BeginChild( "##uipreviewleft", EditorChrome::measureTextExtent( EditorTextExtent::ofText( Internal::kLeftColumns, 0.0f ) ), ImGuiChildFlags_None );
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
        _model.updateProbe( pScreen, isDocumentDirty(), _pTextureID != nullptr );
    }
} // namespace sw::editor
