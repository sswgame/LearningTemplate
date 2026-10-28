#include "pch.h"

#include "GameFramework/PlugIn/Editor/CameraPresets/CameraPresetPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Scene/Scene.h"

#include "GameFramework/Base/Actor/Camera/CameraMode.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CameraPresetPanelInternal
        {
            /// @brief 대상 캡슐의 높이(m)입니다 — 사람 크기로 서서 카메라가 무엇을 보는지 가늠하게 한다.
            static constexpr float32 kSubjectHeight = 1.8f;
            /// @brief 프리셋이 따르는 초점의 지면 위 높이(m)입니다(대상의 가운데 높이).
            static constexpr float32 kFocusHeight = 1.0f;
            /// @brief 미리보기 바닥의 높이입니다(`EditorPreviewEnvironment::_floorHeight` 기본값).
            static constexpr float32 kFloorHeight = -0.5f;
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다.
            static constexpr float32 kMinViewportHeight = 160.0f;
            /// @brief 프리셋 목록의 높이(줄 수)입니다.
            static constexpr float32 kListRowCount = 6.0f;

            [[nodiscard]] static const utf8* getModeName( CameraPresetMode mode )
            {
                switch ( mode )
                {
                    case CameraPresetMode::Fixed:
                        return "Fixed";
                    case CameraPresetMode::OrthoTopDown:
                        return "OrthoTopDown";
                    case CameraPresetMode::Orbit:
                        return "Orbit";
                    case CameraPresetMode::Follow:
                        return "Follow";
                    case CameraPresetMode::FirstPerson:
                        return "FirstPerson";
                    case CameraPresetMode::ThirdPerson:
                        return "ThirdPerson";
                    case CameraPresetMode::FollowXY:
                        return "FollowXY";
                    case CameraPresetMode::Dolly:
                        return "Dolly";
                }
                return "?";
            }

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
    SW_AUTOMATION_PROBE( editorCameraPresetCount, "Editor.CameraPresetCount", "Presets in the file the Camera Presets panel has open (fails while the panel is closed)",
                         &CameraPresetPanelInternal::readPresetCount );
    SW_AUTOMATION_PROBE( editorCameraPresetSelected, "Editor.CameraPresetSelected", "Index of the preset selected in the Camera Presets panel (-1 = none, fails while the panel is closed)",
                         &CameraPresetPanelInternal::readSelectedIndex );

    CameraPresetPanel::CameraPresetPanel()
        : EditorPreviewPanel{ EditorAssetType::Data, false }
        , _catalog{}
        , _selectedIndex{ -1 }
        , _targetYawDegrees{ 0.0f }
        , _bViewportDirty{ false }
        , _status{}
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
            for ( size_t index = 0; index < _catalog.getCount(); ++index )
            {
                const CameraPresetDef& preset    = _catalog.getAt( index );
                const bool             bSelected = static_cast<int32>( index ) == _selectedIndex;
                const string           label     = string( preset._id.c_str() ) + "  [" + CameraPresetPanelInternal::getModeName( preset._view._mode ) + "]";
                if ( ImGui::Selectable( label.c_str(), bSelected ) )
                {
                    _selectedIndex  = static_cast<int32>( index );
                    _bViewportDirty = true;
                }
            }
            ImGui::EndListBox();
        }

        if ( _selectedIndex >= 0 && static_cast<size_t>( _selectedIndex ) < _catalog.getCount() )
        {
            const CameraPresetDef& preset = _catalog.getAt( static_cast<size_t>( _selectedIndex ) );
            ImGui::Text( "offset %.2f, %.2f, %.2f  distance %.2f  pitch %.3f  yaw %.3f  fov %.3f", preset._view._offset._x, preset._view._offset._y, preset._view._offset._z,
                         preset._view._distance, preset._view._pitch, preset._view._yaw, preset._lens.computeFieldOfViewY() );
            if ( preset._lens._bOrthographic || preset._view._mode == CameraPresetMode::OrthoTopDown )
                ImGui::TextDisabled( "Orthographic preset - the preview draws it in perspective." );
            if ( ImGui::SliderFloat( "Target yaw", &_targetYawDegrees, -180.0f, 180.0f, "%.0f deg" ) )
                _bViewportDirty = true;
            EditorWidgets::drawTooltip( "Direction the target faces - Follow, FirstPerson and ThirdPerson presets turn with it" );
        }

        applySelectedPresetToViewport();
        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = CameraPresetPanelInternal::kMinViewportHeight * EditorThemeUtil::getDpiScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    bool CameraPresetPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, ".cameras.xml" );
    }

    ToolAssetLoadResult CameraPresetPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();

        _catalog.clear();
        _selectedIndex = -1;
        if ( _catalog.loadFromResource( ResourceID{ path } ) == false )
        {
            _status = "The preset file could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        if ( _catalog.isEmpty() == false )
            _selectedIndex = 0;
        _bViewportDirty = true;
        _status         = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void CameraPresetPanel::populatePreviewScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( "PreviewSubject" ) ) : nullptr;
        MeshComponent*     pMesh    = pSubject != nullptr ? pSubject->addComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
        {
            pMesh->setMeshID( "Capsule" );
            pMesh->setLocalPosition( float3{ 0.0f, CameraPresetPanelInternal::kFloorHeight + CameraPresetPanelInternal::kSubjectHeight * 0.5f, 0.0f } ); // 바닥에 선다
            pMesh->setLocalScale( float3{ 0.5f, CameraPresetPanelInternal::kSubjectHeight * 0.5f, 0.5f } );
        }
        _bViewportDirty = true; // 새 씬의 카메라를 프리셋 자리에 다시 놓는다
    }

    void CameraPresetPanel::applySelectedPresetToViewport()
    {
        const bool bHasSelection = _selectedIndex >= 0 && static_cast<size_t>( _selectedIndex ) < _catalog.getCount();
        if ( _bViewportDirty == false || bHasSelection == false || getPreviewScene() == nullptr )
            return;
        _bViewportDirty = false;

        const CameraPresetDef& preset = _catalog.getAt( static_cast<size_t>( _selectedIndex ) );
        CameraTarget           target{};
        target._focus           = float3{ 0.0f, CameraPresetPanelInternal::kFloorHeight + CameraPresetPanelInternal::kFocusHeight, 0.0f };
        target._yaw             = _targetYawDegrees * MathUtil::kDegreeToRadian;
        const float2& imageSize = getPreviewViewport().getImageSize();
        if ( imageSize._y > 1.0f )
            target._aspectRatio = imageSize._x / imageSize._y;

        const CameraPose pose = evaluatePreset( preset, target );
        // 포즈의 시선(+Z 를 회전으로 돌린 방향)을 뷰포트 요 · 피치 규약(시선 = (-sin 요 cos 피치, -sin 피치, -cos 요 cos 피치))으로 바꾼다.
        const float3  forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pose._rotation.toMatrix() ).normalize();
        const float32 pitch   = MathUtil::asin( MathUtil::clamp( -forward._y, -1.0f, 1.0f ) );
        const float32 yaw     = MathUtil::atan2( -forward._x, -forward._z );
        getPreviewViewport().setFreeCamera( pose._position, yaw, pitch, pose._fieldOfViewY );
    }
} // namespace sw::editor
