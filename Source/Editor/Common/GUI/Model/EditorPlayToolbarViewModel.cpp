#include "pch.h"

#include "Editor/Common/GUI/Model/EditorPlayToolbarViewModel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorUICommandQueue.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Config/GameTimeScale.h"

namespace sw::editor
{
    EditorPlayToolbarViewModel& EditorPlayToolbarViewModel::get()
    {
        return getEditorState<EditorPlayToolbarViewModel>();
    }

    void EditorPlayToolbarViewModel::updateNetPlay()
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>().update();
    }

    void EditorPlayToolbarViewModel::gather( EditorPlayToolbarViewState& outState ) const
    {
        EditorNetPlaySession&         netSession  = editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>();
        const EditorPlayStateSnapshot play        = EditorPlaySession::getUIState(); // UI 는 넘긴 상태만 읽는다
        const PlaySessionState        playState   = play._state;
        const bool                    bSimulating = play.isSimulating();
        outState._netSettings                     = netSession.getSettings();
        outState._netConnectedClients             = netSession.isRunning() ? netSession.getConnectedClientCount() : 0;
        outState._netProcessCount                 = netSession.getProcessCount();
        outState._bNetRunning                     = netSession.isRunning() ? SW_TRUE : SW_FALSE;
        outState._bNetStopping                    = netSession.isStopping() ? SW_TRUE : SW_FALSE;
        outState._bStopped                        = play.isStopped() ? SW_TRUE : SW_FALSE;
        outState._bPlayQueued                     = play.isPlayQueued() ? SW_TRUE : SW_FALSE;
        outState._bPlaying                        = playState == PlaySessionState::Playing && bSimulating == false ? SW_TRUE : SW_FALSE;
        outState._bSimulating                     = playState == PlaySessionState::Playing && bSimulating ? SW_TRUE : SW_FALSE;
        outState._bPaused                         = playState == PlaySessionState::Paused ? SW_TRUE : SW_FALSE;
        outState._timeScale                       = editor::getSubsystem<GameTimeScale>().getSetting(); // 정지(0)가 아니라 정한 배율 — 정지 메뉴가 뜬 채 끌면 0 부터 적혀 재개해도 멈춘다
    }

    void EditorPlayToolbarViewModel::requestSession( EditorPlaySessionKind session )
    {
        _uiState._pendingSession = session;
        const bool bSceneDirty   = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().isSceneDirty();
        if ( bSceneDirty && EditorPlaySession::getUIState().isStopped() )
            _uiState._bConfirmUnsavedPlay = SW_TRUE;
        else
            startSession( session );
    }

    void EditorPlayToolbarViewModel::startSession( EditorPlaySessionKind session )
    {
        // 카메라에서 시작은 플레이어가 조종하는 세션만 쓴다(Simulate 는 옮길 플레이어가 없다). 멈춤에서 시작할 때만 정하는 것은 적용 쪽이 본다.
        const bool bSimulate = session == EditorPlaySessionKind::Simulate;
        EditorUICommandQueue::postToEditor( EditorUICommand::makeStart( bSimulate, bSimulate == false && _uiState._bStartAtCamera != SW_FALSE ) );
    }

    void EditorPlayToolbarViewModel::pause()
    {
        EditorUICommandQueue::postToEditor( EditorUICommand::make( EditorUICommandKind::Pause ) );
    }

    void EditorPlayToolbarViewModel::stepOnce()
    {
        EditorUICommandQueue::postToEditor( EditorUICommand::makeStep( 1 ) );
    }

    void EditorPlayToolbarViewModel::stepFrames()
    {
        clampStepFrameCount();
        EditorUICommandQueue::postToEditor( EditorUICommand::makeStep( static_cast<uint32>( _uiState._stepFrameCount ) ) );
    }

    void EditorPlayToolbarViewModel::stop()
    {
        EditorUICommandQueue::postToEditor( EditorUICommand::make( EditorUICommandKind::Stop ) );
    }

    void EditorPlayToolbarViewModel::clampStepFrameCount()
    {
        _uiState._stepFrameCount = MathUtil::clamp( _uiState._stepFrameCount, 1, static_cast<int32>( EditorPlaySession::kMaxStepFrameCount ) );
    }

    void EditorPlayToolbarViewModel::setTimeScale( float32 timeScale )
    {
        editor::getSubsystem<GameTimeScale>().set( timeScale );
    }

    void EditorPlayToolbarViewModel::setNetSettings( const EditorNetPlaySettings& settings )
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>().getSettings() = settings;
    }

    void EditorPlayToolbarViewModel::startNetPlay()
    {
        EditorNetPlaySession& session = editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>();
        (void)session.start( session.getSettings(), {} ); // 실패 이유는 세션이 로그에 남긴다
    }

    void EditorPlayToolbarViewModel::stopNetPlay()
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>().requestStop(); // 합친 트레이스는 Profiler 창의 프레임 단계가 Timeline 탭에 연다
    }
} // namespace sw::editor
