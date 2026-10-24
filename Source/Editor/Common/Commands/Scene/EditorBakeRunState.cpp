#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorBakeRunState.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    EditorBakeRunState::EditorBakeRunState()
        : _sceneGeneration{ 0 }
        , _finishedRunSceneGeneration{ 0 }
        , _heldEditCount{ 0 }
        , _finishedRunCount{ 0 }
        , _bRunning{ false }
    {
    }

    EditorBakeRunState::~EditorBakeRunState()
    {
        if ( _bRunning )
            EditorPlaySession::releaseSceneEdits();
    }

    void EditorBakeRunState::begin( uint64 sceneGeneration )
    {
        _sceneGeneration = sceneGeneration;
        _heldEditCount   = EditorPlaySession::getHeldSceneEditCount();
        _bRunning        = true;
        EditorPlaySession::holdSceneEdits();
    }

    void EditorBakeRunState::end()
    {
        _bRunning = false;
        if ( _finishedRunSceneGeneration != _sceneGeneration )
        {
            _finishedRunSceneGeneration = _sceneGeneration;
            _finishedRunCount           = 0;
        }
        ++_finishedRunCount;
        EditorPlaySession::releaseSceneEdits();
    }

    const utf8* EditorBakeRunState::findStopReason() const
    {
        const SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        const Scene*        pScene        = editor::getActiveScene();
        const bool          bSceneGone    = pSceneManager == nullptr || pScene == nullptr || pSceneManager->getSceneGeneration() != _sceneGeneration;
        if ( bSceneGone )
            return "the active scene changed during the bake";
        if ( EditorPlaySession::isStopped() == false )
            return "play started during the bake";
        if ( EditorPlaySession::getHeldSceneEditCount() != _heldEditCount )
            return "the scene was edited during the bake";
        return nullptr;
    }
} // namespace sw::editor
