/**
 * @file RHIModuleEntry.h
 * @brief RHI 백엔드 MODULE 의 C-ABI 진입점을 1줄로 구현하는 매크로
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIModuleAbi.h"

/**
 * @brief RHI 백엔드 모듈의 C-ABI 진입점 3종을 구현하고 export 합니다.
 * @param DeviceClass sw::IRHIDevice 를 구현하는 백엔드 디바이스 클래스
 * @details 예전에는 백엔드마다 ModuleEntry.cpp 에 같은 25줄을 복사해 두고 클래스 이름만 바꿨다.
 *          진입점이 하나 늘 때마다 4곳을 고쳐야 했고, ABI 스탬프 규약이 흩어져 있었다.
 *          게임 모듈의 SW_IMPLEMENT_GAME_MODULE 과 같은 방식으로 한곳에 모은다.
 */

#define SW_IMPLEMENT_RHI_MODULE( DeviceClass )                  \
    extern "C" SW_MODULE_API uint32 getRHIModuleAbiVersion()    \
    {                                                           \
        return sw::kRHIModuleAbiVersion;                        \
    }                                                           \
    extern "C" SW_MODULE_API const utf8* getRHIModuleAbiStamp() \
    {                                                           \
        return sw::kRHIModuleAbiStamp;                          \
    }                                                           \
    extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()  \
    {                                                           \
        return sw_new DeviceClass();                            \
    }
