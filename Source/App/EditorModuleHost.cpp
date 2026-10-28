#include "pch.h"

#include "App/EditorModuleHost.h"

#include "Core/Memory/Memory.h"

#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"

#include "ModuleHost/LiveReloadManager.h"
#include "ModuleHost/ModuleInstanceUtil.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct EditorModuleHostInternal
        {
            static constexpr ModuleAPISymbols kEditorSymbols{ "getEditorModuleAbiVersion", "getEditorModuleAbiStamp", "exportEditorAPI", "Editor" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "EditorModuleHost" );

    EditorModuleHost::EditorModuleHost()
        : ModuleHost{}
        , _editorAPI{}
        , _editor{ nullptr }
        , _editorServices{ nullptr }
        , _listEditorExtension{}
        , _bEnableEditor{ SW_FALSE }
        , _bEditorModuleActive{ SW_TRUE }
        , _bFreshEditorInstance{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorModuleHost::~EditorModuleHost()
    {
        // 공통 호스트의 소멸자 안에서는 가상 함수가 이 클래스로 오지 않는다. 그래서 에디터를 여기서 먼저 종료한다.
        shutdown();
    }

    bool EditorModuleHost::loadModuleImages( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution )
    {
        _bEditorModuleActive = resolution.isActive( config::kTargetEditorModule ) ? SW_TRUE : SW_FALSE;
        // 에디터 확장은 에디터를 켤 때만 로드한다(initialize 의 에디터 블록). 여기서는 이름과 의존만 적어 둔다. 카탈로그는 initialize 에 없다.
        _listEditorExtension.clear();
        for ( const string& moduleName : resolution._listLoadOrder )
        {
            const ModuleManifest* pManifest = catalog.findManifest( moduleName );
            if ( pManifest == nullptr || pManifest->_kind != ModuleKind::EditorExtension )
                continue;
            EditorExtensionModule extension{};
            extension._name = moduleName;
            for ( const ModuleDependency& dependency : pManifest->_listDependency )
            {
                extension._listDependency.push_back( dependency._name );
            }
            _listEditorExtension.push_back( std::move( extension ) );
        }
        return ModuleHost::loadModuleImages( pLiveReloadManager, catalog, resolution );
    }

    bool EditorModuleHost::initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor )
    {
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        // 게임이 먼저, 에디터가 나중이다. 게임은 처음 여는 씬을 요청하고(`GameInstanceBase::requestFirstScene`) 에디터는 제 시작 씬
        // (`-gv_editorStartupScene`)을 요청한다. 씬 매니저는 마지막 요청을 남기므로 나중에 요청한 에디터의 씬이 열린다(`GameSettings::_startMap` 주석).
        if ( initializeGame( pLiveReloadManager, pRHI, pWindow, pRenderThread ) == false )
            return false;

#if !defined( SW_SHIPPING )
        if ( _bEnableEditor == SW_TRUE && _bEditorModuleActive == SW_FALSE )
        {
            // -EnableEditor 를 줬는데 매니페스트가 에디터 모듈을 껐다(프로젝트나 구성 때문이다). 에디터 없이 조용히 시작하지 않는다.
            SW_LOG_ERROR( "-EnableEditor was given, but the module manifests turn %# off for this project", config::kTargetEditorModule );
            return false;
        }

        if ( _bEnableEditor == SW_TRUE && pLiveReloadManager != nullptr )
        {
            BLOCK( "에디터: 뷰포트 / EditorModule 등록" )
            {
                SW_MEMORY_SCOPE( Editor );
                holdReloadSubscription( pLiveReloadManager->setOnBeforeReload(
                    config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &EditorModuleHost::onBeforeEditorReload, this ) ) );
                holdReloadSubscription( pLiveReloadManager->setOnAfterReload(
                    config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &EditorModuleHost::onAfterEditorReload, this ) ) );
                holdReloadSubscription( pLiveReloadManager->setOnReloadFault(
                    config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &EditorModuleHost::onEditorReloadFault, this ) ) );
                holdReloadSubscription( pLiveReloadManager->setOnValidateImage(
                    config::kTargetEditorModule, SW_DELEGATE_METHOD( LiveReloadManager::OnValidateImageDelegate, &EditorModuleHost::isEditorImageUsable, this ) ) );
                if ( pLiveReloadManager->registerModule( config::kTargetEditorModule ) == false )
                {
                    SW_LOG_ERROR( "Failed to load the Editor Module." );
                    return false;
                }

                registerEditorExtensions( pLiveReloadManager );

                if ( pLiveReloadManager->isGraphBroken() )
                {
                    SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
                    return false;
                }
            }
        }
#endif

        return true;
    }

    void EditorModuleHost::registerEditorExtensions( LiveReloadManager* pLiveReloadManager )
    {
#if defined( SW_SHIPPING )
        (void)pLiveReloadManager; // 배포본에는 에디터 · 확장이 없다
#else
        // 확장 모듈은 EditorModule 과 자기 키트에 의존한다. 의존이 바뀌면 리로드 그래프가 확장을 다시 로드한다(키트와 SWGame 의 관계와 같다).
        // 인스턴스는 없다: 정적 등록자가 EditorModule 의 등록 목록에 오르고, 에디터 매니저가 다음 프레임에 맞춘다(EditorContext::syncExtensionRegistrations).
        for ( const EditorExtensionModule& extension : _listEditorExtension )
        {
            if ( pLiveReloadManager->registerModule( extension._name, extension._listDependency ) == false )
            {
                // 언리얼도 에디터 모듈 하나가 실패하면 그 플러그인만 끈다. 에디터는 시작한다.
                SW_LOG_ERROR( "Editor extension module %# could not be loaded - the editor runs without it", extension._name.c_str() );
                continue;
            }
            SW_LOG_INFO( "Editor extension module %# loaded", extension._name.c_str() );
        }
#endif
    }

    // ======================================================================
    // 프레임 단위 처리
    // ======================================================================

    bool EditorModuleHost::queryGameplayActive() const
    {
        if ( hasEditor() == false || _editorAPI.isPlaying == nullptr )
            return true;
        return _editorAPI.isPlaying( _editor );
    }

    bool EditorModuleHost::queryTickScene() const
    {
        if ( hasEditor() == false )
            return true;
        if ( _editorAPI.isPaused != nullptr && _editorAPI.isPaused( _editor ) )
        {
            if ( _editorAPI.isPlaying == nullptr || _editorAPI.isPlaying( _editor ) == false )
                return false;
        }
        return true;
    }

    void EditorModuleHost::sampleViewTargets()
    {
        ModuleFrameState& frameState = getMutableFrameState();
        frameState._views            = HostViewTargets{};
        if ( hasEditor() == false )
            return;

        if ( _editorAPI.getGameViewport != nullptr )
            _editorAPI.getGameViewport( _editor, &frameState._views._game );
        if ( _editorAPI.getSceneViewport != nullptr )
            _editorAPI.getSceneViewport( _editor, &frameState._views._scene );
        // 툴 창의 미리보기 씬 — 보이는 창만 온다(`Engine::update` 이 씬마다 따로 그린다).
        if ( _editorAPI.getSceneViews != nullptr )
            frameState._views._sceneViewCount = _editorAPI.getSceneViews( _editor, frameState._views._arrSceneView, kMaxHostSceneView );
    }

    void EditorModuleHost::updateEditorUI( float32 /*deltaTime*/ )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false )
            return;

        // 평소의 UI 프레임은 지난 UI 스냅샷이 그려지기를 기다리며(`waitForDrawSnapshotIdle`) 렌더 스레드가 그 프레임의 게임 패킷까지 다 기록한 뒤에 돈다.
        // 새 인스턴스(리로드 · RHI 교체)는 기다릴 스냅샷이 없어, 리로드 프레임에 낸 게임 패킷을 렌더 스레드가 병렬로 기록하는 동안 뷰 RT 를 만들었다
        // (`requireRegistryMutableNow` — DX12 체인의 chainreloadeditor). 첫 UI 프레임은 렌더 스레드를 먼저 비운다.
        if ( _bFreshEditorInstance == SW_TRUE )
        {
            waitRenderThreadIdle();
            _bFreshEditorInstance = SW_FALSE;
        }
        if ( _editorAPI.updateUI != nullptr )
            _editorAPI.updateUI( _editor );

        // 에디터가 이번 프레임 입력을 처리한 **뒤에** 확정한다. Step 버튼은 이 갱신에서 눌리고, 씬을 한 틱 진행한 다음
        // endEditorFrame 에서 소비된다. 이 질의를 프레임 앞으로 옮기면 Step 이 틱 없이 소비되어 아무 일도 일어나지 않는다.
        getMutableFrameState()._bTickScene = queryTickScene() ? SW_TRUE : SW_FALSE;
        sampleViewTargets();
        // 다시 그릴 까닭은 이번 프레임의 UI 를 다 돈 뒤에 묻는다(입력 · 잡은 항목 · 알림 · 패널 요청이 이 프레임 것으로 정해졌다).
        const bool bQuiet                    = _editorAPI.getRedrawReasons != nullptr && _editorAPI.getRedrawReasons( _editor ) == 0u;
        getMutableFrameState()._bEditorQuiet = bQuiet ? SW_TRUE : SW_FALSE;
    }

    void EditorModuleHost::endEditorFrame()
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false || _editorAPI.endFrame == nullptr )
            return;
        _editorAPI.endFrame( _editor );
    }

    bool EditorModuleHost::onWindowMessage( const NativeWindowEvent& event )
    {
        if ( hasEditor() == false || _editorAPI.processEvent == nullptr )
            return false;

        // 에디터 내부 상태 갱신과 입력 필터링은 에디터 모듈 안에서 처리한다
        return _editorAPI.processEvent( _editor, &event );
    }

    CameraComponent* EditorModuleHost::getSceneViewCamera() const
    {
        if ( hasEditor() == false || _editorAPI.getSceneViewCamera == nullptr )
            return nullptr;
        return static_cast<CameraComponent*>( _editorAPI.getSceneViewCamera( _editor ) );
    }

    void EditorModuleHost::renderEditor( IRHIDevice& renderDevice )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false )
            return;
        if ( _editorAPI.render != nullptr )
            _editorAPI.render( _editor, &renderDevice );
    }

    void EditorModuleHost::postPresentEditor( IRHIDevice& renderDevice )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false || _editorAPI.postPresent == nullptr )
            return;
        _editorAPI.postPresent( _editor, &renderDevice );
    }

    // ======================================================================
    // LiveReload 콜백. 에디터.
    // ======================================================================

    void EditorModuleHost::onBeforeEditorReload()
    {
        suspendModules( ModuleScope::Editor, true, DrainReason::Reload );
    }

    void EditorModuleHost::onAfterEditorReload( void* pLibraryModule )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( bindEditorAPI( pLibraryModule ) == false )
        {
            markReloadGraphBroken( "EditorAPI bind failed after reload" );
            return;
        }

        if ( createEditorInstance() == false )
        {
            _editorAPI = {};
            markReloadGraphBroken( "Editor create/initialize failed after reload" );
            return;
        }
    }

    bool EditorModuleHost::isEditorImageUsable( void* pLibraryModule ) const
    {
        EditorAPI api{};
        if ( ModuleInstanceUtil::exportAPIFromImage<EditorAPI, PFN_ExportEditorAPI>( pLibraryModule, getModuleSymbols(), api ) == false )
            return false;
        return api.create != nullptr && api.destroy != nullptr;
    }

    void EditorModuleHost::onEditorReloadFault( uint32 faultCode )
    {
        // 반쯤 만든 인스턴스를 부수는 코드도 결함을 낸 그 모듈이다. 부르지 않고 잊는다(새는 것은 재시작이 치운다).
        SW_LOG_ERROR( "Editor module faulted after the reload (code 0x%#) — the editor is off until restart",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
        _editor    = nullptr;
        _editorAPI = {};
    }

    // ======================================================================
    // 공통 호스트가 부르는 지점. 언로드와 비우기.
    // ======================================================================

    void EditorModuleHost::onBeforeSuspendModules()
    {
        const bool bStopSimulation = hasEditor() && _editorAPI.stopSimulation != nullptr;
        if ( bStopSimulation )
        {
            SW_LOG_INFO( "Stopping editor simulation before module suspend." );
            _editorAPI.stopSimulation( _editor );
        }
    }

    void EditorModuleHost::suspendHostModule( bool bReleaseAPITable )
    {
        destroyEditorInstance( bReleaseAPITable );
    }

    void EditorModuleHost::onBeforeDrainWorkers( DrainReason reason )
    {
        const bool bStopJobs = reason == DrainReason::Shutdown && hasEditor() && _editorAPI.requestBackgroundStop != nullptr;
        if ( bStopJobs )
            _editorAPI.requestBackgroundStop( _editor );
    }

    void EditorModuleHost::onRenderWorkersDrained()
    {
        // 렌더 워커를 비웠다고 해서 에디터가 낸 프레임의 in-flight 표시를 풀면 안 된다. 게임 모듈만 리로드할 때 그 표시는 **아직 렌더 스레드에 제출되기 전인** 프레임의 것이다
        // (게임 스레드가 리로드하느라 제출하지 못했다). 풀어 버리면 UI 스레드가 풀려 다음 `updateUI` 에서 보조 뷰포트를 그리는 동안 렌더 스레드가 그 프레임의 메인 스냅샷을 그려
        // 같은 ImGui 백엔드(Vulkan 의 `RenderState` · 커맨드 풀)를 두 스레드가 함께 쓴다. 표시는 에디터 인스턴스를 없앨 때만 푼다(`destroyEditorInstance`).
    }

    // ======================================================================
    // API 바인딩 · 인스턴스
    // ======================================================================

    const ModuleAPISymbols& EditorModuleHost::getModuleSymbols()
    {
        return EditorModuleHostInternal::kEditorSymbols;
    }

    bool EditorModuleHost::bindEditorAPI( void* pLibraryModule )
    {
        if ( ModuleInstanceUtil::exportAPIFromImage<EditorAPI, PFN_ExportEditorAPI>( pLibraryModule, getModuleSymbols(), _editorAPI ) == false )
            return false;

        rebindEditorService();

        engine::registerModuleTypes( sw::config::kTargetEditorModule );
        return _editorAPI.create != nullptr && _editorAPI.destroy != nullptr;
    }

#if !defined( SW_SHIPPING )
    void EditorModuleHost::attachEditorInstance( const EditorAPI& editorAPI, EditorHandle editor )
    {
        _editorAPI = editorAPI;
        _editor    = editor;
    }
#endif

    bool EditorModuleHost::reinitializeAfterRHISwap( void* pEditorModule, void* pGameModule )
    {
        RHI* const pRHI = getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
            return false;

        // 기동과 같은 순서다. 게임이 먼저 스폰되고 에디터가 그 타입과 서비스를 본다.
        bool bOk = true;
        if ( recreateGameInstance( pGameModule ) == false )
            bOk = false;
        if ( recreateEditorInstance( pEditorModule ) == false )
            bOk = false;
        return bOk;
    }

    bool EditorModuleHost::recreateEditorInstance( void* pEditorModule )
    {
        if ( _bEnableEditor == SW_FALSE )
            return true;

        // 테이블이 비어 있으면 모듈에서 다시 바인딩해야 한다 — 리로드 경로가 그 일을 한다.
        if ( _editorAPI.create == nullptr || _editorAPI.initialize == nullptr )
        {
            onAfterEditorReload( pEditorModule );
            return _editor != nullptr;
        }

        rebindEditorService();
        return createEditorInstance();
    }

    void EditorModuleHost::rebindEditorService()
    {
        if ( _editorAPI.bindService == nullptr )
            return;

        // 새 범위를 채워 넘긴 뒤에 앞 범위를 놓는다 — 모듈이 앞 범위를 들고 있는 동안 사라지지 않게.
        unique_ptr<SubsystemScope> editorServices = make_unique<SubsystemScope>();
        fillModuleServiceScope( *editorServices, false );
        _editorAPI.bindService( editorServices.get() );
        _editorServices = std::move( editorServices );
    }

    void EditorModuleHost::destroyEditorInstance( bool bReleaseAPITable )
    {
        // 인스턴스가 사라지면 그 프레임을 렌더할 쪽도 없다(렌더 워커는 이미 비웠다). 남은 in-flight 표시를 풀지 않으면 종료 단계의 `waitForDrawSnapshotIdle` 이 영원히 반환하지 않는다.
        if ( _editor != nullptr && _editorAPI.abandonPendingDraw != nullptr )
            _editorAPI.abandonPendingDraw( _editor );
        // 이번 프레임에 샘플한 게임 뷰 · 씬 뷰 RT 는 이 인스턴스가 가진 텍스처다. 리로드는 샘플(`updateEditorUI`) 뒤 · 엔진 갱신 앞에 일어나므로,
        // 비우지 않으면 같은 프레임의 `Engine::update` 가 부서진 핸들에 그린다(포맷 Unknown — 출력 PSO 조회가 멈춘다). 새 인스턴스는 다음 샘플에서 채운다.
        getMutableFrameState()._views = HostViewTargets{};
        ModuleInstanceUtil::destroyInstance( _editorAPI, _editor, bReleaseAPITable, sw::config::kTargetEditorModule );
    }

    bool EditorModuleHost::createEditorInstance()
    {
        SW_MEMORY_SCOPE( Editor );
        const bool bCreated   = ModuleInstanceUtil::createInstance( _editorAPI, _editor, getWindow(), getRHI(), true, "Editor" );
        _bFreshEditorInstance = bCreated ? SW_TRUE : SW_FALSE;
        return bCreated;
    }
} // namespace sw
