/**
 * @file EmptyGlobalVariable.h
 * @brief SWGame 전용 전역 변수 — 모듈 로컬 등록 리스트와 벤치 스위치들
 *
 * @note **이 헤더를 include 하면 `SW_GVM_MODULE_HEAD` 가 바뀐다.** 기본값은 Engine.dll 의 등록
 *       리스트라, 그대로 쓰면 모듈이 언로드돼도 매니저가 사라진 DLL 안의 변수를 계속 가리킨다.
 *       모듈 로컬 헤드에 붙여야 `unregisterVariablesByModule` 이 통째로 걷어낼 수 있다.
 */
#pragma once
#include "Core/GlobalVariable/GlobalVariableManager.h"

#undef SW_GVM_MODULE_HEAD
#define SW_GVM_MODULE_HEAD() ( ::sw::game::getGlobalVariableHead() )

SW_DECLARE_MODULE_GLOBAL_VARIABLES( game );

namespace sw
{
    /**
     * @brief `-gv_benchMeshes=N` — 시작 시 만들 벤치 큐브 수. 0 이면 만들지 않습니다.
     * @details 이것이 0 이 아니면 `EmptyGame` 은 `GameConfig` 의 시작 씬 대신 벤치 씬을 세운다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMeshes );

    /**
     * @brief `-gv_benchMaterialInstances=1` — 벤치 큐브마다 개별 MaterialInstance 를 줍니다.
     * @details 배치 키가 인스턴스 포인터를 포함하므로 배치가 1개에서 N개로 갈라진다 — 배치·드로우
     *          경로에 실제 부하를 거는 유일한 방법이다.
     * @warning **DX12 에서 100% 크래시한다.** 렌더 중 상수버퍼를 만들면서 커맨드 얼로케이터가
     *          사용 중에 Reset 되는 기존 버그(간헐 3/8)를 확실히 터뜨린다. 그래서 기본은 꺼 두되,
     *          그 버그를 재현·수정할 때 쓰라고 남겨 둔다. DX11/Vulkan/GL 은 정상이다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMaterialInstances );

    /**
     * @brief `-gv_benchMeshVariants=N` — 벤치가 쓸 **메시 종류 수**. 배치 키에 메시가 들어가므로 곧 배치 수다.
     * @details 기본 1 은 모든 큐브가 한 배치로 묶여 드로우 경로(드로우별 상수·바인딩)를 전혀 재지 않는다.
     *          실제 씬은 늘 여러 메시를 쓰므로, 드로우 경로를 재거나 다중 배치 버그를 보려면 이 값을 올린다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMeshVariants );

    /**
     * @brief `-gv_benchTransparent=<퍼센트>` — 벤치 큐브 중 이 비율을 투명으로 만듭니다 (0=전부 불투명).
     * @details 투명 경로는 불투명과 다른 길을 간다 — 배치가 깊이순으로 갈리고, 컬링이 압축한 순서를
     *          instancesort 가 되돌리며, 블렌딩 PSO 를 쓴다. 벤치가 전부 불투명이면 그 길을 한 번도
     *          지나지 않는다. 투명 큐브는 **소수의 머티리얼 인스턴스를 나눠 쓰므로** 한 배치에 투명
     *          인스턴스가 여럿 들어간다 — 그래야 배치 안의 정렬이 실제로 검사된다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchTransparent );
} // namespace sw
