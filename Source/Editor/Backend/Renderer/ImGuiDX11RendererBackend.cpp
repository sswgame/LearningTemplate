/**
 * @file ImGuiDX11RendererBackend.cpp
 * @brief ImGui Direct3D 11 렌더러 구현
 */
#include "ImGuiDX11RendererBackend.h"
#include <imgui.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include <imgui_impl_dx11.h>
#endif

#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	bool ImGuiDX11RendererBackend::initialize( class IRHIDevice* rhiDevice )
	{
#if defined( SW_PLATFORM_WINDOWS )
		ID3D11Device*		 device	 = static_cast<ID3D11Device*>( rhiDevice->getNativeDevice() );
		ID3D11DeviceContext* context = static_cast<ID3D11DeviceContext*>( rhiDevice->getNativeContext() );
		if ( device != nullptr && context != nullptr )
			return ImGui_ImplDX11_Init( device, context );
#else
		(void)rhiDevice;
#endif
		return true;
	}

	void ImGuiDX11RendererBackend::shutdown()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX11_Shutdown();
		_registeredSrvs.clear();
#endif
	}

	void ImGuiDX11RendererBackend::newFrame()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX11_NewFrame();
#endif
	}

	void ImGuiDX11RendererBackend::render( class IRHIDevice* rhiDevice )
	{
		(void)rhiDevice;
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
#endif
	}

	void* ImGuiDX11RendererBackend::registerTexture( RHITextureHandle texture )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( texture == 0 )
			return nullptr;

		auto*		  tex	 = reinterpret_cast<ID3D11Texture2D*>( texture );
		ID3D11Device* device = nullptr;
		tex->GetDevice( &device );
		if ( device == nullptr )
			return nullptr;

		D3D11_TEXTURE2D_DESC texDesc{};
		tex->GetDesc( &texDesc );

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format					  = texDesc.Format;
		srvDesc.ViewDimension			  = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels		  = texDesc.MipLevels;

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		const HRESULT									 hr = device->CreateShaderResourceView( tex, &srvDesc, srv.GetAddressOf() );
		device->Release();

		if ( FAILED( hr ) || srv == nullptr )
		{
			SW_LOG_ERROR( "[ImGuiDX11] Failed to create SRV for registered texture. HRESULT: %#", hr );
			return nullptr;
		}

		ID3D11ShaderResourceView* srvPtr = srv.Get();
		_registeredSrvs.push_back( std::move( srv ) );
		return static_cast<void*>( srvPtr );
#else
		(void)texture;
		return nullptr;
#endif
	}

	void ImGuiDX11RendererBackend::unregisterTexture( void* textureID )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( textureID == nullptr )
			return;

		auto* srv = static_cast<ID3D11ShaderResourceView*>( textureID );
		for ( auto it = _registeredSrvs.begin(); it != _registeredSrvs.end(); ++it )
		{
			if ( it->Get() == srv )
			{
				_registeredSrvs.erase( it );
				return;
			}
		}
#else
		(void)textureID;
#endif
	}
} // namespace sw
