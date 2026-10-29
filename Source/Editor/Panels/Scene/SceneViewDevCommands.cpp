/**
 * @file SceneViewDevCommands.cpp
 * @brief 씬 뷰 패널의 개발 명령(오버레이 막대 초기화 · 보이기, 카메라 초기화 · 놓기)입니다. 씬 뷰 패널을 찾으므로 패널 층에서 등록합니다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/Scene/SceneViewPanel.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Viewport/EditorViewportOverlays.h"

namespace sw::editor
{
    namespace
    {
        struct SceneViewDevCommandsInternal
        {
            /** @brief 씬 뷰 패널입니다(없으면 nullptr). */
            static SceneViewPanel* findSceneViewPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<SceneViewPanel>() : nullptr;
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
