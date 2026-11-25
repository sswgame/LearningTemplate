#include "pch.h"

#include "Editor/Panels/Scene/Model/SceneViewModel.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/Model/SceneViewOverlayLayoutStore.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Graphics/DebugDraw/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"

namespace sw::editor
{
    namespace
    {
        struct SceneViewModelInternal
        {
            /** @brief 개발 명령이 쌓은 씬 뷰 요청입니다(에디터 범위 상태 — 창이 그릴 때 꺼낸다). */
            struct RequestQueue
            {
                vector<SceneViewRequest> _listRequest;
            };

            static bool runCameraReset( const vector<string>& /*listArgument*/, string& outReply )
            {
                SceneViewModel::postRequest( SceneViewRequest{ SceneViewRequestKind::CameraReset } );
                outReply = "scene view camera reset";
                return true;
            }

            /** @brief `camera.set <x> <y> <z> <pitch> <yaw>` — 씬 뷰 카메라를 그 위치 · 방향(도)에 둡니다(언리얼 `BugReportGoto x y z pitch yaw` 와 같은 쓰임). */
            static bool runCameraSet( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 5 )
                    return false;
                SceneViewRequest request{ SceneViewRequestKind::CameraSet };
                for ( uint32 index = 0; index < 5; ++index )
                {
                    if ( StringUtil::parseFloat( listArgument[index], request._arrPose[index] ) == false )
                        return false;
                }
                SceneViewModel::postRequest( std::move( request ) );
                outReply = "scene view camera set";
                return true;
            }

            static bool runOverlayReset( const vector<string>& /*listArgument*/, string& outReply )
            {
                editor::getSubsystem<SceneViewOverlayLayoutStore>().resetToDefault();
                outReply = "scene view overlays reset";
                return true;
            }

            static bool runOverlayShow( const vector<string>& listArgument, string& outReply )
            {
                bool       bVisible{ false };
                const bool bInvalid = listArgument.size() != 2 || readOptionalBoolArgument( listArgument, 1, bVisible ) == false || SceneViewOverlayLayoutStore::isKnownBarID( listArgument[0] ) == false;
                if ( bInvalid )
                    return false;
                (void)editor::getSubsystem<SceneViewOverlayLayoutStore>().setBarVisible( listArgument[0], bVisible ); // 씬 뷰 바는 초기화에서 모두 등록했다
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "overlay %#%#", listArgument[0], ( bVisible ? " shown" : " hidden" ) );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    void SceneViewModel::postRequest( SceneViewRequest request )
    {
        getEditorState<SceneViewModelInternal::RequestQueue>()._listRequest.push_back( std::move( request ) );
    }

    bool SceneViewModel::takeRequest( SceneViewRequest& outRequest )
    {
        vector<SceneViewRequest>& listRequest = getEditorState<SceneViewModelInternal::RequestQueue>()._listRequest;
        if ( listRequest.empty() )
            return false;
        outRequest = std::move( listRequest.front() );
        listRequest.erase( listRequest.begin() );
        return true;
    }

    bool SceneViewModel::findCameraPosition( float3& outPosition )
    {
        const CameraComponent* pCamera = EditorCamera::find( editor::getActiveScene() );
        if ( pCamera == nullptr )
            return false;
        outPosition = pCamera->getWorldPosition();
        return true;
    }

    bool SceneViewModel::findViewMode( uint32& outMode )
    {
        const FrameRenderer* pRenderer = editor::findSubsystem<FrameRenderer>();
        if ( pRenderer == nullptr ) // 필수 서비스: 없으면 버그
        {
            SW_LOG_ERROR_ID( "SceneViewModel.ViewModeFrameRenderer", "[Scenario] view mode watch: FrameRenderer is missing" );
            return false;
        }
        outMode = static_cast<uint32>( pRenderer->getSceneViewMode() );
        return true;
    }

    int32 SceneViewModel::getGizmoOperation()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getGizmoOperation();
    }

    float32 SceneViewModel::getUnscaledDeltaTime()
    {
        return editor::getSubsystem<GameTimeScale>().getUnscaledDeltaTime().count();
    }

    void SceneViewModel::gatherDebugCategories( vector<SceneViewDebugCategoryRow>& outListRow )
    {
        outListRow.clear();
        const DebugDrawQueue& queue = editor::getSubsystem<DebugDrawQueue>();
        vector<hashed_string> listCategory;
        queue.collectCategories( listCategory );
        for ( const hashed_string& category : listCategory )
        {
            outListRow.push_back( SceneViewDebugCategoryRow{ category, queue.isCategoryEnabled( category ) } );
        }
    }

    void SceneViewModel::setDebugCategoryEnabled( const hashed_string& category, bool bEnabled )
    {
        editor::getSubsystem<DebugDrawQueue>().setCategoryEnabled( category, bEnabled );
    }

    // 오버레이 명령은 배치의 주인(`SceneViewOverlayLayoutStore`)을 바로 고친다 — 씬 뷰 창이 가려져 그리지 않아도 적용된다(언리얼 뷰포트 설정 객체와 같은 선).
    SW_DEV_COMMAND( OverlayReset, "overlay.reset", "overlay.reset", "Put the scene view overlay bars back where they start (and show them all)",
                    &SceneViewModelInternal::runOverlayReset );
    // 카메라 명령은 요청만 쌓고 씬 뷰 창이 그릴 때 뷰포트 클라이언트에 적용한다(카메라는 View 가 든다).
    SW_DEV_COMMAND( CameraReset, "camera.reset", "camera.reset", "Put the scene view camera (pose, projection, speed) back where a new editor session starts",
                    &SceneViewModelInternal::runCameraReset );
    SW_DEV_COMMAND( CameraSet, "camera.set", "camera.set <x> <y> <z> <pitch> <yaw>", "Put the scene view camera at a position looking along pitch and yaw (degrees)",
                    &SceneViewModelInternal::runCameraSet );
    SW_DEV_COMMAND( OverlayShow, "overlay.show", "overlay.show <view|transform|display|tools> <0|1>", "Show or hide a scene view overlay bar",
                    &SceneViewModelInternal::runOverlayShow );
} // namespace sw::editor
