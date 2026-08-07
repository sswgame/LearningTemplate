/**
 * @file ImGuiEditor.cpp
 * @brief ImGui 에디터 셸 구현
 */
#include "ImGuiEditor.h"
#include "Backend/IImGuiPlatformBackend.h"
#include "Backend/IImGuiRendererBackend.h"
#include "Panels/ComputeTestPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/DemoPanel.h"
#include "Panels/EngineStatusPanel.h"
#include "Panels/GameToolbarPanel.h"
#include "Panels/GameViewPanel.h"
#include "Panels/GlobalVariablesPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ResourceBrowserPanel.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Utility/Log/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformHeaders.h"
#endif

namespace sw
{
	ImGuiEditor::ImGuiEditor() = default;

	ImGuiEditor::~ImGuiEditor()
	{
		shutdown();
	}

	void ImGuiEditor::registerDefaultPanels()
	{
		_panels.clear();
		_panels.push_back( std::make_unique<InspectorPanel>() );
		_panels.push_back( std::make_unique<GlobalVariablesPanel>() );
		_panels.push_back( std::make_unique<GameToolbarPanel>() );
		_panels.push_back( std::make_unique<GameViewPanel>() );
		_panels.push_back( std::make_unique<ConsolePanel>() );
		_panels.push_back( std::make_unique<ComputeTestPanel>() );
		_panels.push_back( std::make_unique<EngineStatusPanel>() );
		_panels.push_back( std::make_unique<ResourceBrowserPanel>() );
		_panels.push_back( std::make_unique<DemoPanel>() );
	}

	bool ImGuiEditor::initialize( IWindow* window, IRHIDevice* rhiDevice )
	{
		SW_LOG_INFO( "ImGuiEditor::initialize Start" );
		if ( _bInitialized == true )
			return true;

		SW_LOG_INFO( "Checking ImGui version and creating context" );
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		SW_LOG_INFO( "Configuring ImGui IO" );
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding			   = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;

		SW_LOG_INFO( "Creating Platform Backend" );
		_platformBackend = IImGuiPlatformBackend::createPlatformBackend();
		if ( !_platformBackend )
		{
			SW_LOG_ERROR( "Failed to create platform backend" );
			return false;
		}

		SW_LOG_INFO( "Initializing Platform Backend" );
		if ( !_platformBackend->initialize( window, rhiDevice->getBackendType() ) )
		{
			SW_LOG_ERROR( "Platform backend initialization failed" );
			return false;
		}

		SW_LOG_INFO( "Creating Renderer Backend" );
		_rendererBackend = IImGuiRendererBackend::createRendererBackend( rhiDevice->getBackendType() );
		if ( !_rendererBackend || !_rendererBackend->initialize( rhiDevice ) )
		{
			SW_LOG_ERROR( "Renderer backend initialization failed" );
			_platformBackend->shutdown();
			_platformBackend.reset();
			ImGui::DestroyContext();
			return false;
		}

		registerDefaultPanels();

		_bInitialized		= true;
		_bDockLayoutApplied = false;
		return true;
	}

	void ImGuiEditor::shutdown()
	{
		if ( _bInitialized == false )
			return;

		for ( auto& panel : _panels )
		{
			if ( panel )
				panel->shutdown( nullptr );
		}
		_panels.clear();

		if ( _rendererBackend )
		{
			_rendererBackend->shutdown();
			_rendererBackend.reset();
		}

		if ( _platformBackend )
		{
			_platformBackend->shutdown();
			_platformBackend.reset();
		}

		ImGui::DestroyContext();

		_bInitialized		= false;
		_bDockLayoutApplied = false;
	}

	void ImGuiEditor::beginFrame()
	{
		if ( _bInitialized == false )
			return;

		if ( _rendererBackend )
			_rendererBackend->newFrame();

		if ( _platformBackend )
			_platformBackend->newFrame();

		ImGui::NewFrame();
	}

	void ImGuiEditor::endFrame()
	{
		if ( _bInitialized == false )
			return;

		ImGui::Render();
	}

	void ImGuiEditor::renderBackend( IRHIDevice* rhiDevice )
	{
		if ( _bInitialized == false )
			return;

		if ( _rendererBackend )
			_rendererBackend->render( rhiDevice );

		ImGuiIO& io = ImGui::GetIO();
		if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

#if defined( SW_PLATFORM_WINDOWS )
			if ( rhiDevice->getBackendType() == sw::RHIBackend::OpenGL )
			{
				void* hDC = rhiDevice->getNativeDevice();
				void* hRC = rhiDevice->getNativeContext();
				if ( hDC && hRC )
					wglMakeCurrent( static_cast<HDC>( hDC ), static_cast<HGLRC>( hRC ) );
			}
#endif
		}
	}

	bool ImGuiEditor::processEvent( const NativeWindowEvent& event )
	{
		if ( _bInitialized == false )
			return false;

		if ( _platformBackend )
			return _platformBackend->processEvent( event );

		return false;
	}

	void ImGuiEditor::applyDefaultDockLayout( uint32 dockspaceId )
	{
		const ImGuiID		 id		  = static_cast<ImGuiID>( dockspaceId );
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::DockBuilderRemoveNode( id );
		ImGui::DockBuilderAddNode( id, ImGuiDockNodeFlags_DockSpace );
		ImGui::DockBuilderSetNodeSize( id, viewport->WorkSize );

		ImGuiID dockMain	 = id;
		ImGuiID dockLeft	 = 0;
		ImGuiID dockRight	 = 0;
		ImGuiID dockBottom	 = 0;
		ImGuiID dockTop		 = 0;
		ImGuiID dockRightBot = 0;

		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Right, 0.28f, &dockRight, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Down, 0.30f, &dockBottom, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Up, 0.06f, &dockTop, &dockMain );
		ImGui::DockBuilderSplitNode( dockRight, ImGuiDir_Down, 0.45f, &dockRightBot, &dockRight );

		ImGui::DockBuilderDockWindow( "RHI & Engine Inspector", dockLeft );
		ImGui::DockBuilderDockWindow( "Global Variables Control", dockLeft );
		ImGui::DockBuilderDockWindow( "Game Toolbar", dockTop );
		ImGui::DockBuilderDockWindow( "Game View", dockMain );
		ImGui::DockBuilderDockWindow( "Live Coding & Console Log", dockRight );
		ImGui::DockBuilderDockWindow( "Compute Test", dockRight );
		ImGui::DockBuilderDockWindow( "Engine RHI Status & Command Line", dockRightBot );
		ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );
		ImGui::DockBuilderDockWindow( "Dear ImGui Demo", dockBottom );

		ImGui::DockBuilderFinish( id );
	}

	void ImGuiEditor::beginDockspace()
	{
		if ( _bInitialized == false )
			return;

		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable ) == 0 )
			return;

		const ImGuiViewport* viewport	 = ImGui::GetMainViewport();
		const ImGuiID		 dockspaceId = ImGui::DockSpaceOverViewport(
			ImGui::GetID( "EditorMainDockSpace_v2" ), viewport, ImGuiDockNodeFlags_PassthruCentralNode );

		if ( _bDockLayoutApplied == false )
		{
			ImGuiDockNode* node = ImGui::DockBuilderGetNode( dockspaceId );
			const bool	   empty =
				( node == nullptr ) || ( node->IsSplitNode() == false && node->Windows.Size == 0 );
			if ( empty )
				applyDefaultDockLayout( static_cast<uint32>( dockspaceId ) );
			_bDockLayoutApplied = true;
		}
	}

	void ImGuiEditor::preRender( IRHIDevice* rhiDevice )
	{
		if ( _bInitialized == false )
			return;

		for ( auto& panel : _panels )
		{
			if ( panel )
				panel->preRender( rhiDevice );
		}
	}

	void ImGuiEditor::render( const EditorUIContext& ctx )
	{
		if ( _bInitialized == false )
			return;

		beginFrame();
		beginDockspace();

		for ( auto& panel : _panels )
		{
			if ( panel )
				panel->draw( ctx );
		}

		endFrame();
		renderBackend( ctx.rhiDevice );
	}

	void* ImGuiEditor::registerTexture( RHITextureHandle texture )
	{
		if ( _rendererBackend )
			return _rendererBackend->registerTexture( texture );
		return nullptr;
	}
}
