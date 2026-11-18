#include "pch.h"

#include "Editor/Common/Workspace/EditorUICommandQueue.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/Component/CameraComponent.h"

namespace sw::editor
{
    EditorUICommand EditorUICommand::makeFitViewTarget( EditorViewKind view, uint32 width, uint32 height )
    {
        EditorUICommand command;
        command._kind   = EditorUICommandKind::FitViewTarget;
        command._view   = view;
        command._width  = width;
        command._height = height;
        return command;
    }

    EditorUICommand EditorUICommand::makeStart( bool bSimulate, bool bStartAtCamera )
    {
        EditorUICommand command;
        command._kind           = bSimulate ? EditorUICommandKind::StartSimulate : EditorUICommandKind::StartPlay;
        command._bStartAtCamera = bStartAtCamera ? SW_TRUE : SW_FALSE;
        return command;
    }

    EditorUICommand EditorUICommand::makeStep( uint32 frameCount )
    {
        EditorUICommand command;
        command._kind           = EditorUICommandKind::Step;
        command._stepFrameCount = frameCount;
        return command;
    }

    EditorUICommand EditorUICommand::make( EditorUICommandKind kind )
    {
        EditorUICommand command;
        command._kind = kind;
        return command;
    }
} // namespace sw::editor

namespace sw::editor
{
    EditorUICommandQueue::EditorUICommandQueue()
        : _queue{}
        , _collectingFrame{ 1 }
        , _droppedCount{ 0 }
        , _listTaken{}
        , _listDue{}
    {
    }

    void EditorUICommandQueue::shutdownSubsystem()
    {
        _queue.clear();
        _listTaken.clear();
        _listDue.clear();
    }

    bool EditorUICommandQueue::post( const EditorUICommand& command )
    {
        return postAt( command, getCollectingFrame() );
    }

    bool EditorUICommandQueue::postAt( const EditorUICommand& command, uint32 frame )
    {
        if ( _queue.enqueue( QueuedCommand{ command, frame } ) )
            return true;
        _droppedCount.fetch_add( 1, std::memory_order_relaxed );
        SW_LOG_ERROR_ID( "EditorUICommandQueue.Full", "The editor UI command queue is full (%# slots) - command %# dropped", kCapacity, static_cast<uint32>( command._kind ) );
        return false;
    }

    void EditorUICommandQueue::takeDue( vector<EditorUICommand>& outList )
    {
        outList.clear();
        const uint32 frame = _collectingFrame.fetch_add( 1, std::memory_order_acq_rel );
        _queue.popAll( _listTaken );
        size_t keptCount = 0;
        for ( const QueuedCommand& queued : _listTaken )
        {
            if ( queued._frame <= frame )
                outList.push_back( queued._command );
            else
                _listTaken[keptCount++] = queued;
        }
        _listTaken.resize( keptCount );
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorUICommandApplyInternal
        {
            /** @brief 재생을 시작합니다. 카메라에서 시작은 멈춤에서 시작할 때만 정한다(Simulate 는 옮길 플레이어가 없지만 시작 위치는 세션 값이라 같이 지운다). */
            static void start( const EditorUICommand& command )
            {
                PlaySessionData* pData = EditorPlaySession::findData();
                if ( pData != nullptr && EditorPlaySession::isStopped() )
                {
                    const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
                    if ( command._bStartAtCamera != SW_FALSE && pEditorCamera != nullptr )
                        EditorPlaySession::setStartPosition( *pData, pEditorCamera->getWorldPosition() );
                    else
                        EditorPlaySession::clearStartPosition( *pData );
                }
                if ( command._kind == EditorUICommandKind::StartSimulate )
                    EditorPlaySession::simulate();
                else
                    EditorPlaySession::play();
            }

            static void stop()
            {
                EditorPlaySession::stop();
                GameObjectManager* pObjectManager = editor::getActiveObjectManager();
                if ( pObjectManager != nullptr )
                    editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().remapSelectionByObjectName( pObjectManager );
            }

            static void fitViewTarget( const EditorUICommand& command )
            {
                EditorRenderViews&      views = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>();
                const EditorViewTarget& view  = views.getViewTarget( command._view );
                // 같은 크기를 두 프레임 보낼 수 있다(UI 는 적용 전 프레임에 요청한 크기를 아직 모른다) — 적용 쪽이 다시 견준다.
                if ( EditorViewTargetUtil::needsResize( view.getRequestedWidth(), view.getRequestedHeight(), command._width, command._height ) )
                    views.ensureViewTargetSize( command._view, command._width, command._height );
            }
        };
    } // namespace

    void EditorUICommandQueue::postToEditor( const EditorUICommand& command )
    {
        EditorContext* const pContext = editor::findSubsystem<EditorContext>(); // 컨텍스트 없이 도는 테스트 · 종료 정리
        if ( pContext != nullptr )
            (void)pContext->getSubsystem<EditorUICommandQueue>().post( command ); // 차면 큐가 오류를 남겼다
    }

    void EditorUICommandQueue::applyDue()
    {
        takeDue( _listDue );
        for ( const EditorUICommand& command : _listDue )
        {
            command.apply();
        }
    }

    void EditorUICommand::apply() const
    {
        using Internal                 = EditorUICommandApplyInternal;
        const EditorUICommand& command = *this;
        switch ( _kind )
        {
            case EditorUICommandKind::FitViewTarget:
                Internal::fitViewTarget( command );
                break;
            case EditorUICommandKind::StartPlay:
            case EditorUICommandKind::StartSimulate:
                Internal::start( command );
                break;
            case EditorUICommandKind::Pause:
                EditorPlaySession::pause();
                break;
            case EditorUICommandKind::Resume:
                if ( EditorPlaySession::isPaused() )
                    EditorPlaySession::setState( PlaySessionState::Playing );
                break;
            case EditorUICommandKind::Step:
                EditorPlaySession::stepFrames( command._stepFrameCount );
                break;
            case EditorUICommandKind::Stop:
                Internal::stop();
                break;
            case EditorUICommandKind::Count:
                SW_LOG_ERROR_ID( "EditorUICommandQueue.EmptyCommand", "An editor UI command without a kind reached the game thread" );
                break;
        }
    }
} // namespace sw::editor
