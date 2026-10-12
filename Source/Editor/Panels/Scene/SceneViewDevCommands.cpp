/**
 * @file SceneViewDevCommands.cpp
 * @brief 씬 뷰 패널의 개발 명령(오버레이 막대 초기화 · 보이기)입니다. 씬 뷰 패널을 찾으므로 패널 층에서 등록합니다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
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
            /** @brief 씬 뷰 오버레이입니다(씬 뷰 패널이 없으면 nullptr). */
            static EditorViewportOverlays* findSceneOverlays()
            {
                EditorContext*  pContext = EditorContext::get();
                SceneViewPanel* pPanel   = pContext != nullptr ? static_cast<SceneViewPanel*>( pContext->getPanelManager().findPanel( "scene_view" ) ) : nullptr;
                return pPanel != nullptr ? &pPanel->getViewportClient().getOverlays() : nullptr;
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
                if ( listArgument.size() != 2 || pOverlays == nullptr || pOverlays->setBarVisible( listArgument[0], listArgument[1] != "0" ) == false )
                    return false;
                outReply = "overlay " + listArgument[0] + ( listArgument[1] != "0" ? " shown" : " hidden" );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( OverlayReset, "overlay.reset", "overlay.reset", "Put the scene view overlay bars back where they start (and show them all)",
                    &SceneViewDevCommandsInternal::runOverlayReset );
    SW_DEV_COMMAND( OverlayShow, "overlay.show", "overlay.show <view|transform|display|tools> <0|1>", "Show or hide a scene view overlay bar",
                    &SceneViewDevCommandsInternal::runOverlayShow );
} // namespace sw::editor
