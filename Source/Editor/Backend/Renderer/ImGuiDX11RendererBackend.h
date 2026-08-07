#pragma once
/**
 * @file ImGuiDX11RendererBackend.h
 * @brief Auto-generated documentation header
 */

#include "Editor/Backend/IImGuiRendererBackend.h"

namespace sw
{
	class IRHIDevice;

	class ImGuiDX11RendererBackend : public IImGuiRendererBackend
	{
	public:
		ImGuiDX11RendererBackend()			 = default;
		~ImGuiDX11RendererBackend() override = default;

		/**
		 * @brief initialize 처리를 수행합니다.
		 */
		bool initialize( IRHIDevice* rhiDevice ) override;
		/**
		 * @brief shutdown 처리를 수행합니다.
		 */
		void shutdown() override;
		/**
		 * @brief newFrame 처리를 수행합니다.
		 */
		void newFrame() override;
		/**
		 * @brief render 처리를 수행합니다.
		 */
		void render( IRHIDevice* rhiDevice ) override;

		void* registerTexture( RHITextureHandle texture ) override;

	private:
#if defined( SW_PLATFORM_WINDOWS )
		std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> _registeredSrvs;
#endif
	};
}
