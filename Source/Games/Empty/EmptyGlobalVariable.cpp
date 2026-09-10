/**
 * @file EmptyGlobalVariable.cpp
 * @brief SWGame 전역 변수 정의와 모듈 로컬 등록/해제
 */
#include "pch.h"

#include "Games/Empty/EmptyGlobalVariable.h"

#include "GameFramework/Base/GameService.h"

#include "sw/config/ConfigConstants.h"

SW_IMPLEMENT_MODULE_GLOBAL_VARIABLES( game, config::kTargetGameModule );

namespace sw
{
    SW_GLOBAL_VARIABLE_INT( gv_benchMeshes, 0, "시작 시 생성할 벤치 큐브 수 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchMaterialInstances, 0, "벤치 큐브마다 MaterialInstance 부여 (DX12 크래시 재현용)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchMeshVariants, 1, "벤치 메시 종류 수 (= 배치 수, 드로우 경로 측정용)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchTransparent, 25, "벤치 큐브 중 투명으로 만들 비율 (퍼센트)" );
} // namespace sw
