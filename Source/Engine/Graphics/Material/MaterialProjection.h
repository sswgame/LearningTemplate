/**
 * @file MaterialProjection.h
 * @brief 머티리얼이 텍스처 · 무늬를 표면에 찍는 법의 CPU 식입니다 — 3 축 투영 가중치와 타일 반복 깨기(타일마다 해시로 고른 오프셋 · 회전 · 밝기).
 * @details 셰이더 함수 묶음 `material_functions.hlsli` 의 `SurfaceFunction::computeTriplanarWeights` · `computeTileVariation` 과 같은 식이다. GPU 는 단위 테스트에서 돌지 않으므로
 *          이 식을 CPU 에 두고 값을 테스트로 박는다(`MaterialProjectionTest`) — 셰이더를 바꾸면 이 파일과 테스트를 함께 바꾼다. 배치 · 데칼처럼 CPU 가 같은 타일 변형을
 *          알아야 하는 쪽(같은 타일의 데칼 회전)도 이 식을 읽는다.
 *          언리얼 WorldAlignedTexture(3 축) · TextureVariation(타일 해시 오프셋), 유니티 HDRP 의 Triplanar · Stochastic 샘플링 노드와 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /**
     * @struct MaterialProjection
     * @brief 3 축 투영 가중치와 타일 변형을 계산하는 static 함수 묶음입니다.
     */
    struct MaterialProjection
    {
        /// @brief 타일 해시의 x · y 곱수(서로 다른 홀수 — 이웃 타일이 같은 값을 받지 않게 축을 어긋낸다). 셰이더와 같은 값이다.
        static constexpr uint32 kTileHashX = 0x8DA6B343u;
        static constexpr uint32 kTileHashY = 0xD8163841u;
        /// @brief 씨앗에 더하는 소금 — `mix32( 0 ) == 0` 이라 타일 (0, 0) · 씨앗 0 이 늘 0 이 되지 않게 한다.
        static constexpr uint32 kTileHashSalt = 0x68E31DA4u;
        /// @brief 해시의 상위 24 비트를 [0, 1) 로 옮기는 배율이다(하위 비트는 품질이 낮다).
        static constexpr float32 kHashToUnit = 1.0f / 16777216.0f;
        /// @brief 3 축 날카로움의 아래 한계 — 1 보다 무디면 축 사이가 지나치게 섞인다(0 은 세 축이 같다).
        static constexpr float32 kMinTriplanarSharpness = 1.0f;
        /// @brief 축 성분의 아래 한계 — pow( 0, s ) 의 log( 0 ) 을 피한다(셰이더와 같은 값).
        static constexpr float32 kMinTriplanarAxis = 1.0e-5f;

        /** @brief 타일 하나의 변형입니다 — UV 오프셋(0..1), 90 도 회전 횟수(0..3), 밝기 흔들림(0..1, 0.5 가 중립). */
        struct TileVariation
        {
            float2  _offset{ 0.0f, 0.0f };
            uint32  _rotation{ 0 };
            float32 _brightness{ 0.5f };
        };

        /**
         * @brief 노멀 @p normal 의 3 축 투영 가중치(x 면 · y 면 · z 면, 합 1)입니다. 가중치 = |n|^@p sharpness 를 합으로 나눈 것이다.
         * @details 날카로움이 클수록 축 사이 섞이는 띠가 좁다(지형은 4). 1 보다 작으면 1 로 둔다 — 0 은 세 축이 언제나 같아 투영이 아니다.
         */
        static float3 computeTriplanarWeights( const float3& normal, float32 sharpness )
        {
            const float32 exponent = MathUtil::max( sharpness, kMinTriplanarSharpness );
            const float32 weightX  = MathUtil::pow( MathUtil::max( MathUtil::abs( normal._x ), kMinTriplanarAxis ), exponent );
            const float32 weightY  = MathUtil::pow( MathUtil::max( MathUtil::abs( normal._y ), kMinTriplanarAxis ), exponent );
            const float32 weightZ  = MathUtil::pow( MathUtil::max( MathUtil::abs( normal._z ), kMinTriplanarAxis ), exponent );
            const float32 inverse  = 1.0f / ( weightX + weightY + weightZ );
            return float3{ weightX * inverse, weightY * inverse, weightZ * inverse };
        }

        /** @brief 타일 좌표 (@p tileX, @p tileY) 와 씨앗 @p seed 의 32 비트 해시입니다(`HashUtil::mix32` 두 번, 씨앗에 소금을 더한다). */
        static constexpr uint32 hashTile( int32 tileX, int32 tileY, uint32 seed )
        {
            return HashUtil::mix32( ( static_cast<uint32>( tileX ) * kTileHashX ) ^ HashUtil::mix32( ( static_cast<uint32>( tileY ) * kTileHashY ) ^ ( seed + kTileHashSalt ) ) );
        }

        /**
         * @brief 타일 (@p tileX, @p tileY) 의 변형입니다. 해시 하나를 다시 섞어 오프셋 u · v, 회전, 밝기를 서로 다른 값에서 뽑는다.
         * @details 같은 타일 · 씨앗은 늘 같은 값이다(프레임 · 백엔드와 무관). 타일 경계의 이음매는 쓰는 쪽(셰이더)이 이웃 타일과 섞어 감춘다.
         */
        static TileVariation computeTileVariation( int32 tileX, int32 tileY, uint32 seed )
        {
            const uint32  hash0 = hashTile( tileX, tileY, seed );
            const uint32  hash1 = HashUtil::mix32( hash0 ^ kTileHashX );
            const uint32  hash2 = HashUtil::mix32( hash1 ^ kTileHashY );
            TileVariation result{};
            result._offset     = float2{ static_cast<float32>( hash0 >> 8 ) * kHashToUnit, static_cast<float32>( hash1 >> 8 ) * kHashToUnit };
            result._rotation   = hash2 & 3u;
            result._brightness = static_cast<float32>( hash2 >> 8 ) * kHashToUnit;
            return result;
        }
    };
} // namespace sw
