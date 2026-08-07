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
#include "Panels/OutlinerPanel.h"
#include "Panels/ResourceBrowserPanel.h"
#include "Panels/PlotPanel.h"
#include "Panels/GizmoPanel.h"
#include "Panels/NodeEditorPanel.h"
#include "Panels/SequencerPanel.h"
#include "Panels/NotifyPanel.h"
#include "Panels/TexInspectPanel.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include "Core/Window/NativeWindowEvent.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/File/FileUtil.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <ImGuizmo.h>

#define NOTIFY_RENDER_OUTSIDE_MAIN_WINDOW false
#include <ImGuiNotify.hpp>
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
		_panels.push_back( std::make_unique<GlobalVariablesPanel>() );
		_panels.push_back( std::make_unique<GameToolbarPanel>() );
		_panels.push_back( std::make_unique<GameViewPanel>() );
		_panels.push_back( std::make_unique<ConsolePanel>() );
		_panels.push_back( std::make_unique<ComputeTestPanel>() );
		_panels.push_back( std::make_unique<EngineStatusPanel>() );
		_panels.push_back( std::make_unique<ResourceBrowserPanel>() );
		_panels.push_back( std::make_unique<PlotPanel>() );
		_panels.push_back( std::make_unique<GizmoPanel>() );
		_panels.push_back( std::make_unique<NodeEditorPanel>() );
		_panels.push_back( std::make_unique<SequencerPanel>() );
		_panels.push_back( std::make_unique<NotifyPanel>() );
		_panels.push_back( std::make_unique<TexInspectPanel>() );
	}

	void ImGuiEditor::setupFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();

		const std::filesystem::path consolasPath = EditorUtil::resolveFontFile( editor::path::kConsolasFontFile );
		const std::filesystem::path koreanPath	 = EditorUtil::resolveFontFile( editor::path::kKoreanUiFontFile );

		ImFont* baseFont = nullptr;
		BLOCK( "Base Font (Consolas)" )
		{
			ImFontConfig baseConfig{};
			baseConfig.OversampleH = 2;
			baseConfig.OversampleV = 1;

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
				SW_LOG_WARNING( "[ImGuiEditor] Korean font (%#) not found — Hangul may not render.", editor::path::kKoreanUiFontFile );
			}
		}

		BLOCK( "Font Awesome 6 (ImGuiNotify icons)" )
		{
			const float			  iconFontSize = editor::constant::kFontSize * 2.0f / 3.0f;
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
			// Multi-viewport requires full ImGui hooks. VK/GL report _bImGuiHooks=false;
			// skip ViewportsEnable so docking stays on the main viewport only.
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

		BLOCK( "Default Panels 등록 / 표시 상태 복원" )
		{
			registerDefaultPanels();
			loadPanelVisibility();
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
		ImGuiID dockRightBot = 0;

		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Right, 0.28f, &dockRight, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Down, 0.30f, &dockBottom, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Up, 0.06f, &dockTop, &dockMain );
		ImGui::DockBuilderSplitNode( dockRight, ImGuiDir_Down, 0.45f, &dockRightBot, &dockRight );

		ImGui::DockBuilderDockWindow( "Hierarchy", dockLeft );
		ImGui::DockBuilderDockWindow( "Inspector", dockLeft );
		ImGui::DockBuilderDockWindow( "Global Variables Control", dockLeft );
		ImGui::DockBuilderDockWindow( "Game Toolbar", dockTop );
		ImGui::DockBuilderDockWindow( "Game View", dockMain );
		ImGui::DockBuilderDockWindow( "Output Log", dockRight );
		ImGui::DockBuilderDockWindow( "Compute Test", dockRight );
		ImGui::DockBuilderDockWindow( "Engine RHI Status & Command Line", dockRightBot );
		ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );

		ImGui::DockBuilderFinish( id );
	}

	void ImGuiEditor::drawMainMenuBar()
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
			drawMainMenuBar();
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
