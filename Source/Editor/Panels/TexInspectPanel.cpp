/**
 * @file TexInspectPanel.cpp
 */
#include "Panels/TexInspectPanel.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHITypes.h"

#include <imgui.h>
#include <imgui_tex_inspect.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include <d3d11.h>
	#include <tex_inspect_directx11.h>
#else
	#include <tex_inspect_opengl.h>
#endif

namespace sw
{
	TexInspectPanel::TexInspectPanel() = default;

	TexInspectPanel::~TexInspectPanel()
	{
		destroyBackend();
	}

	void TexInspectPanel::destroyBackend()
	{
		if ( _bInited == false )
			return;

		ImGuiTexInspect::SetCurrentContext( _context );
#if defined( SW_PLATFORM_WINDOWS )
		if ( _backendKind == 1 )
			ImGuiTexInspect::ImplDX11_Shutdown();
#else
		if ( _backendKind == 2 )
			ImGuiTexInspect::ImplOpenGl3_Shutdown();
#endif

		if ( _context != nullptr )
		{
			ImGuiTexInspect::DestroyContext( _context );
			_context = nullptr;
		}
		ImGuiTexInspect::Shutdown();
		_bInited	 = false;
		_backendKind = 0;
	}

	void TexInspectPanel::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		destroyBackend();
	}

	void TexInspectPanel::ensureInit( IRHIDevice* rhiDevice )
	{
		if ( _bInited || _bUnsupported || rhiDevice == nullptr )
			return;

		const RHIBackend backend = rhiDevice->getBackendType();
		ImGuiTexInspect::Init();
		_context = ImGuiTexInspect::CreateContext();
		ImGuiTexInspect::SetCurrentContext( _context );

		bool ok = false;
#if defined( SW_PLATFORM_WINDOWS )
		// Windows build links the DX11 tex-inspect backend only (upstream single BackEnd_* ABI).
		if ( backend == RHIBackend::DirectX11 )
		{
			auto* device  = static_cast<ID3D11Device*>( rhiDevice->getNativeDevice() );
			auto* context = static_cast<ID3D11DeviceContext*>( rhiDevice->getNativeContext() );
			ok			  = ImGuiTexInspect::ImplDX11_Init( device, context );
			_backendKind  = 1;
		}
#else
		if ( backend == RHIBackend::OpenGL )
		{
			ok			 = ImGuiTexInspect::ImplOpenGL3_Init( "#version 460" );
			_backendKind = 2;
		}
#endif

		if ( ok == false )
		{
			_bUnsupported = true;
#if defined( SW_PLATFORM_WINDOWS )
			if ( _backendKind == 1 )
				ImGuiTexInspect::ImplDX11_Shutdown();
#else
			if ( _backendKind == 2 )
				ImGuiTexInspect::ImplOpenGl3_Shutdown();
#endif
			if ( _context != nullptr )
			{
				ImGuiTexInspect::DestroyContext( _context );
				_context = nullptr;
			}
			ImGuiTexInspect::Shutdown();
			_backendKind = 0;
			return;
		}

		_bInited = true;
	}

	void TexInspectPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle(), &_bOpen ) == false )
		{
			ImGui::End();
			return;
		}

		ensureInit( ctx.rhiDevice );
		if ( _bUnsupported )
		{
#if defined( SW_PLATFORM_WINDOWS )
			ImGui::TextUnformatted( "Tex Inspect (this build): DirectX11 RHI only." );
#else
			ImGui::TextUnformatted( "Tex Inspect (this build): OpenGL RHI only." );
#endif
			ImGui::End();
			return;
		}
		if ( _bInited == false )
		{
			ImGui::TextUnformatted( "Waiting for RHI device…" );
			ImGui::End();
			return;
		}

		ImGuiTexInspect::SetCurrentContext( _context );

		if ( ctx.gameTextureID == nullptr )
		{
			ImGui::TextUnformatted( "No game viewport texture registered." );
			ImGui::End();
			return;
		}

		const ImVec2 texSize( static_cast<float>( ctx.gameViewportWidth > 0 ? ctx.gameViewportWidth : 1 ),
							  static_cast<float>( ctx.gameViewportHeight > 0 ? ctx.gameViewportHeight : 1 ) );

		if ( ImGuiTexInspect::BeginInspectorPanel( "GameViewport", reinterpret_cast<ImTextureID>( ctx.gameTextureID ), texSize,
												   ImGuiTexInspect::InspectorFlags_FillHorizontal | ImGuiTexInspect::InspectorFlags_FillVertical ) )
		{
			ImGuiTexInspect::DrawAnnotations( ImGuiTexInspect::ValueText{} );
		}
		ImGuiTexInspect::EndInspectorPanel();

		ImGui::End();
	}
} // namespace sw
