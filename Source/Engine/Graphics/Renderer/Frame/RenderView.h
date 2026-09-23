/**
 * @file RenderView.h
 * @brief "어느 눈으로 보는가" 를 한 자리에 모은 타입입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHIConstantBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @enum RenderViewType
     * @brief 프레임이 그리는 시점입니다. 컬링 · 정렬 산출물이 이 단위로 갈립니다.
     * @details 언리얼이 뷰마다 `FViewInfo` 와 `FInstanceCullingContext` 를 두는 자리와 같습니다.
     */
    enum class RenderViewType : uint32
    {
        Main   = 0, ///< 게임 카메라
        Shadow = 1, ///< 그림자 라이트
        Count  = 2
    };

    /**
     * @struct RenderView
     * @brief 뷰 하나가 갖는 상태입니다. 행렬, 절두체, 그리고 **자기 상수버퍼**를 담습니다.
     * @details 뷰마다 달라지는 것을 모두 여기 모읍니다. 흩어져 있던 동안 같은 실수를 두 번 했습니다:
     *          한 번은 패스 상수버퍼를 드로우들이 나눠 써서 모두 마지막 값을 읽었고, 한 번은 컬링
     *          상수버퍼를 뷰들이 나눠 써서 **메인 패스가 그림자 라이트의 절두체로 걸러졌습니다**
     *          (화면 절반이 사라졌습니다). 둘 다 "이 값은 누구 것인가" 가 타입에 없어서 생겼습니다.
     *
     *          이제 뷰를 얻으면 그 뷰의 버퍼가 딸려 옵니다. 다른 뷰의 것을 집으려면 일부러 다른
     *          인덱스를 써야 하고, 그것은 눈에 띕니다.
     *
     * @note 컬링 **산출물**(간접 인자 · 가시 인스턴스 목록)은 인스턴스 수에 맞춰 커지므로 GpuScene 이
     *       소유합니다(`GpuScene::getCullView`). 여기 있는 것은 그 산출물을 **만들 때 넣는 입력**입니다.
     */
    struct SW_API RenderView
    {
        /// @brief 이 뷰의 뷰 x 프로젝션입니다. 절두체는 여기서만 뽑습니다(둘이 어긋날 자리를 없앱니다).
        float4x4 _viewProj{};
        /// @brief 이 뷰의 눈 위치입니다. 투명 정렬 키(카메라까지의 거리)가 이 값을 씁니다.
        float3 _position{};
        /// @brief `_viewProj` 에서 뽑은 절두체 여섯 평면입니다(왼/오/아래/위/근/원, 정규화됨). 식은 `Frustum` 하나로, CPU 공간 질의(`BVHTree3D`)와 같습니다.
        Frustum _frustum{};
        /**
         * @brief 이 뷰 전용 컬링 상수버퍼입니다.
         * @details **뷰마다 하나여야 합니다.** 하나를 나눠 쓰면 두 번째 업로드가 첫 번째 디스패치가 읽을
         *          내용을 덮어씁니다. CPU 는 디스패치 사이에 쓰지만 GPU 는 제출 뒤에 읽기 때문입니다.
         */
        RHIConstantBufferSlot _cullCb;

        /** @brief 컬링을 돌릴 준비가 됐는지(상수버퍼가 있는지) 반환합니다. */
        bool isReadyForCulling() const { return _cullCb.isValid(); }

        /**
         * @brief 뷰 행렬을 정하고 절두체를 **함께** 갱신합니다.
         * @details 행렬만 바꾸고 평면을 안 바꾸면 컬링이 지난 프레임의 시점으로 판정합니다. 둘을 한
         *          함수로 묶어 그 상태가 존재할 수 없게 합니다.
         */
        void setViewProjection( const float4x4& viewProj );
    };
} // namespace sw
