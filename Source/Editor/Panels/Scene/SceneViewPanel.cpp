#include "pch.h"

#include "Editor/Panels/Scene/SceneViewPanel.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorViewSurface.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Scene/Model/SceneViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Viewport/EditorViewportOverlays.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( SceneViewPanel, SceneViewPanel::kPanelID, EditorPanelCategory::Core, 300 );

    SceneViewPanel::SceneViewPanel()
        : _viewportClient{}
        , _listDebugCategory{}
    {
    }

    void SceneViewPanel::drawContent()
    {
        const bool bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        const bool bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        // 엔진 프레임의 실제 경과(시간 배율 · 정지와 무관) — 고정 프레임 시간(`-gv_fixedFrameDelta`)으로 도는 시나리오에서 카메라 비행이 결정적이다.
        const float32 dt = SceneViewModel::getUnscaledDeltaTime();
        _viewportClient.update( dt, bFocused, bHovered );

        if ( bFocused )
            EditorViewSurface::recordFocusedView( EditorViewKind::Scene );

        if ( EditorChrome::beginToolbar( "##SceneViewToolbar" ) )
        {
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Camera" ), "##SceneViewScreenshot" ) ) )
                EditorViewSurface::captureScreenshot( EditorViewKind::Scene );
            EditorSelfTestMarks::record( "sceneView.screenshot" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "SceneViewScreenshot" ) );
            EditorWidgets::drawToolbarSeparator();

            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Bug" ), SW_LOCKEY( "EdButton", "DebugDraw" ), "Debug Draw" ) ) ) // 예전 "Dbg Cat" — 줄임말이 무엇을 거르는지 말하지 않았다
                EditorInputGUI::openPopup( "##DebugDrawCategories" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "DebugDrawCategories" ) );
            drawDebugCategoryPopup();
            EditorWidgets::drawToolbarSeparator();
            _viewportClient.drawOverlaysMenu(); // 보기 · 표시 · 도구 · 트랜스폼 바는 씬 뷰 위에 떠 있다(`EditorViewportOverlays`)
        }
        EditorChrome::endToolbar();

        // 크기 규칙은 게임 뷰와 같다(정수 픽셀로 내림) — 같은 도크 영역의 두 탭이 같은 크기의 RT 를 갖는다.
        const ImVec2         available = ImGui::GetContentRegionAvail();
        const EditorViewRect rect      = EditorViewTargetUtil::fitViewImage( float2{ available.x, available.y }, EditorGameViewAspect::Free );
        const ImVec2         size{ rect._size._x, rect._size._y };
        if ( size.x > 1.0f && size.y > 1.0f )
            EditorViewSurface::fitTargetSize( EditorViewKind::Scene, static_cast<uint32>( size.x ), static_cast<uint32>( size.y ) );
        // 이 패널이 보이는 프레임만 씬 뷰를 그린다(접힘 · 닫힘 · 다른 탭이면 drawContent 가 불리지 않는다).
        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        EditorViewSurface::markDrawn( EditorViewKind::Scene, float2{ imagePos.x - ImGui::GetMainViewport()->Pos.x, imagePos.y - ImGui::GetMainViewport()->Pos.y } );

        _viewportClient.draw( EditorViewSurface::getTextureID( EditorViewKind::Scene ), float2{ size.x, size.y } );

        _viewportClient.drawOverlays( float2{ imagePos.x, imagePos.y }, float2{ size.x, size.y } );
    }

    void SceneViewPanel::drawDebugCategoryPopup()
    {
        if ( ImGui::BeginPopup( "##DebugDrawCategories" ) == false )
            return;
        SceneViewModel::gatherDebugCategories( _listDebugCategory );
        if ( _listDebugCategory.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoDebugDrawYet" ) );
        for ( const SceneViewDebugCategoryRow& row : _listDebugCategory )
        {
            bool bEnabled = row._bEnabled;
            if ( ImGui::Checkbox( row._name.c_str(), &bEnabled ) )
                SceneViewModel::setDebugCategoryEnabled( row._name, bEnabled );
        }
        ImGui::EndPopup();
    }

    namespace
    {
        struct SceneViewDevCommandsInternal
        {
            /** @brief 씬 뷰 패널입니다(없으면 nullptr). */
            static SceneViewPanel* findSceneViewPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                return pPanels != nullptr ? pPanels->findPanel<SceneViewPanel>() : nullptr;
            }

            /** @brief 씬 뷰 오버레이입니다(씬 뷰 패널이 없으면 nullptr). */
            static EditorViewportOverlays* findSceneOverlays()
            {
                SceneViewPanel* pPanel = findSceneViewPanel();
                return pPanel != nullptr ? &pPanel->getViewportClient().getOverlays() : nullptr;
            }

            static bool runCameraReset( const vector<string>& /*listArgument*/, string& outReply )
            {
                SceneViewPanel* pPanel = findSceneViewPanel();
                if ( pPanel == nullptr )
                    return false;
                pPanel->getViewportClient().resetCameraToDefault();
                outReply = "scene view camera reset";
                return true;
            }

            /** @brief `camera.set <x> <y> <z> <pitch> <yaw>` — 씬 뷰 카메라를 그 위치 · 방향(도)에 둡니다(언리얼 `BugReportGoto x y z pitch yaw` 와 같은 쓰임). */
            static bool runCameraSet( const vector<string>& listArgument, string& outReply )
            {
                SceneViewPanel* pPanel = findSceneViewPanel();
                if ( listArgument.size() != 5 || pPanel == nullptr )
                    return false;
                float32 arrValue[5]{};
                for ( uint32 index = 0; index < 5; ++index )
                {
                    if ( StringUtil::parseFloat( listArgument[index], arrValue[index] ) == false )
                        return false;
                }
                pPanel->getViewportClient().setCameraPose( float3{ arrValue[0], arrValue[1], arrValue[2] }, float3{ arrValue[3], arrValue[4], 0.0f } );
                outReply = "scene view camera set";
                return true;
            }

            static bool runOverlayReset( const vector<string>& /*listArgument*/, string& outReply )
            {
                EditorViewportOverlays* pOverlays = findSceneOverlays();
                if ( pOverlays == nullptr )
                    return false;
                pOverlays->resetToDefault();
                outReply = "scene view overlays reset";
                return true;
            }

            static bool runOverlayShow( const vector<string>& listArgument, string& outReply )
            {
                EditorViewportOverlays* pOverlays = findSceneOverlays();
                bool                    bVisible{ false };
                const bool              bSetOverlayFailed = listArgument.size() != 2 || readOptionalBoolArgument( listArgument, 1, bVisible ) == false || pOverlays == nullptr ||
                                               pOverlays->setBarVisible( listArgument[0], bVisible ) == false;
                if ( bSetOverlayFailed )
                    return false;
                outReply = "overlay " + listArgument[0] + ( bVisible ? " shown" : " hidden" );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( OverlayReset, "overlay.reset", "overlay.reset", "Put the scene view overlay bars back where they start (and show them all)",
                    &SceneViewDevCommandsInternal::runOverlayReset );
    SW_DEV_COMMAND( CameraReset, "camera.reset", "camera.reset", "Put the scene view camera (pose, projection, speed) back where a new editor session starts",
                    &SceneViewDevCommandsInternal::runCameraReset );
    SW_DEV_COMMAND( CameraSet, "camera.set", "camera.set <x> <y> <z> <pitch> <yaw>", "Put the scene view camera at a position looking along pitch and yaw (degrees)",
                    &SceneViewDevCommandsInternal::runCameraSet );
    SW_DEV_COMMAND( OverlayShow, "overlay.show", "overlay.show <view|transform|display|tools> <0|1>", "Show or hide a scene view overlay bar",
                    &SceneViewDevCommandsInternal::runOverlayShow );
} // namespace sw::editor
