#include "pch.h"

#include "Editor/ImGuiEditor.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"
#include "Editor/Common/Backend/IImGuiPlatformBackend.h"
#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorCamera.h"
#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorMenuBar.h"
#include "Editor/Common/Workspace/AssetEditorManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Popups/EditorPopupManager.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
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
            static void loadSplashDefaultRenderPass( const TaskArgs& args )
            {
                shared_ptr<RenderPassResource> pPass = args.get<shared_ptr<RenderPassResource>>( 0 );
                if ( pPass == nullptr )
                    return;
                SW_LOG_TRACE( "Splash: reading DefaultRenderPass.xml" );
                pPass->loadFromXmlFile( editor::getService<const EngineData>()->_defaultRenderPass );
            }

            static void loadSplashForwardPipeline( const TaskArgs& args )
            {
                shared_ptr<RenderPipelineResource> pPipeline = args.get<shared_ptr<RenderPipelineResource>>( 0 );
                if ( pPipeline == nullptr )
                    return;
                SW_LOG_TRACE( "Splash: reading ForwardPipeline.xml" );
                pPipeline->loadFromXmlFile( editor::getService<const EngineData>()->_defaultForwardPipeline );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiEditor" );

    ImGuiEditor::ImGuiEditor()
        : _platformBackend{ nullptr }
        , _rendererBackend{ nullptr }
        , _editorData{ nullptr }
        , _editorContext{ nullptr }
        , _dockLayout{}
        , _arrDrawSnapshot{}
        , _publishedDrawSlot{ 0 }
        , _inFlightDrawSlot{ _s_kInvalidDrawSlot }
        , _bInitialized{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    ImGuiEditor::~ImGuiEditor()
    {
        shutdown();
    }

    bool ImGuiEditor::initialize( IWindow* pWindow, IRHIDevice* pRhiDevice )
    {
        SW_LOG_TRACE( "Initialize start." );
        if ( _bInitialized != SW_FALSE )
            return true;

#if !defined( SW_SHIPPING )
        BLOCK( "EditorConfig host load" )
        {
            EditorConfig::loadFromHost();
            _editorData = make_unique<EditorData>();
            _editorData->loadFromHostPath( EditorConfig::getActive()._editorData );
            editor::setEditorData( _editorData.get() );
        }
#endif

        BLOCK( "ImGui Context / IO / Style" )
        {
            SW_LOG_TRACE( "Checking ImGui version and creating context" );
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImPlot::CreateContext();

            SW_LOG_TRACE( "Configuring ImGui IO" );
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            // 백엔드가 ImGui 훅을 보고하면 멀티 뷰포트 (DX11/12/GL/Vulkan + 플랫폼 뷰포트).
            const RHICapabilities caps = RHIAvailability::query( pRhiDevice->getBackendType() );
            if ( caps._bImGuiHooks )
                io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            _dockLayout.setupPersistencePaths();
            _dockLayout.applyIniFilename();

            ImGui::StyleColorsDark();
            ImGuiStyle& style                 = ImGui::GetStyle();
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        EditorUtil::setupFonts();

        BLOCK( "Platform Backend create / init" )
        {
            SW_LOG_TRACE( "Creating Platform Backend" );
            _platformBackend = IImGuiPlatformBackend::createPlatformBackend();
            if ( _platformBackend == nullptr )
            {
                SW_LOG_ERROR( "Failed to create platform backend" );
                return false;
            }

            SW_LOG_TRACE( "Initializing Platform Backend" );
            if ( _platformBackend->initialize( pWindow, pRhiDevice->getBackendType() ) == false )
            {
                SW_LOG_ERROR( "Platform backend initialization failed" );
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
                _platformBackend->shutdown();
                _platformBackend.reset();

                editor::setEditorData( nullptr );
                _editorData.reset();
                ImGui::DestroyContext();
                return false;
            }
        }

        BLOCK( "Splash / async RenderPass load then panels" )
        {
            SW_LOG_TRACE( "Splash: loading DefaultRenderPass / ForwardPipeline..." );

            const shared_ptr<RenderPassResource>     defaultPass     = sw::make_shared<RenderPassResource>();
            const shared_ptr<RenderPipelineResource> forwardPipeline = sw::make_shared<RenderPipelineResource>();

            TaskManager* pTaskManager = editor::getService<TaskManager>();
            TaskHandle   hDefault     = pTaskManager->emplaceTask(
                "EditorSplash_DefaultRenderPass",
                SW_DELEGATE_FUNCTION( TaskArgsDelegate, ImGuiEditorInternal::loadSplashDefaultRenderPass ),
                MakeTaskArgs( defaultPass ) );

            TaskHandle hForward = pTaskManager->emplaceTask(
                "EditorSplash_ForwardPipeline",
                SW_DELEGATE_FUNCTION( TaskArgsDelegate, ImGuiEditorInternal::loadSplashForwardPipeline ),
                MakeTaskArgs( forwardPipeline ) );

            TaskStageHandle stage = pTaskManager->getOrCreateStage( "EditorSplash" );
            stage.addTask( hDefault ).addTask( hForward );

            hDefault.submit();
            hForward.submit();

            pTaskManager->waitStage( stage );
        }

        BLOCK( "Register Default Windows" )
        {
            _editorContext = make_unique<EditorContext>();
            _editorContext->initialize();
            _editorContext->setRhiDevice( pRhiDevice );
            _editorContext->setRendererBackend( _rendererBackend.get() );

            _editorContext->getPanelManager().registerDefaultPanels();
            _dockLayout.loadPanelVisibility();
        }

        if ( pWindow != nullptr )
            pWindow->setCloseQueryHandler( SW_DELEGATE_METHOD( WindowCloseQueryDelegate, &ImGuiEditor::onWindowCloseQuery, this ) );

        _bInitialized = SW_TRUE;
        return true;
    }

    void ImGuiEditor::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        IWindow* pActiveWindow = IWindow::getActiveWindow();
        if ( pActiveWindow != nullptr )
            pActiveWindow->setCloseQueryHandler( {} );

        waitForDrawSnapshotIdle();
        for ( EditorDrawDataSnapshot& snapshot : _arrDrawSnapshot )
            snapshot.clear();

        _dockLayout.save();

        if ( _editorContext != nullptr )
        {
            _editorContext->destroyGameView();
            _editorContext->getPanelManager().shutdownAllPanels( nullptr );
            _editorContext->getPanelManager().clear();
        }

        if ( _rendererBackend != nullptr )
        {
            _rendererBackend->shutdown();
            _rendererBackend.reset();
        }

        if ( _platformBackend != nullptr )
        {
            _platformBackend->shutdown();
            _platformBackend.reset();
            editor::setEditorData( nullptr );
            _editorData.reset();
        }

        if ( _editorContext != nullptr )
        {
            _editorContext->shutdown();
            _editorContext.reset();
        }

        if ( ImPlot::GetCurrentContext() != nullptr )
            ImPlot::DestroyContext();

        if ( ImGui::GetCurrentContext() != nullptr )
            ImGui::DestroyContext();

        _bInitialized = SW_FALSE;
    }

    void ImGuiEditor::preRender( IRHIDevice* pRhiDevice )
    {
        if ( _bInitialized == SW_FALSE || _editorContext == nullptr )
            return;

        _editorContext->getPanelManager().preRenderOpenPanels( pRhiDevice );
    }

    void ImGuiEditor::updateUI()
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
            EditorMenuBar::draw( _dockLayout );
            _dockLayout.beginDockspace();
        }

        EditorMenuBar::processHotkeys();
        EditorMenuBar::processOpenPanelRequests();
        EditorMenuBar::processSceneSession();

        BLOCK( "Editor Panels Draw" )
        {
            if ( _editorContext != nullptr )
            {
                _editorContext->getPanelManager().drawOpenPanels();
                _editorContext->getPopupManager().drawOpenPopups();
                _editorContext->getNotificationManager().updateAndDraw( ImGui::GetIO().DeltaTime, 1920.0f, 1080.0f );
            }
        }

        BLOCK( "ImGui EndFrame / Platform Windows Update" )
        {
            endFrame();

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
                // 플랫폼(OS 윈도우) 갱신은 항상 UI 스레드에서 한다.
                // 보조(플로팅) 뷰포트의 GPU 렌더·present 는 단일 스레드 호출을 전제하는
                // imgui 1.92 뷰포트 관리 때문에 한 스레드에서만 돌려야 하며,
                // GL 이면 그 스레드는 렌더 스레드다(아래 render() 에서 처리).
                ImGui::UpdatePlatformWindows();
                if ( bRenderThreadCtx == false )
                    ImGui::RenderPlatformWindowsDefault();
            }

            const uint32 writeSlot =
                ( _publishedDrawSlot.load( std::memory_order_acquire ) + 1u ) % _s_kDrawSnapshotCount;
            while ( _inFlightDrawSlot.load( std::memory_order_acquire ) == writeSlot )
                std::this_thread::yield();

            _arrDrawSnapshot[writeSlot].capture();
            _publishedDrawSlot.store( writeSlot, std::memory_order_release );

            // 이 프레임을 "렌더 대기" 상태로 표시한다. 다음 updateUI 는 상단 waitForDrawSnapshotIdle
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
        if ( slot < _s_kDrawSnapshotCount )
        {
            ImDrawData* pDrawData = _arrDrawSnapshot[slot].getMainDrawData();
            if ( pDrawData != nullptr )
                renderBackend( pRhiDevice, pDrawData );
        }

        // 보조(플로팅) 뷰포트도 GL 이면 여기 렌더 스레드에서 렌더·present 한다.
        // (UI 스레드는 updateUI 상단 waitForDrawSnapshotIdle 에서 막혀 있어 ImGui 상태가 안정적이다)
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
        // 보조 뷰포트는 updateUI 에서 UI 스레드가 이미 렌더·present 했다.
        _inFlightDrawSlot.store( _s_kInvalidDrawSlot, std::memory_order_release );
    }

    bool ImGuiEditor::processEvent( const NativeWindowEvent& event )
    {
        if ( _bInitialized == SW_FALSE )
            return false;

        if ( _platformBackend != nullptr )
            _platformBackend->processEvent( event );

        // ImGui가 점유한 입력은 게임으로 넘기지 않습니다. Game View 위에서는 예외.
        const ImGuiIO& io               = ImGui::GetIO();
        const bool     bGameViewHovered = _editorContext != nullptr && _editorContext->isGameViewHovered();
        const bool     bGameViewFocused = _editorContext != nullptr && _editorContext->isGameViewFocused();

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
        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return nullptr;
        return EditorCamera::getViewportCamera( pSceneManager->getActiveScene(), EditorPlaySession::isPlaying() );
    }

    bool ImGuiEditor::isPlaying() const
    {
        return EditorPlaySession::isPlaying();
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

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        // 기즈모를 호스트하는 패널은 캔버스가 입력을 받을 수 있을 때 다시 켭니다.
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
// EditorModule C-ABI 진입점 매크로 자동 구현
// ==============================================================================
SW_IMPLEMENT_EDITOR_MODULE( sw::editor::ImGuiEditor );
