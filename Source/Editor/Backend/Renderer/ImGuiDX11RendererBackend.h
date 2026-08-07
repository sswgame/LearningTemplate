#pragma once
/**
 * @file ImGuiDX11RendererBackend.h
 * @brief ImGui Direct3D 11 렌더러 백엔드
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

		/** @brief D3D11 ImGui 렌더러를 초기화합니다. */
		bool initialize( IRHIDevice* rhiDevice ) override;
		/** @brief D3D11 ImGui 렌더러를 종료합니다. */
		void shutdown() override;
		/** @brief ImGui D3D11 프레임을 시작합니다. */
		void newFrame() override;
		/** @brief ImGui draw data를 D3D11로 그립니다. */
		void render( IRHIDevice* rhiDevice ) override;

		/** @brief RHI 텍스처를 ImGui용 SRV로 등록합니다. */
		void* registerTexture( RHITextureHandle texture ) override;

	private:
#if defined( SW_PLATFORM_WINDOWS )
		std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> _registeredSrvs;
#endif
	};
} // namespace sw
