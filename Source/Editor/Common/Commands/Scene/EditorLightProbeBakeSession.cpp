#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorLightProbeBakeSession.h"

#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Scene/EditorBakeOutputPath.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Graphics/Lighting/LightProbeGrid.h"
#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorLightProbeBake" );

    namespace
    {
        struct EditorLightProbeBakeSessionInternal
        {
            /** @brief 굽을 볼륨 — 선택한 오브젝트의 볼륨, 선택에 볼륨이 없으면 씬의 모든 볼륨입니다. */
            static void collectVolumes( GameObjectManager& manager, vector<LightProbeVolumeComponent*>& outListVolume )
            {
                outListVolume.clear();
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                {
                    vector<GameObject*> listSelected;
                    editor::getMainSceneSelection().getSelectedObjects( listSelected );
                    for ( GameObject* pObject : listSelected )
                    {
                        LightProbeVolumeComponent* pVolume = ( pObject != nullptr ) ? pObject->getComponent<LightProbeVolumeComponent>() : nullptr;
                        if ( pVolume != nullptr )
                            outListVolume.push_back( pVolume );
                    }
                }
                if ( outListVolume.empty() == false )
                    return;
                for ( LightProbeVolumeComponent* pVolume : manager.getComponentRegistry().getAll<LightProbeVolumeComponent>() )
                {
                    if ( pVolume != nullptr )
                        outListVolume.push_back( pVolume );
                }
            }

            static LightProbeVolumeComponent* findVolume( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<LightProbeVolumeComponent>() : nullptr;
            }

            static void runFrame( EditorContext& /*context*/ )
            {
                EditorLightProbeBakeSession* pSession = editor::findSubsystem<EditorLightProbeBakeSession>();
                if ( pSession != nullptr )
                    pSession->update();
            }

            [[nodiscard]] static bool readBakeRunCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorLightProbeBakeSession* pSession      = editor::findSubsystem<EditorLightProbeBakeSession>();
                const SceneManager*                pSceneManager = editor::findSubsystem<SceneManager>();
                const bool                         bReadable     = pSession != nullptr && pSceneManager != nullptr;
                outValue                                         = static_cast<float64>( bReadable ? pSession->getFinishedRunCount( pSceneManager->getSceneGeneration() ) : 0u );
                return true;
            }

            [[nodiscard]] static bool readBakeRunning( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorLightProbeBakeSession* pSession = editor::findSubsystem<EditorLightProbeBakeSession>();
                outValue                                    = ( pSession != nullptr && pSession->isRunning() ) ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readBakedVolumeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                uint32             count{ 0 };
                if ( pManager != nullptr )
                {
                    for ( const LightProbeVolumeComponent* pVolume : pManager->getComponentRegistry().getAll<LightProbeVolumeComponent>() )
                    {
                        if ( pVolume != nullptr && pVolume->findProbeData() != nullptr )
                            ++count;
                    }
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_SUBSYSTEM( EditorLightProbeBakeSession, 9100 );
    SW_EDITOR_FRAME_STEP( LightProbeBake, "lightProbeBake", EditorFrameStepOrder::kCommands + 20, &EditorLightProbeBakeSessionInternal::runFrame );

    SW_AUTOMATION_PROBE( editorLightProbeBakeCount, "Editor.LightProbeBakeCount", "Times a lighting.bakeProbes run finished (or was canceled) in the active scene",
                         &EditorLightProbeBakeSessionInternal::readBakeRunCount );
    SW_AUTOMATION_PROBE( editorLightProbeBakeRunning, "Editor.LightProbeBakeRunning", "1 while a light probe bake is running over editor frames",
                         &EditorLightProbeBakeSessionInternal::readBakeRunning );
    SW_AUTOMATION_PROBE( editorBakedLightProbeVolumeCount, "Editor.BakedLightProbeVolumeCount", "Light probe volumes of the active scene with usable baked data",
                         &EditorLightProbeBakeSessionInternal::readBakedVolumeCount );

    EditorLightProbeBakeSession::EditorLightProbeBakeSession()
        : _listObjectID{}
        , _pOccluders{}
        , _job{}
        , _lastError{}
        , _sceneGeneration{ 0 }
        , _heldEditCount{ 0 }
        , _nextVolumeIndex{ 0 }
        , _bakedCount{ 0 }
        , _finishedRunSceneGeneration{ 0 }
        , _finishedRunCount{ 0 }
        , _bRunning{ false }
        , _bJobActive{ false }
    {
    }

    EditorLightProbeBakeSession::~EditorLightProbeBakeSession()
    {
        if ( _bRunning )
            EditorPlaySession::releaseSceneEdits();
    }

    void EditorLightProbeBakeSession::start()
    {
        Scene*             pScene        = editor::getActiveScene();
        GameObjectManager* pManager      = editor::getActiveObjectManager();
        SceneManager*      pSceneManager = editor::findSubsystem<SceneManager>();
        const bool         bNoScene      = pScene == nullptr || pManager == nullptr || pSceneManager == nullptr;
        if ( bNoScene || _bRunning )
            return;
        vector<LightProbeVolumeComponent*> listVolume;
        EditorLightProbeBakeSessionInternal::collectVolumes( *pManager, listVolume );
        _listObjectID.clear();
        for ( const LightProbeVolumeComponent* pVolume : listVolume )
        {
            if ( pVolume->getOwner() != nullptr )
                _listObjectID.push_back( pVolume->getOwner()->getObjectID() );
        }
        _sceneGeneration = pSceneManager->getSceneGeneration();
        _heldEditCount   = EditorPlaySession::getHeldSceneEditCount();
        _nextVolumeIndex = 0;
        _bakedCount      = 0;
        _lastError.clear();
        _bJobActive = false;
        _bRunning   = true;
        EditorPlaySession::holdSceneEdits();
        if ( _listObjectID.empty() )
            finish( false );
    }

    void EditorLightProbeBakeSession::cancel( const utf8* pReason )
    {
        if ( _bRunning == false )
            return;
        _lastError = pReason;
        finish( true );
    }

    void EditorLightProbeBakeSession::update()
    {
        if ( _bRunning == false )
            return;
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        Scene*        pScene        = editor::getActiveScene();
        const bool    bSceneGone    = pSceneManager == nullptr || pScene == nullptr || pSceneManager->getSceneGeneration() != _sceneGeneration;
        if ( bSceneGone )
        {
            cancel( "the active scene changed during the bake" );
            return;
        }
        if ( EditorPlaySession::isStopped() == false )
        {
            cancel( "play started during the bake" );
            return;
        }
        if ( EditorPlaySession::getHeldSceneEditCount() != _heldEditCount )
        {
            cancel( "the scene was edited during the bake" );
            return;
        }
        const Stopwatch stopwatch;
        while ( _bRunning && stopwatch.getElapsedMilliseconds() < kFrameBudgetMilliseconds )
        {
            if ( _bJobActive == false && beginNextVolume( *pScene ) == false )
                continue; // 다음 볼륨으로(이유는 `_lastError`) — 끝이면 `finish` 가 `_bRunning` 을 내린다
            if ( _job.step( 1 ) )
                completeVolume( *pScene );
        }
    }

    float32 EditorLightProbeBakeSession::getVolumeProgress() const
    {
        return _job.getProbeCount() > 0 ? static_cast<float32>( _job.getBakedProbeCount() ) / static_cast<float32>( _job.getProbeCount() ) : 0.0f;
    }

    bool EditorLightProbeBakeSession::beginNextVolume( const Scene& scene )
    {
        if ( _nextVolumeIndex >= _listObjectID.size() )
        {
            finish( false );
            return false;
        }
        LightProbeVolumeComponent* pVolume = EditorLightProbeBakeSessionInternal::findVolume( _listObjectID[_nextVolumeIndex] );
        ++_nextVolumeIndex;
        if ( pVolume == nullptr )
        {
            reportFailure( "a light probe volume was removed before it was baked" );
            return false;
        }
        LightProbeBakeInput input{};
        string              error;
        const bool          bStarted = LightProbeBaker::collectInput( scene, *pVolume, _pOccluders, input, &error ) && _job.begin( pVolume->computeLayout(), input, &error );
        if ( bStarted == false )
        {
            reportFailure( error );
            return false;
        }
        _bJobActive = true;
        return true;
    }

    void EditorLightProbeBakeSession::completeVolume( const Scene& scene )
    {
        _bJobActive                        = false;
        LightProbeVolumeComponent* pVolume = EditorLightProbeBakeSessionInternal::findVolume( _listObjectID[getVolumeIndex()] );
        LightProbeGrid             grid{};
        if ( pVolume == nullptr || _job.takeGrid( grid ) == false )
        {
            reportFailure( "a light probe volume was removed while it was baked" );
            return;
        }
        // 출력 경로 규칙(`<씬>.<오브젝트 이름>.probes`)은 반사 프로브 베이크와 같은 함수 하나다.
        const string resourcePath = pVolume->getOwner() != nullptr ? makeSceneBakeResourcePath( scene, *pVolume->getOwner(), LightProbeGrid::kExtension ) : string{};
        if ( resourcePath.empty() )
        {
            reportFailure( "the scene has never been saved - save it before baking light probes" );
            return;
        }
        if ( grid.saveToFile( ResourcePaths::getWritePath( resourcePath ) ) == false )
        {
            reportFailure( "could not write " + resourcePath );
            return;
        }
        const uint32 probeCount = static_cast<uint32>( grid._listProbe.size() );
        if ( pVolume->adoptProbeData( std::move( grid ), resourcePath ) == false )
        {
            reportFailure( "the baked grid does not fit the volume" );
            return;
        }
        SW_LOG_INFO( "Baked light probes '%#' (%# probes)", resourcePath.c_str(), probeCount );
        ++_bakedCount; // 씬 dirty 는 잠금을 푼 뒤 `finish` 가 표시한다(잠긴 동안의 표시는 잠금을 지나친 편집으로 센다)
    }

    void EditorLightProbeBakeSession::reportFailure( const string& error )
    {
        SW_LOG_ERROR( "Light probe bake failed: %#", error.c_str() );
        _lastError = error;
    }

    void EditorLightProbeBakeSession::finish( bool bCanceled )
    {
        _bRunning   = false;
        _bJobActive = false;
        _job        = LightProbeBakeJob{};
        _pOccluders.reset(); // 작업이 빌리던 가림 씬 — 작업을 비운 뒤에 놓는다
        if ( _finishedRunSceneGeneration != _sceneGeneration )
        {
            _finishedRunSceneGeneration = _sceneGeneration;
            _finishedRunCount           = 0;
        }
        ++_finishedRunCount;
        EditorPlaySession::releaseSceneEdits();
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        if ( _bakedCount > 0 )
            pContext->getSubsystem<EditorWorkspace>().markSceneDirty(); // 볼륨의 경로가 바뀌었다 — 저장해야 남는다
        EditorNotificationManager& notification = pContext->getSubsystem<EditorNotificationManager>();
        const uint32               volumeCount  = getVolumeCount();
        utf8                       arrMessage[constant::kMaxBuffer512];
        if ( volumeCount == 0 )
        {
            notification.push( "Bake Light Probes", "Nothing to bake - add a Light Probe Volume", NotificationType::Warning );
        }
        else if ( bCanceled || _bakedCount < volumeCount )
        {
            formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# of %# volume(s) - %#", _bakedCount, volumeCount, _lastError.c_str() );
            notification.push( "Bake Light Probes", arrMessage, bCanceled ? NotificationType::Warning : NotificationType::Error );
        }
        else
        {
            formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# volume(s) - save the scene to keep the paths", _bakedCount );
            notification.push( "Bake Light Probes", arrMessage, NotificationType::Success );
        }
    }
} // namespace sw::editor
