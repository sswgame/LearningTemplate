/**
 * @file RHIModuleEntry.h
 * @brief RHI 백엔드의 진입점을 한 줄로 구현하는 매크로입니다 — 동적 모듈이면 C-ABI 진입점, 정적 링크(배포)면 정적 등록자입니다.
 */
#pragma once
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHIModuleABI.h"

/**
 * @brief RHI 백엔드 모듈의 C-ABI 진입점 세 개를 구현하고 export 합니다. 정적 링크(배포)에서는 같은 줄이 등록표에 스스로 오르는 정적 등록자다 —
 *        등록표가 백엔드 헤더를 include 하거나 `SW_RHI_TARGET_*` 로 고르지 않는다(배포 빌드는 고른 백엔드의 이 파일을 엔진에 넣는다).
 * @param DeviceClass sw::IRHIDevice 를 구현하는 백엔드 디바이스 클래스
 * @param Backend     그 백엔드(`sw::RHIBackend::DirectX12` 등)
 * @details 진입점과 ABI 스탬프 규약을 백엔드마다 복사하지 않고 이 한곳에 둡니다(게임 모듈의 SW_IMPLEMENT_GAME_MODULE 과 같은 방식).
 * @note 정의 앞에 선언을 먼저 둡니다. 이들은 어느 헤더에도 선언이 없는 export 전용 심볼이라,
 *       선언 없이 정의만 있으면 clang 이 -Wmissing-prototypes 를 붙입니다(백엔드 4개 x 3개 = 12건).
 */

#if SW_WITH_DYNAMIC_MODULES
    #define SW_IMPLEMENT_RHI_MODULE( DeviceClass, Backend )               \
        extern "C" SW_MODULE_API uint32      getRHIModuleABIVersion();    \
        extern "C" SW_MODULE_API const utf8* getRHIModuleABIStamp();      \
        extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice();       \
        extern "C" SW_MODULE_API uint32          getRHIModuleABIVersion() \
        {                                                                 \
            return sw::kRHIModuleABIVersion;                              \
        }                                                                 \
        extern "C" SW_MODULE_API const utf8* getRHIModuleABIStamp()       \
        {                                                                 \
            return sw::kRHIModuleABIStamp;                                \
        }                                                                 \
        extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice()        \
        {                                                                 \
            return sw::make_unique<DeviceClass>().release();              \
        }
#else
    #define SW_IMPLEMENT_RHI_MODULE( DeviceClass, Backend )                       \
        static const ::sw::RHIStaticBackendRegistrar sw_rhiStaticBackendRegistrar \
        {                                                                         \
            Backend, []() -> ::sw::IRHIDevice*                                    \
            {                                                                     \
                return ::sw::make_unique<DeviceClass>().release();                \
            }                                                                     \
        }
#endif
