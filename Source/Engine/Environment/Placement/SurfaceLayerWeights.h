/**
 * @file SurfaceLayerWeights.h
 * @brief 표면 레이어 가중치 여덟(float4 둘)입니다 - 지형 스플랫의 한 점, 배치 규칙의 레이어 필터, 에디터 브러시가 같은 값을 씁니다.
 * @details 레이어 0..3 이 `_low`, 4..7 이 `_high` 입니다. 지형 스플랫 맵 둘(RGBA8 한 장에 레이어 넷)과 같은 배치라 GPU 텍스처가 이 값에서 곧바로 나옵니다(`LayerWeightsAsset`).
 *          레이어 수의 정본은 `kLayerCount` 입니다 - 파일 형식 · 브러시 · 배치 필터 · 머티리얼이 이 값을 따릅니다. 기본값은 레이어 0 이 가득입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 표면 레이어 가중치 여덟입니다. 합이 1 이어야 하는 것은 아닙니다(`computeNormalized` 가 나눈다). */
    struct SW_API SurfaceLayerWeights
    {
        static constexpr uint32 kLayerCount = 8;
        /// @brief RGBA8 스플랫 평면 수입니다 - 평면 하나가 레이어 넷을 담는다(GPU 텍스처 한 장).
        static constexpr uint32 kPlaneCount = kLayerCount / 4;
        static_assert( kLayerCount % 4 == 0, "레이어 수는 RGBA 평면 한 장(넷)의 배수여야 한다" );

        float4 _low{ 1.0f, 0.0f, 0.0f, 0.0f };  ///< 레이어 0..3
        float4 _high{ 0.0f, 0.0f, 0.0f, 0.0f }; ///< 레이어 4..7

        /** @brief 레이어 @p layer 의 가중치입니다. 범위 밖이면 0 입니다. */
        float32 get( uint32 layer ) const
        {
            const float32 arrValue[kLayerCount] = { _low._x, _low._y, _low._z, _low._w, _high._x, _high._y, _high._z, _high._w };
            return layer < kLayerCount ? arrValue[layer] : 0.0f;
        }
        /** @brief 레이어 @p layer 의 가중치를 바꿉니다. 범위 밖이면 아무것도 하지 않습니다. */
        void set( uint32 layer, float32 value )
        {
            if ( layer >= kLayerCount )
                return;
            float32 arrValue[kLayerCount] = { _low._x, _low._y, _low._z, _low._w, _high._x, _high._y, _high._z, _high._w };
            arrValue[layer]               = value;
            _low                          = float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
            _high                         = float4{ arrValue[4], arrValue[5], arrValue[6], arrValue[7] };
        }
        /** @brief 여덟 가중치의 합입니다. */
        float32 getSum() const { return _low._x + _low._y + _low._z + _low._w + _high._x + _high._y + _high._z + _high._w; }
        /** @brief 합 1 로 나눈 값입니다. 합이 0 이하(거의 0)이면 레이어 0 이 가득입니다. 셰이더 `normalizeWeights`(terrain.hlsl)와 같은 식입니다. */
        SurfaceLayerWeights computeNormalized() const
        {
            const float32 total = getSum();
            if ( total <= 1.0e-5f )
                return SurfaceLayerWeights{};
            const float32       inverse = 1.0f / total;
            SurfaceLayerWeights result{};
            result._low  = _low * inverse;
            result._high = _high * inverse;
            return result;
        }
    };
} // namespace sw
