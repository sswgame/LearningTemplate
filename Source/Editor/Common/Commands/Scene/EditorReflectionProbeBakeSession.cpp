#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorReflectionProbeBakeSession.h"

#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Scene/EditorBakeOutputPath.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Graphics/Lighting/ReflectionProbeData.h"
#include "Engine/Object/Component/3D/ReflectionProbeComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Renderer/Capture/ReflectionProbeBaker.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorReflectionProbeBake" );

    namespace
    {
        struct EditorReflectionProbeBakeSessionInternal
        {
            /** @brief 굽을 프로브 — 선택한 오브젝트의 프로브, 선택에 프로브가 없으면 씬의 모든 프로브입니다. */
            static void collectProbes( GameObjectManager& manager, vector<ReflectionProbeComponent*>& outListProbe )
            {
                outListProbe.clear();
                vector<GameObject*> listSelected;
                editor::getMainSceneSelection().getSelectedObjects( listSelected );
                for ( GameObject* pObject : listSelected )
                {
                    ReflectionProbeComponent* pProbe = ( pObject != nullptr ) ? pObject->getComponent<ReflectionProbeComponent>() : nullptr;
                    if ( pProbe != nullptr )
                        outListProbe.push_back( pProbe );
                }
                if ( outListProbe.empty() == false )
                    return;
                for ( ReflectionProbeComponent* pProbe : manager.getComponentRegistry().getAll<ReflectionProbeComponent>() )
                {
                    if ( pProbe != nullptr )
                        outListProbe.push_back( pProbe );
                }
            }

            static ReflectionProbeComponent* findProbe( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<ReflectionProbeComponent>() : nullptr;
            }

            /** @brief 베이크한 데이터 하나를 파일로 쓰고 프로브에 넘깁니다. 실패하면 이유를 @p outError 에 적습니다. */
            static bool storeProbe( const Scene& scene, ReflectionProbeComponent& probe, ReflectionProbeData&& data, string& outError )
            {
                const string resourcePath = probe.getOwner() != nullptr ? makeSceneBakeResourcePath( scene, *probe.getOwner(), ReflectionProbeData::kExtension ) : string{};
                if ( resourcePath.empty() )
                {
                    outError = "the scene has never been saved - save it before baking reflection probes";
                    return false;
                }
                if ( data.saveToFile( ResourcePaths::getWritePath( resourcePath ) ) == false )
                {
                    utf8 arrMessage[constant::kMaxBuffer512];
                    formatstring( arrMessage, constant::kMaxBuffer512, "could not write %#", resourcePath.c_str() );
                    outError = arrMessage;
                    return false;
                }
                const uint32 resolution = data._resolution;
                if ( probe.adoptProbeData( std::move( data ), resourcePath ) == false )
                {
                    outError = "the baked reflection does not fit the probe";
                    return false;
                }
                SW_LOG_INFO( "Baked reflection probe '%#' (%#x%# octahedral)", resourcePath.c_str(), resolution, resolution );
                return true;
            }

            static void runFrame( EditorContext& /*context*/ )
            {
                editor::getSubsystem<EditorReflectionProbeBakeSession>().update(); // 필수 - 에디터 서브시스템이다
            }

            [[nodiscard]] static bool readBakeRunCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorReflectionProbeBakeSession& session = editor::getSubsystem<EditorReflectionProbeBakeSession>();
                outValue                                        = static_cast<float64>( session.getFinishedRunCount( editor::getSubsystem<SceneManager>().getSceneGeneration() ) );
                return true;
            }

            [[nodiscard]] static bool readBakeRunning( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = editor::getSubsystem<EditorReflectionProbeBakeSession>().isRunning() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readBakedProbeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                uint32             count{ 0 };
                if ( pManager != nullptr )
                {
                    for ( const ReflectionProbeComponent* pProbe : pManager->getComponentRegistry().getAll<ReflectionProbeComponent>() )
                    {
                        if ( pProbe != nullptr && pProbe->findProbeData() != nullptr )
                            ++count;
                    }
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_SUBSYSTEM( EditorReflectionProbeBakeSession, 9110 );
    SW_EDITOR_FRAME_STEP( ReflectionProbeBake, "reflectionProbeBake", EditorFrameStepOrder::kCommands + 21, &EditorReflectionProbeBakeSessionInternal::runFrame );

    SW_AUTOMATION_PROBE( editorReflectionProbeBakeCount, "Editor.ReflectionProbeBakeCount",
                         "Times a lighting.bakeReflectionProbes run finished (or was canceled) in the active scene", &EditorReflectionProbeBakeSessionInternal::readBakeRunCount );
    SW_AUTOMATION_PROBE( editorReflectionProbeBakeRunning, "Editor.ReflectionProbeBakeRunning", "1 while a reflection probe bake is running over editor frames",
                         &EditorReflectionProbeBakeSessionInternal::readBakeRunning );
    SW_AUTOMATION_PROBE( editorBakedReflectionProbeCount, "Editor.BakedReflectionProbeCount", "Reflection probes of the active scene with usable baked data",
                         &EditorReflectionProbeBakeSessionInternal::readBakedProbeCount );

    EditorReflectionProbeBakeSession::EditorReflectionProbeBakeSession()
        : _listObjectID{}
        , _lastError{}
        , _run{}
        , _nextProbeIndex{ 0 }
        , _bakedCount{ 0 }
        , _bDeferFirstSlice{ false }
    {
    }

    void EditorReflectionProbeBakeSession::start()
    {
        Scene*             pScene       = editor::getActiveScene();
        GameObjectManager* pManager     = editor::getActiveObjectManager();
        SceneManager&      sceneManager = editor::getSubsystem<SceneManager>();
        if ( pScene == nullptr || pManager == nullptr )
        {
            SW_LOG_WARNING( "Reflection probe bake refused: there is no active scene" );
            return;
        }
        if ( _run.isRunning() )
            return; // 이미 도는 중 - 명령이 꺼져 있어야 하는 경우라 조용히 둔다
        vector<ReflectionProbeComponent*> listProbe;
        EditorReflectionProbeBakeSessionInternal::collectProbes( *pManager, listProbe );
        _listObjectID.clear();
        for ( const ReflectionProbeComponent* pProbe : listProbe )
        {
            if ( pProbe->getOwner() != nullptr )
                _listObjectID.push_back( pProbe->getOwner()->getObjectID() );
        }
        _nextProbeIndex = 0;
        _bakedCount     = 0;
        _lastError.clear();
        _bDeferFirstSlice = true;
        _run.begin( sceneManager.getSceneGeneration() );
        if ( _listObjectID.empty() )
            finish( false );
    }

    void EditorReflectionProbeBakeSession::cancel( const utf8* pReason )
    {
        if ( _run.isRunning() == false )
            return;
        _lastError = pReason;
        finish( true );
    }

    void EditorReflectionProbeBakeSession::update()
    {
        if ( _run.isRunning() == false )
            return;
        const utf8* pStopReason = _run.findStopReason();
        if ( pStopReason != nullptr )
        {
            cancel( pStopReason );
            return;
        }
        // 시작한 프레임에는 굽지 않는다 — 프로브 하나가 프레임을 멈추게 하므로, 상태줄이 "0/N" 을 한 번 그린 뒤에 첫 프로브를 굽는다.
        if ( _bDeferFirstSlice )
        {
            _bDeferFirstSlice = false;
            return;
        }
        Scene*          pScene = editor::getActiveScene(); // 멈출 이유가 없으면 활성 씬이 있다
        const Stopwatch stopwatch;
        // 프로브 하나가 예산을 넘길 수 있다 — 첫 프로브는 늘 굽고, 남은 예산이 있을 때만 다음 프로브로 간다.
        while ( _run.isRunning() && stopwatch.getElapsedMilliseconds() < EditorBakeRunState::kFrameBudgetMilliseconds )
        {
            if ( _nextProbeIndex >= _listObjectID.size() )
            {
                finish( false );
                return;
            }
            bakeNextProbe( *pScene );
        }
    }

    void EditorReflectionProbeBakeSession::bakeNextProbe( Scene& scene )
    {
        ReflectionProbeComponent* pProbe = EditorReflectionProbeBakeSessionInternal::findProbe( _listObjectID[_nextProbeIndex] );
        ++_nextProbeIndex;
        if ( pProbe == nullptr )
        {
            reportFailure( "a reflection probe was removed before it was baked" );
            return;
        }
        IRHIDevice* pDevice = editor::getSubsystem<EditorRenderViews>().getRHIDevice(); // 뷰 서브시스템은 필수, 장치는 헤드리스에서 없을 수 있다
        if ( pDevice == nullptr )
        {
            reportFailure( "no graphics device" );
            return;
        }
        // 프로브 하나씩 굽는다 — 굽기마다 렌더 스레드를 비우고 이 스레드가 컨텍스트를 잡았다 놓으므로 프레임 사이에 컨텍스트를 쥐고 있지 않는다.
        // 선택 조회(②): 렌더 스레드 없이 동기로 굽는 구성이 있어 `bakeProbes` 가 nullptr 렌더 스레드를 받는다.
        RenderThreadSubsystem*            pRenderThreadSubsystem = editor::findSubsystem<RenderThreadSubsystem>();
        vector<ReflectionProbeComponent*> listProbe;
        listProbe.push_back( pProbe );
        vector<ReflectionProbeData> listData;
        string                      error;
        const bool                  bBaked = ReflectionProbeBaker::bakeProbes( *pDevice, pRenderThreadSubsystem != nullptr ? pRenderThreadSubsystem->findRenderThread() : nullptr, scene,
                                                                               listProbe, listData, &error );
        if ( bBaked == false )
        {
            reportFailure( error );
            return;
        }
        if ( listData.empty() || listData[0]._listTexel.empty() )
        {
            reportFailure( "the baker produced no reflection data" );
            return;
        }
        if ( EditorReflectionProbeBakeSessionInternal::storeProbe( scene, *pProbe, std::move( listData[0] ), error ) == false )
        {
            reportFailure( error );
            return;
        }
        ++_bakedCount; // 씬 dirty 는 잠금을 푼 뒤 `finish` 가 표시한다(잠긴 동안의 표시는 잠금을 지나친 편집으로 센다)
    }

    void EditorReflectionProbeBakeSession::reportFailure( const string& error )
    {
        SW_LOG_ERROR( "Reflection probe bake failed: %#", error.c_str() );
        _lastError = error;
    }

    void EditorReflectionProbeBakeSession::finish( bool bCanceled )
    {
        _run.end();
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( _bakedCount > 0 )
            editorContext.getSubsystem<EditorWorkspace>().markSceneDirty(); // 프로브의 경로가 바뀌었다 — 저장해야 남는다
        EditorNotificationManager& notification = editorContext.getSubsystem<EditorNotificationManager>();
        const uint32               probeCount   = getProbeCount();
        utf8                       arrMessage[constant::kMaxBuffer512];
        if ( probeCount == 0 )
        {
            notification.push( "Bake Reflection Probes", "Nothing to bake - add a Reflection Probe", NotificationType::Warning );
        }
        else if ( bCanceled || _bakedCount < probeCount )
        {
            formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# of %# probe(s) - %#", _bakedCount, probeCount, _lastError.c_str() );
            notification.push( "Bake Reflection Probes", arrMessage, bCanceled ? NotificationType::Warning : NotificationType::Error );
        }
        else
        {
            formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# probe(s) - save the scene to keep the paths", _bakedCount );
            notification.push( "Bake Reflection Probes", arrMessage, NotificationType::Success );
        }
    }
} // namespace sw::editor
