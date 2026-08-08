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
#include "Panels/ConsolePanel.h"
#include "Panels/GameToolbarPanel.h"
#include "Panels/GameViewPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/OutlinerPanel.h"
#include "Panels/ResourceBrowserPanel.h"
#include "Panels/SequencerPanel.h"
#include "Panels/AnimationGraphPanel.h"
#include "Panels/AIGraphPanel.h"
#include "Panels/GlobalVariablesPanel.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include "Core/Graphics/RenderPass/RenderPassResource.h"
#include "Core/Common/CoreServices.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Window/NativeWindowEvent.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/File/FileUtil.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <ImGuizmo.h>

#define NOTIFY_RENDER_OUTSIDE_MAIN_WINDOW false
#if defined( SW_PLATFORM_LINUX )
	#if defined( Success )
		#undef Success
		#include <ImGuiNotify.hpp>
	#endif
#else
	#include <ImGuiNotify.hpp>
#endif
#include <fa-solid-900.h>
#include <IconsFontAwesome6.h>

namespace sw
{
	ImGuiEditor::ImGuiEditor()
		: _bInitialized{ 0 }
		, _bDockLayoutApplied{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	ImGuiEditor::~ImGuiEditor()
	{
		shutdown();
	}

	void ImGuiEditor::registerDefaultPanels()
	{
		_panels.clear();
		_panels.push_back( std::make_unique<OutlinerPanel>() );
		_panels.push_back( std::make_unique<InspectorPanel>() );
		_panels.push_back( std::make_unique<GameToolbarPanel>() );
		_panels.push_back( std::make_unique<GameViewPanel>() );
		_panels.push_back( std::make_unique<ConsolePanel>() );
		_panels.push_back( std::make_unique<ResourceBrowserPanel>() );
		_panels.push_back( std::make_unique<SequencerPanel>() );
		_panels.push_back( std::make_unique<AnimationGraphPanel>() );
		_panels.push_back( std::make_unique<AIGraphPanel>() );
		_panels.push_back( std::make_unique<GlobalVariablesPanel>() );
	}

	void ImGuiEditor::setupFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();

		const std::filesystem::path basePath = EditorUtil::resolveFontFile(
			editor::path::kBaseFontCandidates,
			sizeof( editor::path::kBaseFontCandidates ) / sizeof( editor::path::kBaseFontCandidates[0] ) );
		const std::filesystem::path koreanPath = EditorUtil::resolveFontFile(
			editor::path::kKoreanFontCandidates,
			sizeof( editor::path::kKoreanFontCandidates ) / sizeof( editor::path::kKoreanFontCandidates[0] ) );

		ImFont* baseFont = nullptr;
		BLOCK( "Base Font" )
		{
			ImFontConfig baseConfig{};
			baseConfig.OversampleH = 2;
			baseConfig.OversampleV = 1;
			// Explicit size required: ImGui asserts if MergeMode uses an explicit size
			// over a destination font that used an implicit reference size (AddFontDefault).
			baseConfig.SizePixels = editor::constant::kFontSize;

			if ( basePath.empty() == false )
			{
				baseFont = io.Fonts->AddFontFromFileTTF( basePath.string().c_str(), editor::constant::kFontSize, &baseConfig,
														 io.Fonts->GetGlyphRangesDefault() );
				SW_LOG_INFO( "[ImGuiEditor] Loaded base font: %#", basePath.string().c_str() );
			}

			if ( baseFont == nullptr )
			{
				baseFont = io.Fonts->AddFontDefault( &baseConfig );
				SW_LOG_WARNING( "[ImGuiEditor] No system UI font found — using ImGui default font." );
			}
		}

		BLOCK( "Korean Glyph Merge" )
		{
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
				SW_LOG_WARNING( "[ImGuiEditor] Korean font not found — Hangul may not render." );
			}
		}

		BLOCK( "Font Awesome 6 (ImGuiNotify icons)" )
		{
			const float				 iconFontSize  = editor::constant::kFontSize * 2.0f / 3.0f;
			static constexpr ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
			ImFontConfig			 iconsConfig{};
			iconsConfig.MergeMode		 = true;
			iconsConfig.PixelSnapH		 = true;
			iconsConfig.GlyphMinAdvanceX = iconFontSize;
			io.Fonts->AddFontFromMemoryCompressedTTF( fa_solid_900_compressed_data, fa_solid_900_compressed_size, iconFontSize, &iconsConfig,
													  iconsRanges );
		}

		io.FontDefault = baseFont;
	}

	void ImGuiEditor::setupLayoutPersistencePaths()
	{
		_imguiIniPath.clear();
		_panelsIniPath.clear();

		const std::filesystem::path imguiPath  = EditorUtil::resolveEditorConfigFile( editor::path::kImGuiIniFile );
		const std::filesystem::path panelsPath = EditorUtil::resolveEditorConfigFile( editor::path::kPanelsIniFile );
		if ( imguiPath.empty() || panelsPath.empty() )
		{
			SW_LOG_WARNING( "[ImGuiEditor] Failed to resolve Config/Editor — layout will not persist." );
			return;
		}

		_imguiIniPath  = imguiPath.string();
		_panelsIniPath = panelsPath.string();
		SW_LOG_INFO( "[ImGuiEditor] Layout persistence dir: %#", imguiPath.parent_path().string().c_str() );
	}

	void ImGuiEditor::loadPanelVisibility()
	{
		if ( _panelsIniPath.empty() || FileUtil::isFileExist( _panelsIniPath ) == false )
			return;

		std::ifstream in( _panelsIniPath );
		if ( in.is_open() == false )
		{
			SW_LOG_WARNING( "[ImGuiEditor] Failed to open panels.ini: %#", _panelsIniPath.c_str() );
			return;
		}

		std::unordered_map<std::string, bool> visibility;
		std::string							  line;
		while ( std::getline( in, line ) )
		{
			if ( line.empty() || line[0] == '#' || line[0] == ';' )
				continue;
			if ( line.front() == '[' )
				continue;

			const size_t eq = line.find( '=' );
			if ( eq == std::string::npos || eq == 0 )
				continue;

			std::string title = line.substr( 0, eq );
			std::string value = line.substr( eq + 1 );
			while ( title.empty() == false && ( title.back() == ' ' || title.back() == '\t' || title.back() == '\r' ) )
				title.pop_back();
			while ( value.empty() == false && ( value.front() == ' ' || value.front() == '\t' ) )
				value.erase( value.begin() );
			while ( value.empty() == false && ( value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ) )
				value.pop_back();

			visibility[title] = ( value == "1" || value == "true" || value == "True" );
		}

		for ( auto& panel : _panels )
		{
			if ( panel == nullptr )
				continue;
			const auto it = visibility.find( panel->getWindowTitle() );
			if ( it != visibility.end() )
				panel->setOpen( it->second );
		}

		SW_LOG_INFO( "[ImGuiEditor] Restored panel visibility from %#", _panelsIniPath.c_str() );
	}

	void ImGuiEditor::saveEditorLayout()
	{
		if ( _panelsIniPath.empty() == false )
		{
			std::ostringstream out;
			out << "# Editor panel visibility (1=open, 0=closed)\n";
			out << "[PanelVisibility]\n";
			for ( const auto& panel : _panels )
			{
				if ( panel == nullptr )
					continue;
				out << panel->getWindowTitle() << '=' << ( panel->isOpen() ? '1' : '0' ) << '\n';
			}

			const std::string text = out.str();
			if ( FileUtil::writeFile( _panelsIniPath, reinterpret_cast<const uint8*>( text.data() ), text.size() ) )
				SW_LOG_INFO( "[ImGuiEditor] Saved panel visibility to %#", _panelsIniPath.c_str() );
			else
				SW_LOG_WARNING( "[ImGuiEditor] Failed to write panels.ini: %#", _panelsIniPath.c_str() );
		}

		if ( _imguiIniPath.empty() == false && ImGui::GetCurrentContext() != nullptr )
		{
			ImGui::SaveIniSettingsToDisk( _imguiIniPath.c_str() );
			SW_LOG_INFO( "[ImGuiEditor] Saved ImGui layout to %#", _imguiIniPath.c_str() );
		}
	}

	bool ImGuiEditor::initialize( IWindow* window, IRHIDevice* rhiDevice )
	{
		SW_LOG_INFO( "ImGuiEditor::initialize Start" );
		if ( _bInitialized == true )
			return true;

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
			// Multi-viewport when backend reports ImGui hooks (DX11/12/GL/Vulkan + platform viewports).
			const RHICapabilities caps = RHIAvailability::query( rhiDevice->getBackendType() );
			if ( caps._bImGuiHooks )
				io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

			setupLayoutPersistencePaths();
			if ( _imguiIniPath.empty() == false )
				io.IniFilename = _imguiIniPath.c_str();
			else
				io.IniFilename = nullptr;

			ImGui::StyleColorsDark();
			ImGuiStyle& style				  = ImGui::GetStyle();
			style.WindowRounding			  = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		BLOCK( "Fonts Setup" )
		{
			setupFonts();
		}

		BLOCK( "Platform Backend 생성 / 초기화" )
		{
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
		}

		BLOCK( "Renderer Backend 생성 / 초기화" )
		{
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
		}

		BLOCK( "Splash / async RenderPass load then panels" )
		{
			SW_LOG_INFO( "[ImGuiEditor] Splash: loading DefaultRenderPass / ForwardPipeline..." );

			auto defaultPass = std::make_shared<RenderPassResource>();
			auto forwardPass = std::make_shared<RenderPassResource>();

			TaskHandle hDefault = core::getTaskManager().emplaceTask(
				"EditorSplash_DefaultRenderPass",
				SW_DELEGATE_LAMBDA( TaskDelegate, [defaultPass]()
				{
					SW_LOG_INFO( "[ImGuiEditor] Splash: reading DefaultRenderPass.xml" );
					defaultPass->loadFromXmlFile( "Engine/RenderPass/DefaultRenderPass.xml" );
				} ) );

			TaskHandle hForward = core::getTaskManager().emplaceTask(
				"EditorSplash_ForwardPipeline",
				SW_DELEGATE_LAMBDA( TaskDelegate, [forwardPass]()
				{
					SW_LOG_INFO( "[ImGuiEditor] Splash: reading ForwardPipeline.xml" );
					forwardPass->loadFromXmlFile( "Engine/RenderPass/ForwardPipeline.xml" );
				} ) );

			(void)hDefault;
			(void)hForward;
			core::getTaskManager().dispatch();
			core::getTaskManager().waitAll();

			SW_LOG_INFO( "[ImGuiEditor] Splash: pipelines ready (Default='%#', Forward='%#').",
						 defaultPass->getDesc()._name.c_str(),
						 forwardPass->getDesc()._name.c_str() );

			registerDefaultPanels();
			loadPanelVisibility();
			SW_LOG_INFO( "[ImGuiEditor] Splash complete — panels registered." );
		}

		_bInitialized		= true;
		_bDockLayoutApplied = false;
		return true;
	}

	void ImGuiEditor::shutdown()
	{
		if ( _bInitialized == false )
			return;

		BLOCK( "Save Editor Layout" )
		{
			saveEditorLayout();
		}

		BLOCK( "Panels Shutdown" )
		{
			for ( auto& panel : _panels )
			{
				if ( panel )
					panel->shutdown( nullptr );
			}
			_panels.clear();
		}

		BLOCK( "Renderer / Platform Backend Shutdown" )
		{
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
		}

		BLOCK( "ImGui Context Destroy" )
		{
			ImPlot::DestroyContext();
			ImGui::DestroyContext();
		}

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
		ImGuizmo::BeginFrame();
		// Panels that host a gizmo re-enable when their canvas may accept input.
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

		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Right, 0.28f, &dockRight, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Down, 0.30f, &dockBottom, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Up, 0.06f, &dockTop, &dockMain );

		ImGui::DockBuilderDockWindow( "Hierarchy", dockLeft );
		ImGui::DockBuilderDockWindow( "Inspector", dockLeft );
		ImGui::DockBuilderDockWindow( "Game Toolbar", dockTop );
		ImGui::DockBuilderDockWindow( "Game View", dockMain );
		ImGui::DockBuilderDockWindow( "Output Log", dockRight );
		ImGui::DockBuilderDockWindow( "Global Variables Control", dockRight );
		ImGui::DockBuilderDockWindow( "Animation Graph", dockRight );
		ImGui::DockBuilderDockWindow( "AI Graph", dockRight );
		ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );

		ImGui::DockBuilderFinish( id );
	}

	void ImGuiEditor::drawMainMenuBar( const EditorUIContext& ctx )
	{
		if ( ImGui::BeginMainMenuBar() == false )
			return;

		if ( ImGui::BeginMenu( "View" ) )
		{
			for ( auto& panel : _panels )
			{
				if ( panel == nullptr )
					continue;

				bool open = panel->isOpen();
				if ( ImGui::MenuItem( panel->getWindowTitle(), nullptr, open ) )
					panel->setOpen( !open );
			}
			ImGui::EndMenu();
		}

		// EngineStatus folded into status line (plan P6).
		const char* backend = ( ctx.rhiDevice != nullptr ) ? ctx.rhiDevice->getBackendName() : "n/a";
		const float statusW = 280.0f;
		ImGui::SameLine( ImGui::GetWindowWidth() - statusW );
		ImGui::TextDisabled( "RHI %s | %.0f FPS", backend, static_cast<double>( ImGui::GetIO().Framerate ) );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted( "Switch RHI: -dx11 / -dx12 / -vk / -gl" );
			const bool bVk = RHIAvailability::query( RHIBackend::Vulkan )._bEditorSupported;
			const bool bGl = RHIAvailability::query( RHIBackend::OpenGL )._bEditorSupported;
			ImGui::Text( "Vulkan editor: %s", bVk ? "yes" : "no" );
			ImGui::Text( "OpenGL editor: %s", bGl ? "yes" : "no" );
			ImGui::EndTooltip();
		}

		ImGui::EndMainMenuBar();
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
			ImGui::GetID( "EditorMainDockSpace_v5" ), viewport, ImGuiDockNodeFlags_PassthruCentralNode );

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
			if ( panel && panel->isOpen() )
				panel->preRender( rhiDevice );
		}
	}

	void ImGuiEditor::render( const EditorUIContext& ctx )
	{
		if ( _bInitialized == false )
			return;

		BLOCK( "ImGui NewFrame / Dockspace" )
		{
			beginFrame();
			drawMainMenuBar( ctx );
			beginDockspace();
		}

		BLOCK( "Editor Panels Draw" )
		{
			for ( auto& panel : _panels )
			{
				if ( panel && panel->isOpen() )
					panel->draw( ctx );
			}
		}

		BLOCK( "ImGui Render / Backend Submit" )
		{
			endFrame();
			renderBackend( ctx.rhiDevice );
		}
	}

	void* ImGuiEditor::registerTexture( RHITextureHandle texture )
	{
		if ( _rendererBackend )
			return _rendererBackend->registerTexture( texture );
		return nullptr;
	}

	void ImGuiEditor::unregisterTexture( void* textureID )
	{
		if ( _rendererBackend )
			_rendererBackend->unregisterTexture( textureID );
	}
} // namespace sw
