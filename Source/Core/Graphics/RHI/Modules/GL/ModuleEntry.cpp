/**
 * @file ModuleEntry.cpp
 * @brief RHI_GL MODULE export — C ABI createRHIDevice
 */
#include "Core/Graphics/RHI/GL/OpenGLRHIDevice.h"

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice();

extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()
{
	return new sw::OpenGLRHIDevice();
}
