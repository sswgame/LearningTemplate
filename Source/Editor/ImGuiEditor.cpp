/**
 * @file ImGuiEditor.cpp
 * @brief ImGui 에디터 셸 구현
 */
#include "ImGuiEditor.h"
#include "EditorDefines.h"
#include "EditorUtil.h"
#include "Backend/IImGuiPlatformBackend.h"
#include "Backend/IImGuiRendererBackend.h"
#include "Panels/IEditorPanel.h"
#include "Panels/ComputeTestPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/EngineStatusPanel.h"
#include "Panels/GameToolbarPanel.h"
#include "Panels/GameViewPanel.h"
#include "Panels/GlobalVariablesPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ResourceBrowserPanel.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Window/NativeWindowEvent.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Utility/Log/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

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
	}

	void ImGuiEditor::setupFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();

		const std::filesystem::path consolasPath = EditorUtil::resolveFontFile( editor::path::kConsolasFontFile );
		const std::filesystem::path koreanPath	 = EditorUtil::resolveFontFile( editor::path::kKoreanUiFontFile );

		ImFontConfig baseConfig{};
		baseConfig.OversampleH = 2;
		baseConfig.OversampleV = 1;

		ImFont* baseFont = nullptr;
		if ( consolasPath.empty() == false )
		{
			baseFont = io.Fonts->AddFontFromFileTTF( consolasPath.string().c_str(), editor::constant::kFontSize, &baseConfig,
													 io.Fonts->GetGlyphRangesDefault() );
			SW_LOG_INFO( "[ImGuiEditor] Loaded Consolas: %#", consolasPath.string().c_str() );
		}

		if ( baseFont == nullptr )
		{
			baseFont = io.Fonts->AddFontDefault( &baseConfig );
			SW_LOG_WARNING( "[ImGuiEditor] Consolas not found — using ImGui default font." );
		}

		if ( koreanPath.empty() == false )
		{
			ImFontConfig mergeConfig{};
			mergeConfig.MergeMode	= true;
			mergeConfig.OversampleH = 2;
			mergeConfig.OversampleV = 1;
			mergeConfig.PixelSnapH	= true;
			io.Fonts->AddFontFromFileTTF( koreanPath.string().c_str(), editor::constant::kFontSize, &mergeConfig,
										  io.Fonts->GetGlyphRangesKorean() );
			SW_LOG_INFO( "[ImGuiEditor] Merged Korean glyphs from: %#", koreanPath.string().c_str() );
		}
		else
		{
			SW_LOG_WARNING( "[ImGuiEditor] Korean font (%#) not found — Hangul may not render.", editor::path::kKoreanUiFontFile );
		}

		io.FontDefault = baseFont;
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
		// Vulkan 멀티 뷰포트 백엔드는 아직 미연동. DX12는 Present 이후 postPresent + 0-size 가드로 지원.
		if ( rhiDevice->getBackendType() != RHIBackend::Vulkan )
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding			   = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;

		setupFonts();

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
	}

	void ImGuiEditor::renderPlatformWindows( IRHIDevice* rhiDevice )
	{
		if ( _bInitialized == false || rhiDevice == nullptr )
			return;

		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) == 0 )
			return;

		// 공식 예제와 같이 메인 Present 이후에 보조 뷰포트를 제출한다.
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

#if defined( SW_PLATFORM_WINDOWS )
		if ( rhiDevice->getBackendType() == RHIBackend::OpenGL )
		{
			void* hDC = rhiDevice->getNativeDevice();
			void* hRC = rhiDevice->getNativeContext();
			if ( hDC && hRC )
				wglMakeCurrent( static_cast<HDC>( hDC ), static_cast<HGLRC>( hRC ) );
		}
#else
		(void)rhiDevice;
#endif
	}

	void ImGuiEditor::postPresent( IRHIDevice* rhiDevice )
	{
		renderPlatformWindows( rhiDevice );
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
		ImGui::DockBuilderDockWindow( "Output Log", dockRight );
		ImGui::DockBuilderDockWindow( "Compute Test", dockRight );
		ImGui::DockBuilderDockWindow( "Engine RHI Status & Command Line", dockRightBot );
		ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );

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
			ImGui::GetID( "EditorMainDockSpace_v4" ), viewport, ImGuiDockNodeFlags_PassthruCentralNode );

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
