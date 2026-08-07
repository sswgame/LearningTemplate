#pragma once
/**
 * @file ImGuiOpenGLRendererBackend.h
 * @brief ImGui OpenGL 렌더러 백엔드
 */

#include "Editor/Backend/IImGuiRendererBackend.h"

namespace sw
{
	class IRHIDevice;

	class ImGuiOpenGLRendererBackend : public IImGuiRendererBackend
	{
	public:
		ImGuiOpenGLRendererBackend()		   = default;
		~ImGuiOpenGLRendererBackend() override = default;

		/** @brief OpenGL ImGui 렌더러를 초기화합니다. */
		bool initialize( IRHIDevice* rhiDevice ) override;
		/** @brief OpenGL ImGui 렌더러를 종료합니다. */
		void shutdown() override;
		/** @brief ImGui OpenGL 프레임을 시작합니다. */
		void newFrame() override;
		/** @brief ImGui draw data를 OpenGL로 그립니다. */
		void render( IRHIDevice* rhiDevice ) override;

		void* registerTexture( RHITextureHandle /*texture*/ ) override { return nullptr; }
	};
}
