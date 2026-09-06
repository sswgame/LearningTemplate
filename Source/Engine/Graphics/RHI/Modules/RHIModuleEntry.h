/**
 * @file RHIModuleEntry.h
 * @brief RHI 백엔드 MODULE 의 C-ABI 진입점을 1줄로 구현하는 매크로
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/Modules/RHIModuleAbi.h"

/**
 * @brief RHI 백엔드 모듈의 C-ABI 진입점 3종을 구현하고 export 합니다.
 * @param DeviceClass sw::IRHIDevice 를 구현하는 백엔드 디바이스 클래스
 * @details 예전에는 백엔드마다 ModuleEntry.cpp 에 같은 25줄을 복사해 두고 클래스 이름만 바꿨다.
 *          진입점이 하나 늘 때마다 4곳을 고쳐야 했고, ABI 스탬프 규약이 흩어져 있었다.
 *          게임 모듈의 SW_IMPLEMENT_GAME_MODULE 과 같은 방식으로 한곳에 모은다.
 * @note 정의 앞에 선언을 먼저 둔다. 이들은 어느 헤더에도 선언이 없는 export 전용 심볼이라,
 *       선언 없이 정의만 있으면 clang 이 -Wmissing-prototypes 를 붙인다(백엔드 4개 x 3개 = 12건).
 */

#define SW_IMPLEMENT_RHI_MODULE( DeviceClass )                        \
    extern "C" SW_MODULE_API uint32      getRHIModuleAbiVersion();    \
    extern "C" SW_MODULE_API const utf8* getRHIModuleAbiStamp();      \
    extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice();       \
    extern "C" SW_MODULE_API uint32          getRHIModuleAbiVersion() \
    {                                                                 \
        return sw::kRHIModuleAbiVersion;                              \
    }                                                                 \
    extern "C" SW_MODULE_API const utf8* getRHIModuleAbiStamp()       \
    {                                                                 \
        return sw::kRHIModuleAbiStamp;                                \
    }                                                                 \
    extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()        \
    {                                                                 \
        return sw_new DeviceClass();                                  \
    }
