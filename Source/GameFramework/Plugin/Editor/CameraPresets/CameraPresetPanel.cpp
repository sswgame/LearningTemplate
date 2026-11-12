#include "pch.h"

#include "GameFramework/Plugin/Editor/CameraPresets/CameraPresetPanel.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/EnumName.h"

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
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "OpenACamerasXmlPresetFileToSee" ) );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Reload" ) ) )
            reloadDocument();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "ReadThePresetFileAgainAfterSaving" ) );

        // 프리셋 목록 — 고르면 뷰포트 카메라가 그 프리셋이 놓는 자리로 간다.
        const float32 listHeight = ImGui::GetTextLineHeightWithSpacing() * CameraPresetPanelInternal::kListRowCount;
        if ( EditorWidgets::beginListBox( "##presets", ImVec2{ -FLT_MIN, listHeight } ) )
        {
            const CameraPresetCatalog& catalog = _model.getCatalog();
            for ( size_t index = 0; index < catalog.getCount(); ++index )
            {
                const CameraPresetDef& preset    = catalog.getAt( index );
                const bool             bSelected = static_cast<int32>( index ) == _model.getSelectedIndex();
                const string           label     = string( preset._id.c_str() ) + "  [" + getEnumName( preset._view._mode ) + "]";
                if ( ImGui::Selectable( label.c_str(), bSelected ) )
                    _model.selectPreset( index );
                EditorSelfTestMarks::record( ( string( "camerapreset.row." ) + preset._id.c_str() ).c_str() ); // 시나리오가 행을 고른다(editor/camerapresets)
            }
            ImGui::EndListBox();
        }

        const CameraPresetDef* pPreset = _model.findSelectedPreset();
        if ( pPreset != nullptr )
        {
            const CameraPresetDef& preset = *pPreset;
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "OffsetDistancePitchYaw", TextArgumentList().addDecimal( "offsetX", static_cast<float64>( preset._view._offset._x ), 2 ).addDecimal( "offsetY", static_cast<float64>( preset._view._offset._y ), 2 ).addDecimal( "offsetZ", static_cast<float64>( preset._view._offset._z ), 2 ).addDecimal( "distance", static_cast<float64>( preset._view._distance ), 2 ).addDecimal( "pitch", static_cast<float64>( preset._view._pitch ), 3 ).addDecimal( "yaw", static_cast<float64>( preset._view._yaw ), 3 ).addDecimal( "computeFieldOfViewY", static_cast<float64>( preset._lens.computeFieldOfViewY() ), 3 ) ).c_str() );
            if ( preset._lens._bOrthographic || preset._view._mode == CameraPresetMode::OrthoTopDown )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "OrthographicPresetThePreviewDrawsItInPerspective" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "TargetYaw" ) );
            if ( EditorWidgets::sliderFloat( "##Targetyaw", &_model.editTargetYawDegrees(), -180.0f, 180.0f, "%.0f deg" ) )
                _model.markViewportDirty();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "DirectionTheTargetFacesFollowFirstPersonAnd" ) );
        }

        applySelectedPresetToViewport();
        const float2 viewSize = computeViewportFillSize( CameraPresetPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale() );
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    bool CameraPresetPanel::canAcceptDocumentPath( string_view assetPath ) const
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
