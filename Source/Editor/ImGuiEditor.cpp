#include "pch.h"

#include "Editor/ImGuiEditor.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"
#include "Editor/Common/Backend/IImGuiPlatformBackend.h"
#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorCommandGui.h"
#include "Editor/Common/Gui/EditorFontSetup.h"
#include "Editor/Common/Gui/EditorMenuBar.h"
#include "Editor/Common/Gui/EditorNotificationManager.h"
#include "Editor/Common/Gui/EditorPanelDump.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/ConfigHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Popups/EditorPopupManager.h"
#include "Editor/SelfTest/EditorRegistryDump.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/Viewport/EditorCamera.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "RuntimeAPI/Export/EditorModuleExports.h"

#include <imgui.h>
#include <ImGuiNotify.hpp>
#include <ImGuizmo.h>
#include <implot.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiEditorInternal
        {
            /** @brief ImGui 할당을 sw 할당자로 보낸다 — 할당 헤더에 태그가 적혀 메모리 프로파일러의 Editor 줄로 세인다. */
            static void* allocateForImGui( size_t size, void* /*pUserData*/ ) { return Memory::allocate( size ); }
            /** @brief `allocateForImGui` 의 짝입니다. */
            static void freeForImGui( void* pPtr, void* /*pUserData*/ ) { Memory::free( pPtr ); }

            static void loadSplashDefaultRenderPass( const TaskArgs& args )
            {
                shared_ptr<RenderPassAsset> pPass = args.get<shared_ptr<RenderPassAsset>>( 0 );
                if ( pPass == nullptr )
                    return;
                // `getService<T>()` 는 nullptr 을 반환할 수 있다. 이 둘은 **워커 스레드**에서 도는 스플래시 로드라, 서비스 연결이 아직
                // 없거나 이미 끊긴 틈에 걸리면 조용히 죽는다.
                const EngineDefaultAssets* pEngineDefaultAssets = editor::getService<const EngineDefaultAssets>();
                if ( pEngineDefaultAssets == nullptr )
                    return;
                SW_LOG_TRACE( "Splash: reading DefaultRenderPass.xml" );
                if ( pPass->loadFromXmlFile( pEngineDefaultAssets->_defaultRenderPass ) == false )
                    SW_LOG_WARNING( "Splash: could not read %#", pEngineDefaultAssets->_defaultRenderPass.c_str() );
            }

            static void loadSplashForwardPipeline( const TaskArgs& args )
            {
                shared_ptr<RenderPipelineAsset> pPipeline = args.get<shared_ptr<RenderPipelineAsset>>( 0 );
                if ( pPipeline == nullptr )
                    return;
                const EngineDefaultAssets* pEngineDefaultAssets = editor::getService<const EngineDefaultAssets>();
                if ( pEngineDefaultAssets == nullptr )
                    return;
                SW_LOG_TRACE( "Splash: reading ForwardPipeline.xml" );
                if ( pPipeline->loadFromXmlFile( pEngineDefaultAssets->_defaultForwardPipeline ) == false )
                    SW_LOG_WARNING( "Splash: could not read %#", pEngineDefaultAssets->_defaultForwardPipeline.c_str() );
            }

            /**
             * @brief 이 모듈이 Undo 스택에 넣은 모듈 명령(패널 람다 · 문서 편집)과 알림 처리기를 뗍니다. 오브젝트 편집(엔진 데이터 명령)은 남습니다.
             * @details 스택은 엔진 소유라 이 모듈보다 오래 산다. 모듈 명령을 남긴 채 내려가면 리로드 뒤 Ctrl+Z 가 언맵된 코드로 뛰고, 종료할 때는
             *          델리게이트 소멸자(`Delegate::_managerFunc`)가 그리로 뛴다. 범위는 이 함수가 든 이미지다.
             */
            static void releaseModuleUndoCommands()
            {
                CommandStack* pCommandStack = editor::getService<CommandStack>();
                const void*   pBegin{ nullptr };
                const void*   pEnd{ nullptr };
                if ( pCommandStack == nullptr ||
                     FileUtil::findLoadedImageRange( reinterpret_cast<const void*>( &ImGuiEditorInternal::releaseModuleUndoCommands ), pBegin, pEnd ) == false )
                    return;
                pCommandStack->setObjectEditListener( {} );
                const uint32 droppedCount = pCommandStack->releaseCodeWithin( pBegin, pEnd );
                if ( droppedCount > 0 )
                    SW_LOG_INFO( "Dropped %# undo commands that run editor module code; scene edits stay undoable", droppedCount );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiEditor" );

    // 이 파일만 읽으므로 여기서 정의한다(헤더에 선언하지 않는다).
    /**
     * @brief `-gv_editorStartupScene=<리소스 경로>`: 에디터가 시작할 때 이 씬을 엽니다.
     * @details 빈 씬으로 기동 검증을 하면 오브젝트를 순회하는 코드(뷰포트 피킹 · 컴포넌트 시각화 · Hierarchy 트리 ·
     *          Profiler 분포표 · 씬 세대 변경 훅)가 실행되지 않습니다. 이 스위치로 테스트 씬을 열면 그 경로가 모두 켜집니다.
     *          예: `-gv_editorStartupScene=game/empty/maps/editortest.scene.xml`
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_editorStartupScene, "", "에디터 시작 시 열 씬의 리소스 경로 (비우면 열지 않는다)" );

    /**
     * @brief `-gv_editorUiScale=<배율>`: 에디터 UI 배율입니다. 0 이면 창이 놓인 모니터의 DPI 를 따릅니다.
     * @details 언리얼 Editor Preferences 의 UI 배율 · 유니티 UI Scaling 설정과 같은 자리입니다. 모니터 DPI 와 상관없이 크게 · 작게 보고
     *          싶을 때, 그리고 고해상도 모니터가 없는 기계에서 배율 경로를 확인할 때 씁니다.
     */
    SW_GLOBAL_VARIABLE( float32, gv_editorUiScale, 0.0f, "에디터 UI 배율 (0 = 모니터 DPI 를 따름)" );

    ImGuiEditor::ImGuiEditor()
        : _platformBackend{ nullptr }
        , _rendererBackend{ nullptr }
        , _editorToolDefaults{ nullptr }
        , _editorContext{ nullptr }
        , _dockLayout{}
        , _arrDrawSnapshot{}
        , _publishedDrawSlot{ 0 }
        , _inFlightDrawSlot{ _s_kInvalidDrawSlot }
        , _lastDrawSnapshotSequence{ 0 }
        , _bInitialized{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    ImGuiEditor::~ImGuiEditor()
    {
        ImGuiEditor::shutdown();
    }

    bool ImGuiEditor::initialize( IWindow* pWindow, IRHIDevice* pRhiDevice )
    {
        SW_LOG_TRACE( "Initialize start." );
        if ( _bInitialized != SW_FALSE )
            return true;

        if ( pWindow == nullptr || pRhiDevice == nullptr )
        {
            SW_LOG_ERROR( "Cannot initialize without window and RHI device." );
            shutdownPartialInitialization();
            return false;
        }

#if !defined( SW_SHIPPING )
        BLOCK( "EditorConfig host load" )
        {
            EditorConfig::loadFromHost();
            _editorToolDefaults = make_unique<EditorToolDefaults>();
            if ( _editorToolDefaults->loadFromHostPath() == false )
                SW_LOG_WARNING( "Editor data could not be read - using defaults" );
            editor::setEditorToolDefaults( _editorToolDefaults.get() );
        }
#endif

        BLOCK( "ImGui Context / IO / Style" )
        {
            SW_LOG_TRACE( "Checking ImGui version and creating context" );
            IMGUI_CHECKVERSION();
            // 컨텍스트보다 먼저 건다 — 이 모듈의 ImGui 할당이 모두 같은 할당자에서 잡히고 풀린다(컨텍스트는 shutdown 이 이 모듈 안에서 지운다).
            ImGui::SetAllocatorFunctions( &ImGuiEditorInternal::allocateForImGui, &ImGuiEditorInternal::freeForImGui, nullptr );
            ImGui::CreateContext();
            ImPlot::CreateContext();

            SW_LOG_TRACE( "Configuring ImGui IO" );
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            // 멀티 뷰포트는 네 백엔드 모두에 렌더러 백엔드가 있어 항상 켠다. 렌더러 백엔드가 없는 백엔드라면 아래
            // createRendererBackend 가 nullptr 을 반환하고 초기화가 거기서 멈춘다.
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            _dockLayout.initializePersistencePaths();
            _dockLayout.applyIniFilename();
        }

        EditorFontSetup::apply();

        BLOCK( "Platform Backend create / init" )
        {
            SW_LOG_TRACE( "Creating Platform Backend" );
            _platformBackend = IImGuiPlatformBackend::createPlatformBackend();
            if ( _platformBackend == nullptr )
            {
                SW_LOG_ERROR( "Failed to create platform backend" );
                shutdownPartialInitialization();
                return false;
            }

            SW_LOG_TRACE( "Initializing Platform Backend" );
            if ( _platformBackend->initialize( pWindow, pRhiDevice->getBackendType() ) == false )
            {
                SW_LOG_ERROR( "Platform backend initialization failed" );
                shutdownPartialInitialization();
                return false;
            }
        }

        BLOCK( "Renderer Backend create / init" )
        {
            SW_LOG_TRACE( "Creating Renderer Backend" );
            _rendererBackend = IImGuiRendererBackend::createRendererBackend( pRhiDevice->getBackendType() );
            if ( _rendererBackend == nullptr || _rendererBackend->initialize( pRhiDevice ) == false )
            {
                SW_LOG_ERROR( "Renderer backend initialization failed" );
                shutdownPartialInitialization();
                return false;
            }
        }

        BLOCK( "Splash / async RenderPass load then panels" )
        {
            SW_LOG_TRACE( "Splash: loading DefaultRenderPass / ForwardPipeline..." );

            const shared_ptr<RenderPassAsset>     defaultPass     = sw::make_shared<RenderPassAsset>();
            const shared_ptr<RenderPipelineAsset> forwardPipeline = sw::make_shared<RenderPipelineAsset>();

            TaskManager* pTaskManager = editor::getService<TaskManager>();
            if ( pTaskManager == nullptr )
            {
                SW_LOG_ERROR( "TaskManager service is not bound — cannot load splash resources" );
                shutdownPartialInitialization();
                return false;
            }

            TaskHandle hDefault = pTaskManager->emplaceTask(
                "EditorSplash_DefaultRenderPass",
                SW_DELEGATE_FUNCTION( TaskArgsDelegate, ImGuiEditorInternal::loadSplashDefaultRenderPass ),
                MakeTaskArgs( defaultPass ) );

            TaskHandle hForward = pTaskManager->emplaceTask(
                "EditorSplash_ForwardPipeline",
                SW_DELEGATE_FUNCTION( TaskArgsDelegate, ImGuiEditorInternal::loadSplashForwardPipeline ),
                MakeTaskArgs( forwardPipeline ) );

            TaskStageHandle stage = pTaskManager->createStage();
            stage.addTask( hDefault ).addTask( hForward );

            hDefault.submit();
            hForward.submit();

            pTaskManager->waitStage( stage );
        }

        BLOCK( "Register Default Windows" )
        {
            _editorContext = make_unique<EditorContext>();
            _editorContext->initialize();
            _editorContext->setDockLayout( &_dockLayout );
            // 테마는 **컨텍스트가 활성화된 뒤에** 읽는다. 테마 상태를 컨텍스트가 들고 있으므로, 앞에서 부르면 적용한 테마가 갈 곳이
            // 없어 조용히 버려진다(실측: stored preset 이 0 에 머문다).
            EditorThemeUtil::loadFromConfig();
            _editorContext->setRhiDevice( pRhiDevice );
            _editorContext->setRendererBackend( _rendererBackend.get() );

            _editorContext->getPanelManager().registerDefaultPanels();
            EditorCommandGui::registerDefaults();
            // 리로드 전에 기록한 오브젝트 편집도 되돌리면 이 모듈이 선택 · 씬 dirty 를 맞춘다.
            CommandStack* pCommandStack = editor::getService<CommandStack>();
            if ( pCommandStack != nullptr )
                EditorTransaction::bindObjectEditListener( *pCommandStack );
            EditorRegistryDump::dumpIfRequested();
            _dockLayout.loadPanelVisibility();

            // 모니터 DPI 로 스타일 · 글자를 키운다(테마를 읽은 **뒤** — 테마가 96 DPI 기준 크기를 적는다). 모니터를 옮기면 글자와 플랫폼 창이
            // 따라간다(ImGui 1.92 동적 폰트).
            // 배율을 직접 정했으면(`gv_editorUiScale`) 모니터를 옮겨도 글자 배율을 덮어쓰지 않는다.
            const bool bFixedUiScale = gv_editorUiScale > 0.0f;
            EditorThemeUtil::setDpiScale( bFixedUiScale ? static_cast<float32>( gv_editorUiScale ) : _platformBackend->getDpiScale() );
            ImGui::GetIO().ConfigDpiScaleFonts     = bFixedUiScale == false;
            ImGui::GetIO().ConfigDpiScaleViewports = bFixedUiScale == false;
            SW_LOG_INFO( "Editor UI scale %# (%#, frame padding %#x%#)", EditorThemeUtil::getDpiScale(), bFixedUiScale ? "fixed" : "monitor DPI",
                         ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y );

            // `-gv_editorStartupScene=<경로>`: 검증용이다 — 기동 검증이 오브젝트를 순회하는 코드까지 다루게 한다. 정의는 이 파일 위에 있다.
            if ( gv_editorStartupScene.empty() == false )
            {
                SW_LOG_INFO( "시작 씬을 엽니다: %#", gv_editorStartupScene.c_str() );
                if ( EditorAssetCommands::loadScene( gv_editorStartupScene ) == false )
                    SW_LOG_ERROR( "시작 씬을 열지 못했습니다: %#", gv_editorStartupScene.c_str() );
            }
        }

        if ( pWindow != nullptr )
            pWindow->setCloseQueryHandler( SW_DELEGATE_METHOD( WindowCloseQueryDelegate, &ImGuiEditor::onWindowCloseQuery, this ) );

        _bInitialized = SW_TRUE;
        return true;
    }

    void ImGuiEditor::shutdownPartialInitialization()
    {
        // 세운 역순이다(헤더의 단계 목록). 각 단계는 세워지지 않았으면 아무것도 하지 않는다.

        // 7) 창 닫기 질의 처리기 — 이 DLL 의 메서드를 가리킨다.
        IWindow* pActiveWindow = IWindow::getActiveWindow();
        if ( pActiveWindow != nullptr )
            pActiveWindow->setCloseQueryHandler( {} );

        // 6) 에디터 컨텍스트 · 패널. Undo 스택에서는 이 모듈의 코드를 쥔 명령과 편집 리스너만 뗀다(오브젝트 편집은 엔진 데이터 명령이라 남는다).
        ImGuiEditorInternal::releaseModuleUndoCommands();
        if ( _editorContext != nullptr )
        {
            _editorContext->destroyGameView();
            _editorContext->getPanelManager().shutdownAllPanels( nullptr );
            _editorContext->getPanelManager().clear();
            _editorContext->shutdown();
            _editorContext.reset();
        }

        // 4) 렌더 백엔드
        if ( _rendererBackend != nullptr )
        {
            _rendererBackend->shutdown();
            _rendererBackend.reset();
        }

        // 3) 플랫폼 백엔드
        if ( _platformBackend != nullptr )
        {
            _platformBackend->shutdown();
            _platformBackend.reset();
        }

        // 2) ImPlot · ImGui 컨텍스트(글꼴 아틀라스는 컨텍스트가 든다)
        if ( ImPlot::GetCurrentContext() != nullptr )
            ImPlot::DestroyContext();
        if ( ImGui::GetCurrentContext() != nullptr )
            ImGui::DestroyContext();

        // 1) 에디터 데이터
        editor::setEditorToolDefaults( nullptr );
        _editorToolDefaults.reset();
    }

    void ImGuiEditor::shutdown()
    {
        if ( _bInitialized == SW_FALSE && _editorContext == nullptr && _rendererBackend == nullptr && _platformBackend == nullptr && ImGui::GetCurrentContext() == nullptr )
            return;

        // 열려 있는 파일 대화 상자의 결과 델리게이트를 끊는다. 모듈이 내려가기 전에 걷어 내야 하는 이 DLL 안의 주소다(전역 변수는
        // 모듈을 내리는 쪽이 모듈 이름으로 걷는다). 그 델리게이트는 이 DLL 안의 함수와 `this` 를 잡고
        // 있고, 네이티브 대화 상자는 사용자가 닫을 때까지 떠 있다. Undo 스택과 같은 종류의 함정이다.
        FileUtil::cancelFileDialogResults();

        // 기다리지 않는다. 여기로 오는 경로(ModuleHost::suspendModules)는 이미 drainRenderWorkers 로 렌더 워커를 비운 뒤라
        // 기다릴 상대가 없다.
        abandonPendingDraw();
        for ( EditorDrawDataSnapshot& snapshot : _arrDrawSnapshot )
            snapshot.clear();

        _dockLayout.save();

        // 초기화 단계의 내리기는 실패 경로와 같은 본문 하나다.
        shutdownPartialInitialization();

        _bInitialized = SW_FALSE;
    }

    void ImGuiEditor::preRender( IRHIDevice* pRhiDevice )
    {
        if ( _bInitialized == SW_FALSE || _editorContext == nullptr )
            return;

        _editorContext->getPanelManager().preRenderOpenPanels( pRhiDevice );
    }

    void ImGuiEditor::updateUi()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        waitForDrawSnapshotIdle();

        if ( _editorContext != nullptr )
        {
            _editorContext->setGameViewFocused( false );
            _editorContext->setGameViewHovered( false );
        }

        BLOCK( "ImGui NewFrame / Dockspace" )
        {
            beginFrame();
            EditorMenuBar::drawThemeDialog();
            EditorMenuBar::draw( _dockLayout );
            _dockLayout.beginDockspace();
        }

        EditorCommandGui::processHotkeys();
        EditorMenuBar::processOpenPanelRequests();
        EditorMenuBar::processSceneSession();

        // 에셋 파일 감시는 **에디터 프레임에서만** 돈다. 리로드가 패널 그리기보다
        // 앞에 있어야 이번 프레임에 바뀐 머티리얼이 그대로 보인다.
        if ( _editorContext != nullptr )
        {
            _editorContext->getAssetHotReload().update();
            _editorContext->getConfigHotReload().update();
            _editorContext->getAssetValidation().update();
            _editorContext->getSourceControl().update();
        }

        BLOCK( "Editor Panels Draw" )
        {
            if ( _editorContext != nullptr )
            {
                _editorContext->getPanelManager().drawOpenPanels();
                _editorContext->getPopupManager().drawOpenPopups();
                _editorContext->getNotificationManager().updateAndDraw( ImGui::GetIO().DeltaTime, 1920.0f, 1080.0f );
            }
            // -gv_editorSelfTest=<패턴> 이 없으면 아무것도 하지 않는다. 패널을 그린 뒤라 시험이 이번 프레임의 패널 상태를 본다.
            EditorSelfTestRunner::runFrame();
        }

        BLOCK( "ImGui EndFrame / Platform Windows Update" )
        {
            endFrame();

            // EndFrame 이후여야 창의 DrawList 가 이번 프레임의 최종 내용을 담는다.
            // -gv_editorPanelDump=N 이 없으면 아무것도 하지 않는다.
            EditorPanelDump::dumpIfRequested();

            // GL 처럼 컨텍스트가 렌더 스레드 전용이면 GPU 작업(텍스처 갱신·보조 뷰포트 렌더)을
            // present 훅으로 옮긴다. 그 외 백엔드는 여기 UI 스레드에서 처리한다.
            const bool bRenderThreadCtx =
                _rendererBackend != nullptr && _rendererBackend->requiresRenderThreadContext();

            // ImGui 1.92 동적 아틀라스: 폰트/텍스처 생성·갱신을 그리기 전에 마친다.
            // (메인 스냅샷은 Textures==nullptr 로 넘겨 렌더 스레드가 이 리스트를 만지지 않는다)
            if ( _rendererBackend != nullptr && bRenderThreadCtx == false )
                _rendererBackend->processTextureUpdates();

            ImGuiIO& io = ImGui::GetIO();
            if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
            {
                // 플랫폼(OS 창) 갱신은 항상 UI 스레드에서 한다. imgui 1.92 뷰포트 관리는 단일 스레드 호출을 전제하므로 보조(플로팅)
                // 뷰포트의 GPU 렌더 · present 는 한 스레드에서만 돌려야 하고, GL 이면 그 스레드는 렌더 스레드다(아래 render() 에서 처리).
                // 창 생성 · 크기 변경 · 파괴와 렌더가 GPU 큐에 제출 · 대기하므로 렌더러 백엔드의 큐 잠금 안에서 한다.
                const std::unique_lock<mutex> queueLock =
                    ( _rendererBackend != nullptr ) ? _rendererBackend->lockSubmissionQueue() : std::unique_lock<mutex>{};
                ImGui::UpdatePlatformWindows();
                if ( bRenderThreadCtx == false )
                    ImGui::RenderPlatformWindowsDefault();
            }

            const uint32 writeSlot =
                ( _publishedDrawSlot.load( std::memory_order_acquire ) + 1u ) % constant::kMaxFrameCountInFlight;
            while ( _inFlightDrawSlot.load( std::memory_order_acquire ) == writeSlot )
                std::this_thread::yield();

            // 번호는 내기 전에 알린다. 이 뒤에 놓는 자원은 이 스냅샷이 그릴 수 있으므로 다음 번호를 받아야 한다.
            ++_lastDrawSnapshotSequence;
            _arrDrawSnapshot[writeSlot].capture( _lastDrawSnapshotSequence );
            if ( _rendererBackend != nullptr )
                _rendererBackend->getDrawReleaseQueue().markSnapshotPublished( _lastDrawSnapshotSequence );
            _publishedDrawSlot.store( writeSlot, std::memory_order_release );

            // 이 프레임을 "렌더 대기" 상태로 표시한다. 다음 updateUi 는 상단 waitForDrawSnapshotIdle
            // 에서 postPresent 까지 막히므로, 렌더 스레드가 present 훅에서 ImGui 공유 상태
            // (텍스처 리스트·뷰포트)를 만지는 GL 경로에서도 UI 스레드와 겹치지 않는다.
            // (렌더 스레드 render() 도 같은 값을 다시 저장하지만 값이 같아 무해하다)
            _inFlightDrawSlot.store( writeSlot, std::memory_order_release );
        }
    }

    void ImGuiEditor::render( IRHIDevice* pRhiDevice )
    {
        if ( _bInitialized == SW_FALSE || pRhiDevice == nullptr )
            return;

        // GL: 컨텍스트가 이 스레드(렌더 스레드)에 바인딩된 지금이 프레임 GPU 작업을 할 유일한 지점이다.
        const bool bRenderThreadCtx =
            _rendererBackend != nullptr && _rendererBackend->requiresRenderThreadContext();
        if ( bRenderThreadCtx )
        {
            _rendererBackend->newFrame();
            _rendererBackend->processTextureUpdates();
        }

        const uint32 slot = _publishedDrawSlot.load( std::memory_order_acquire );
        _inFlightDrawSlot.store( slot, std::memory_order_release );
        if ( slot < constant::kMaxFrameCountInFlight )
        {
            ImDrawData* pDrawData = _arrDrawSnapshot[slot].getMainDrawData();
            if ( pDrawData != nullptr )
                renderBackend( pRhiDevice, pDrawData );

            // 이 프레임이 그린 스냅샷보다 먼저 놓인 자원은 앞 프레임들만 그렸다. 이 프레임의 GPU 완료 뒤에 놓이도록 디바이스로 넘긴다.
            if ( _rendererBackend != nullptr )
                _rendererBackend->getDrawReleaseQueue().handOverToDevice( *pRhiDevice, _arrDrawSnapshot[slot].getSequence() );
        }

        // 보조(플로팅) 뷰포트도 GL 이면 여기 렌더 스레드에서 렌더·present 한다.
        // (UI 스레드는 updateUi 상단 waitForDrawSnapshotIdle 에서 막혀 있어 ImGui 상태가 안정적이다)
        if ( bRenderThreadCtx )
        {
            const ImGuiIO& io = ImGui::GetIO();
            if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
                ImGui::RenderPlatformWindowsDefault();
        }
    }

    void ImGuiEditor::postPresent( IRHIDevice* pRhiDevice )
    {
        std::ignore = pRhiDevice;
        // 메인 스냅샷 렌더가 끝났으니 UI 스레드가 다음 슬롯을 쓰도록 해제한다.
        // 보조 뷰포트는 updateUi 에서 UI 스레드가 이미 렌더·present 했다.
        _inFlightDrawSlot.store( _s_kInvalidDrawSlot, std::memory_order_release );
    }

    void ImGuiEditor::abandonPendingDraw()
    {
        // in-flight 표시는 "렌더 스레드가 이 슬롯을 읽고 postPresent 에서 풀어 준다" 는 약속이다. 렌더 워커를 비워 세우면 그
        // 약속을 지킬 쪽이 사라지므로, 비운 쪽이 여기로 알려 준다. 이것이 없으면 다음 waitForDrawSnapshotIdle 이 영원히 돌아오지
        // 않는다(에디터 모듈 핫 리로드: destroyEditorInstance → shutdown → 무한 대기).
        _inFlightDrawSlot.store( _s_kInvalidDrawSlot, std::memory_order_release );
    }

    bool ImGuiEditor::processEvent( const NativeWindowEvent& event )
    {
        if ( _bInitialized == SW_FALSE )
            return false;

        if ( _platformBackend != nullptr )
            _platformBackend->processEvent( event );

        // ImGui 가 차지한 입력은 게임으로 넘기지 않는다. Game View 위에서는 예외다.
        const ImGuiIO& io               = ImGui::GetIO();
        const bool     bGameViewHovered = _editorContext != nullptr && _editorContext->isGameViewHovered();
        const bool     bGameViewFocused = _editorContext != nullptr && _editorContext->isGameViewFocused();

        // Simulate 는 월드만 돈다 — 게임 입력을 주지 않는다(에디터 카메라 · 패널은 위의 ImGui 처리로 이미 받았다).
        if ( EditorPlaySession::isSimulating() && ( event.isMouseInput() || event.isKeyboardInput() ) )
            return true;

        if ( event.isMouseInput() )
        {
            if ( io.WantCaptureMouse && bGameViewHovered == false )
                return true;
            return false;
        }

        if ( event.isKeyboardInput() )
        {
            if ( event.isInputRelease() == false && io.WantCaptureKeyboard && bGameViewFocused == false )
                return true;
        }

        return false;
    }

    void* ImGuiEditor::registerTexture( uint64 texture )
    {
        if ( _rendererBackend != nullptr )
            return _rendererBackend->registerTexture( texture );
        return nullptr;
    }

    void ImGuiEditor::unregisterTexture( void* pTextureID )
    {
        if ( _rendererBackend != nullptr )
            _rendererBackend->unregisterTexture( pTextureID );
    }

    void ImGuiEditor::getGameViewport( uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) const
    {
        const EditorGameView* pGameView = ( _editorContext != nullptr ) ? &_editorContext->getGameView() : nullptr;
        if ( pRenderTarget != nullptr )
            *pRenderTarget = ( pGameView != nullptr ) ? pGameView->_renderTarget : 0;
        if ( pWidth != nullptr )
            *pWidth = ( pGameView != nullptr ) ? pGameView->_width : 0;
        if ( pHeight != nullptr )
            *pHeight = ( pGameView != nullptr ) ? pGameView->_height : 0;
    }

    CameraComponent* ImGuiEditor::getViewportCamera() const
    {
        // Simulate 는 에디터 카메라를 그대로 쓴다. 게임 카메라는 플레이어가 조종하는 세션에서만.
        return EditorCamera::getViewportCamera( editor::getActiveScene(), EditorPlaySession::isPlayerActive() );
    }

    bool ImGuiEditor::isPlaying() const
    {
        // 호스트는 이 답으로 게임 모듈 업데이트를 켠다. Simulate 는 씬 틱만 하고(isPaused 가 false) 게임 모듈은 돌리지 않는다.
        return EditorPlaySession::isPlayerActive();
    }

    bool ImGuiEditor::isPaused() const
    {
        return EditorPlaySession::isPaused() && EditorPlaySession::hasPendingStep() == false;
    }

    void ImGuiEditor::stopSimulation()
    {
        EditorPlaySession::stop();
    }

    void ImGuiEditor::onHostFrameEnd()
    {
        // 씬을 여는 중에 누른 Play 는 미뤄져 있다 — 로드가 끝난 프레임에 여기서 시작한다.
        EditorPlaySession::update();
        EditorPlaySession::consumePendingStep();
    }

    bool ImGuiEditor::onWindowCloseQuery()
    {
        return EditorAssetCommands::tryBeginQuit();
    }

    void ImGuiEditor::beginFrame()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // GL 처럼 컨텍스트가 렌더 스레드 전용인 백엔드는 newFrame 을 present 훅에서 호출한다.
        if ( _rendererBackend != nullptr && _rendererBackend->requiresRenderThreadContext() == false )
            _rendererBackend->newFrame();

        if ( _platformBackend != nullptr )
            _platformBackend->newFrame();

        // 이름 붙인 레이아웃은 프레임 밖에서 읽어야 이미 있는 창 · 도킹 노드에 적용된다.
        _dockLayout.applyPendingNamedLayout();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        // 기즈모를 띄우는 패널이, 캔버스가 입력을 받을 수 있을 때 다시 켠다.
        ImGuizmo::Enable( false );
    }

    void ImGuiEditor::endFrame()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.10f, 0.10f, 0.10f, 1.00f ) );
        ImGui::RenderNotifications();
        ImGui::PopStyleColor( 1 );
        ImGui::PopStyleVar( 2 );

        ImGui::Render();
    }

    void ImGuiEditor::waitForDrawSnapshotIdle()
    {
        while ( _inFlightDrawSlot.load( std::memory_order_acquire ) != _s_kInvalidDrawSlot )
            std::this_thread::yield();
    }

    void ImGuiEditor::renderBackend( IRHIDevice* pRhiDevice, ImDrawData* pDrawData )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        if ( _rendererBackend != nullptr )
            _rendererBackend->render( pRhiDevice, pDrawData );
    }
} // namespace sw::editor

// ==============================================================================
// EditorModule C-ABI 진입점(매크로가 구현한다)
// ==============================================================================
SW_IMPLEMENT_EDITOR_MODULE( sw::editor::ImGuiEditor );
