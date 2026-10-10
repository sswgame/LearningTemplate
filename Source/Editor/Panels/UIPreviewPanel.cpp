#include "pch.h"

#include "Editor/Panels/UIPreviewPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/UISystem.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( UIPreviewPanel, "ui_preview", EditorPanelCategory::Tool, 950 );

    namespace
    {
        struct UIPreviewPanelInternal
        {
            static constexpr float32 kListWidth      = 260.0f;
            static constexpr float32 kIndentPerLevel = 12.0f;
            static constexpr ImU32   kLayoutColor    = IM_COL32( 90, 200, 255, 110 );
            static constexpr ImU32   kSelectColor    = IM_COL32( 255, 200, 60, 255 );
            static constexpr ImU32   kSafeZoneColor  = IM_COL32( 255, 80, 80, 200 );
        };
    } // namespace

    UIPreviewPanel::UIPreviewPanel()
        : IEditorPanel( false )
        , _listRow{}
        , _documentPath{ "engine/ui/pause.ui.xml" }
        , _openedDocument{}
        , _targetPath{}
        , _error{}
        , _theme{}
        , _viewport{}
        , _pTextureID{ nullptr }
        , _texture{ 0 }
        , _screen{ kInvalidUIScreenHandle }
        , _selected{ kInvalidWidgetID }
        , _resolutionIndex{ 1 }
        , _uiScale{ 1.0f }
        , _textScale{ 1.0f }
        , _safeZone{ 0.0f }
        , _bShowLayout{ true }
    {
    }

    void UIPreviewPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        releaseTexture();
        closePreview();
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
            pUI->closeOffscreenScreen( _screen );
        _screen = kInvalidUIScreenHandle;
        _openedDocument.clear();
        _targetPath.clear();
        _selected = kInvalidWidgetID;
    }

    void UIPreviewPanel::syncPreview( UISystem& ui )
    {
        _viewport               = UIPreviewLogic::makeViewport( ui.getScaleSettings(), _resolutionIndex, _uiScale, _safeZone );
        const string targetPath = UIPreviewLogic::makeTargetPath( _viewport );
        const bool   bReopen    = _screen == kInvalidUIScreenHandle || ui.findOffscreenScreen( _screen ) == nullptr || _openedDocument != _documentPath ||
                             _targetPath != targetPath;
        if ( bReopen && _documentPath.empty() == false && _error.empty() == false && _openedDocument == _documentPath && _targetPath == targetPath )
            return; // 같은 문서가 지난번에 짓지 못했다 — 고칠 때까지(경로를 바꾸거나 다시 열기) 프레임마다 다시 시도하지 않는다
        if ( bReopen )
        {
            if ( _screen != kInvalidUIScreenHandle )
                ui.closeOffscreenScreen( _screen );
            releaseTexture(); // 크기가 바뀌면 렌더 텍스처도 새것이다
            _screen         = _documentPath.empty() ? kInvalidUIScreenHandle : ui.openOffscreenScreen( _documentPath, targetPath );
            _openedDocument = _documentPath;
            _targetPath     = targetPath;
            _selected       = kInvalidWidgetID;
            _error          = _screen == kInvalidUIScreenHandle ? "Could not build the document (see the log)." : "";
        }
        if ( _screen != kInvalidUIScreenHandle )
            ui.setOffscreenView( _screen, _viewport, _textScale, _theme );
    }

    void UIPreviewPanel::syncTexture()
    {
        AssetManager*    pAssets  = editor::getService<AssetManager>();
        const Texture2D* pTexture = pAssets != nullptr && _targetPath.empty() == false ? pAssets->getTextureCache().find( _targetPath ) : nullptr;
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

    void UIPreviewPanel::drawToolbar( UISystem& ui )
    {
        string path = _documentPath;
        ImGui::SetNextItemWidth( 320.0f * EditorThemeUtil::getDpiScale() );
        if ( EditorWidgets::drawTextField( "##uipreviewdoc", path ) )
        {
            _documentPath = path;
            _error.clear();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Reload" ) )
        {
            closePreview();
            _error.clear();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 190.0f * EditorThemeUtil::getDpiScale() );
        if ( ImGui::BeginCombo( "##uipreviewres", UIPreviewLogic::getResolution( _resolutionIndex )._pName ) )
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
        if ( ImGui::BeginCombo( "##uipreviewtheme", _theme.empty() ? "(game theme)" : _theme.c_str() ) )
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
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SliderFloat( "UI scale", &_uiScale, 0.5f, 2.0f, "%.2f" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SliderFloat( "Text scale", &_textScale, 0.75f, 2.0f, "%.2f" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SliderFloat( "Safe zone", &_safeZone, 0.0f, 0.1f, "%.3f" );
        ImGui::SameLine();
        ImGui::Checkbox( "Layout rects", &_bShowLayout );
    }

    void UIPreviewPanel::drawWidgetList( const UIScreen& screen )
    {
        using Internal = UIPreviewPanelInternal;
        UIPreviewLogic::collectWidgetRows( screen.getTree(), _listRow );
        ImGui::BeginChild( "##uipreviewtree", ImVec2{ Internal::kListWidth, 0.0f }, ImGuiChildFlags_Borders );
        for ( const UIPreviewWidgetRow& row : _listRow )
        {
            ImGui::PushID( static_cast<int32>( row._widget ) );
            ImGui::Indent( Internal::kIndentPerLevel * static_cast<float32>( row._depth ) + 0.001f );
            if ( ImGui::Selectable( row._label.c_str(), row._widget == _selected ) )
                _selected = row._widget;
            ImGui::Unindent( Internal::kIndentPerLevel * static_cast<float32>( row._depth ) + 0.001f );
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void UIPreviewPanel::drawImage( const UIScreen& screen )
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
            ImGui::TextDisabled( "%.0f x %.0f UI units, scale %.2f - click the image or the tree to select a widget",
                                 static_cast<float64>( _viewport._size._x ), static_cast<float64>( _viewport._size._y ), static_cast<float64>( _viewport._uiScale ) );
        }
        if ( _pTextureID == nullptr )
        {
            ImGui::TextDisabled( "Waiting for the render thread to create the preview texture..." );
            ImGui::EndChild();
            return;
        }
        const ImVec2 avail    = ImGui::GetContentRegionAvail();
        const float2 drawSize = UIPreviewLogic::fitImage( _viewport._physicalSize, float2{ avail.x, avail.y } );
        const ImVec2 origin   = ImGui::GetCursorScreenPos();
        ImGui::Image( reinterpret_cast<ImTextureID>( _pTextureID ), ImVec2{ drawSize._x, drawSize._y } );
        if ( ImGui::IsItemClicked() )
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float2 point = UIPreviewLogic::mapImageToUI( float2{ mouse.x - origin.x, mouse.y - origin.y }, drawSize, _viewport );
            _selected          = UIPreviewLogic::findWidgetAt( screen.getTree(), point );
        }

        // 겹쳐 그리기 — 레이아웃 사각형 · 고른 위젯 · 안전 영역(그림 위 ImGui 선, 캔버스는 바꾸지 않는다).
        ImDrawList* pDraw   = ImGui::GetWindowDrawList();
        const auto  drawBox = [pDraw, origin]( const UIRect& rect, ImU32 color, float32 thickness )
        {
            pDraw->AddRect( ImVec2{ origin.x + rect._left, origin.y + rect._top }, ImVec2{ origin.x + rect._right, origin.y + rect._bottom }, color, 0.0f, 0,
                            thickness );
        };
        if ( _bShowLayout )
        {
            vector<Widget*> listWidget;
            screen.getTree().collectWidgetsInDocumentOrder( listWidget );
            for ( const Widget* pWidget : listWidget )
            {
                if ( pWidget->isVisible() )
                    drawBox( UIPreviewLogic::mapUIToImage( pWidget->getGeometry().computeScreenBounds(), drawSize, _viewport ), Internal::kLayoutColor, 1.0f );
            }
        }
        if ( pSelected != nullptr )
            drawBox( UIPreviewLogic::mapUIToImage( pSelected->getGeometry().computeScreenBounds(), drawSize, _viewport ), Internal::kSelectColor, 2.0f );
        const float4& insets = _viewport._safeInsets;
        if ( insets._x > 0.0f || insets._y > 0.0f || insets._z > 0.0f || insets._w > 0.0f )
        {
            const UIRect safe{ insets._x, insets._y, _viewport._size._x - insets._z, _viewport._size._y - insets._w };
            drawBox( UIPreviewLogic::mapUIToImage( safe, drawSize, _viewport ), Internal::kSafeZoneColor, 1.0f );
        }
        ImGui::EndChild();
    }

    void UIPreviewPanel::drawContent()
    {
        UISystem* pUI = editor::getService<UISystem>();
        if ( pUI == nullptr || pUI->isInitialized() == false )
        {
            EditorWidgets::drawEmptyHint( "Runtime UI is not running." );
            return;
        }
        drawToolbar( *pUI );
        syncPreview( *pUI );
        syncTexture();
        ImGui::Separator();
        const UIScreen* pScreen = pUI->findOffscreenScreen( _screen );
        if ( pScreen == nullptr )
        {
            EditorWidgets::drawEmptyHint( _error.empty() ? "Enter a UI document path (*.ui.xml)." : _error.c_str() );
            return;
        }
        drawWidgetList( *pScreen );
        ImGui::SameLine();
        drawImage( *pScreen );
    }
} // namespace sw::editor
