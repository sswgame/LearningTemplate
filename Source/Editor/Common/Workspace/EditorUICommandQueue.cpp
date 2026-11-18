#include "pch.h"

#include "Editor/Common/Workspace/EditorUICommandQueue.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/IEditorPanelService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Localization/LocText.h"
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
            static void start( EditorPlaySessionWriteKey key, const EditorUICommand& command )
            {
                PlaySessionData* pData = EditorPlaySession::findData( key );
                if ( pData != nullptr && EditorPlaySession::isStopped() )
                {
                    const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
                    if ( command._bStartAtCamera != SW_FALSE && pEditorCamera != nullptr )
                        EditorPlaySession::setStartPosition( *pData, pEditorCamera->getWorldPosition() );
                    else
                        EditorPlaySession::clearStartPosition( *pData );
                }
                if ( command._kind == EditorUICommandKind::StartSimulate )
                    EditorPlaySession::simulate( key );
                else
                    EditorPlaySession::play( key );
            }

            static void stop( EditorPlaySessionWriteKey key )
            {
                EditorPlaySession::stop( key );
                GameObjectManager* pObjectManager = editor::getActiveObjectManager();
                if ( pObjectManager != nullptr )
                    editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().remapSelectionByObjectName( pObjectManager );
            }

            /**
             * @brief F8 Eject/Possess — 세션 칸(`_bEjected` · 최대화 배치)과 패널 포커스를 이 한 적용이 함께 바꾼다. 패널 포커스는 다음 UI 프레임에 여는 요청이다(`IEditorPanelService::focusPanel`).
             * @details 플레이어 조종 중이면 조종을 놓는다 — 월드는 계속 돌고(Simulate) 에디터 카메라로 둘러본다. Eject 한 세션이면 다시 조종한다(언리얼 F8 Eject/Possess).
             */
            static void toggleEject( EditorPlaySessionWriteKey key )
            {
                PlaySessionData* pData                = EditorPlaySession::findData( key );
                const bool       bNoActivePlaySession = pData == nullptr || pData->_state == PlaySessionState::Stopped;
                if ( bNoActivePlaySession )
                    return;
                IEditorPanelService& panels = editor::getSubsystem<EditorContext>().getSubsystem<IEditorPanelService>();
                if ( EditorPlaySession::isPlayerActive( *pData ) )
                {
                    // 최대화 중이면 씬 뷰가 닫혀 있으니 배치를 먼저 되돌린다.
                    if ( pData->_bMaximizedForPlay == SW_TRUE )
                    {
                        pData->_bMaximizedForPlay = SW_FALSE;
                        (void)panels.restoreMaximizedLayout(); // 없으면 지금 배치 그대로 씬 뷰를 연다
                    }
                    EditorPlaySession::simulate( key );
                    pData->_bEjected = SW_TRUE;
                    (void)panels.focusPanel( editorpanel::kSceneView ); // 패널이 없으면 세션만 바뀐다
                    return;
                }
                if ( pData->_bEjected == SW_TRUE )
                {
                    EditorPlaySession::play( key );
                    pData->_bEjected = SW_FALSE;
                    (void)panels.focusPanel( editorpanel::kGameView ); // 패널이 없으면 세션만 바뀐다
                }
            }

            /** @brief Keep Simulation Changes — 플레이 세션은 메인 월드에서 돈다. 남길 변경은 메인 씬의 선택이다. 적었으면 알린다. */
            static void keepSelectedChanges( EditorPlaySessionWriteKey key )
            {
                PlaySessionData* pData                = EditorPlaySession::findData( key );
                const bool       bNoActivePlaySession = pData == nullptr || pData->_state == PlaySessionState::Stopped;
                if ( bNoActivePlaySession )
                    return;
                vector<GameObject*> listSelected;
                editor::getMainSceneSelection().getSelectedObjects( listSelected );
                uint32 keptCount{ 0 };
                for ( const GameObject* pObject : listSelected )
                {
                    keptCount += EditorPlaySession::keepObjectState( *pData, pObject ) ? 1u : 0u;
                }
                if ( keptCount > 0 )
                    editor::getSubsystem<EditorContext>().getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyKeepSimulationTitle" ),
                                                                                                          SW_LOCKEY( "EdCommon", "NotifyKeepSimulation" ), NotificationKind::Info );
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
        using Internal                          = EditorUICommandApplyInternal;
        const EditorUICommand&          command = *this;
        const EditorPlaySessionWriteKey key{}; // 게임 스레드 입구 — 아래 적용만 세션을 바꾼다
        switch ( _kind )
        {
            case EditorUICommandKind::FitViewTarget:
                Internal::fitViewTarget( command );
                break;
            case EditorUICommandKind::StartPlay:
            case EditorUICommandKind::StartSimulate:
                Internal::start( key, command );
                break;
            case EditorUICommandKind::Pause:
                EditorPlaySession::pause( key );
                break;
            case EditorUICommandKind::Resume:
                if ( EditorPlaySession::isPaused() )
                    EditorPlaySession::setState( key, PlaySessionState::Playing );
                break;
            case EditorUICommandKind::Step:
                EditorPlaySession::stepFrames( key, command._stepFrameCount );
                break;
            case EditorUICommandKind::Stop:
                Internal::stop( key );
                break;
            case EditorUICommandKind::ToggleEject:
                Internal::toggleEject( key );
                break;
            case EditorUICommandKind::KeepSelectedChanges:
                Internal::keepSelectedChanges( key );
                break;
            case EditorUICommandKind::Count:
                SW_LOG_ERROR_ID( "EditorUICommandQueue.EmptyCommand", "An editor UI command without a kind reached the game thread" );
                break;
        }
    }
} // namespace sw::editor
