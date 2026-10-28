/**
 * @file SphericalHarmonics.h
 * @brief 2차 구면 조화 함수(SH9, RGB)입니다. 라이트 프로브의 조명 표현이고, 유니티 APV · 라이트 프로브와 같은 차수입니다.
 * @details 계수는 실수 기저(Sloan, "Stupid Spherical Harmonics Tricks" 의 순서)로 9 개, 채널마다 하나씩 27 개입니다.
 *          투영은 방향마다 복사휘도에 기저를 곱해 더하고(`addSample`, 균일 구 샘플이면 가중치 4π / 샘플 수), 합산은 계수를 그대로 더합니다(회전 없음).
 *          평가는 둘입니다 — `evaluateRadiance` 는 그 방향의 복사휘도, `evaluateIrradiance` 는 코사인 로브(램버트)를 컨볼루션한 조도입니다
 *          (차수마다 Â0 = π, Â1 = 2π/3, Â2 = π/4 를 곱한다, Ramamoorthi-Hanrahan 2001).
 *          GPU 원소(`makeGPUElement`)는 float4 일곱 개이고 셰이더가 내적 몇 번으로 조도 / π(알베도에 곱할 값)를 얻게 상수를 미리 곱해 둡니다
 *          (유니티 ShadeSH9 의 SHAr..SHC 배치와 같다 — 마지막 원소의 w 는 0).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct SH9Color
     * @brief RGB 2차 구면 조화 함수 계수입니다. `_arrCoefficient[계수 * 3 + 채널]` 입니다.
     */
    struct SW_API SH9Color
    {
        static constexpr uint32 kBasisCount       = 9;
        static constexpr uint32 kChannelCount     = 3;
        static constexpr uint32 kCoefficientCount = kBasisCount * kChannelCount;
        /// @brief GPU 원소의 float4 수입니다(셰이더 `LightProbe::evaluateSphericalHarmonics` 와 같은 배치).
        static constexpr uint32 kGPUFloat4Count = 7;

        float32 _arrCoefficient[kCoefficientCount]{};

        /** @brief 방향 @p direction(단위 벡터)의 기저 값 아홉 개입니다. */
        static void computeBasis( const float3& direction, float32 ( &outArrBasis )[kBasisCount] );
        /** @brief 모든 방향에서 같은 복사휘도 @p radiance 의 계수입니다(해석해 — 하늘이 한 색일 때). */
        static SH9Color makeConstant( const float3& radiance );
        /**
         * @brief 한 방향 @p toLight 에서만 오는 빛(방향광, 델타 분포)의 계수입니다. @p irradiance 는 그 빛에 수직인 면이 받는 조도입니다.
         * @details 델타 분포의 투영은 그 방향의 기저 값 × 세기다. 조도 평가(`evaluateIrradiance`)가 그 방향에서 가장 크다.
         */
        static SH9Color makeDirectional( const float3& toLight, const float3& irradiance );
        /**
         * @brief 결정적 균일 구 방향 하나를 냅니다(난수 둘을 꺼낸다). 같은 씨앗이면 어느 플랫폼에서도 같은 수열입니다(투영의 샘플).
         */
        static float3 makeUniformSphereDirection( DeterministicRandom32& inoutRandom );

        /** @brief 방향 @p direction 에서 온 복사휘도 @p radiance 를 가중치 @p weight 로 투영해 더합니다. 균일 구 샘플 N 개면 가중치는 4π / N 입니다. */
        void addSample( const float3& direction, const float3& radiance, float32 weight );
        /** @brief 다른 계수를 더합니다(회전 없음 — 같은 좌표계의 빛을 합친다). */
        void add( const SH9Color& other );
        /** @brief 모든 계수에 @p factor 를 곱합니다. */
        void scale( float32 factor );

        /** @brief 방향 @p direction 에서 오는 복사휘도입니다. */
        float3 evaluateRadiance( const float3& direction ) const;
        /** @brief 노멀 @p normal 인 면이 받는 조도(코사인 로브 컨볼루션)입니다. 램버트 반사광은 알베도 × 조도 / π 입니다. */
        float3 evaluateIrradiance( const float3& normal ) const;

        /**
         * @brief 셰이더 원소(float4 일곱 개)를 만듭니다. 셰이더 `LightProbe::evaluateSphericalHarmonics( n )` 의 결과가 `evaluateIrradiance( n ) / π` 입니다.
         * @details [0..2] 채널 r g b 의 (L1 x, L1 y, L1 z, L0) · [3..5] 채널마다 L2 의 xy · yz · zz · xz 항 · [6] L2 의 x² − y² 항 rgb, w 0.
         */
        void makeGPUElement( float4 ( &outArrElement )[kGPUFloat4Count] ) const;
    };
} // namespace sw
