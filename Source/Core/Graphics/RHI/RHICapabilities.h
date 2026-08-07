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
		bool _bBindless		   = false;
		bool _bCompute		   = true;
		bool _bOffscreenRT	   = false;
		bool _bImGuiHooks	   = false;
		bool _bEditorSupported = false;
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
					// Stub backends remain creatable for experimentation; editor unsupported.
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
					caps._bBindless		   = false;
					caps._bCompute		   = true;
					caps._bOffscreenRT	   = false;
					caps._bImGuiHooks	   = false;
					caps._bEditorSupported = false;
					break;
			}
			return caps;
		}
	};
}
