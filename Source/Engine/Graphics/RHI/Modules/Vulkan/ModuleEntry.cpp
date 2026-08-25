#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIModuleAbi.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

extern "C" SW_MODULE_API uint32		 getRHIModuleAbiVersion();
extern "C" SW_MODULE_API const utf8* getRHIModuleAbiStamp();
extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice();

extern "C" SW_MODULE_API uint32 getRHIModuleAbiVersion()
{
	return sw::kRHIModuleAbiVersion;
}

extern "C" SW_MODULE_API const utf8* getRHIModuleAbiStamp()
{
	return sw::kRHIModuleAbiStamp;
}

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()
{
	return sw_new sw::VulkanRHIDevice();
}
