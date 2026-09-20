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
    SW_GLOBAL_VARIABLE_INT( gv_benchMaterialChurn, 0, "프레임당 값을 무작위로 바꿀 머티리얼 인스턴스 수 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchMaterialChurnAdd, 0, "프레임당 새로 붙이거나 떼어낼 머티리얼 인스턴스 수 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchMaterialChurnKeyword, 0, "N 프레임마다 키워드·멀티컴파일을 흔듭니다 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchMeshShapes, 1, "벤치가 섞어 쓸 도형 수 (1=큐브만 · 최대 5: 큐브·구·실린더·캡슐·원뿔)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchMeshMorph, 0, "벤치 도형의 정점을 GPU 가 매 프레임 변형합니다 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchAnimate, 1, "벤치의 시간 구동 변화(회전·상하 이동·스케일) (0=멈춤, 픽셀 비교 검증용)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchLights, 0, "격자에 흩뿌릴 점광·스포트라이트 수 (주광은 별개)" );
    SW_GLOBAL_VARIABLE_FLOAT( gv_benchLightRadius, 0.0f, "벤치 라이트 반경 (0=격자 간격에서 정한다)" );
    SW_GLOBAL_VARIABLE_INT( gv_benchGround, 0, "격자 아래에 바닥 평면을 깝니다 (그림자를 받는 면)" );
} // namespace sw
