/**
 * @file ImGuiEditor.cpp
 * @brief ImGui ?ÃÂÃÂ«ÃÂÃÂ??ÃÂªÃÂµÃÂ¬ÃÂ­ÃÂÃÂ
 */
#include "ImGuiEditor.h"
#include "Core/Game/GameState.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Graphics/Shader/ShaderReflection.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/String/StringUtil.h"
#include "Backend/IImGuiPlatformBackend.h"
#include "Backend/IImGuiRendererBackend.h"

#include <imgui.h>

#include "Core/Common/CommonMacros.h"
#include "Core/Utility/Log/Logger.h"

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
		// ViewportsEnableë DX12 ë©í° ìëì° ì¤ìì²´ì¸/íì¤ì² ìë¡ë ê²½ë¡ì ë§ì¶°ì¼ í¨.
		// íì¬ ë°±ìë ì°ëì´ ë¶ìì í´ í°í¸ ìë¡ë CreateCommittedResource assertë¥¼ ì ë°íë¯ë¡ ë¹íì±.
		// io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		ImGui::StyleColorsDark();

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

			unsigned char* pixels;
			int			   width, height;
			io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
		}

		_rhiBackendName = rhiDevice->getBackendName();
		_bInitialized	= true;
		return true;
	}

	void ImGuiEditor::shutdown()
	{
		if ( _bInitialized == false )
			return;

		getTypeRegistry().unregisterTypesByModule( "EditorModule" );

		if ( _rendererBackend )
			_rendererBackend->shutdown();

		if ( _platformBackend )
			_platformBackend->shutdown();

		ImGui::DestroyContext();

		_bInitialized = false;
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
				{
					wglMakeCurrent( static_cast<HDC>( hDC ), static_cast<HGLRC>( hRC ) );
				}
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

	void ImGuiEditor::beginDockspace( const utf8* dockspaceName )
	{
		if ( _bInitialized == false )
			return;

		static bool				  p_open		  = true;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

		ImGuiWindowFlags	 window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		const ImGuiViewport* viewport	  = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos( viewport->WorkPos );
		ImGui::SetNextWindowSize( viewport->WorkSize );
		ImGui::SetNextWindowViewport( viewport->ID );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		if ( ( dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode ) != 0 )
			window_flags |= ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
		ImGui::Begin( "DockSpace Demo", &p_open, window_flags );
		ImGui::PopStyleVar();
		ImGui::PopStyleVar( 2 );

		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable ) != 0 )
		{
			ImGuiID dockspace_id = ImGui::GetID( dockspaceName != nullptr ? dockspaceName : "MainDockSpace" );
			ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), dockspace_flags );
		}
	}

	void ImGuiEditor::endDockspace()
	{
		if ( _bInitialized == false )
			return;

		ImGui::End();
	}

	void ImGuiEditor::showDemoWindow( bool* pOpen )
	{
		if ( _bInitialized == false )
			return;

		ImGui::ShowDemoWindow( pOpen );
	}

	void ImGuiEditor::showGlobalVariablesWindow( bool* pOpen )
	{
		if ( _bInitialized == false )
			return;

		if ( ImGui::Begin( "Global Variables Control", pOpen ) == false )
		{
			ImGui::End();
			return;
		}

		static char filterBuffer[128] = "";
		ImGui::InputText( "Filter", filterBuffer, sizeof( filterBuffer ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Reset All Defaults" ) )
		{
			sw::getGlobalVariableManager().resetAllToDefault();
		}

		ImGui::Separator();

		static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

		if ( ImGui::BeginTable( "GlobalVariablesTable", 5, flags ) )
		{
			ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 150.0f );
			ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
			ImGui::TableSetupColumn( "Value / Control", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Description", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f );
			ImGui::TableHeadersRow();

			const auto& vars	  = sw::getGlobalVariableManager().getAllVariables();
			std::string filterStr = filterBuffer;
			std::transform( filterStr.begin(), filterStr.end(), filterStr.begin(), []( unsigned char c )
			{ return static_cast<char>( std::tolower( c ) ); } );

			int idIndex = 0;
			for ( const auto& [name, info] : vars )
			{
				if ( filterStr.empty() == false )
				{
					std::string nameLower = name;
					std::string descLower = info._description;
					std::transform( nameLower.begin(), nameLower.end(), nameLower.begin(), []( unsigned char c )
					{ return static_cast<char>( std::tolower( c ) ); } );
					std::transform( descLower.begin(), descLower.end(), descLower.begin(), []( unsigned char c )
					{ return static_cast<char>( std::tolower( c ) ); } );

					if ( nameLower.find( filterStr ) == std::string::npos && descLower.find( filterStr ) == std::string::npos )
					{
						continue;
					}
				}

				ImGui::PushID( idIndex++ );
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex( 0 );
				ImGui::TextUnformatted( name.c_str() );

				ImGui::TableSetColumnIndex( 1 );
				switch ( info._type )
				{
					case GlobalVariableType::Bool:
						ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "Bool" );
						break;
					case GlobalVariableType::Int32:
						ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ), "Int32" );
						break;
					case GlobalVariableType::Float:
						ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.4f, 1.0f ), "Float" );
						break;
					case GlobalVariableType::String:
						ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.8f, 1.0f ), "String" );
						break;
					case GlobalVariableType::Enum:
						ImGui::TextColored( ImVec4( 0.8f, 1.0f, 0.4f, 1.0f ), "Enum" );
						break;
				}

				ImGui::TableSetColumnIndex( 2 );
				if ( info._pData != nullptr )
				{
					switch ( info._type )
					{
						case GlobalVariableType::Bool:
						{
							bool* pVal = static_cast<bool*>( info._pData );
							if ( ImGui::Checkbox( "##bool_val", pVal ) )
							{
								if ( info._onValueChanged.isBound() )
									info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							}
							break;
						}
						case GlobalVariableType::Int32:
						{
							int32* pVal = static_cast<int32*>( info._pData );
							if ( ImGui::InputInt( "##int_val", pVal ) )
							{
								if ( info._onValueChanged.isBound() )
									info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							}
							break;
						}
						case GlobalVariableType::Float:
						{
							float32* pVal = static_cast<float32*>( info._pData );
							if ( ImGui::InputFloat( "##float_val", pVal ) )
							{
								if ( info._onValueChanged.isBound() )
									info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							}
							break;
						}
						case GlobalVariableType::String:
						{
							std::string* pVal = static_cast<std::string*>( info._pData );
							char		 buf[256];
							strncpy_s( buf, pVal->c_str(), sizeof( buf ) - 1 );
							if ( ImGui::InputText( "##string_val", buf, sizeof( buf ) ) )
							{
								*pVal = buf;
								if ( info._onValueChanged.isBound() )
									info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							}
							break;
						}
						case GlobalVariableType::Enum:
						{
							int32*				pVal	  = static_cast<int32*>( info._pData );
							const sw::EnumInfo* pEnumInfo = sw::getTypeRegistry().findEnum( sw::hashed_string( info._enumType.c_str() ) );
							if ( pEnumInfo )
							{
								std::string currentEnumStr = pEnumInfo->toString( *pVal ).c_str();
								if ( ImGui::BeginCombo( "##enum_val", currentEnumStr.c_str() ) )
								{
									for ( const auto& [enumName, enumValue] : pEnumInfo->_mapNameToValue )
									{
										bool isSelected = ( *pVal == enumValue );
										if ( ImGui::Selectable( enumName.c_str(), isSelected ) )
										{
											*pVal = static_cast<int32>( enumValue );
											if ( info._onValueChanged.isBound() )
												info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
										}
										if ( isSelected )
										{
											ImGui::SetItemDefaultFocus();
										}
									}
									ImGui::EndCombo();
								}
							}
							else
							{

								if ( ImGui::InputInt( "##int_val", pVal ) )
								{
									if ( info._onValueChanged.isBound() )
										info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
								}
							}
							break;
						}
					}
				}

				ImGui::TableSetColumnIndex( 3 );
				ImGui::TextUnformatted( info._description.c_str() );

				ImGui::TableSetColumnIndex( 4 );
				if ( ImGui::Button( "Reset" ) )
				{
					sw::getGlobalVariableManager().resetToDefault( name );
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::End();
	}

	void ImGuiEditor::showComputeTestWindow( bool* pOpen, IRHIDevice* rhiDevice )
	{
		if ( !ImGui::Begin( "Compute Test", pOpen ) )
		{
			ImGui::End();
			return;
		}

		if ( ImGui::Button( "Initialize Compute Resources" ) )
		{
			if ( !_bComputeTestInitialized )
			{
				_csPso = rhiDevice->createComputePipelineState( "Shaders/SampleIndirect.hlsl", "CSMain" );

				RHIPipelineStateDesc indirectDesc{};
				indirectDesc._vertexShaderPath = "Shaders/ComputeTestGeometry.hlsl";
				indirectDesc._vertexEntryPoint = "VSMain";
				indirectDesc._pixelShaderPath = "Shaders/ComputeTestGeometry.hlsl";
				indirectDesc._pixelEntryPoint = "PSMain";
				_indirectPso = rhiDevice->createPipelineState( indirectDesc );

				_uavBuffer = rhiDevice->createStructuredBuffer( sizeof( RHIDrawIndirectCommand ), 1 );
				_uavIndex = rhiDevice->registerBindlessUAV( _uavBuffer );

				_dispatchUavBuffer = rhiDevice->createStructuredBuffer( sizeof( RHIDispatchIndirectCommand ), 1 );
				_dispatchUavIndex = rhiDevice->registerBindlessUAV( _dispatchUavBuffer );

				_bComputeTestInitialized = ( _csPso != 0 && _uavIndex != kInvalidDescriptorIndex );
				if ( _bComputeTestInitialized == false )
					SW_LOG_ERROR( "[ComputeTest] Failed to initialize compute resources." );
			}
		}

		if ( _bComputeTestInitialized )
		{
			ImGui::Text( "CS PSO Handle: %llu", _csPso );
			ImGui::Text( "Indirect PSO Handle: %llu", _indirectPso );
			ImGui::Text( "UAV Buffer Index: %u", _uavIndex );

			if ( ImGui::Button( "Dispatch Compute & Draw Indirect" ) )
			{
				_bRequestComputeDispatch = true;
			}
		}

		ImGui::End();
	}

	void ImGuiEditor::renderMaterialUI( Material* material, IRHIDevice* rhiDevice )
	{
		if ( material == nullptr || rhiDevice == nullptr )
			return;

		ImGui::PushID( material );

		const auto& props = material->getProperties();
		const auto& buffer = material->getBuffer();

		bool bChanged = false;

		std::vector<uint8> tempBuffer = buffer;

		for ( const auto& prop : props )
		{
			ImGui::PushID( prop.name.c_str() );

			if ( prop.type == MaterialPropertyType::Float )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat( prop.name.c_str(), ptr, 0.01f ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float2 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat2( prop.name.c_str(), ptr, 0.01f ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float3 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat3( prop.name.c_str(), ptr, 0.01f ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float4 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::ColorEdit4( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float4x4 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				ImGui::Text("%s", prop.name.c_str());
				if ( ImGui::DragFloat4( "##r0", ptr, 0.01f ) ) bChanged = true;
				if ( ImGui::DragFloat4( "##r1", ptr + 4, 0.01f ) ) bChanged = true;
				if ( ImGui::DragFloat4( "##r2", ptr + 8, 0.01f ) ) bChanged = true;
				if ( ImGui::DragFloat4( "##r3", ptr + 12, 0.01f ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint2 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt2( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint3 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt3( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint4 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt4( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Int )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Int2 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt2( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Int3 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt3( prop.name.c_str(), ptr ) ) bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Int4 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt4( prop.name.c_str(), ptr ) ) bChanged = true;
			}

			ImGui::PopID();
		}

		if ( bChanged )
		{
			material->setPropertyData( rhiDevice, 0, static_cast<uint32>(tempBuffer.size()), tempBuffer.data() );
		}

		ImGui::PopID();
	}

	void ImGuiEditor::preRender( IRHIDevice* rhiDevice )
	{
		if ( _bInitialized == false )
			return;

		if ( popComputeTestRequest() )
		{
			executeComputeDispatch( rhiDevice );
		}
	}

	void ImGuiEditor::render( const EditorUIContext& ctx )
	{
		if ( _bInitialized == false )
			return;

		executeComputeDraw( ctx.rhiDevice );

		beginFrame();
		beginDockspace( "MainDockSpace" );

		if ( ctx.bShowDemoWindow && *ctx.bShowDemoWindow )
			showDemoWindow( ctx.bShowDemoWindow );

		if ( ImGui::Begin( "RHI & Engine Inspector" ) )
		{
			ImGui::Text( "Active RHI Backend : %s", ctx.rhiDevice->getBackendName() );

			if ( ctx.playerSpeed )
				ImGui::SliderFloat( "Player Speed", ctx.playerSpeed, 0.0f, 20.0f );
			if ( ctx.clearColor )
				ImGui::ColorEdit3( "Clear Color", ctx.clearColor );

			if ( ctx.material )
				renderMaterialUI( ctx.material, ctx.rhiDevice );

			if ( ImGui::Button( "Reset Settings" ) )
			{
				if ( ctx.playerSpeed )
					*ctx.playerSpeed = 5.0f;
				if ( ctx.clearColor )
				{
					ctx.clearColor[0] = 0.12f;
					ctx.clearColor[1] = 0.15f;
					ctx.clearColor[2] = 0.18f;
				}

				if ( ctx.material )
				{
					ctx.material->loadFromFile( "Material/DefaultMaterial.material" );
					ctx.material->setPropertyData( ctx.rhiDevice, 0, static_cast<uint32>( ctx.material->getBuffer().size() ),
												   ctx.material->getBuffer().data() );
				}
			}
		}
		ImGui::End();

		if ( ImGui::Begin( "Live Coding & Console Log" ) )
		{
			ImGui::TextUnformatted( "[LIVE CODING] EditorModule.dll loaded via shadow copy (triggerReload to hot-reload)." );
			ImGui::Text( "[INFO] RHI Device Type: %s", ctx.rhiDevice->getBackendName() );

			if ( ctx.material )
				ImGui::Text( "[BINDLESS] Material Descriptor Index: %u", ctx.material->getDescriptorIndex() );

			if ( ctx.reflectionData && ctx.reflectionData->_constantBuffers.empty() == false )
			{
				const ShaderBufferInfo& cb = ctx.reflectionData->_constantBuffers[0];
				ImGui::Text( "[REFLECTION] CBuffer: %s (Size: %u bytes)", cb._name.c_str(), cb._totalSize );
			}
			ImGui::TextUnformatted( "[RHI COMPUTE] Direct Dispatch (4x1x1) & Indirect Command (drawIndirect) Ready." );
		}
		ImGui::End();

		static bool bShowComputeTest = true;
		showComputeTestWindow( &bShowComputeTest, ctx.rhiDevice );

		showGlobalVariablesWindow();
		setRHIBackendInfo( ctx.rhiDevice->getBackendName() );
		showEngineStatusWindow();

		if ( ImGui::Begin( "Game Toolbar" ) )
		{
			const GameState currentState = getGameState();

			if ( currentState == GameState::Playing )
				ImGui::Button( "[ Playing ]" );
			else if ( ImGui::Button( "Play" ) )
				setGameState( GameState::Playing );

			ImGui::SameLine();

			if ( currentState == GameState::Paused )
				ImGui::Button( "[ Paused ]" );
			else if ( ImGui::Button( "Pause" ) )
				setGameState( GameState::Paused );

			ImGui::SameLine();

			if ( ImGui::Button( "Stop" ) )
				setGameState( GameState::Stopped );
		}
		ImGui::End();

		if ( ImGui::Begin( "Game View" ) )
		{
			if ( ctx.gameTextureID != nullptr )
			{
				const ImVec2 size = ImGui::GetContentRegionAvail();
				if ( size.x > 0 && size.y > 0 )
					ImGui::Image( reinterpret_cast<ImTextureID>( ctx.gameTextureID ), size );
			}
			else
			{
				ImGui::TextUnformatted( "Game RenderTarget is not available." );
			}
		}
		ImGui::End();

		endDockspace();
		endFrame();
		renderBackend( ctx.rhiDevice );
	}

	bool ImGuiEditor::popComputeTestRequest()
	{
		bool ret = _bRequestComputeDispatch;
		_bRequestComputeDispatch = false;
		return ret;
	}

	void ImGuiEditor::executeComputeDispatch( IRHIDevice* rhiDevice )
	{
		if ( _bComputeTestInitialized == false || _csPso == 0 || _uavIndex == kInvalidDescriptorIndex )
			return;

		rhiDevice->setComputePipelineState( _csPso );
		rhiDevice->bindComputeUAV( _uavIndex, 0 );
		rhiDevice->bindComputeUAV( _dispatchUavIndex, 1 );

		auto cmdList = rhiDevice->createCommandList();
		cmdList->beginCommandList();
		cmdList->dispatchCompute( 1, 1, 1 );
		cmdList->endCommandList();
		rhiDevice->executeCommandList( cmdList.get() );

		_bComputeTestDispatched = true;
	}

	void ImGuiEditor::executeComputeDraw( IRHIDevice* rhiDevice )
	{
		if ( _bComputeTestInitialized == false || _bComputeTestDispatched == false || _indirectPso == 0 || _uavBuffer == 0 )
			return;

		auto cmdList = rhiDevice->createCommandList();
		cmdList->beginCommandList();

		RHIViewport vp{};
		vp._width = 1280;
		vp._height = 720;
		cmdList->setViewport( vp );

		cmdList->setPipelineState( _indirectPso );
		cmdList->drawIndirect( _uavBuffer, 0 );

		cmdList->endCommandList();
		rhiDevice->executeCommandList( cmdList.get() );
	}

	void ImGuiEditor::setRHIBackendInfo( const utf8* backendName )
	{
		if ( backendName != nullptr )
		{
			_rhiBackendName = backendName;
		}
	}

	void ImGuiEditor::showEngineStatusWindow( bool* pOpen )
	{
		if ( _bInitialized == false )
			return;

		if ( ImGui::Begin( "Engine RHI Status & Command Line", pOpen ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::TextColored( ImVec4( 0.2f, 0.8f, 1.0f, 1.0f ), "Active RHI Backend: %s", _rhiBackendName.c_str() );
		ImGui::Separator();

		ImGui::TextUnformatted( "Supported Command Line Arguments to switch RHI Backend:" );
		ImGui::BulletText( "-DIRECTX_11  (or -dx11, -d3d11) : Direct3D 11 Backend" );
		ImGui::BulletText( "-DIRECTX_12  (or -dx12, -d3d12) : Direct3D 12 Backend" );
		ImGui::BulletText( "-VULKAN      (or -vk)           : Vulkan Backend" );
		ImGui::BulletText( "-OPENGL      (or -gl)           : OpenGL Backend" );

		ImGui::End();
	}

	void* ImGuiEditor::registerTexture( RHITextureHandle texture )
	{
		if ( _rendererBackend )
			return _rendererBackend->registerTexture( texture );
		return nullptr;
	}
}
