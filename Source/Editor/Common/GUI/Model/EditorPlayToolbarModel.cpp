#include "pch.h"

#include "Editor/Common/GUI/Model/EditorPlayToolbarModel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw::editor
{
    EditorPlayToolbarModel& EditorPlayToolbarModel::get()
    {
        return getEditorState<EditorPlayToolbarModel>();
    }

    void EditorPlayToolbarModel::updateNetPlay()
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>().update();
    }

    void EditorPlayToolbarModel::gather( EditorPlayToolbarViewState& outState ) const
    {
        EditorNetPlaySession&  netSession  = editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>();
        const PlaySessionState playState   = EditorPlaySession::getState();
        const bool             bSimulating = EditorPlaySession::isSimulating();
        outState._netSettings              = netSession.getSettings();
        outState._netConnectedClients      = netSession.isRunning() ? netSession.getConnectedClientCount() : 0;
        outState._netProcessCount          = netSession.getProcessCount();
        outState._bNetRunning              = netSession.isRunning() ? SW_TRUE : SW_FALSE;
        outState._bNetStopping             = netSession.isStopping() ? SW_TRUE : SW_FALSE;
        outState._bStopped                 = EditorPlaySession::isStopped() ? SW_TRUE : SW_FALSE;
        outState._bPlayQueued              = EditorPlaySession::isPlayQueued() ? SW_TRUE : SW_FALSE;
        outState._bPlaying                 = playState == PlaySessionState::Playing && bSimulating == false ? SW_TRUE : SW_FALSE;
        outState._bSimulating              = playState == PlaySessionState::Playing && bSimulating ? SW_TRUE : SW_FALSE;
        outState._bPaused                  = playState == PlaySessionState::Paused ? SW_TRUE : SW_FALSE;
        outState._timeScale                = editor::getSubsystem<GameTimeScale>().get();
    }

    void EditorPlayToolbarModel::requestSession( EditorPlaySessionKind session )
    {
        _uiState._pendingSession = session;
        const bool bSceneDirty   = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().isSceneDirty();
        if ( bSceneDirty && EditorPlaySession::isStopped() )
            _uiState._bConfirmUnsavedPlay = SW_TRUE;
        else
            startSession( session );
    }

    void EditorPlayToolbarModel::startSession( EditorPlaySessionKind session )
    {
        // 카메라에서 시작은 플레이어가 조종하는 세션만 쓴다(Simulate 는 옮길 플레이어가 없다). 멈춤에서 시작할 때만 정한다.
        PlaySessionData* pData = EditorPlaySession::findData();
        if ( pData != nullptr && EditorPlaySession::isStopped() )
        {
            const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
            if ( _uiState._bStartAtCamera != SW_FALSE && pEditorCamera != nullptr )
                EditorPlaySession::setStartPosition( *pData, pEditorCamera->getWorldPosition() );
            else
                EditorPlaySession::clearStartPosition( *pData );
        }
        if ( session == EditorPlaySessionKind::Simulate )
            EditorPlaySession::simulate();
        else
            EditorPlaySession::play();
    }

    void EditorPlayToolbarModel::pause()
    {
        EditorPlaySession::pause();
    }

    void EditorPlayToolbarModel::stepOnce()
    {
        EditorPlaySession::stepOnce();
    }

    void EditorPlayToolbarModel::stepFrames()
    {
        clampStepFrameCount();
        EditorPlaySession::stepFrames( static_cast<uint32>( _uiState._stepFrameCount ) );
    }

    void EditorPlayToolbarModel::stop()
    {
        EditorPlaySession::stop();
        GameObjectManager* pObjectManager = editor::getActiveObjectManager();
        if ( pObjectManager != nullptr )
            editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().remapSelectionByObjectName( pObjectManager );
    }

    void EditorPlayToolbarModel::clampStepFrameCount()
    {
        _uiState._stepFrameCount = MathUtil::clamp( _uiState._stepFrameCount, 1, static_cast<int32>( EditorPlaySession::kMaxStepFrameCount ) );
    }

    void EditorPlayToolbarModel::setTimeScale( float32 timeScale )
    {
        editor::getSubsystem<GameTimeScale>().set( timeScale );
    }

    void EditorPlayToolbarModel::setNetSettings( const EditorNetPlaySettings& settings )
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>().getSettings() = settings;
    }

    void EditorPlayToolbarModel::startNetPlay()
    {
        EditorNetPlaySession& session = editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>();
        (void)session.start( session.getSettings(), {} ); // 실패 이유는 세션이 로그에 남긴다
    }

    void EditorPlayToolbarModel::stopNetPlay()
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>().requestStop(); // 합친 트레이스는 Profiler 창의 프레임 단계가 Timeline 탭에 연다
    }
} // namespace sw::editor
