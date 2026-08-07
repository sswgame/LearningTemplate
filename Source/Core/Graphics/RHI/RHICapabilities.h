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

		uint8				   _bBindless			   : 1;
		uint8				   _bCompute			   : 1;
		uint8				   _bOffscreenRT		   : 1; ///< Basic createTexture2D + offscreen path available
		uint8				   _bImGuiHooks			   : 1; ///< Full ImGui renderer hooks (multi-viewport etc.)
		uint8				   _bEditorSupported	   : 1; ///< Safe to run EditorModule on this backend
		uint8				   _bComputeRootConstants  : 1; ///< Compute root/push constants (DX12 native, DX11/GL CB/UBO shim)
		[[maybe_unused]] uint8 _reserved			   : 2;
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
					caps._bBindless				 = true;
					caps._bCompute				 = true;
					caps._bOffscreenRT			 = true;
					caps._bImGuiHooks			 = true;
					caps._bEditorSupported		 = true;
					caps._bComputeRootConstants = true;
					break;
				case RHIBackend::DirectX11:
					// Descriptor-index tables (CB/UAV/texture SRV) — bind-at-draw, not DX12 heaps.
					caps._bBindless				 = true;
					caps._bCompute				 = true;
					caps._bOffscreenRT			 = true;
					caps._bImGuiHooks			 = true;
					caps._bEditorSupported		 = true;
					caps._bComputeRootConstants = true; // CS cbuffer shim (≤64 DWORD)
					break;
				case RHIBackend::OpenGL:
					// Descriptor-index tables for CB/SSBO/texture (bind-at-draw).
					// ImGui OpenGL backend + Win32 multi-viewport hooks are wired.
					caps._bBindless				 = true;
					caps._bCompute				 = true;
					caps._bOffscreenRT			 = true;
					caps._bImGuiHooks			 = true;
					caps._bEditorSupported		 = true;
					caps._bComputeRootConstants = true; // UBO shim (≤64 DWORD)
					break;
				case RHIBackend::Vulkan:
					// Texture2D / offscreen work; full descriptor-indexing bindless + editor hooks deferred.
					caps._bBindless				 = false;
					caps._bCompute				 = true;
					caps._bOffscreenRT			 = true;
					caps._bImGuiHooks			 = false;
					caps._bEditorSupported		 = false;
					caps._bComputeRootConstants = true; // vkCmdPushConstants path
					break;
			}
			return caps;
		}
	};
} // namespace sw
