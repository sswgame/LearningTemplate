#include "pch.h"

#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"
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
                EditorViewKind _focusedView{ EditorViewKind::Game };  ///< 마지막으로 포커스를 받은 뷰
                uint32         _waitSerial{ invalid_index::kUint32 }; ///< 결과를 기다리는 요청이 있으면 그 요청 전의 완료 번호, 없으면 `invalid_index::kUint32`
            };

            static CaptureState& getState() { return getEditorState<CaptureState>(); }

            static void request( string_view path, RHITextureHandle sourceTexture )
            {
                getState()._waitSerial = RenderThread::getCompletedScreenshotSerial();
                RenderThread::requestScreenshot( path, sourceTexture, RHIFormat::R8G8B8A8_UNORM );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorScreenshotCommands::captureGameView()
    {
        EditorScreenshotCommandsInternal::request( ScreenshotPathUtil::makeDefaultPathNow(), 0 );
    }

    void EditorScreenshotCommands::captureSceneView()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        // 씬 뷰 RT 는 에디터가 만든 R8G8B8A8_UNORM 이다(`EditorContext::ensureViewTargetSize`).
        const uint64 renderTarget = editorContext.getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Scene )._renderTarget;
        if ( renderTarget == 0 )
        {
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyScreenshotTitle" ), SW_LOCKEY( "EdCommon", "NotifyScreenshotNoSceneView" ), NotificationType::Warning );
            return;
        }
        EditorScreenshotCommandsInternal::request( ScreenshotPathUtil::makeDefaultPathNow(), static_cast<RHITextureHandle>( renderTarget ) );
    }

    void EditorScreenshotCommands::captureFocusedView()
    {
        if ( EditorScreenshotCommandsInternal::getState()._focusedView == EditorViewKind::Scene )
            captureSceneView();
        else
            captureGameView();
    }

    void EditorScreenshotCommands::noteFocusedView( EditorViewKind kind )
    {
        EditorScreenshotCommandsInternal::getState()._focusedView = kind;
    }

    void EditorScreenshotCommands::update()
    {
        EditorScreenshotCommandsInternal::CaptureState& captureState = EditorScreenshotCommandsInternal::getState();
        if ( captureState._waitSerial == invalid_index::kUint32 || RenderThread::getCompletedScreenshotSerial() == captureState._waitSerial )
            return;
        captureState._waitSerial = invalid_index::kUint32;

        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        const string   path          = RenderThread::getLastScreenshotPath();
        if ( path.empty() )
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyScreenshotTitle" ), SW_LOCKEY( "EdCommon", "NotifyScreenshotFailed" ), NotificationType::Error );
        else
            editorContext.getSubsystem<EditorNotificationManager>().pushRevealable( SW_LOCKEY( "EdCommon", "NotifyScreenshotSaved" ), path, NotificationType::Success, path );
    }
} // namespace sw::editor
