/**
 * @file ModuleEntry.cpp
 * @brief RHI_DX12 MODULE export — C ABI createRHIDevice
 */
#include "Core/Graphics/RHI/DX12/D3D12RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice();

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()
{
	return new sw::D3D12RHIDevice();
}

#endif
