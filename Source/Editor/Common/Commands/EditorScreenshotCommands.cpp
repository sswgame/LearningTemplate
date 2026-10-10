#include "pch.h"

#include "Editor/Common/Commands/EditorScreenshotCommands.h"

#include "Editor/Common/GUI/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw::editor
{
    namespace
    {
        struct EditorScreenshotCommandsInternal
        {
            /** @brief 마지막으로 포커스를 받은 뷰입니다. 모듈 리로드 때 처음 값으로 돌아가도 해가 없다. */
            inline static EditorViewKind _s_focusedView = EditorViewKind::Game;
            /** @brief 결과를 기다리는 요청이 있으면 그 요청 전의 완료 번호, 없으면 `invalid_index::kUint32` 입니다. */
            inline static uint32 _s_waitSerial = invalid_index::kUint32;

            static void request( string_view path, RHITextureHandle sourceTexture )
            {
                _s_waitSerial = RenderThread::getCompletedScreenshotSerial();
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
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        // 씬 뷰 RT 는 에디터가 만든 R8G8B8A8_UNORM 이다(`EditorContext::ensureViewTargetSize`).
        const uint64 renderTarget = pContext->getViewTarget( EditorViewKind::Scene )._renderTarget;
        if ( renderTarget == 0 )
        {
            pContext->getNotificationManager().push( "Screenshot", "The scene view is not drawn - open the Scene tab first", NotificationType::Warning );
            return;
        }
        EditorScreenshotCommandsInternal::request( ScreenshotPathUtil::makeDefaultPathNow(), static_cast<RHITextureHandle>( renderTarget ) );
    }

    void EditorScreenshotCommands::captureFocusedView()
    {
        if ( EditorScreenshotCommandsInternal::_s_focusedView == EditorViewKind::Scene )
            captureSceneView();
        else
            captureGameView();
    }

    void EditorScreenshotCommands::noteFocusedView( EditorViewKind kind )
    {
        EditorScreenshotCommandsInternal::_s_focusedView = kind;
    }

    void EditorScreenshotCommands::update()
    {
        if ( EditorScreenshotCommandsInternal::_s_waitSerial == invalid_index::kUint32 ||
             RenderThread::getCompletedScreenshotSerial() == EditorScreenshotCommandsInternal::_s_waitSerial )
            return;
        EditorScreenshotCommandsInternal::_s_waitSerial = invalid_index::kUint32;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        const string path = RenderThread::getLastScreenshotPath();
        if ( path.empty() )
            pContext->getNotificationManager().push( "Screenshot", "The screenshot was not written (see the log)", NotificationType::Error );
        else
            pContext->getNotificationManager().push( "Screenshot saved", path, NotificationType::Success );
    }
} // namespace sw::editor
