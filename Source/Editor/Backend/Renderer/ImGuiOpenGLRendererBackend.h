#pragma once
/**
 * @file ImGuiOpenGLRendererBackend.h
 * @brief Auto-generated documentation header
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

		void* registerTexture( RHITextureHandle /*texture*/ ) override { return nullptr; }
	};
}
