#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorReflectionProbeBakeSession.h"

#include "Editor/Common/Commands/Scene/EditorBakeSession.h"
#include "Editor/Common/Commands/Scene/ReflectionProbeBaker.h"
#include "Editor/Common/Task/EditorBackgroundTask.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Lighting/ReflectionProbeAsset.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/Component/ReflectionProbeComponent.h"

namespace sw::editor
{
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

            /** @brief "could not write <경로>" 입니다. */
            static string makeWriteError( const string& assetPath )
            {
                utf8 arrMessage[constant::buffer::kMax512];
                formatstring( arrMessage, constant::buffer::kMax512, "could not write %#", assetPath.c_str() );
                return string{ arrMessage };
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

    /** @brief 접기 · 쓰기 잡의 입력입니다. UI 스레드가 만든다 - 워커는 찍은 면(읽기만)과 경로 사본만 본다. */
    struct EditorReflectionProbeFilterInput
    {
        shared_ptr<const ReflectionProbeCapture> _pCapture{};
        optional<AbsolutePath>                   _dataPath{}; ///< 쓸 파일(UI 스레드가 `AssetPaths` 로 정했다)
        string                                   _assetPath{};
        uint64                                   _objectID{ 0 };
    };

    /** @brief 접기 · 쓰기 잡의 결과입니다. */
    struct EditorReflectionProbeFilterResult
    {
        ReflectionProbeAsset _data{};
        string               _error{};
        string               _assetPath{};
        uint64               _objectID{ 0 };
        bool                 _bSucceeded{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @class EditorReflectionProbeFilterJob
     * @brief 찍은 면을 풀고(half → float) 접고 거르고 `.reflprobe` 로 쓰는 에디터 백그라운드 잡입니다. 프로브에 넘기기(컴포넌트)는 결과를 받은 UI 스레드가 한다.
     */
    class EditorReflectionProbeFilterJob final : public EditorBackgroundTask<EditorReflectionProbeFilterInput, EditorReflectionProbeFilterResult>
    {
    public:
        /** @brief 잡을 냅니다(UI 스레드). */
        void request( EditorReflectionProbeFilterInput input )
        {
            const uint32 generation = beginRequest( std::move( input ) );
            submitJob( "EditorReflectionProbeFilter", SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorReflectionProbeFilterJob::runJob ),
                       MakeTaskArgs( _pState, generation ) );
        }

    private:
        static void runJob( const TaskArgs& args )
        {
            const shared_ptr<State>          pState     = args.get<shared_ptr<State>>( 0 );
            const uint32                     generation = args.get<uint32>( 1 );
            EditorReflectionProbeFilterInput input;
            if ( readInput( pState, generation, input ) == false )
                return;
            EditorReflectionProbeFilterResult result;
            result._assetPath         = input._assetPath;
            result._objectID          = input._objectID;
            const expected<void> made = ReflectionProbeBaker::makeProbeData( *input._pCapture, result._data );
            result._bSucceeded        = made.has_value();
            if ( made.has_value() == false )
                result._error = string( made.error().message() );
            if ( result._bSucceeded && result._data._listTexel.empty() )
            {
                result._bSucceeded = false;
                result._error      = "the baker produced no reflection data";
            }
            if ( result._bSucceeded && ( input._dataPath.has_value() == false || result._data.saveToFile( *input._dataPath ) == false ) )
            {
                result._bSucceeded = false;
                result._error      = EditorReflectionProbeBakeSessionInternal::makeWriteError( input._assetPath );
            }
            publish( pState, generation, std::move( result ) );
        }
    };

    SW_EDITOR_SUBSYSTEM( EditorReflectionProbeBakeSession, 9110 );
    SW_EDITOR_FRAME_STEP( ReflectionProbeBake, "reflectionProbeBake", EditorFrameStepOrder::kCommands + 21, &EditorReflectionProbeBakeSessionInternal::runFrame );

    SW_AUTOMATION_WATCH( editorReflectionProbeBakeCount, "Editor.ReflectionProbeBakeCount",
                         "Times a lighting.bakeReflectionProbes run finished (or was canceled) in the active scene", &EditorReflectionProbeBakeSessionInternal::readBakeRunCount );
    SW_AUTOMATION_WATCH( editorReflectionProbeBakeRunning, "Editor.ReflectionProbeBakeRunning", "1 while a reflection probe bake is running over editor frames",
                         &EditorReflectionProbeBakeSessionInternal::readBakeRunning );
    SW_AUTOMATION_WATCH( editorBakedReflectionProbeCount, "Editor.BakedReflectionProbeCount", "Reflection probes of the active scene with usable baked data",
                         &EditorReflectionProbeBakeSessionInternal::readBakedProbeCount );

    EditorReflectionProbeBakeSession::EditorReflectionProbeBakeSession()
        : _listObjectID{}
        , _lastError{}
        , _pFilterJob{ make_unique<EditorReflectionProbeFilterJob>() }
        , _run{}
        , _nextProbeIndex{ 0 }
        , _bakedCount{ 0 }
        , _bDeferFirstSlice{ SW_FALSE }
    {
    }

    EditorReflectionProbeBakeSession::~EditorReflectionProbeBakeSession() = default; // 잡의 상태는 shared_ptr 라 도는 워커가 있어도 이 객체보다 오래 산다

    void EditorReflectionProbeBakeSession::start()
    {
        Scene*             pScene       = editor::getActiveScene();
        GameObjectManager* pManager     = editor::getActiveObjectManager();
        SceneManager&      sceneManager = editor::getSubsystem<SceneManager>();
        if ( pScene == nullptr || pManager == nullptr )
        {
            SW_LOG_WARNING_ID( "EditorReflectionProbeBake.ReflectionProbeBakeRefused", "Reflection probe bake refused: there is no active scene" );
            return;
        }
        if ( _run.isRunning() )
            return; // 이미 도는 중 - 명령이 꺼져 있어야 하는 경우라 조용히 둔다
        // 취소한 베이크의 접기 · 쓰기가 아직 돈다 - 같은 파일을 겹쳐 쓰지 않게 끝난 뒤에 받는다.
        if ( _pFilterJob->isPending() )
        {
            SW_LOG_WARNING_ID( "EditorReflectionProbeBake.ReflectionProbeBakeRefused2", "Reflection probe bake refused: the previous bake is still writing a probe - try again in a moment" );
            return;
        }
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
        _bDeferFirstSlice = SW_TRUE;
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
        // 시작한 프레임에는 굽지 않는다 — 찍기가 프레임을 멈추게 하므로, 상태줄이 "0/N" 을 한 번 그린 뒤에 첫 프로브를 찍는다.
        if ( _bDeferFirstSlice )
        {
            _bDeferFirstSlice = SW_FALSE;
            return;
        }
        // 앞 프로브의 접기 · 쓰기(워커)가 끝났으면 넘기고, 아직이면 이 프레임은 기다린다 - 잡은 한 번에 하나다.
        EditorReflectionProbeFilterResult result;
        const EditorTaskPoll              poll = _pFilterJob->take( result );
        if ( poll == EditorTaskPoll::Running )
            return;
        if ( poll == EditorTaskPoll::Ready )
            applyResult( result );
        if ( _nextProbeIndex >= _listObjectID.size() )
        {
            finish( false );
            return;
        }
        // 찍기(GPU)는 프레임마다 하나다. 그 프로브의 접기는 다음 프레임들에 워커가 한다.
        captureNextProbe( *editor::getActiveScene() ); // 멈출 이유가 없으면 활성 씬이 있다
    }

    void EditorReflectionProbeBakeSession::applyResult( EditorReflectionProbeFilterResult& result )
    {
        if ( result._bSucceeded == false )
        {
            reportFailure( result._error );
            return;
        }
        ReflectionProbeComponent* pProbe = EditorReflectionProbeBakeSessionInternal::findProbe( result._objectID );
        if ( pProbe == nullptr )
        {
            reportFailure( "a reflection probe was removed before its bake finished" );
            return;
        }
        const uint32 resolution = result._data._resolution;
        if ( pProbe->applyProbeData( std::move( result._data ), result._assetPath ) == false )
        {
            reportFailure( "the baked reflection does not fit the probe" );
            return;
        }
        SW_LOG_INFO_ID( "EditorReflectionProbeBake.BakedReflectionProbeX", "Baked reflection probe '%#' (%#x%# octahedral)", result._assetPath.c_str(), resolution, resolution );
        ++_bakedCount; // 씬 dirty 는 잠금을 푼 뒤 `finish` 가 표시한다(잠긴 동안의 표시는 잠금을 지나친 편집으로 센다)
    }

    void EditorReflectionProbeBakeSession::captureNextProbe( Scene& scene )
    {
        const uint64              objectID = _listObjectID[_nextProbeIndex];
        ReflectionProbeComponent* pProbe   = EditorReflectionProbeBakeSessionInternal::findProbe( objectID );
        ++_nextProbeIndex;
        if ( pProbe == nullptr )
        {
            reportFailure( "a reflection probe was removed before it was baked" );
            return;
        }
        // 찍기는 렌더 스레드가 명령으로 한다 — 장치가 붙은 렌더 스레드가 없으면(헤드리스) 찍지 않는다.
        RenderThreadSubsystem* pRenderThreadSubsystem = editor::findSubsystem<RenderThreadSubsystem>();
        RenderThread*          pRenderThread          = pRenderThreadSubsystem != nullptr ? pRenderThreadSubsystem->findRenderThread() : nullptr;
        if ( pRenderThread == nullptr || pRenderThread->getDevice() == nullptr )
        {
            reportFailure( "no render thread with a graphics device" );
            return;
        }
        // 쓸 경로는 찍기 전에 정한다 - 씬 · 서비스는 이 스레드에서만 본다(워커는 경로 사본을 받는다). 저장 안 한 씬이면 찍지 않는다.
        const string& sceneAssetID = scene.getSourcePath(); // 씬의 소스는 늘 리소스 id 다(`SceneManager::saveActiveSceneAs`)
        if ( sceneAssetID.empty() )
        {
            reportFailure( "the scene has never been saved - save it before baking reflection probes" );
            return;
        }
        const string assetPath = pProbe->getOwner() != nullptr ? makeSceneBakeAssetPath( sceneAssetID, pProbe->getOwner()->getName().view(), ReflectionProbeAsset::kExtension ) : string{};
        if ( assetPath.empty() )
        {
            reportFailure( "no bake output for scene '" + sceneAssetID + "' - it must be a .scene.xml resource and the probe needs a named owner" );
            return;
        }
        optional<AbsolutePath> dataPath = editor::getSubsystem<AssetPaths>().toDiskPath( assetPath );
        if ( dataPath.has_value() == false )
        {
            reportFailure( EditorReflectionProbeBakeSessionInternal::makeWriteError( assetPath ) );
            return;
        }
        // 프로브 하나씩 찍는다 — 씬 → 패킷은 이 스레드가, 그리기 · 읽기는 렌더 스레드가 명령으로 한다(`ReflectionProbeBaker`).
        vector<ReflectionProbeComponent*> listProbe;
        listProbe.push_back( pProbe );
        vector<ReflectionProbeCapture> listCapture;
        const expected<void>           captured = ReflectionProbeBaker::captureProbes( *pRenderThread, scene, listProbe, listCapture );
        if ( captured.has_value() == false || listCapture.empty() )
        {
            reportFailure( captured.has_value() ? string( "the baker captured nothing" ) : string( captured.error().message() ) );
            return;
        }
        EditorReflectionProbeFilterInput input;
        input._pCapture  = make_shared<const ReflectionProbeCapture>( std::move( listCapture[0] ) );
        input._dataPath  = std::move( dataPath );
        input._assetPath = assetPath;
        input._objectID  = objectID;
        _pFilterJob->request( std::move( input ) );
    }

    void EditorReflectionProbeBakeSession::reportFailure( const string& error )
    {
        SW_LOG_ERROR_ID( "EditorReflectionProbeBake.ReflectionProbeBakeFailed", "Reflection probe bake failed: %#", error.c_str() );
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
        utf8                       arrMessage[constant::buffer::kMax512];
        if ( probeCount == 0 )
        {
            notification.push( "Bake Reflection Probes", "Nothing to bake - add a Reflection Probe", NotificationKind::Warning );
        }
        else if ( bCanceled || _bakedCount < probeCount )
        {
            formatstring( arrMessage, constant::buffer::kMax512, "Baked %# of %# probe(s) - %#", _bakedCount, probeCount, _lastError.c_str() );
            notification.push( "Bake Reflection Probes", arrMessage, bCanceled ? NotificationKind::Warning : NotificationKind::Error );
        }
        else
        {
            formatstring( arrMessage, constant::buffer::kMax512, "Baked %# probe(s) - save the scene to keep the paths", _bakedCount );
            notification.push( "Bake Reflection Probes", arrMessage, NotificationKind::Success );
        }
    }
} // namespace sw::editor
