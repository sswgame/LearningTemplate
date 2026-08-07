/**
 * @file ModuleEntry.cpp
 * @brief RHI_Vulkan MODULE export — C ABI createRHIDevice
 */
#include "Core/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice();

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()
{
	return new sw::VulkanRHIDevice();
}
