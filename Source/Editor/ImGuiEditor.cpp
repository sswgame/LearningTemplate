#include "pch.h"

#include "Editor/ImGuiEditor.h"

#include "Core/Task/TaskManager.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorMenuBar.h"
#include "Editor/Common/Platform/IImGuiPlatformBackend.h"
#include "Editor/Common/Platform/IImGuiRendererBackend.h"
#include "Editor/Common/Workspace/AssetEditorRegistry.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Inspector/ComponentDrawerRegistry.h"
#include "Editor/Inspector/DefaultPropertyDrawers.h"
#include "Editor/Panels/EditorPanelRegistry.h"
#include "Editor/Popups/BoneHierarchyPopup.h"
#include "Editor/Popups/CommandPalettePopup.h"
#include "Editor/Popups/EditorNotificationManager.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "RuntimeAPI/Export/EditorModuleExports.h"
#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>
#include <ImGuiNotify.hpp>
#include <ImGuizmo.h>
#include <implot.h>

// X11 매크로(None/Success/…)가 ImGui / notify 식별자와 충돌합니다 — 에디터 전용.

#if defined( SW_PLATFORM_LINUX )
	#ifdef None
		#undef None
	#endif
	#ifdef Success
		#undef Success
	#endif
	#ifdef Status
		#undef Status
	#endif
	#ifdef Always
		#undef Always
	#endif
	#ifdef Complex
		#undef Complex
	#endif
#endif

namespace sw
{
	ImGuiEditor::ImGuiEditor()
		: _platformBackend{ nullptr }
		, _rendererBackend{ nullptr }
		, _editorData{ nullptr }
		, _editorContext{ nullptr }
		, _dockLayout{}
		, _bInitialized{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	ImGuiEditor::~ImGuiEditor()
	{
		shutdown();
	}

	bool ImGuiEditor::initialize( IWindow* pWindow, IRHIDevice* pRhiDevice )
	{
		SW_LOG_INFO( "ImGuiEditor::initialize Start" );
		if ( _bInitialized != 0 )
			return true;

		registerDefaultPropertyDrawers();
		ComponentDrawerRegistry::registerDefaultDrawers();
		AssetEditorRegistry::registerDefaultMappings();

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
			SW_LOG_INFO( "Checking ImGui version and creating context" );
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImPlot::CreateContext();

			SW_LOG_INFO( "Configuring ImGui IO" );
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

		BLOCK( "Fonts Setup" )
		{
			EditorUtil::setupFonts();
		}

		BLOCK( "Platform Backend create / init" )
		{
			SW_LOG_INFO( "Creating Platform Backend" );
			_platformBackend = IImGuiPlatformBackend::createPlatformBackend();
			if ( _platformBackend == nullptr )
			{
				SW_LOG_ERROR( "Failed to create platform backend" );
				return false;
			}

			SW_LOG_INFO( "Initializing Platform Backend" );
			if ( _platformBackend->initialize( pWindow, pRhiDevice->getBackendType() ) == false )
			{
				SW_LOG_ERROR( "Platform backend initialization failed" );
				return false;
			}
		}

		BLOCK( "Renderer Backend create / init" )
		{
			SW_LOG_INFO( "Creating Renderer Backend" );
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
			SW_LOG_INFO( "[ImGuiEditor] Splash: loading DefaultRenderPass / ForwardPipeline..." );

			const shared_ptr<RenderPassResource>	 defaultPass	 = sw::make_shared<RenderPassResource>();
			const shared_ptr<RenderPipelineResource> forwardPipeline = sw::make_shared<RenderPipelineResource>();

			TaskHandle hDefault = editor::getService<TaskManager>()->emplaceTask( "EditorSplash_DefaultRenderPass",
																				  SW_DELEGATE_LAMBDA( TaskDelegate, [defaultPass]()
			{
				SW_LOG_INFO( "[ImGuiEditor] Splash: reading DefaultRenderPass.xml" );
				defaultPass->loadFromXmlFile( editor::getService<const EngineData>()->_defaultRenderPass );
			} ) );

			TaskHandle hForward = editor::getService<TaskManager>()->emplaceTask( "EditorSplash_ForwardPipeline",
																				  SW_DELEGATE_LAMBDA( TaskDelegate, [forwardPipeline]()
			{
				SW_LOG_INFO( "[ImGuiEditor] Splash: reading ForwardPipeline.xml" );
				forwardPipeline->loadFromXmlFile( editor::getService<const EngineData>()->_defaultForwardPipeline );
			} ) );

			TaskStageHandle stage = editor::getService<TaskManager>()->getOrCreateStage( "EditorSplash" );
			stage.addTask( hDefault ).addTask( hForward );

			hDefault.submit();
			hForward.submit();

			editor::getService<TaskManager>()->waitStage( stage );
		}

		BLOCK( "Register Default Windows" )
		{
			_editorContext = make_unique<EditorContext>();
			_editorContext->initialize();
			_editorContext->setRhiDevice( pRhiDevice );
			_editorContext->setRendererBackend( _rendererBackend.get() );

			EditorPanelRegistry::registerDefaultPanels();
			_dockLayout.loadPanelVisibility();
		}

		_bInitialized = true;
		return true;
	}

	void ImGuiEditor::shutdown()
	{
		if ( _bInitialized == false )
			return;

		_dockLayout.save();

		if ( _editorContext != nullptr )
			_editorContext->destroyGameView();

		EditorPanelRegistry::shutdownAllPanels( nullptr );
		EditorPanelRegistry::clear();

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

		if ( ImGui::GetCurrentContext() != nullptr )
			ImGui::DestroyContext();

		_bInitialized = false;
	}

	void ImGuiEditor::preRender( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == false )
			return;

		EditorPanelRegistry::preRenderOpenPanels( pRhiDevice );
	}

	void ImGuiEditor::updateUI()
	{
		if ( _bInitialized == false )
			return;

		if ( _editorContext != nullptr )
		{
			_editorContext->setGameViewFocused( false );
			_editorContext->setGameViewHovered( false );
		}

		BLOCK( "ImGui NewFrame / Dockspace" )
		{
			beginFrame();
			editor::drawMainMenuBar( _dockLayout );
			_dockLayout.beginDockspace();
		}

		BLOCK( "Editor Hotkeys" )
		{
			editor::processMenuHotkeys();
		}

		BLOCK( "Open Panel Requests" )
		{
			editor::processOpenPanelRequests();
		}

		BLOCK( "Open Scene Requests" )
		{
			editor::processPendingSceneLoad();
		}

		BLOCK( "Editor Panels Draw" )
		{
			EditorPanelRegistry::drawOpenPanels();
			drawBoneHierarchyPopup();
			CommandPalettePopup::draw();
			EditorNotificationManager::updateAndDraw( ImGui::GetIO().DeltaTime, 1920.0f, 1080.0f );
		}

		BLOCK( "ImGui EndFrame / Platform Windows Update" )
		{
			endFrame();

			ImGuiIO& io = ImGui::GetIO();
			if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
			{
				// 메인 스레드에서 플랫폼 윈도우(HWND)를 생성, 위치 이동, 파괴합니다.
				ImGui::UpdatePlatformWindows();
			}
		}
	}

	void ImGuiEditor::render( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == false || pRhiDevice == nullptr )
			return;

		ImDrawData* pDrawData = ImGui::GetDrawData();
		if ( pDrawData == nullptr )
			return;

		BLOCK( "ImGui Render / Backend Submit" )
		{
			renderBackend( pRhiDevice );
		}
	}

	void ImGuiEditor::postPresent( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == false || pRhiDevice == nullptr )
			return;

		ImDrawData* pDrawData = ImGui::GetDrawData();
		if ( pDrawData == nullptr )
			return;

		renderPlatformWindows( pRhiDevice );
	}

	bool ImGuiEditor::processEvent( const NativeWindowEvent& event )
	{
		if ( _bInitialized == false )
			return false;

		if ( _platformBackend != nullptr )
			_platformBackend->processEvent( event );

		// ImGui가 점유한 입력은 게임으로 넘기지 않습니다. Game View 위에서는 예외.
		const ImGuiIO& io				= ImGui::GetIO();
		const bool	   bGameViewHovered = _editorContext != nullptr && _editorContext->isGameViewHovered();
		const bool	   bGameViewFocused = _editorContext != nullptr && _editorContext->isGameViewFocused();

		if ( event.isMouseInput() )
		{
			if ( event.isInputRelease() == false && io.WantCaptureMouse && bGameViewHovered == false )
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

	void* ImGuiEditor::registerTexture( RHITextureHandle texture )
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

	void ImGuiEditor::beginFrame()
	{
		if ( _bInitialized == false )
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
		if ( _bInitialized == false )
			return;

		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
		ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.10f, 0.10f, 0.10f, 1.00f ) );
		ImGui::RenderNotifications();
		ImGui::PopStyleColor( 1 );
		ImGui::PopStyleVar( 2 );

		ImGui::Render();
	}

	void ImGuiEditor::renderBackend( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == false )
			return;

		if ( _rendererBackend != nullptr )
			_rendererBackend->render( pRhiDevice );
	}

	void ImGuiEditor::renderPlatformWindows( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == false || pRhiDevice == nullptr )
			return;

		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) == 0 )
			return;

		// Present 복원 전에 플랫폼 뷰포트(멀티 뷰포트) GPU 버퍼를 렌더/출력합니다.
		ImGui::RenderPlatformWindowsDefault();

		// 멀티 뷰포트 GL 백엔드는 MakeCurrent를 바꾸므로 메인 디바이스 컨텍스트를 복원합니다.
		if ( pRhiDevice->getBackendType() == RHIBackend::OpenGL )
			pRhiDevice->bindGraphicsContext();
	}
} // namespace sw

// ==============================================================================
// EditorModule C-ABI 진입점 매크로 자동 구현
// ==============================================================================
SW_IMPLEMENT_EDITOR_MODULE( sw::ImGuiEditor );
