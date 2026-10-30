#include "pch.h"

#include "GameFramework/Plugin/Editor/CameraPresets/CameraPresetPanel.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CameraPresetPanelInternal
        {
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다.
            static constexpr float32 kMinViewportHeight = 160.0f;
            /// @brief 프리셋 목록의 높이(줄 수)입니다.
            static constexpr float32 kListRowCount = 6.0f;

            [[nodiscard]] static const CameraPresetPanel* findOpenPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const CameraPresetPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<CameraPresetPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readPresetCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CameraPresetPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPresetCount() );
                return true;
            }

            [[nodiscard]] static bool readSelectedIndex( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CameraPresetPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getSelectedIndex() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "CameraPresetPanel" );
    SW_EDITOR_PANEL( CameraPresetPanel, CameraPresetPanel::kPanelID, EditorPanelCategory::Tool, 1330 );
    SW_AUTOMATION_WATCH( editorCameraPresetCount, "Editor.CameraPresetCount", "Presets in the file the Camera Presets panel has open (fails while the panel is closed)",
                         &CameraPresetPanelInternal::readPresetCount );
    SW_AUTOMATION_WATCH( editorCameraPresetSelected, "Editor.CameraPresetSelected", "Index of the preset selected in the Camera Presets panel (-1 = none, fails while the panel is closed)",
                         &CameraPresetPanelInternal::readSelectedIndex );

    CameraPresetPanel::CameraPresetPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _model{}
    {
    }

    void CameraPresetPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "camerapreset" );

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( "Open a .cameras.xml preset file to see its cameras here. Edit presets in the Catalog Editor." );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( "Reload" ) )
            reloadDocument();
        EditorWidgets::drawTooltip( "Read the preset file again (after saving it in the Catalog Editor)" );

        // 프리셋 목록 — 고르면 뷰포트 카메라가 그 프리셋이 놓는 자리로 간다.
        const float32 listHeight = ImGui::GetTextLineHeightWithSpacing() * CameraPresetPanelInternal::kListRowCount;
        if ( ImGui::BeginListBox( "##presets", ImVec2{ -FLT_MIN, listHeight } ) )
        {
            const CameraPresetCatalog& catalog = _model.getCatalog();
            for ( size_t index = 0; index < catalog.getCount(); ++index )
            {
                const CameraPresetDef& preset    = catalog.getAt( index );
                const bool             bSelected = static_cast<int32>( index ) == _model.getSelectedIndex();
                const string           label     = string( preset._id.c_str() ) + "  [" + CameraPresetModel::getModeName( preset._view._mode ) + "]";
                if ( ImGui::Selectable( label.c_str(), bSelected ) )
                    _model.selectPreset( index );
            }
            ImGui::EndListBox();
        }

        const CameraPresetDef* pPreset = _model.findSelectedPreset();
        if ( pPreset != nullptr )
        {
            const CameraPresetDef& preset = *pPreset;
            ImGui::Text( "offset %.2f, %.2f, %.2f  distance %.2f  pitch %.3f  yaw %.3f  fov %.3f", preset._view._offset._x, preset._view._offset._y, preset._view._offset._z,
                         preset._view._distance, preset._view._pitch, preset._view._yaw, preset._lens.computeFieldOfViewY() );
            if ( preset._lens._bOrthographic || preset._view._mode == CameraPresetMode::OrthoTopDown )
                ImGui::TextDisabled( "Orthographic preset - the preview draws it in perspective." );
            if ( ImGui::SliderFloat( "Target yaw", &_model.editTargetYawDegrees(), -180.0f, 180.0f, "%.0f deg" ) )
                _model.markViewportDirty();
            EditorWidgets::drawTooltip( "Direction the target faces - Follow, FirstPerson and ThirdPerson presets turn with it" );
        }

        applySelectedPresetToViewport();
        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = CameraPresetPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    bool CameraPresetPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, ".cameras.xml" );
    }

    ToolAssetLoadResult CameraPresetPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        return _model.loadAsset( path );
    }

    void CameraPresetPanel::populatePreviewScene( Scene& scene )
    {
        _model.populateScene( scene );
    }

    void CameraPresetPanel::applySelectedPresetToViewport()
    {
        // 씬이 아직 없으면 놓지 않는다 — 모델은 놓아야 함을 그대로 들고 다음 그리기에서 다시 낸다.
        CameraPresetViewportPose pose{};
        if ( getPreviewScene() == nullptr || _model.takeViewportPose( getPreviewViewport().getImageSize(), pose ) == false )
            return;
        getPreviewViewport().setFreeCamera( pose._position, pose._yaw, pose._pitch, pose._fieldOfViewY );
    }
} // namespace sw::editor
