#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorBakeSession.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    EditorBakeRunState::EditorBakeRunState()
        : _sceneGeneration{ 0 }
        , _finishedRunSceneGeneration{ 0 }
        , _heldEditCount{ 0 }
        , _finishedRunCount{ 0 }
        , _hold{}
        , _bRunning{ SW_FALSE }
    {
    }

    EditorBakeRunState::~EditorBakeRunState() = default; // 돌던 중이면 `_hold` 가 사라지며 막기를 푼다

    void EditorBakeRunState::begin( EditorPlaySessionWriteKey key, uint64 sceneGeneration )
    {
        _sceneGeneration = sceneGeneration;
        _heldEditCount   = EditorPlaySession::getHeldSceneEditCount();
        _bRunning        = SW_TRUE;
        _hold            = EditorPlaySession::holdSceneEdits( key );
    }

    void EditorBakeRunState::end()
    {
        _bRunning = SW_FALSE;
        if ( _finishedRunSceneGeneration != _sceneGeneration )
        {
            _finishedRunSceneGeneration = _sceneGeneration;
            _finishedRunCount           = 0;
        }
        ++_finishedRunCount;
        _hold.release();
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

    namespace
    {
        struct EditorBakeSessionInternal
        {
            /// @brief 씬 파일 이름의 끝 — 이것을 떼고 오브젝트 이름을 붙인다.
            static constexpr string_view kSceneSuffix = ".scene.xml";
        };
    } // namespace

    string makeSceneBakeAssetPath( string_view sceneAssetID, string_view ownerName, string_view extension )
    {
        if ( ownerName.empty() || StringUtil::endsWith( sceneAssetID, EditorBakeSessionInternal::kSceneSuffix ) == false )
            return {};
        string path{ sceneAssetID.substr( 0, sceneAssetID.size() - EditorBakeSessionInternal::kSceneSuffix.size() ) };
        string name = StringUtil::toLower( ownerName );
        for ( utf8& character : name )
        {
            const bool bAllowed = ( 'a' <= character && character <= 'z' ) || ( '0' <= character && character <= '9' );
            if ( bAllowed == false )
                character = '_';
        }
        path += '.';
        path += name;
        path += extension;
        return path;
    }
} // namespace sw::editor
