/**
 * @file OctahedralNormal.h
 * @brief 단위 벡터(법선 · 탄젠트)를 `snorm16` 둘(4 바이트)에 담는 옥타헤드럴 인코딩 `OctahedralNormal` 입니다 — **저장 전용**입니다.
 * @details 단위 구를 팔면체(|x| + |y| + |z| = 1)에 투영하고 아래 반구(z < 0)를 접어 [-1, 1]^2 사각형에 펼친 두 좌표를 `snorm16` 으로 담습니다.
 *          12 바이트 `float3` 의 1/3 이고, 같은 4 바이트인 `RGB10A2` 노멀(성분마다 10 비트)보다 방향 오차가 작습니다. 법선이 xyz 세 성분이 아니라 방향
 *          하나이므로 정규화가 풀 때 한 번 일어납니다.
 *          - **셰이더와 같은 식**: 풀이는 `Resource/engine/shaders/binding.hlsli` 의 `VertexAnimation::unpackNormal` 과 같은 팔면체 식입니다(`decodeOctant`).
 *            달라지는 것은 좌표 해상도뿐입니다(VAT 는 12 + 12 비트 정수를 실수 하나에 담고, 여기는 `snorm16` 두 개). 둘이 어긋나면 안 되므로
 *            테스트(`OctahedralNormalTest.DecodeMatchesShaderFormula`)가 셰이더 본문을 옮겨 적은 식과 비교합니다. 인코딩은 `MeshVertexAnimation::packNormal` 과 같은 식입니다.
 *          - **오차**: 좌표 한 칸이 2/65535 라 방향 오차는 접힘 근처의 늘어남을 고려해도 1e-4 라디안(약 0.006°) 아래이고, 테스트가 모든 옥탄트 · 극점 · 축 방향과
 *            결정적 난수 방향에서 이 상한을 못 박습니다.
 *          - 입력은 단위 길이일 필요가 없습니다(방향만 씁니다). 영벡터나 NaN 은 +z 로 갑니다. 풀면 항상 단위 벡터입니다.
 *          - 풀었던 단위 벡터를 다시 인코딩하면 같은 비트가 나온다고 보장하지 않습니다(양자화 단계 한 칸 안에서 흔들 수 있음). 같은 입력은 어느 플랫폼에서나 같은 비트입니다.
 *          상용 엔진의 대응: 언리얼의 옥타헤드럴 법선(Nanite · G버퍼), 유니티 SRP `PackNormalOctQuadEncode`.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct OctahedralNormal
     * @brief 옥타헤드럴 좌표 둘(`_u`, `_v`)입니다. 메모리에서 `snorm16x2`(DXGI `R16G16_SNORM`)와 같은 배치라 GPU 가 그대로 `float2` 로 풉니다.
     */
    struct OctahedralNormal final
    {
        snorm16 _u{};
        snorm16 _v{};

        /** @brief 단위 길이가 아니어도 되는 방향 @p direction 을 인코딩합니다. 영벡터 · NaN 은 +z 입니다. */
        [[nodiscard]] static OctahedralNormal fromNormal( const float3& direction ) noexcept
        {
            const float32 sum = MathUtil::abs( direction._x ) + MathUtil::abs( direction._y ) + MathUtil::abs( direction._z );
            float32       u   = 0.0f;
            float32       v   = 0.0f;
            if ( sum > 0.0f )
            {
                u = direction._x / sum;
                v = direction._y / sum;
            }
            if ( direction._z < 0.0f )
            {
                const float32 foldedU = ( 1.0f - MathUtil::abs( v ) ) * getSignNotZero( u );
                const float32 foldedV = ( 1.0f - MathUtil::abs( u ) ) * getSignNotZero( v );
                u                     = foldedU;
                v                     = foldedV;
            }
            return OctahedralNormal{ snorm16::fromFloat( u ), snorm16::fromFloat( v ) };
        }

        /** @brief 단위 벡터로 풉니다. */
        [[nodiscard]] float3 toNormal() const noexcept { return decodeOctant( _u.toFloat(), _v.toFloat() ); }

        /**
         * @brief 옥타헤드럴 좌표 (@p u, @p v) ∈ [-1, 1]^2 를 단위 벡터로 풉니다. 셰이더 `VertexAnimation::unpackNormal` 의 식과 같습니다.
         */
        [[nodiscard]] static float3 decodeOctant( const float32 u, const float32 v ) noexcept
        {
            float3 normal{ u, v, 1.0f - MathUtil::abs( u ) - MathUtil::abs( v ) };
            if ( normal._z < 0.0f )
            {
                const float32 foldedX = ( 1.0f - MathUtil::abs( normal._y ) ) * getSignNotZero( normal._x );
                const float32 foldedY = ( 1.0f - MathUtil::abs( normal._x ) ) * getSignNotZero( normal._y );
                normal._x             = foldedX;
                normal._y             = foldedY;
            }
            return normal.normalize();
        }

    private:
        /** @brief 0 을 양수로 보는 부호(+1 / -1)입니다. 셰이더의 `x >= 0 ? 1 : -1` 과 같습니다. */
        [[nodiscard]] static constexpr float32 getSignNotZero( const float32 value ) noexcept { return value >= 0.0f ? 1.0f : -1.0f; }
    };

    // CPU · GPU 구조체 미러에 쓰이므로 크기 · 정렬을 못 박는다(`R16G16_SNORM` 과 같은 4 바이트, 패딩 없음).
    static_assert( sizeof( OctahedralNormal ) == 4 && alignof( OctahedralNormal ) == 2, "OctahedralNormal 은 snorm16 두 개, 4 바이트다" );
    static_assert( sizeof( OctahedralNormal ) == sizeof( snorm16x2 ), "R16G16_SNORM 과 같은 크기다" );
    static_assert( std::is_trivially_copyable_v<OctahedralNormal> && std::is_standard_layout_v<OctahedralNormal>, "OctahedralNormal 은 비트 복사가 가능해야 한다" );
} // namespace sw
