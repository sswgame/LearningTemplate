#include "pch.h"

#include "Editor/Backend/ImGuiEditor.h"

#include "Editor/Backend/IImGuiPlatformBackend.h"
#include "Editor/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/EditorContext.h"
#include "Editor/Config/EditorConfig.h"
#include "Editor/Config/EditorData.h"
#include "Editor/EditorUtil.h"
#include "Editor/Overlay/BoneHierarchyPopup.h"
#include "Editor/Overlay/CommandPaletteWindow.h"
#include "Editor/Overlay/EditorNotificationManager.h"
#include "Editor/Overlay/EditorTransportBar.h"
#include "Editor/Property/ComponentDrawerRegistry.h"
#include "Editor/Property/DefaultPropertyDrawers.h"
#include "Editor/Windows/EditorWindowRegistry.h"
#include "Editor/Windows/IEditorWindow.h"
#include "Editor/Workspace/AssetEditorRegistry.h"
#include "Editor/Workspace/EditorWorkspace.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/File/KeyValueFile.h"
#include "Engine/Utility/Task/TaskManager.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "RuntimeAPI/EditorService.h"
#include "RuntimeAPI/EditorUIContext.h"

#include <sw/config/ConfigConstants.h>

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

#include <imgui.h>

#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <ImSequencer.h>
#include <implot.h>

#define NOTIFY_RENDER_OUTSIDE_MAIN_WINDOW false
#include <ImGuiNotify.hpp>
#include <IconsFontAwesome6.h>
#include <fa_solid_900.h>

namespace sw
{
	ImGuiEditor::ImGuiEditor()
		: _platformBackend{ nullptr }
		, _rendererBackend{ nullptr }
		, _editorData{ nullptr }
		, _imguiIniPath{}
		, _windowsIniPath{}
		, _bInitialized{ 0 }
		, _bDockLayoutApplied{ 0 }
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
			EditorConfig cfg{};
			string		 jsonStr;
			if ( FileUtil::readTextFile( config::kFileRuntimeEditorConfig, jsonStr ) )
			{
				if ( JsonSerializer::deserialize( &cfg, *EditorConfig::StaticType(), jsonStr ) )
					SW_LOG_INFO( "[Editor] EditorConfig source=file" );
				else
					SW_LOG_WARNING( "[Editor] EditorConfig deserialize failed — cpp defaults" );
			}
			else
				SW_LOG_WARNING( "[Editor] EditorConfig missing — cpp defaults" );
			EditorConfig::setActive( cfg );
			_editorData = make_unique<EditorData>();
			_editorData->loadFromHostPath( cfg._editorData );
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

			registerDefaultWindows();
			loadWindowVisibility();
		}

		_bInitialized		= true;
		_bDockLayoutApplied = false;
		return true;
	}

	void ImGuiEditor::shutdown()
	{
		if ( _bInitialized == false )
			return;

		saveEditorLayout();

		for ( const EditorWindowEntry& entry : EditorWindowRegistry::getWindows() )
		{
			if ( entry._pInstance != nullptr )
				entry._pInstance->shutdown( nullptr );
		}
		EditorWindowRegistry::clear();

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
		{
			ImGui::DestroyContext();
		}

		_bInitialized		= false;
		_bDockLayoutApplied = false;
	}

	void ImGuiEditor::preRender( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == false )
			return;

		for ( const EditorWindowEntry& entry : EditorWindowRegistry::getWindows() )
		{
			if ( entry._pInstance && entry._pInstance->isOpen() )
				entry._pInstance->preRender( pRhiDevice );
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
			drawEditorTransportBar( ctx );
			beginDockspace();
		}

		BLOCK( "Editor Hotkeys" )
		{
			ImGuiIO& io = ImGui::GetIO();
			if ( io.WantTextInput == false && ( io.KeyCtrl || io.KeySuper ) )
			{
				if ( ImGui::IsKeyPressed( ImGuiKey_Z, false ) )
					editor::getService<CommandStack>()->undo();
				if ( ImGui::IsKeyPressed( ImGuiKey_Y, false ) )
					editor::getService<CommandStack>()->redo();
				if ( ImGui::IsKeyPressed( ImGuiKey_P, false ) || ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
					CommandPaletteWindow::toggle();
			}
		}

		BLOCK( "Open Window Requests" )
		{
			string openTitle;
			if ( EditorWorkspace::consumeOpenWindow( openTitle ) )
			{
				if ( EditorWindowRegistry::setWindowOpen( openTitle, true ) )
				{
					ImGui::SetWindowFocus( openTitle.c_str() );
				}
			}
		}

		BLOCK( "Editor Windows Draw" )
		{
			for ( const EditorWindowEntry& entry : EditorWindowRegistry::getWindows() )
			{
				if ( entry._pInstance && entry._pInstance->isOpen() )
					entry._pInstance->draw( ctx );
			}
			drawBoneHierarchyPopup();
			CommandPaletteWindow::draw();
			EditorNotificationManager::updateAndDraw( ImGui::GetIO().DeltaTime, 1920.0f, 1080.0f );
		}

		BLOCK( "ImGui Render / Backend Submit" )
		{
			endFrame();
			auto* const pRhiDevice = static_cast<IRHIDevice*>( ctx._pRHIDevice );
			renderBackend( pRhiDevice );
		}
	}

	void ImGuiEditor::postPresent( IRHIDevice* pRhiDevice )
	{
		renderPlatformWindows( pRhiDevice );
	}

	bool ImGuiEditor::processEvent( const NativeWindowEvent& event, const EditorUIContext* pContext )
	{
		if ( _bInitialized == false )
			return false;

		bool bConsumed = false;
		if ( _platformBackend )
			bConsumed = _platformBackend->processEvent( event );

		// ImGui 캡처 상태(마우스/키보드 점유 여부)에 따른 게임 입력 강제 차단 필터링
#if defined( SW_PLATFORM_WINDOWS )
		const ImGuiIO& io		   = ImGui::GetIO();
		const uint32   msg		   = event._message;
		const bool	   bIsMouse	   = ( msg >= 0x0200 && msg <= 0x020E );
		const bool	   bIsKeyboard = ( msg >= 0x0100 && msg <= 0x0109 );

		if ( bIsMouse )
		{
			if ( io.WantCaptureMouse && pContext != nullptr && pContext->_bIsGameViewHovered == false )
				bConsumed = true;
		}
		else if ( bIsKeyboard )
		{
			if ( io.WantCaptureKeyboard && pContext != nullptr && pContext->_bIsGameViewFocused == false )
				bConsumed = true;
		}
#else
		(void)pContext;
#endif

		return bConsumed;
	}

	void* ImGuiEditor::registerTexture( RHITextureHandle texture )
	{
		if ( _rendererBackend )
			return _rendererBackend->registerTexture( texture );
		return nullptr;
	}

	void ImGuiEditor::unregisterTexture( void* pTextureID )
	{
		if ( _rendererBackend )
			_rendererBackend->unregisterTexture( pTextureID );
	}

	void ImGuiEditor::registerDefaultWindows()
	{
		EditorWindowRegistry::registerDefaultWindows();
	}

	void ImGuiEditor::setupFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();

		const EditorData& data		 = editor::getEditorData();
		const string	  basePath	 = EditorUtil::resolveFontFile( data._listBaseFonts );
		const string	  koreanPath = EditorUtil::resolveFontFile( data._listKoreanFonts );

		ImFont* pBaseFont{ nullptr };
		BLOCK( "Base Font" )
		{
			ImFontConfig baseConfig{};
			baseConfig.OversampleH = 2;
			baseConfig.OversampleV = 1;
			// 명시적 크기가 필요합니다: MergeMode가 명시 크기를 쓰는데
			// 대상 폰트가 암시적 참조 크기(AddFontDefault)이면 ImGui가 assert합니다.
			baseConfig.SizePixels = data._fontSize;

			if ( basePath.empty() == false )
			{
				pBaseFont = io.Fonts->AddFontFromFileTTF( basePath.c_str(), data._fontSize, &baseConfig,
														  io.Fonts->GetGlyphRangesDefault() );
				SW_LOG_INFO( "[ImGuiEditor] Loaded base font: %#", basePath.c_str() );
			}

			if ( pBaseFont == nullptr )
			{
				pBaseFont = io.Fonts->AddFontDefault( &baseConfig );
				SW_LOG_WARNING( "[ImGuiEditor] No system UI font found - using ImGui default font." );
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
				io.Fonts->AddFontFromFileTTF( koreanPath.c_str(), data._fontSize, &mergeConfig,
											  io.Fonts->GetGlyphRangesKorean() );
				SW_LOG_INFO( "[ImGuiEditor] Merged Korean glyphs from: %#", koreanPath.c_str() );
			}
			else
				SW_LOG_WARNING( "[ImGuiEditor] Korean font not found - Hangul may not render." );
		}

		BLOCK( "Font Awesome 6 (ImGuiNotify icons)" )
		{
			const float32			 iconFontSize  = data._fontSize * 2.0f / 3.0f;
			static constexpr ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
			ImFontConfig			 iconsConfig{};
			iconsConfig.MergeMode			 = true;
			iconsConfig.PixelSnapH			 = true;
			iconsConfig.GlyphMinAdvanceX	 = iconFontSize;
			iconsConfig.FontDataOwnedByAtlas = false;
			io.Fonts->AddFontFromMemoryTTF( const_cast<void*>( static_cast<const void*>( fa_solid_900 ) ),
											sizeof( fa_solid_900 ), iconFontSize, &iconsConfig,
											iconsRanges );
		}

		io.FontDefault = pBaseFont;
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

		if ( _rendererBackend )
			_rendererBackend->render( pRhiDevice );
	}

	void ImGuiEditor::renderPlatformWindows( IRHIDevice* pRhiDevice )
	{
		if ( _bInitialized == false || pRhiDevice == nullptr )
			return;

		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) == 0 )
			return;

		// Present 복원 전에 플랫폼 뷰포트(멀티 뷰포트)를 갱신/렌더합니다.
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

		// 멀티 뷰포트 GL 백엔드는 MakeCurrent를 바꾸므로 메인 디바이스 컨텍스트를 복원합니다.
		if ( pRhiDevice->getBackendType() == RHIBackend::OpenGL )
			pRhiDevice->bindGraphicsContext();
	}

	void ImGuiEditor::drawMainMenuBar( const EditorUIContext& ctx )
	{
		if ( ImGui::BeginMainMenuBar() == false )
			return;

		if ( ImGui::BeginMenu( "File" ) )
		{
			ImGui::TextDisabled( "Scene IO ? use Content Browser" );
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Edit" ) )
		{
			const bool canUndo = editor::getService<CommandStack>()->canUndo();
			const bool canRedo = editor::getService<CommandStack>()->canRedo();
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, canUndo ) )
				editor::getService<CommandStack>()->undo();
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, canRedo ) )
				editor::getService<CommandStack>()->redo();
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Assets" ) )
		{
			if ( ImGui::MenuItem( "Tile Map Tool" ) )
				EditorWorkspace::requestOpenWindow( "Tile Map Tool" );
			if ( ImGui::MenuItem( "Sprite Clip" ) )
				EditorWorkspace::requestOpenWindow( "Sprite Clip" );
			if ( ImGui::MenuItem( "Animation Graph" ) )
				EditorWorkspace::requestOpenWindow( "Animation Graph" );
			if ( ImGui::MenuItem( "Sequencer" ) )
				EditorWorkspace::requestOpenWindow( "Sequencer" );
			ImGui::EndMenu();
		}

		if ( ImGui::BeginMenu( "Window" ) )
		{
			ImGui::SeparatorText( "Windows" );
			for ( const EditorWindowEntry& entry : EditorWindowRegistry::getWindows() )
			{
				if ( entry._pInstance == nullptr || entry._category != EditorWindowCategory::Core )
					continue;
				const bool open = entry._pInstance->isOpen();
				if ( ImGui::MenuItem( entry._title.c_str(), nullptr, open ) )
					entry._pInstance->setOpen( !open );
			}

			ImGui::SeparatorText( "Tools" );
			for ( const EditorWindowEntry& entry : EditorWindowRegistry::getWindows() )
			{
				if ( entry._pInstance == nullptr || entry._category == EditorWindowCategory::Core )
					continue;
				const bool open = entry._pInstance->isOpen();
				if ( ImGui::MenuItem( entry._title.c_str(), nullptr, open ) )
					entry._pInstance->setOpen( !open );
			}

			ImGui::Separator();
			if ( ImGui::MenuItem( "Reset Default Layout" ) )
			{
				_bDockLayoutApplied = false;
			}

			ImGui::EndMenu();
		}

		auto* const		  pRhiDevice = static_cast<IRHIDevice*>( ctx._pRHIDevice );
		const utf8*		  pBackend	 = ( pRhiDevice != nullptr ) ? pRhiDevice->getBackendName() : "n/a";
		constexpr float32 statusW	 = 280.0f;
		ImGui::SameLine( ImGui::GetWindowWidth() - statusW );
		ImGui::TextDisabled( "RHI %s | %.0f FPS", pBackend, static_cast<float64>( ImGui::GetIO().Framerate ) );
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

		const ImGuiViewport* pViewport	 = ImGui::GetMainViewport();
		const ImGuiID		 dockspaceId = ImGui::DockSpaceOverViewport(
			ImGui::GetID( "EditorMainDockSpace_v6" ), pViewport, ImGuiDockNodeFlags_PassthruCentralNode );

		if ( _bDockLayoutApplied == false )
		{
			const ImGuiDockNode* const pNode = ImGui::DockBuilderGetNode( dockspaceId );
			const bool				   empty =
				( pNode == nullptr ) || ( pNode->IsSplitNode() == false && pNode->Windows.Size == 0 );
			if ( empty )
				applyDefaultDockLayout( dockspaceId );
			_bDockLayoutApplied = true;
		}
	}

	void ImGuiEditor::applyDefaultDockLayout( uint32 dockspaceId )
	{
		const ImGuiID		 id		   = dockspaceId;
		const ImGuiViewport* pViewport = ImGui::GetMainViewport();

		ImGui::DockBuilderRemoveNode( id );
		ImGui::DockBuilderAddNode( id, ImGuiDockNodeFlags_DockSpace );
		ImGui::DockBuilderSetNodeSize( id, pViewport->WorkSize );

		ImGuiID dockMain = id;
		ImGuiID dockLeft{ 0 };
		ImGuiID dockRight{ 0 };
		ImGuiID dockBottom{ 0 };
		ImGuiID dockTop{ 0 };

		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Right, 0.28f, &dockRight, &dockMain );
		ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Down, 0.28f, &dockBottom, &dockMain );
		(void)dockTop;

		ImGui::DockBuilderDockWindow( "Hierarchy", dockLeft );
		ImGui::DockBuilderDockWindow( "Inspector", dockRight );

		ImGui::DockBuilderDockWindow( "Game View", dockMain );
		ImGui::DockBuilderDockWindow( "Profiler", dockMain );
		ImGui::DockBuilderDockWindow( "Tile Map Tool", dockMain );
		ImGui::DockBuilderDockWindow( "Sprite Clip", dockMain );
		ImGui::DockBuilderDockWindow( "Animation Graph", dockMain );
		ImGui::DockBuilderDockWindow( "Sequencer", dockMain );

		ImGui::DockBuilderDockWindow( "Content Browser", dockBottom );
		ImGui::DockBuilderDockWindow( "Output Log", dockBottom );

		ImGui::DockBuilderFinish( id );
	}

	void ImGuiEditor::setupLayoutPersistencePaths()
	{
		_imguiIniPath.clear();
		_windowsIniPath.clear();

		const EditorConfig& cfg			= EditorConfig::getActive();
		const string		imguiPath	= EditorUtil::resolveEditorConfigFile( cfg._imguiIniFile.c_str() );
		const string		windowsPath = EditorUtil::resolveEditorConfigFile( cfg._windowsIniFile.c_str() );
		if ( imguiPath.empty() || windowsPath.empty() )
		{
			SW_LOG_WARNING( "[ImGuiEditor] Failed to resolve Config/Editor - layout will not persist." );
			return;
		}

		_imguiIniPath	= imguiPath;
		_windowsIniPath = windowsPath;
		SW_LOG_INFO( "[ImGuiEditor] Layout persistence dir: %#", FileUtil::getDirectoryPart( imguiPath ).c_str() );
	}

	void ImGuiEditor::loadWindowVisibility()
	{
		if ( _windowsIniPath.empty() || FileUtil::fileExists( _windowsIniPath ) == false )
			return;

		KeyValueMap visibilityKv;
		if ( KeyValueFile::loadFile( _windowsIniPath, visibilityKv ) == false )
		{
			SW_LOG_WARNING( "[ImGuiEditor] Failed to open windows.ini: %#", _windowsIniPath.c_str() );
			return;
		}

		for ( const EditorWindowEntry& entry : EditorWindowRegistry::getWindows() )
		{
			if ( entry._pInstance == nullptr )
				continue;
			const utf8* pValue = KeyValueFile::get( visibilityKv, entry._title.c_str(), nullptr );
			if ( pValue == nullptr )
				continue;
			const bool open = ( StringUtil::strcmp( pValue, "1" ) == 0 || StringUtil::strcmp( pValue, "true" ) == 0 ||
								StringUtil::strcmp( pValue, "True" ) == 0 );
			entry._pInstance->setOpen( open );
		}

		SW_LOG_INFO( "[ImGuiEditor] Restored window visibility from %#", _windowsIniPath.c_str() );
	}

	void ImGuiEditor::saveEditorLayout()
	{
		if ( _windowsIniPath.empty() == false )
		{
			StringBuilder<2048> sb;
			sb.append( "# Editor window visibility (1=open, 0=closed)\n" );
			sb.append( "[WindowVisibility]\n" );
			for ( const EditorWindowEntry& entry : EditorWindowRegistry::getWindows() )
			{
				if ( entry._pInstance == nullptr )
					continue;
				sb.append( entry._title.c_str() )
					.append( '=' )
					.append( entry._pInstance->isOpen() ? '1' : '0' )
					.append( '\n' );
			}

			if ( FileUtil::writeTextFile( _windowsIniPath, sb.c_str() ) )
				SW_LOG_INFO( "[ImGuiEditor] Saved window visibility to %#", _windowsIniPath.c_str() );
			else
				SW_LOG_WARNING( "[ImGuiEditor] Failed to write windows.ini: %#", _windowsIniPath.c_str() );
		}

		if ( _imguiIniPath.empty() == false && ImGui::GetCurrentContext() != nullptr )
		{
			ImGui::SaveIniSettingsToDisk( _imguiIniPath.c_str() );
			SW_LOG_INFO( "[ImGuiEditor] Saved ImGui layout to %#", _imguiIniPath.c_str() );
		}
	}
} // namespace sw

#include "RuntimeAPI/EditorModuleExports.h"

// ==============================================================================
// EditorModule C-ABI 진입점 매크로 자동 구현
// ==============================================================================
SW_IMPLEMENT_EDITOR_MODULE( sw::ImGuiEditor );
