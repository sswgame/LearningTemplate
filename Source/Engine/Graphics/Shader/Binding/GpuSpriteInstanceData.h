/**
 * @file GpuSpriteInstanceData.h
 * @brief 인스턴스마다 다른 스프라이트 값(UV 사각형 · 색)을 셰이더가 읽는 꼴 그대로 묶은 12 바이트입니다.
 * @details HLSL 쪽은 `Resource/engine/shaders/instancedata.hlsli` 의 `uvStart` · `uvEnd` · `tint` 이고, 푸는 함수는
 *          `swComputeInstanceUvRect` · `swComputeInstanceTint` 입니다. 이 값은 `GpuInstance` 에 그대로 실리고(오프셋은
 *          ShaderBindingContractTest.InstanceElementLayoutMatchesCpuStruct 가 구운 바이너리로 대조합니다), 메시 컴포넌트 ·
 *          인스턴스 배치 항목이 들고 있다가 빌더가 옮깁니다. 컴포넌트 층(Object)이 렌더러(Renderer)를 include 할 수 없어
 *          이 묶음만 렌더러 아래의 계약 폴더에 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct GpuSpriteInstanceData
     * @brief 인스턴스 하나의 아틀라스 프레임(UV 사각형)과 색입니다. 기본값은 텍스처 전체 · 흰색 불투명입니다.
     * @details **머티리얼 인스턴스가 아니라 인스턴스에 싣는 이유:** 배치 키는 머티리얼 인스턴스입니다. 프레임마다 바뀌는 아틀라스 프레임 ·
     *          페이드 알파를 인스턴스 파라미터로 바꾸면 스프라이트마다 인스턴스가 생겨 배치가 하나씩 갈립니다. 여기 실으면 같은 텍스처의
     *          스프라이트는 프레임 · 색이 달라도 한 드로우입니다(언리얼 Custom Primitive Data · 유니티 MaterialPropertyBlock 의 자리).
     *
     *          UV 사각형은 꼭짓점 둘로 담습니다: 사각형의 왼쪽 위가 읽는 `_uvStart` 와 오른쪽 아래가 읽는 `_uvEnd`(각각 unorm16 둘).
     *          (u, v, 폭, 높이) 그대로가 아니라 꼭짓점으로 담는 것은 **뒤집기** 때문입니다 — 폭이 음수인 프레임(좌우 반전)이 그대로 들어갑니다.
     *          대신 꼭짓점은 [0, 1] 로 묶입니다(아틀라스 안의 사각형이 이 칸의 일입니다. 반복 타일링은 머티리얼 uvRect 의 일입니다).
     *          unorm16 은 4096 텍셀 아틀라스에서 텍셀의 1/16 보다 곱습니다. 색은 RGBA8 입니다.
     */
    struct GpuSpriteInstanceData
    {
        uint32 _uvStart{ 0u };        ///< (u, v) — unorm16 둘, u 가 하위 16비트
        uint32 _uvEnd{ 0xFFFFFFFFu }; ///< (u, v) — unorm16 둘. 기본은 (1, 1)
        uint32 _tint{ 0xFFFFFFFFu };  ///< RGBA8 unorm, r 이 하위 바이트. 기본은 흰색 불투명

        /**
         * @brief (u, v, 폭, 높이) 사각형과 색으로 만듭니다. 머티리얼 uvRect 와 같은 꼴입니다.
         * @details 꼭짓점 (u, v) 와 (u + 폭, v + 높이) 를 [0, 1] 로 묶어 담고, 색은 [0, 1] 로 묶어 8비트로 담습니다.
         */
        static GpuSpriteInstanceData make( const float4& uvRect, const float4& tint )
        {
            GpuSpriteInstanceData data{};
            data._uvStart = makeUnorm16x2( uvRect._x, uvRect._y );
            data._uvEnd   = makeUnorm16x2( uvRect._x + uvRect._z, uvRect._y + uvRect._w );
            data._tint    = makeRgba8( tint );
            return data;
        }

        /** @brief 실수 둘을 [0, 1] 로 묶어 unorm16 둘로 담습니다(x 가 하위 16비트). HLSL `swUnpackUnorm16x2` 의 역입니다. */
        static uint32 makeUnorm16x2( float32 x, float32 y )
        {
            const uint32 low  = static_cast<uint32>( MathUtil::saturate( x ) * 65535.0f + 0.5f );
            const uint32 high = static_cast<uint32>( MathUtil::saturate( y ) * 65535.0f + 0.5f );
            return low | ( high << 16 );
        }

        /** @brief 색을 [0, 1] 로 묶어 RGBA8 로 담습니다(r 이 하위 바이트). */
        static uint32 makeRgba8( const float4& color )
        {
            const uint32 red   = static_cast<uint32>( MathUtil::saturate( color._x ) * 255.0f + 0.5f );
            const uint32 green = static_cast<uint32>( MathUtil::saturate( color._y ) * 255.0f + 0.5f );
            const uint32 blue  = static_cast<uint32>( MathUtil::saturate( color._z ) * 255.0f + 0.5f );
            const uint32 alpha = static_cast<uint32>( MathUtil::saturate( color._w ) * 255.0f + 0.5f );
            return red | ( green << 8 ) | ( blue << 16 ) | ( alpha << 24 );
        }

        /** @brief 담긴 UV 사각형 (u, v, 폭, 높이) 입니다. 셰이더 `swComputeInstanceUvRect` 와 같은 값입니다(양자화 뒤). */
        float4 getUvRect() const
        {
            const float32 startU = static_cast<float32>( _uvStart & 0xFFFFu ) / 65535.0f;
            const float32 startV = static_cast<float32>( _uvStart >> 16 ) / 65535.0f;
            const float32 endU   = static_cast<float32>( _uvEnd & 0xFFFFu ) / 65535.0f;
            const float32 endV   = static_cast<float32>( _uvEnd >> 16 ) / 65535.0f;
            return float4{ startU, startV, endU - startU, endV - startV };
        }

        /** @brief 담긴 색입니다. 셰이더 `swComputeInstanceTint` 와 같은 값입니다(양자화 뒤). */
        float4 getTint() const
        {
            return float4{ static_cast<float32>( _tint & 0xFFu ) / 255.0f, static_cast<float32>( ( _tint >> 8 ) & 0xFFu ) / 255.0f,
                           static_cast<float32>( ( _tint >> 16 ) & 0xFFu ) / 255.0f, static_cast<float32>( _tint >> 24 ) / 255.0f };
        }

        /** @brief 세 칸이 모두 같으면 true 입니다. */
        bool operator==( const GpuSpriteInstanceData& other ) const
        {
            return _uvStart == other._uvStart && _uvEnd == other._uvEnd && _tint == other._tint;
        }
        /** @brief operator== 의 부정입니다. */
        bool operator!=( const GpuSpriteInstanceData& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw
