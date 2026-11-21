#include "pch.h"

#include "App/ClientModuleManager.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"

#include "ModuleManager/ModuleInstanceUtil.h"

#include "sw/config/ConfigConstants.h"

#if SW_WITH_DYNAMIC_MODULES
    #include "Core/File/FileUtil.h"
    #include "Core/Module/ModuleLibraryUtil.h"
    #include "Core/Path/AbsolutePath.h"

    #include "ModuleManager/ILiveReloadManager.h"
#endif

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct ClientModuleManagerInternal
        {
            static constexpr ModuleAPISymbols kEditorSymbols{ kEditorModuleABIStamp, "getEditorModuleABIStamp", "exportEditorAPI", "Editor" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    ClientModuleManager::ClientModuleManager()
        : GameModuleManager{}
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

    ClientModuleManager::~ClientModuleManager()
    {
        // 공통 호스트의 소멸자 안에서는 가상 함수가 이 클래스로 오지 않는다. 그래서 에디터를 여기서 먼저 종료한다.
        shutdown();
    }

    bool ClientModuleManager::loadModuleLibraries( ILiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution )
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
            extension._name           = moduleName;
            extension._listDependency = pManifest->collectDependencyNames();
            _listEditorExtension.push_back( std::move( extension ) );
        }
        return GameModuleManager::loadModuleLibraries( pLiveReloadManager, catalog, resolution );
    }

    bool ClientModuleManager::initialize( ILiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, IEngineRenderDriver* pRenderDriver, bool bEnableEditor )
    {
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        // 게임이 먼저, 에디터가 나중이다. 게임은 처음 여는 씬을 요청하고(`GameSceneFlow::requestFirstScene`) 에디터는 제 시작 씬
        // (`-gv_editorStartupScene`)을 요청한다. 씬 매니저는 마지막 요청을 남기므로 나중에 요청한 에디터의 씬이 열린다(`GameSettings::_startMap` 주석).
        if ( initializeGame( pLiveReloadManager, pRHI, pWindow, pRenderDriver ) == false )
            return false;

        // 에디터 모듈을 리로드 그래프에 올리는 일은 동적 모듈 구성에만 있다(`ClientModuleManager.cpp` 의 `SW_WITH_DYNAMIC_MODULES` 구역 — 정적 구성은 짓지 않는다).
        if constexpr ( build::kStaticModules )
        {
            // 에디터를 켜 달라는 실행을 에디터 없이 조용히 시작하지 않는다.
            if ( bEnableEditor )
                SW_LOG_ERROR_ID( "ClientModuleManager.EnableEditorNeedsEditorModule", "-EnableEditor needs the editor module, which a build with statically linked modules (Shipping, Web-Release, Android-Release) does not have - run it from a desktop Dev build." );
            return bEnableEditor == false;
        }
        else
        {
            return registerEditorModule( pLiveReloadManager );
        }
    }

    bool ClientModuleManager::isEditorReloadHeld() const
    {
        return _editorAPI.isReloadHeld != nullptr && _editorAPI.isReloadHeld();
    }

    // ======================================================================
    // 프레임 단위 처리
    // ======================================================================

    bool ClientModuleManager::queryGameplayActive() const
    {
        if ( hasEditor() == false || _editorAPI.isPlaying == nullptr )
            return true;
        return _editorAPI.isPlaying( _editor );
    }

    bool ClientModuleManager::queryTickScene() const
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

    void ClientModuleManager::sampleViewTargets()
    {
        ModuleFrameState& frameState = getMutableFrameState();
        frameState._views            = FrameViewports{};
        if ( hasEditor() == false )
            return;

        if ( _editorAPI.getGameViewport != nullptr )
            _editorAPI.getGameViewport( _editor, &frameState._views._game );
        if ( _editorAPI.getSceneViewport != nullptr )
            _editorAPI.getSceneViewport( _editor, &frameState._views._scene );
        // 툴 창의 미리보기 씬 — 보이는 창만 온다(`Engine::update` 이 씬마다 따로 그린다).
        if ( _editorAPI.getPreviewSceneViews != nullptr )
            frameState._views._previewViewCount = _editorAPI.getPreviewSceneViews( _editor, frameState._views._arrPreviewView, kMaxPreviewSceneView );
    }

    void ClientModuleManager::updateEditorUI( float32 /*deltaTime*/ )
    {
        if ( hasEditor() == false )
            return;

        // UI 프레임은 렌더 스레드가 앞 UI 스냅샷과 게임 패킷을 그리는 동안 돈다(`DrawSnapshotHandoff`). 그래서 UI 프레임의 RHI 자원 생성 · 등록은 렌더 스레드의
        // 기록 밖으로 간다(뷰 RT 는 `TextureCache` 완료가, 패널 텍스처 칸은 그리기 해제 큐가). 새 인스턴스(리로드 · RHI 교체)의 첫 UI 프레임은 초기화가 그 규칙 밖에서
        // 자원을 만들 수 있어(`requireRegistryMutableNow` — DX12 체인의 chainreloadeditor) 렌더 스레드를 먼저 비운다.
        if ( _bFreshEditorInstance == SW_TRUE )
        {
            waitRenderThreadIdle();
            _bFreshEditorInstance = SW_FALSE;
        }
        if ( _editorAPI.updateUI != nullptr )
            _editorAPI.updateUI( _editor );
        // UI 가 보낸 명령(뷰 크기 · Play/Pause/Step)을 씬 틱 앞, 틱 여부 질의 앞에서 적용한다 — 이 프레임의 씬 틱이 받는다(예전 직접 변경과 같은 지연, `EditorUICommandQueue`).
        // UI 를 게임 틱과 나란히 돌리게 되어도 이 자리(게임 스레드)는 그대로다.
        if ( _editorAPI.applyUICommands != nullptr )
            _editorAPI.applyUICommands( _editor );

        // 명령을 적용한 **뒤에** 확정한다. Step 은 위에서 적용되고, 씬을 한 틱 진행한 다음 endEditorFrame 에서 소비된다.
        // 이 질의를 적용 앞으로 옮기면 Step 이 틱 없이 소비되어 아무 일도 일어나지 않는다.
        getMutableFrameState()._bTickScene = queryTickScene() ? SW_TRUE : SW_FALSE;
        sampleViewTargets();
        // 다시 그릴 까닭은 이번 프레임의 UI 를 다 돈 뒤에 묻는다(입력 · 잡은 항목 · 알림 · 패널 요청이 이 프레임 것으로 정해졌다).
        const bool bQuiet                    = _editorAPI.getRedrawReasons != nullptr && _editorAPI.getRedrawReasons( _editor ) == 0u;
        getMutableFrameState()._bEditorQuiet = bQuiet ? SW_TRUE : SW_FALSE;
    }

    void ClientModuleManager::endEditorFrame()
    {
        if ( hasEditor() == false || _editorAPI.endFrame == nullptr )
            return;
        _editorAPI.endFrame( _editor );
    }

    bool ClientModuleManager::onWindowMessage( const NativeWindowEvent& event )
    {
        if ( hasEditor() == false || _editorAPI.processEvent == nullptr )
            return false;

        // 에디터 내부 상태 갱신과 입력 필터링은 에디터 모듈 안에서 처리한다
        return _editorAPI.processEvent( _editor, &event );
    }

    CameraComponent* ClientModuleManager::getSceneViewCamera() const
    {
        if ( hasEditor() == false || _editorAPI.getSceneViewCamera == nullptr )
            return nullptr;
        return static_cast<CameraComponent*>( _editorAPI.getSceneViewCamera( _editor ) );
    }

    void ClientModuleManager::prepareRenderEditor( IRHIDevice& renderDevice )
    {
        if ( hasEditor() == false )
            return;
        if ( _editorAPI.prepareRender != nullptr )
            _editorAPI.prepareRender( _editor, &renderDevice );
    }

    void ClientModuleManager::renderEditor( IRHIDevice& renderDevice )
    {
        if ( hasEditor() == false )
            return;
        if ( _editorAPI.render != nullptr )
            _editorAPI.render( _editor, &renderDevice );
    }

    void ClientModuleManager::postPresentEditor( IRHIDevice& renderDevice )
    {
        if ( hasEditor() == false || _editorAPI.postPresent == nullptr )
            return;
        _editorAPI.postPresent( _editor, &renderDevice );
    }

    // ======================================================================
    // LiveReload 콜백. 에디터.
    // ======================================================================

    void ClientModuleManager::onBeforeEditorReload()
    {
        suspendModules( ModuleScope::Host, true, FlushReason::Reload );
    }

    void ClientModuleManager::onAfterEditorReload( ModuleLibraryHandle library )
    {
        const ScopedMemoryTag moduleMemoryTag{ kEditorModuleMemoryTag }; // 다시 올린 모듈의 서비스 묶기(표의 `bindService`)가 모듈 코드다
        if ( bindEditorAPI( library ) == false )
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

    bool ClientModuleManager::isEditorLibraryUsable( ModuleLibraryHandle library ) const
    {
        return ModuleInstanceUtil::isLibraryUsable<EditorAPI, PFN_ExportEditorAPI>( library, getModuleSymbols() );
    }

    void ClientModuleManager::onEditorReloadFault( uint32 faultCode )
    {
        // 반쯤 만든 인스턴스를 부수는 코드도 결함을 낸 그 모듈이다. 부르지 않고 잊는다(새는 것은 재시작이 치운다).
        SW_LOG_ERROR_ID( "ClientModuleManager.EditorModuleFaultedAfter", "Editor module faulted after the reload (code 0x%#) — the editor is off until restart",
                         Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
        _editor    = nullptr;
        _editorAPI = {};
    }

    // ======================================================================
    // 공통 호스트가 부르는 지점. 언로드와 비우기.
    // ======================================================================

    bool ClientModuleManager::onBeforeSuspendModules( FlushReason reason )
    {
        if ( hasEditor() == false )
            return false;
        // 리로드 · 백엔드 교체는 플레이(PIE)를 잇는다. 세션은 에디터 인스턴스가 내려가며 호스트에 맡기고(`EditorPlaySession::carryOut` — 상태는 `IModuleStateStore`,
        // 롤백 스냅샷은 씬 관리자) 새 인스턴스가 되찾는다. 월드 · 씬 오브젝트 · 게임 인스턴스는 처음부터 호스트 것이라 옮길 것이 없다. 언리얼 Live Coding 이 PIE 를 잇는 자리다.
        const bool bCarrySession = reason == FlushReason::Reload || reason == FlushReason::BackendSwap;
        if ( bCarrySession )
            return true;
        // 종료(다시 설 인스턴스가 없다)는 Stop 해 편집 씬을 되돌리고 월드를 멈춘다.
        if ( _editorAPI.stopSimulation != nullptr )
        {
            SW_LOG_INFO_ID( "ClientModuleManager.StoppingEditorSimulationBefore", "Stopping editor simulation before module suspend." );
            _editorAPI.stopSimulation( _editor );
        }
        return false;
    }

    void ClientModuleManager::suspendHostModule( bool bReleaseAPITable )
    {
        destroyEditorInstance( bReleaseAPITable );
    }

    void ClientModuleManager::onBeforeFlushWorkers( FlushReason reason )
    {
        const bool bStopJobs = reason == FlushReason::Shutdown && hasEditor() && _editorAPI.requestBackgroundStop != nullptr;
        if ( bStopJobs )
            _editorAPI.requestBackgroundStop( _editor );
    }

    void ClientModuleManager::onRenderWorkersFlushed()
    {
        // 렌더 워커를 비웠다고 해서 에디터가 낸 프레임의 in-flight 표시를 풀면 안 된다. 게임 모듈만 리로드할 때 그 표시는 **아직 렌더 스레드에 제출되기 전인** 프레임의 것이다
        // (게임 스레드가 리로드하느라 제출하지 못했다). 풀어 버리면 UI 스레드가 풀려 다음 `updateUI` 에서 보조 뷰포트를 그리는 동안 렌더 스레드가 그 프레임의 메인 스냅샷을 그려
        // 같은 ImGui 백엔드(Vulkan 의 `RenderState` · 커맨드 풀)를 두 스레드가 함께 쓴다. 표시는 에디터 인스턴스를 없앨 때만 푼다(`destroyEditorInstance`).
    }

    // ======================================================================
    // API 바인딩 · 인스턴스
    // ======================================================================

    const ModuleAPISymbols& ClientModuleManager::getModuleSymbols()
    {
        return ClientModuleManagerInternal::kEditorSymbols;
    }

    bool ClientModuleManager::bindEditorAPI( ModuleLibraryHandle library )
    {
        if ( ModuleInstanceUtil::exportAPIFromLibrary<EditorAPI, PFN_ExportEditorAPI>( library, getModuleSymbols(), _editorAPI ) == false )
            return false;

        rebindEditorService();

        if ( engine::registerModuleTypes( sw::config::kTargetEditorModule ) == false )
            return false;
        return _editorAPI.create != nullptr && _editorAPI.destroy != nullptr;
    }

    bool ClientModuleManager::recreateHostModule()
    {
        if ( _bEnableEditor == SW_FALSE )
            return true;

        // 테이블이 비어 있으면 모듈에서 다시 바인딩해야 한다 — 리로드 경로가 그 일을 한다. 핸들은 모듈 수명을 아는 공통 호스트가 찾는다(배포는 정적 링크라 nullptr).
        if ( _editorAPI.create == nullptr || _editorAPI.initialize == nullptr )
        {
            onAfterEditorReload( getLoadedModuleHandle( sw::config::kTargetEditorModule ) );
            return _editor != nullptr;
        }

        rebindEditorService();
        return createEditorInstance();
    }

    void ClientModuleManager::destroyEditorInstance( bool bReleaseAPITable )
    {
        // 인스턴스가 사라지면 그 프레임을 렌더할 쪽도 없다(렌더 워커는 이미 비웠다). 남은 in-flight 표시를 풀지 않으면 종료 단계의 `waitForDrawSnapshotIdle` 이 영원히 반환하지 않는다.
        if ( _editor != nullptr && _editorAPI.abandonPendingDraw != nullptr )
            _editorAPI.abandonPendingDraw( _editor );
        // 이번 프레임에 샘플한 게임 뷰 · 씬 뷰 RT 는 이 인스턴스가 가진 텍스처다. 리로드는 샘플(`updateEditorUI`) 뒤 · 엔진 갱신 앞에 일어나므로,
        // 비우지 않으면 같은 프레임의 `Engine::update` 가 부서진 핸들에 그린다(포맷 Unknown — 출력 PSO 조회가 멈춘다). 새 인스턴스는 다음 샘플에서 채운다.
        getMutableFrameState()._views = FrameViewports{};
        ModuleInstanceUtil::destroyInstance( _editorAPI, _editor, bReleaseAPITable, sw::config::kTargetEditorModule );
    }

    bool ClientModuleManager::createEditorInstance()
    {
        const auto initializeEditor = [this]( void* pHandle )
        { return _editorAPI.initialize( pHandle, getWindow(), getRHI() != nullptr && getRHI()->hasDevice() ? &getRHI()->getDevice() : nullptr ); };
        const bool bCreated   = ModuleInstanceUtil::createInstance( _editorAPI, _editor, initializeEditor, getRHI(), true, "Editor" );
        _bFreshEditorInstance = bCreated ? SW_TRUE : SW_FALSE;
        return bCreated;
    }

#if SW_WITH_DYNAMIC_MODULES
    // ----------------------------------------------------------------------------------------------------------------------
    // 동적 모듈 구성에만 있는 경로 — 리로드 그래프 등록(LiveReload). 인스턴스 없는 헤드리스 작업은 `EditorModuleHeadlessTasks.cpp` 다.
    // 정적 구성(배포 · 웹 · Android 개발)은 이 헤더 · 라이브러리를 짓지 않아 여기만 `#if` 로 가른다(docs/agents/CppStyle.md "Build configuration switches" 둘째 줄). 부르는 자리는 `build::kStaticModules` 로 가른다.
    // ----------------------------------------------------------------------------------------------------------------------
    bool ClientModuleManager::registerEditorModule( ILiveReloadManager* pLiveReloadManager )
    {
        if ( _bEnableEditor == SW_TRUE && _bEditorModuleActive == SW_FALSE )
        {
            // -EnableEditor 를 줬는데 매니페스트가 에디터 모듈을 껐다(프로젝트나 구성 때문이다). 에디터 없이 조용히 시작하지 않는다.
            SW_LOG_ERROR_ID( "ClientModuleManager.EnableEditorGivenButModule", "-EnableEditor was given, but the module manifests turn %# off for this project", config::kTargetEditorModule );
            return false;
        }
        if ( _bEnableEditor == SW_FALSE || pLiveReloadManager == nullptr )
            return true;

        BLOCK( "에디터: 뷰포트 / EditorModule 등록" )
        {
            const ScopedMemoryTag moduleMemoryTag{ kEditorModuleMemoryTag }; // 라이브러리 로드가 모듈의 정적 등록자를 돌린다
            // 등록이 곧 첫 로드의 "리로드 직후" 다 — 주인 창구를 먼저 걸어 그때 에디터 인스턴스가 선다.
            holdReloadSubscription( pLiveReloadManager->setModuleClient( config::kTargetEditorModule, _editorReloadClient ) );
            // 편집기 코드가 다른 스레드에서 돌고 있으면(열린 파일 대화상자) 닫힐 때까지 리로드를 기다린다 — 변경은 버리지 않는다.
            holdReloadSubscription( pLiveReloadManager->addReloadHold( SW_DELEGATE_METHOD( ILiveReloadManager::ReloadHoldDelegate, &ClientModuleManager::isEditorReloadHeld, this ) ) );
            if ( pLiveReloadManager->registerModule( config::kTargetEditorModule ) == false )
            {
                SW_LOG_ERROR_ID( "ClientModuleManager.FailedLoadEditorModule", "Failed to load the Editor Module." );
                return false;
            }

            // 확장 모듈은 EditorModule 과 자기 키트에 의존한다. 의존이 바뀌면 리로드 그래프가 확장을 다시 로드한다(키트와 SWGame 의 관계와 같다).
            // 인스턴스는 없다: 정적 등록자가 EditorModule 의 등록 목록에 오르고, 에디터 매니저가 다음 프레임에 맞춘다(EditorSubsystemSet::syncExtensionRegistrations).
            for ( const EditorExtensionModule& extension : _listEditorExtension )
            {
                if ( pLiveReloadManager->registerModule( extension._name, extension._listDependency ) == false )
                {
                    // 언리얼도 에디터 모듈 하나가 실패하면 그 플러그인만 끈다. 에디터는 시작한다.
                    SW_LOG_ERROR_ID( "ClientModuleManager.EditorExtensionModuleCould", "Editor extension module %# could not be loaded - the editor runs without it", extension._name.c_str() );
                    continue;
                }
                SW_LOG_INFO_ID( "ClientModuleManager.EditorExtensionModuleLoaded", "Editor extension module %# loaded", extension._name.c_str() );
            }

            if ( pLiveReloadManager->isGraphBroken() )
            {
                SW_LOG_ERROR_ID( "ClientModuleManager.LiveReloadGraphBrokenDuring", "LiveReload graph broken during module registration — aborting initialize" );
                return false;
            }
        }
        return true;
    }
#endif // SW_WITH_DYNAMIC_MODULES
} // namespace sw
