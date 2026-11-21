#include "pch.h"

#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Renderer/Capture/ScreenshotLocalTime.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw::editor
{
    namespace
    {
        struct EditorScreenshotCommandsInternal
        {
            /** @brief 스크린샷 명령이 프레임 사이에 들고 있는 상태입니다. 에디터 범위의 상태 저장소가 소유한다. */
            struct CaptureState
            {
                EditorViewKind _focusedView{ EditorViewKind::Game }; ///< 마지막으로 포커스를 받은 뷰
                uint32         _waitTicket{ 0 };                     ///< 결과를 기다리는 요청의 표(`RenderThread::requestScreenshot`), 없으면 0
                string         _waitPath{};                          ///< 그 요청의 파일 경로(알림이 보인다)
            };

            static CaptureState& getState() { return getEditorState<CaptureState>(); }

            static void request( string_view path, RHITextureHandle sourceTexture )
            {
                const optional<AbsolutePath> absolutePath = AbsolutePath::tryMake( path );
                if ( absolutePath.has_value() == false )
                    return; // 이름을 잡지 못했다(빈 값) — 이유는 `reserveUniquePath` 가 남겼다
                // 제 표의 결과만 읽는다 — 같은 실행의 다른 요청(시작 인자 사진 · 미리보기 창)과 섞이지 않는다.
                CaptureState& state = getState();
                state._waitPath     = absolutePath->str();
                state._waitTicket   = RenderThread::requestScreenshot( *absolutePath, sourceTexture, RHIFormat::R8G8B8A8_UNORM );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorScreenshotCommands::captureGameView()
    {
        EditorScreenshotCommandsInternal::request( ScreenshotLocalTime::makeDefaultPathNow(), 0 );
    }

    void EditorScreenshotCommands::captureSceneView()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        // 씬 뷰 RT 는 에디터가 요청한 R8G8B8A8_UNORM 이다(`EditorRenderViews::ensureViewTargetSize`). 단축키 · 버튼(UI)이 부르므로 UI 쪽 타깃 값을 읽는다.
        const RHITextureHandle renderTarget = editorContext.getSubsystem<EditorRenderViews>().getUITargetState( EditorViewKind::Scene )._renderTarget;
        if ( renderTarget == 0 )
        {
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyScreenshotTitle" ), SW_LOCKEY( "EdCommon", "NotifyScreenshotNoSceneView" ), NotificationKind::Warning );
            return;
        }
        EditorScreenshotCommandsInternal::request( ScreenshotLocalTime::makeDefaultPathNow(), renderTarget );
    }

    void EditorScreenshotCommands::captureFocusedView()
    {
        if ( EditorScreenshotCommandsInternal::getState()._focusedView == EditorViewKind::Scene )
            captureSceneView();
        else
            captureGameView();
    }

    void EditorScreenshotCommands::recordFocusedView( EditorViewKind kind )
    {
        EditorScreenshotCommandsInternal::getState()._focusedView = kind;
    }

    void EditorScreenshotCommands::update()
    {
        EditorScreenshotCommandsInternal::CaptureState& captureState = EditorScreenshotCommandsInternal::getState();
        if ( captureState._waitTicket == 0 )
            return;
        const ScreenshotTicketState state = RenderThread::findScreenshotResult( captureState._waitTicket );
        if ( state == ScreenshotTicketState::Pending )
            return;
        captureState._waitTicket = 0;

        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        const string   path          = std::move( captureState._waitPath );
        if ( state != ScreenshotTicketState::Written )
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyScreenshotTitle" ), SW_LOCKEY( "EdCommon", "NotifyScreenshotFailed" ), NotificationKind::Error );
        else
            editorContext.getSubsystem<EditorNotificationManager>().pushRevealable( SW_LOCKEY( "EdCommon", "NotifyScreenshotSaved" ), path, NotificationKind::Success, path );
    }
} // namespace sw::editor
