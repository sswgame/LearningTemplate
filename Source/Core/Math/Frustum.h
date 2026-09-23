/**
 * @file Frustum.h
 * @brief 뷰-투영 행렬에서 뽑은 절두체의 여섯 평면과 겹침 판정입니다. GPU 컬링 입력(`RenderView`)과 CPU 공간 질의(`BVHTree3D`)가 같은 식을 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct Frustum
     * @brief 절두체의 여섯 평면(왼쪽 · 오른쪽 · 아래 · 위 · 가까운 면 · 먼 면)입니다. 법선은 안쪽을 향하고 길이 1 로 정규화돼 있습니다.
     * @details 예전에는 이 식(Gribb-Hartmann)이 `RenderView::setViewProjection` 과 `BVHTree3D::queryFrustum` 에 두 벌 있었고,
     *          법선 길이가 0 인 퇴화 평면도 서로 다르게 다뤘습니다(한쪽은 0 으로 지우고, 다른 쪽은 정규화하지 않은 채 두었습니다).
     *          엔진은 행벡터 규약(`mul( v, M )`)이라 클립 좌표는 M 의 **열**과의 내적입니다. 그래서 열을 더하고 뺍니다.
     *          행 우선으로 저장하므로 col( i ) = ( _1i, _2i, _3i, _4i ) 입니다. 깊이는 D3D 규약 [0,1] 이라 near 평면은 열 2 하나입니다
     *          (GL 도 glClipControl 로 같은 규약에 맞춰 둡니다). `_arrPlane` 은 float4 여섯 개가 빈틈없이 이어져 GPU 상수버퍼에
     *          그대로 복사됩니다. 셰이더는 `dot( plane.xyz, center ) + plane.w < -radius` 이면 바깥으로 봅니다.
     */
    struct Frustum
    {
        float4 _arrPlane[6];

        /** @brief 뷰-투영 행렬에서 여섯 평면을 뽑아 정규화합니다. */
        static Frustum fromViewProjection( const float4x4& m ) noexcept
        {
            Frustum frustum;
            frustum._arrPlane[0] = makePlane( m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 ); // left
            frustum._arrPlane[1] = makePlane( m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 ); // right
            frustum._arrPlane[2] = makePlane( m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 ); // bottom
            frustum._arrPlane[3] = makePlane( m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 ); // top
            frustum._arrPlane[4] = makePlane( m._13, m._23, m._33, m._43 );                                 // near (z >= 0)
            frustum._arrPlane[5] = makePlane( m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 ); // far
            return frustum;
        }

        /**
         * @brief 상자가 절두체와 겹치면(일부라도 안쪽이면) true 입니다.
         * @details 평면마다 법선 쪽으로 가장 먼 꼭짓점(p-vertex) 하나만 봅니다. 그것마저 바깥이면 상자 전체가 바깥입니다.
         *          보수적인 판정이라 모서리 근처의 상자는 실제로는 바깥이어도 겹친다고 답할 수 있습니다(컬링에는 그것으로 충분합니다).
         */
        bool overlapsBox( const float3& boxMin, const float3& boxMax ) const noexcept
        {
            for ( const float4& plane : _arrPlane )
            {
                const float3 farthest{ plane._x > 0.0f ? boxMax._x : boxMin._x,
                                       plane._y > 0.0f ? boxMax._y : boxMin._y,
                                       plane._z > 0.0f ? boxMax._z : boxMin._z };
                if ( plane._x * farthest._x + plane._y * farthest._y + plane._z * farthest._z + plane._w < 0.0f )
                    return false;
            }
            return true;
        }

        /** @brief 구가 절두체와 겹치면 true 입니다. 셰이더의 판정과 같은 식입니다. */
        bool overlapsSphere( const float3& center, float32 radius ) const noexcept
        {
            for ( const float4& plane : _arrPlane )
            {
                if ( plane._x * center._x + plane._y * center._y + plane._z * center._z + plane._w < -radius )
                    return false;
            }
            return true;
        }

    private:
        /** @brief 평면 하나를 정규화합니다. 법선 길이가 0 이면(퇴화한 행렬) 평면 전체를 0 으로 두어, 그 평면은 아무것도 거르지 않습니다. */
        static float4 makePlane( float32 x, float32 y, float32 z, float32 w ) noexcept
        {
            const float32 length = MathUtil::sqrt( x * x + y * y + z * z );
            const float32 scale  = ( length > 0.0f ) ? ( 1.0f / length ) : 0.0f;
            return float4{ x * scale, y * scale, z * scale, w * scale };
        }
    };
    static_assert( sizeof( Frustum ) == sizeof( float32 ) * 24, "Frustum 은 GPU 상수버퍼에 그대로 복사된다 — float4 여섯 개여야 한다" );
} // namespace sw
