/**
 * @file HZBPyramid.h
 * @brief HZB(계층 깊이 버퍼 — 장면 깊이의 최소 · 최대 밉 사슬)의 배치와, 그 피라미드를 읽는 쪽이 함께 쓰는 순수 함수(경계 상자 → 화면 사각형 · 밉 고르기)입니다.
 * @details 피라미드는 뷰마다 구조 버퍼 하나(float2 — x 최소 깊이 · y 최대 깊이)에 밉 0 부터 차례로 붙는다. 밉 0 은 깊이 사각형 2 x 2 텍셀마다 하나이고, 밉 m 의 크기는
 *          ceil( 깊이 / 2^(m+1) ) 이라 위 밉의 텍셀 하나가 아래 밉의 2 x 2 를 덮는다(가장자리의 홀수 줄은 넘친 쪽을 접어 넣는다 — 보수적).
 *          GPU 컬링(gpu_cull.hlsl)은 최대 깊이로 가림을, 화면 공간 반사(screen_space_reflection.hlsl)는 최소 깊이로 빈 칸 건너뛰기(hi-z trace)를 한다 — 같은 피라미드다
 *          (언리얼 FurthestHZB · ClosestHZB 를 한 버퍼에 둔 모양). 셰이더 쪽 같은 식은 hzb.hlsli 다(바꾸면 두 곳을 함께 고친다).
 *          텍스처 밉 사슬이 아니라 구조 버퍼인 까닭: 에뮬 백엔드(DX11 · GL · WebGPU)의 컴퓨트가 밉마다 UAV 를 걸거나 샘플 텍스처를 읽지 못한다 — 버퍼는 다섯 백엔드가 같은 길이다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct HZBScreenRect
     * @brief 월드 경계 상자를 한 뷰-투영으로 옮긴 화면 사각형(UV, 0 ~ 1 로 자른다)과 가장 가까운 깊이입니다.
     */
    struct SW_API HZBScreenRect
    {
        float32 _minU{ 0.0f };
        float32 _minV{ 0.0f };
        float32 _maxU{ 0.0f };
        float32 _maxV{ 0.0f };
        float32 _minDepth{ 1.0f }; ///< 상자 모서리 가운데 가장 가까운 NDC 깊이(0 가까움 · 1 멂)

        /// 이보다 작은 클립 w 는 카메라 뒤 · 눈 가까이로 본다(gpu_cull.hlsl `kMinOcclusionClipW`).
        static constexpr float32 kMinClipW = 1.0e-4f;

        /**
         * @brief 월드 상자(중심 @p center, 반 크기 @p extent)의 여덟 모서리를 @p viewProj(행 벡터 규약)로 옮겨 화면 사각형을 구합니다.
         * @return 그 깊이가 자리를 모르면(모서리 하나라도 카메라 뒤 · 가까운 평면 앞, 사각형이 화면 밖) nullopt — 부르는 쪽은 "보인다" 로 본다.
         */
        static optional<HZBScreenRect> projectBox( const float4x4& viewProj, const float3& center, const float3& extent );
    };

    /**
     * @struct HZBLayout
     * @brief 깊이 사각형 하나에서 지은 피라미드의 밉 크기 · 시작 원소입니다. 파일 머리말 참고.
     */
    struct SW_API HZBLayout
    {
        /// 밉 수 상한 — 깊이 한 축 2^16 px 까지 1 x 1 에 닿는다.
        static constexpr uint32 kMaxMipCount = 16;
        /// 텍셀 하나가 16 px(2^(3+1))인 밉 — 가림 타일 되읽기(`FrameRendererUtil::kOcclusionTileSize`)가 이 밉의 최대 깊이다. 피라미드는 늘 이 밉까지 있다.
        static constexpr uint32 kTileMip = 3;

        /** @brief 밉 하나입니다. */
        struct Mip
        {
            uint32 _width{ 0 };
            uint32 _height{ 0 };
            uint32 _offset{ 0 }; ///< 버퍼 안 첫 원소(float2 단위)
        };

        uint32 _depthWidth{ 0 }; ///< 지은 깊이 사각형(px)
        uint32 _depthHeight{ 0 };
        uint32 _mipCount{ 0 };   ///< 0 이면 피라미드가 없다(깊이 크기 0)
        uint32 _texelCount{ 0 }; ///< 모든 밉의 원소 수 — 버퍼 용량
        Mip    _arrMip[kMaxMipCount]{};

        /**
         * @brief 깊이 사각형 @p depthWidth x @p depthHeight 의 피라미드 배치를 만듭니다. 1 x 1 에 닿거나(그리고 `kTileMip` 까지 있거나) 상한에 닿으면 멈춘다.
         */
        static HZBLayout make( uint32 depthWidth, uint32 depthHeight );
        /** @brief 깊이 한 축 @p depthExtent 의 밉 @p mip 크기입니다 — ceil( 깊이 / 2^(mip+1) ), 최소 1(hzb.hlsli `HZB::computeMipSize` 와 같다). */
        static constexpr uint32 computeMipExtent( uint32 depthExtent, uint32 mip )
        {
            const uint32 shift  = mip + 1u;
            const uint32 extent = ( depthExtent + ( 1u << shift ) - 1u ) >> shift;
            return extent < 1u ? 1u : extent;
        }

        /**
         * @brief 화면 사각형 @p rect 를 한 축 @p footprintTexels 텍셀 안에 덮는 가장 고운 밉을 고릅니다(hzb.hlsli `HZB::selectMip` 와 같다).
         * @details 사각형을 깊이 픽셀로 옮겨 밉마다 (끝 텍셀 - 첫 텍셀 + 1)을 센다 — 2^n 경계를 걸친 작은 사각형도 넘치지 않는다.
         * @return 맞는 밉이 없으면(피라미드가 없거나 상한 밉에서도 넘친다) nullopt — 부르는 쪽은 "보인다" 로 본다.
         */
        optional<uint32> selectMip( const HZBScreenRect& rect, uint32 footprintTexels ) const;
    };
} // namespace sw
