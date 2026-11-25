/**
 * @file RHIModuleABI.h
 * @brief RHI MODULE 의 C ABI 진입점 형입니다(Engine 과 RHI_* 가 같은 헤더로 빌드됐는지는 엔진 ABI 스탬프로 대조합니다).
 *
 * @details 이 경계는 함수 포인터 표가 아니라 IRHIDevice / IRHIResourceFactory / IRHICommandList / IRHICommandContext 의 vtable 과
 *          경계를 넘는 구조체 배치다. 예전에는 손 판 번호(`kRHIModuleABIVersion`, v64 까지)와 스탬프 문자열을 사람이 올렸고, 잊으면 옛 백엔드 DLL 이
 *          한 칸 밀린 가상 함수를 불렀다. 이제 로더(`RHIBackendRegistry::tryLoadModule`)는 모든 동적 모듈과 같은 대조(`engine::isModuleBuiltWithThisEngine`)로 백엔드 모듈이 박은 엔진 ABI 스탬프
 *          (`sw_moduleEngineABIStamp_<모듈>`, Core · Engine · ModuleAPI 헤더 내용의 지문 — `cmake/Modules/Build/EngineABIStamp.cmake`)를
 *          돌고 있는 엔진의 것(`engine::getEngineABIStamp`)과 대조한다. 헤더가 바뀌면 빌드가 지문을 다시 만들므로 올릴 번호가 없다
 *          (언리얼 모듈의 빌드가 만드는 `BuildId` 와 같은 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class IRHIDevice;

    using PFN_CreateRHIDevice = IRHIDevice* (*)( uint32 deviceGeneration ); ///< v45: 장치 세대를 등록표가 넘긴다
} // namespace sw
