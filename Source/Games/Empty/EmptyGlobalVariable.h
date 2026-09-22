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

    /**
     * @brief `-gv_benchMaterialChurn=N` — 매 프레임 머티리얼 인스턴스 N 개의 **값**을 무작위로 바꿉니다.
     * @details 색·러프니스를 흔든다. 정적인 벤치는 머티리얼 바이트가 한 번 올라간 뒤 영원히 그대로라
     *          "바뀐 것만 올린다" 경로(`GpuMaterialGpu::_lastBytes` 비교, 인스턴스 CB 재작성,
     *          구조버퍼 재업로드)를 **한 번도 지나지 않는다.** 이 스위치가 그 길을 매 프레임 태운다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMaterialChurn );

    /**
     * @brief `-gv_benchMaterialChurnAdd=N` — 매 프레임 인스턴스 N 개를 새로 붙이거나 떼어냅니다.
     * @details 값이 아니라 **집합**을 흔든다. 붙이면 배치 키가 갈리고 머티리얼 원소 표에 자리가 하나
     *          늘며, 떼면 참조가 사라져 `retireUnusedMaterialElements` 가 그 자리를 회수하고 다음 번에
     *          재사용한다(자리를 **옮기지는 않는다** — 인스턴스에 적힌 materialIndex 가 그대로여야 한다).
     *          떼어낸 인스턴스는 렌더 패킷이 아직 들고 있을 수 있으므로, 소유가 실제로 마지막 참조를
     *          따라 사라지는지(상수버퍼 해제 포함)도 같이 검사된다.
     * @note 인스턴스 수는 512 개에서 멈춘다 — 그 뒤로는 새로 붙이는 대신 떼는 쪽으로 기운다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMaterialChurnAdd );

    /**
     * @brief `-gv_benchMaterialChurnKeyword=N` — N 프레임마다 키워드·멀티컴파일을 하나 흔듭니다.
     * @warning **셰이더가 다시 컴파일된다.** 퍼뮤테이션 해시가 바뀌면 배치 키가 갈리고 PSO 가 새로
     *          만들어지므로 프레임이 크게 튄다. 성능 측정용이 아니라 **퍼뮤테이션 경로가 살아 있는지**
     *          보는 스위치다. 값은 크게 준다(예: 60 = 1초에 한 번).
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMaterialChurnKeyword );

    /**
     * @brief `-gv_benchMeshShapes=N` — 벤치가 섞어 쓸 도형 수 (1=큐브만, 최대 5).
     * @details 순서는 큐브 · 구 · 실린더 · 캡슐 · 원뿔이다. 큐브 하나만 쓰면 삼각형이 12개뿐이고 면이
     *          축에 정렬돼 있어 래스터화·보간·컬링을 거의 흔들지 않는다 — 곡면을 섞으면 배치마다
     *          **정점 수가 크게 달라져서** 간접 인자·정점 버퍼 바인딩·바운드 반경이 전부 다른 값을 탄다.
     * @note 기본이 1 인 이유는 **기존 측정과 스크린샷을 그대로 두기 위해서**다. 도형을 섞으면 그림이
     *       달라지므로 예전 수치와 직접 비교할 수 없다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMeshShapes );

    /**
     * @brief `-gv_benchMeshMorph=1` — 벤치 도형의 **정점**을 GPU 가 매 프레임 변형합니다.
     * @details 컴퓨트(meshmorph.hlsl)가 레스트 포즈를 읽어 결과 버퍼에 쓰고, 정점 셰이더가 그 결과를
     *          `SV_VertexID` 로 풀링한다. CPU 는 정점을 한 번도 다시 올리지 않는다 — `Mesh::setVertices` 로
     *          매 프레임 바꾸면 메시마다 정점 버퍼를 파괴하고 다시 만들고, 게임 스레드에서 부르는 것이라
     *          OpenGL 에서는 컨텍스트도 없다.
     * @note 기본이 0 인 이유는 **픽셀 비교 검증을 흔들지 않기 위해서**다. 켠 상태의 검증은
     *       `-gv_screenshotFrame` 으로 서로 다른 시각을 찍어 그림이 실제로 달라지는지로 본다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMeshMorph );

    /**
     * @brief `-gv_benchAnimate=0` — 벤치의 **모든** 시간 구동 변화를 멈춥니다 (기본 1 = 움직인다).
     * @details **기하가 있는 씬을 픽셀로 비교하려면 이것이 필요하다.** 벤치는 두 가지를 시간으로
     *          움직인다 — 컴퓨트(instanceanim)가 만드는 회전과, `update()` 의 사인파가 만드는 상하
     *          이동·스케일이다. 둘 다 벽시계 델타에서 나오므로 같은 `-gv_screenshotFrame` 으로 두 번
     *          찍어도 그림이 다르다. 실제로 같은 설정 두 실행이 **1.5%** 어긋났고, 그 위에서 셰이더
     *          변경의 0.5% 차이를 "다르다" 고 읽을 뻔했다 — 손대지 않은 대조군이 움직이면 하니스를
     *          먼저 의심할 것. 회전만 멈춰서는 부족하다(사인파가 남아 여전히 0.05% 흔들렸다).
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchAnimate );
    /**
     * @brief `-gv_benchMovePercent=<퍼센트>` — 프레임마다 위치·스케일을 다시 쓰는 큐브의 비율 (기본 100 = 전부).
     * @details "일부만 움직이는 씬" 을 재기 위한 것이다 — 트랜스폼 플러시가 더티 루트만 돌게 된 뒤(2026-09-22) 그 이득은
     *          전부 움직이는 벤치에서는 보이지 않는다. 10 이면 열 개 중 하나만 쓴다(index % 100 < percent).
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchMovePercent );

    /**
     * @brief `-gv_benchLights=N` — 격자 위에 점광·스포트라이트를 N 개 흩뿌립니다 (주광은 별개).
     * @details 라이트 수를 늘리면 **픽셀당 루프가 길어진다** — 디퍼드는 화면 픽셀 수만큼, 포워드는
     *          오버드로를 포함한 픽셀 수만큼이다. 그래서 두 경로의 라이트 비용이 어떻게 갈리는지를
     *          이 스위치 하나로 잰다(`-gv_deferred` 와 함께 쓴다).
     * @note 홀수 번째는 스포트라이트다 — 점광만 두면 원뿔 감쇠 경로가 한 번도 실행되지 않는다.
     * @note 기본이 0 인 이유는 **기존 측정과 스크린샷을 그대로 두기 위해서**다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchLights );

    /**
     * @brief `-gv_benchLightRadius=<유닛>` — 벤치 라이트가 닿는 반경 (0 이면 격자 간격에서 정한다).
     * @details 반경은 **비용을 정하는 값**이다. 크면 한 픽셀에 닿는 라이트가 늘어 루프가 실제로 돌고,
     *          작으면 감쇠가 0 이라 일찍 빠진다 — 타일 컬링의 효과를 재려면 이 값을 흔들어야 한다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_FLOAT( gv_benchLightRadius );

    /**
     * @brief `-gv_benchGround=1` — 격자 아래에 바닥 평면을 깝니다.
     * @details **그림자를 눈으로 확인하려면 받을 면이 있어야 한다.** 큐브만 떠 있으면 그림자는 다른
     *          큐브 위에만 지고, 그것도 큐브가 서로 떨어져 있어 거의 보이지 않는다 — 그림자 투영이
     *          맞는지 그림으로 판단할 수가 없었다.
     * @note 기본이 0 인 이유는 **기존 측정과 스크린샷을 그대로 두기 위해서**다.
     */
    SW_EXTERN_GLOBAL_VARIABLE_INT( gv_benchGround );
} // namespace sw
