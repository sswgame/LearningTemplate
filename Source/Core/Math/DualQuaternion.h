/**
 * @file DualQuaternion.h
 * @brief 회전 + 이동만 담는 듀얼 쿼터니언과 그 선형 혼합(DLB)입니다.
 *
 * @warning **스케일은 담지 못합니다.** 강체 변환 전용이므로 `fromMatrix` 는 행렬의 스케일을 떼어
 *          버리고, `toMatrix4x4` 는 스케일 1 인 행렬을 반환합니다. 스케일이 있는 포즈를 섞어야 하면
 *          스케일을 따로 보간한 뒤 다시 곱해야 합니다. `BlendSpace` 가 그렇게 합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @brief 3D 강체 변환(회전 + 이동)을 나타내는 듀얼 쿼터니언입니다. 캔디 래퍼 왜곡 없는 스키닝에 씁니다.
     */
    struct SW_API DualQuaternion
    {
        quaternion _real; /**< 회전을 담는 실수부입니다. */
        quaternion _dual; /**< 이동을 담는 허수부(= 0.5 * t * _real)입니다. */

        /** @brief 회전도 이동도 없는 항등 듀얼 쿼터니언입니다. */
        DualQuaternion();
        /** @brief 회전 @p r 과 이동 @p t 로 만듭니다. */
        DualQuaternion( const quaternion& r, const float3& t );
        /** @brief 실수부와 허수부를 그대로 받습니다. */
        DualQuaternion( const quaternion& real, const quaternion& dual );

        /** @brief 이동과 회전으로 만듭니다. */
        static DualQuaternion fromTransform( const float3& translation, const quaternion& rotation );
        /**
         * @brief 변환 행렬에서 만듭니다.
         * @details 스케일은 버립니다(강체 변환만 담는 표현이라 담을 자리가 없습니다). 회전은
         *          `float4x4::decompose` 로 뽑으므로 스케일이 섞인 행렬에서도 맞습니다.
         */
        static DualQuaternion fromMatrix( const float4x4& mat );
        /**
         * @brief 변환 행렬에서 만들고 떼어 낸 스케일을 @p outScale 로 돌려줍니다. 행렬은 `스케일 * 회전 * 이동`(행벡터)으로 읽습니다.
         * @details DQ 스키닝이 본마다 이 둘을 싣습니다(스케일은 따로 선형 혼합한 뒤 회전 앞에 곱한다 — `transformPoint` 와 mesh_skin.hlsl).
         *          축 하나가 0 인 행렬(숨긴 본)은 회전이 단위이고 스케일은 0 에 가깝습니다 — 점이 이동 자리로 모이는 것은 선형 스키닝과 같습니다.
         */
        static DualQuaternion fromMatrix( const float4x4& mat, float3& outScale );

        /** @brief 실수부 길이로 양쪽을 나눠 단위 듀얼 쿼터니언으로 만듭니다. */
        void normalize();
        /** @brief 정규화한 사본을 반환합니다. */
        DualQuaternion normalized() const;

        /** @brief 이동 성분을 반환합니다. */
        float3 getTranslation() const;
        /** @brief 회전 성분을 반환합니다. */
        quaternion getRotation() const;
        /** @brief 변환 행렬로 되돌립니다. 스케일은 항상 1 입니다. */
        float4x4 toMatrix4x4() const;

        /**
         * @brief 단위 듀얼 쿼터니언으로 점을 옮깁니다(회전 뒤 이동). `toMatrix4x4()` 로 옮긴 것과 같고, mesh_skin.hlsl 의 DQ 경로가 같은 식입니다.
         */
        float3 transformPoint( const float3& point ) const;
        /** @brief 회전만으로 방향을 옮깁니다(이동 없음). */
        float3 transformDirection( const float3& direction ) const;

        /** @brief 두 변환을 선형 결합한 뒤 정규화(DLB)해 최단 경로 스크루 보간을 만듭니다. */
        static DualQuaternion dlb( const DualQuaternion& a, const DualQuaternion& b, float32 t );
    };
} // namespace sw
