#pragma once
/**
 * @file RHICapabilities.h
 * @brief RHI backend capability and availability queries
 */

#include "Core/Graphics/RHI/RHITypes.h"

namespace sw
{
	struct RHICapabilities
	{
		RHICapabilities() noexcept;

		uint8				   _bBindless		 : 1;
		uint8				   _bCompute		 : 1;
		uint8				   _bOffscreenRT	 : 1; ///< Basic createTexture2D + offscreen path available
		uint8				   _bImGuiHooks		 : 1; ///< Full ImGui renderer hooks (multi-viewport etc.)
		uint8				   _bEditorSupported : 1; ///< Safe to run EditorModule on this backend
		[[maybe_unused]] uint8 _reserved		 : 3;
	};

	struct RHIAvailability
	{
		/** @brief Returns true if the backend can be created on this OS/build. */
		static bool isAvailable( RHIBackend backend ) noexcept
		{
			switch ( backend )
			{
				case RHIBackend::DirectX11:
				case RHIBackend::DirectX12:
#if defined( SW_PLATFORM_WINDOWS )
					return true;
#else
					return false;
#endif
				case RHIBackend::Vulkan:
				case RHIBackend::OpenGL:
					return true;
			}
			return false;
		}

		static RHICapabilities query( RHIBackend backend ) noexcept
		{
			RHICapabilities caps{};
			switch ( backend )
			{
				case RHIBackend::DirectX12:
					caps._bBindless		   = true;
					caps._bCompute		   = true;
					caps._bOffscreenRT	   = true;
					caps._bImGuiHooks	   = true;
					caps._bEditorSupported = true;
					break;
				case RHIBackend::DirectX11:
					caps._bBindless		   = false;
					caps._bCompute		   = true;
					caps._bOffscreenRT	   = true;
					caps._bImGuiHooks	   = true;
					caps._bEditorSupported = true;
					break;
				case RHIBackend::Vulkan:
				case RHIBackend::OpenGL:
					// Texture2D allocation works for basic color targets (_bOffscreenRT).
					// Editor / ImGui multi-viewport remain incomplete — keep editor flags false
					// so App rejects editor hot-swap onto these backends.
					caps._bBindless		   = false;
					caps._bCompute		   = true;
					caps._bOffscreenRT	   = true;
					caps._bImGuiHooks	   = false;
					caps._bEditorSupported = false;
					break;
			}
			return caps;
		}
	};
} // namespace sw
