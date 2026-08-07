/**
 * @file ModuleEntry.cpp
 * @brief RHI_DX11 MODULE export — C ABI createRHIDevice
 */
#include "Core/Graphics/RHI/DX11/D3D11RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice();

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()
{
	return new sw::D3D11RHIDevice();
}

#endif
