#pragma once
/**
 * @file IImGuiRendererBackend.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Common.h"
#include "Core/Graphics/RHI/RHITypes.h"
namespace sw
{
	class IRHIDevice;

	class IImGuiRendererBackend
	{
	public:
		virtual ~IImGuiRendererBackend() = default;

		/**
		 * @brief initialize 처리를 수행합니다.
		 */
		virtual bool initialize( IRHIDevice* rhiDevice ) = 0;
		/**
		 * @brief shutdown 처리를 수행합니다.
		 */
		virtual void shutdown()							 = 0;
		/**
		 * @brief newFrame 처리를 수행합니다.
		 */
		virtual void newFrame()							 = 0;
		/**
		 * @brief render 처리를 수행합니다.
		 */
		virtual void render( IRHIDevice* rhiDevice )	 = 0;

		/**
		 * @brief registerTexture 처리를 수행합니다.
		 */
		virtual void* registerTexture( RHITextureHandle texture ) = 0;

		/**
		 * @brief createRendererBackend 처리를 수행합니다.
		 */
		static std::unique_ptr<IImGuiRendererBackend> createRendererBackend( RHIBackend backend );
	};
}
