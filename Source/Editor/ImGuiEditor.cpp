#include "pch.h"

#include "Editor/ImGuiEditor.h"

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
#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>
#include <ImGuiNotify.hpp>
#include <ImGuizmo.h>
#include <implot.h>
#include <thread>

namespace sw::editor
{
	namespace
	{
		struct ImGuiEditorInternal
		{
			static constexpr uint32 kInvalidDrawSlot   = 0xFFFFFFFFu;
			static constexpr uint32 kDrawSnapshotCount = 2;
			static void				loadSplashDefaultRenderPass( const TaskArgs& args )
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
		, _inFlightDrawSlot{ ImGuiEditorInternal::kInvalidDrawSlot }
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
			ImGuiStyle& style				  = ImGui::GetStyle();
			style.WindowRounding			  = 0.0f;
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

			const shared_ptr<RenderPassResource>	 defaultPass	 = sw::make_shared<RenderPassResource>();
			const shared_ptr<RenderPipelineResource> forwardPipeline = sw::make_shared<RenderPipelineResource>();

			TaskManager* pTaskManager = editor::getService<TaskManager>();
			TaskHandle	 hDefault	  = pTaskManager->emplaceTask(
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
		_arrDrawSnapshot[0].clear();
		_arrDrawSnapshot[1].clear();

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

			ImGuiIO& io = ImGui::GetIO();
			if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
				ImGui::UpdatePlatformWindows();

			const uint32 writeSlot = 1u - _publishedDrawSlot.load( std::memory_order_acquire );
			while ( _inFlightDrawSlot.load( std::memory_order_acquire ) == writeSlot )
				std::this_thread::yield();

			_arrDrawSnapshot[writeSlot].capture();
			_publishedDrawSlot.store( writeSlot, std::memory_order_release );
		}
	}

	void ImGuiEditor::render( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == SW_FALSE || pRhiDevice == nullptr )
			return;

		const uint32 slot = _publishedDrawSlot.load( std::memory_order_acquire );
		_inFlightDrawSlot.store( slot, std::memory_order_release );
		if ( slot >= ImGuiEditorInternal::kDrawSnapshotCount )
			return;

		ImDrawData* pDrawData = _arrDrawSnapshot[slot].getMainDrawData();
		if ( pDrawData == nullptr )
			return;

		renderBackend( pRhiDevice, pDrawData );
	}

	void ImGuiEditor::postPresent( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == SW_FALSE || pRhiDevice == nullptr )
		{
			_inFlightDrawSlot.store( ImGuiEditorInternal::kInvalidDrawSlot, std::memory_order_release );
			return;
		}

		renderPlatformWindows( pRhiDevice );
		_inFlightDrawSlot.store( ImGuiEditorInternal::kInvalidDrawSlot, std::memory_order_release );
	}

	bool ImGuiEditor::processEvent( const NativeWindowEvent& event )
	{
		if ( _bInitialized == SW_FALSE )
			return false;

		if ( _platformBackend != nullptr )
			_platformBackend->processEvent( event );

		// ImGui가 점유한 입력은 게임으로 넘기지 않습니다. Game View 위에서는 예외.
		const ImGuiIO& io				= ImGui::GetIO();
		const bool	   bGameViewHovered = _editorContext != nullptr && _editorContext->isGameViewHovered();
		const bool	   bGameViewFocused = _editorContext != nullptr && _editorContext->isGameViewFocused();

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

		if ( _rendererBackend != nullptr )
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
		while ( _inFlightDrawSlot.load( std::memory_order_acquire ) != ImGuiEditorInternal::kInvalidDrawSlot )
			std::this_thread::yield();
	}

	void ImGuiEditor::renderBackend( IRHIDevice* pRhiDevice, ImDrawData* pDrawData )
	{
		if ( _bInitialized == SW_FALSE )
			return;

		if ( _rendererBackend != nullptr )
			_rendererBackend->render( pRhiDevice, pDrawData );
	}

	void ImGuiEditor::renderPlatformWindows( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == SW_FALSE || pRhiDevice == nullptr )
			return;

		const uint32 slot = _publishedDrawSlot.load( std::memory_order_acquire );
		if ( slot >= ImGuiEditorInternal::kDrawSnapshotCount )
			return;

		_arrDrawSnapshot[slot].presentExtraViewports();

		if ( pRhiDevice->getBackendType() == RHIBackend::OpenGL )
			pRhiDevice->bindGraphicsContext();
	}
} // namespace sw::editor

// ==============================================================================
// EditorModule C-ABI 진입점 매크로 자동 구현
// ==============================================================================
SW_IMPLEMENT_EDITOR_MODULE( sw::editor::ImGuiEditor );
