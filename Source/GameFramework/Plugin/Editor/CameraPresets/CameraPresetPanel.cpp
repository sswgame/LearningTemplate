/**
 * @file CameraPresetPanel.cpp
 * @brief 카메라 프리셋 파일(`*.cameras.xml`)의 프리셋을 미리보기 씬에서 보는 창입니다(Cinemachine 의 프리셋 미리보기, 언리얼 카메라 리그 미리보기에 해당).
 * @details 프리셋 파일 · 고름 · 대상 요 · 카메라 자리 풀기는 `CameraPresetViewModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 미리보기 씬 · 뷰포트만 쥔다.
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Plugin/Editor/CameraPresets/Model/CameraPresetViewModel.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @class CameraPresetPanel
     * @brief 연 프리셋 파일의 프리셋 하나를 골라, 미리보기 씬의 대상(원점 위 캡슐)에 대해 그 프리셋이 놓는 카메라를 자유 카메라로 보입니다.
     * @details **보기 전용**이다. 프리셋 파일은 데이터 종류(`camerapresets`)라 카탈로그 편집기가 고친다 — 같은 파일을 두 편집기가 쥐지 않게 이 창은 저장하지 않는다.
     *          프리셋은 `evaluatePreset`(상태 없는 순수 계산)으로 푼다. 입력 · 암 · 프레이밍 · 흔들림 상태는 없어 Orbit 은 프리셋의 요 · 피치 그대로,
     *          Follow · FirstPerson · ThirdPerson 은 "대상 요" 슬라이더가 정한 방향의 대상에서 본 자리다. 직교 프리셋은 뷰포트가 원근만 그려 시야각으로 대신 보인다.
     *          뷰포트 카메라는 프리셋을 고르거나 대상 요를 바꿀 때만 놓는다 — 그 뒤 끌기 · 휠은 자유 카메라로 둘러본다.
     */
    class CameraPresetPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "camerapreset_preview";

        CameraPresetPanel();

        const utf8* getPanelTitle() const override { return "Camera Presets"; }
        void        drawDocumentContent() override;
        /** @brief 보기 전용이라 저장하지 않습니다(프리셋은 카탈로그 편집기가 고친다). */
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /** @brief 지금 고른 프리셋 번호입니다(감시 값 `Editor.CameraPresetSelected`). 없으면 -1 입니다. */
        int32 getSelectedIndex() const { return _model.getSelectedIndex(); }
        /** @brief 문서의 프리셋 수입니다. */
        uint32 getPresetCount() const { return static_cast<uint32>( _model.getCatalog().getCount() ); }

    private:
        bool                canAcceptDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 미리보기 씬에 대상(캡슐) 하나를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 모델이 카메라 자리를 내면 뷰포트 카메라를 그 자리에 놓습니다. */
        void applySelectedPresetToViewport();

    private:
        CameraPresetViewModel _model;
    };

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
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const CameraPresetPanel* pPanel = pPanels->findPanel<CameraPresetPanel>();
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
        if ( EditorWidgets::beginListBox( SW_WIDGET_ID( "##presets" ), ImVec2{ -FLT_MIN, listHeight } ) )
        {
            const CameraPresetCatalog& catalog = _model.getCatalog();
            for ( size_t index = 0; index < catalog.getCount(); ++index )
            {
                const CameraPresetDef&                  preset    = catalog.getAt( index );
                const bool                              bSelected = static_cast<int32>( index ) == _model.getSelectedIndex();
                fixed_string<constant::buffer::kMax128> label;
                formatstring( label.data(), label.capacity(), "%#  [%#]", preset._id.c_str(), getEnumName( preset._view._mode ) );
                if ( ImGui::Selectable( label.c_str(), bSelected ) )
                    _model.selectPreset( index );
                EditorSelfTestMarks::recordFormat( "camerapreset.row.%#", preset._id.c_str() ); // 시나리오가 행을 고른다(editor/camerapresets)
            }
            ImGui::EndListBox();
        }

        const CameraPresetDef* pPreset = _model.findSelectedPreset();
        if ( pPreset != nullptr )
        {
            const CameraPresetDef& preset = *pPreset;
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "OffsetDistancePitchYaw", TextArgumentList().addDecimal( "offsetX", static_cast<float64>( preset._view._offset._x ), 2 ).addDecimal( "offsetY", static_cast<float64>( preset._view._offset._y ), 2 ).addDecimal( "offsetZ", static_cast<float64>( preset._view._offset._z ), 2 ).addDecimal( "distance", static_cast<float64>( preset._view._distance ), 2 ).addDecimal( "pitch", static_cast<float64>( preset._view._pitch.radians() ), 3 ).addDecimal( "yaw", static_cast<float64>( preset._view._yaw.radians() ), 3 ).addDecimal( "computeFieldOfViewY", static_cast<float64>( preset._lens.computeFieldOfViewY() ), 3 ) ).c_str() );
            if ( preset._lens._bOrthographic || preset._view._mode == CameraPresetMode::OrthoTopDown )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "OrthographicPresetThePreviewDrawsItInPerspective" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "TargetYaw" ) );
            EditorWidgets::beginResetToDefaultGroup();
            if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Targetyaw" ), &_model.editTargetYawDegrees(), -180.0f, 180.0f, "%.0f deg" ) )
                _model.markViewportDirty();
            bool       bHovered  = false;
            const bool bResetYaw = EditorWidgets::endResetToDefaultGroup( _model.editTargetYawDegrees() != CameraPresetViewModel::kDefaultTargetYawDegrees, bHovered ); // R10 — 휠클릭은 기본 요로
            if ( bResetYaw )
            {
                _model.editTargetYawDegrees() = CameraPresetViewModel::kDefaultTargetYawDegrees;
                _model.markViewportDirty();
            }
            EditorSelfTestMarks::record( "camerapreset.targetYaw" );
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
