/**
 * @file RHIModuleEntry.h
 * @brief RHI 백엔드의 진입점을 한 줄로 구현하는 매크로입니다 — 동적 모듈이면 C-ABI 진입점, 정적 링크(배포)면 정적 등록자입니다.
 */
#pragma once
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIBackendRegistry.h"

/**
 * @brief RHI 백엔드 모듈의 C-ABI 진입점(`createRHIDevice`)을 구현하고 export 합니다. 정적 링크(배포)에서는 같은 줄이 등록표에 스스로 오르는 정적 등록자다 —
 *        등록표가 백엔드 헤더를 include 하거나 `SW_RHI_TARGET_*` 로 고르지 않는다(배포 빌드는 고른 백엔드의 이 파일을 엔진에 넣는다).
 * @param DeviceClass sw::IRHIDevice 를 구현하는 백엔드 디바이스 클래스
 * @param Backend     그 백엔드(`sw::RHIBackend::DirectX12` 등)
 * @details 진입점 규약을 백엔드마다 복사하지 않고 이 한곳에 둡니다(게임 모듈의 SW_IMPLEMENT_GAME_MODULE 과 같은 방식). ABI 대조는 빌드가 모듈마다 박는
 *          엔진 ABI 스탬프로 한다(아래 `PFN_CreateRHIDevice` 절).
 * @note 정의 앞에 선언을 먼저 둡니다. 이들은 어느 헤더에도 선언이 없는 export 전용 심볼이라,
 *       선언 없이 정의만 있으면 clang 이 -Wmissing-prototypes 를 붙입니다.
 */

#if SW_WITH_DYNAMIC_MODULES
    #define SW_IMPLEMENT_RHI_MODULE( DeviceClass, Backend )                                  \
        extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice( uint32 deviceGeneration ); \
        extern "C" SW_MODULE_API sw::IRHIDevice* createRHIDevice( uint32 deviceGeneration )  \
        {                                                                                    \
            return sw::make_unique<DeviceClass>( deviceGeneration ).release();               \
        }
#else
    #define SW_IMPLEMENT_RHI_MODULE( DeviceClass, Backend )                          \
        static const ::sw::RHIStaticBackendRegistrar sw_rhiStaticBackendRegistrar    \
        {                                                                            \
            Backend, []( uint32 deviceGeneration ) -> ::sw::IRHIDevice*              \
            {                                                                        \
                return ::sw::make_unique<DeviceClass>( deviceGeneration ).release(); \
            }                                                                        \
        }
#endif

// ------------------------------------------------------------------------------
// RHIModuleABI — RHI MODULE 의 C ABI 진입점 형입니다(Engine 과 RHI_* 가 같은 헤더로 빌드됐는지는 엔진 ABI 스탬프로 대조합니다).
//   이 경계는 함수 포인터 표가 아니라 IRHIDevice / IRHIResourceFactory / IRHICommandList / IRHICommandContext 의 vtable 과
//   경계를 넘는 구조체 배치다. 예전에는 손 판 번호(`kRHIModuleABIVersion`, v64 까지)와 스탬프 문자열을 사람이 올렸고, 잊으면 옛 백엔드 DLL 이
//   한 칸 밀린 가상 함수를 불렀다. 이제 로더(`RHIBackendRegistry::tryLoadModule`)는 백엔드 모듈이 박은 엔진 ABI 스탬프
//   (`sw_moduleEngineABIStamp_<모듈>`, Core · Engine · ModuleAPI 헤더 내용의 지문 — `cmake/Modules/Build/EngineABIStamp.cmake`)를
//   돌고 있는 엔진의 것(`engine::getEngineABIStamp`)과 대조한다. 헤더가 바뀌면 빌드가 지문을 다시 만들므로 올릴 번호가 없다
//   (언리얼 모듈의 빌드가 만드는 `BuildId` 와 같은 자리).
// ------------------------------------------------------------------------------
namespace sw
{
    class IRHIDevice;

    using PFN_CreateRHIDevice = IRHIDevice* (*)( uint32 deviceGeneration ); ///< v45: 장치 세대를 등록표가 넘긴다
} // namespace sw
