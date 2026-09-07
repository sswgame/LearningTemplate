#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/RenderView.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 평면 하나를 정규화해 넣습니다.
         * @details 길이를 1 로 맞춰야 셰이더가 `dot( plane.xyz, center ) + plane.w < -radius` 로
         *          바운드 반지름을 그대로 비교할 수 있다.
         */
        void setPlane( float32 ( &outPlanes )[6][4], uint32 index, float32 x, float32 y, float32 z, float32 w )
        {
            const float32 length = MathUtil::sqrt( x * x + y * y + z * z );
            const float32 scale  = ( length > 0.0f ) ? ( 1.0f / length ) : 0.0f;
            outPlanes[index][0]  = x * scale;
            outPlanes[index][1]  = y * scale;
            outPlanes[index][2]  = z * scale;
            outPlanes[index][3]  = w * scale;
        }
    } // namespace

    void RenderView::setViewProjection( const float4x4& viewProj )
    {
        _viewProj = viewProj;

        // Gribb-Hartmann. 엔진은 행벡터 규약(`mul( v, M )`)이라 클립 좌표는 M 의 **열**과의 내적이다 —
        // 그래서 열을 더하고 뺀다. 행 우선 저장이므로 col( i ) = ( _1i, _2i, _3i, _4i ) 이다.
        // 깊이는 D3D 규약 [0,1] 이라 near 는 열 2 하나다(GL 도 glClipControl 로 같은 규약에 맞춰 둔다).
        const float4x4& v = viewProj;
        setPlane( _arrFrustumPlane, 0, v._14 + v._11, v._24 + v._21, v._34 + v._31, v._44 + v._41 ); // left
        setPlane( _arrFrustumPlane, 1, v._14 - v._11, v._24 - v._21, v._34 - v._31, v._44 - v._41 ); // right
        setPlane( _arrFrustumPlane, 2, v._14 + v._12, v._24 + v._22, v._34 + v._32, v._44 + v._42 ); // bottom
        setPlane( _arrFrustumPlane, 3, v._14 - v._12, v._24 - v._22, v._34 - v._32, v._44 - v._42 ); // top
        setPlane( _arrFrustumPlane, 4, v._13, v._23, v._33, v._43 );                                 // near (z >= 0)
        setPlane( _arrFrustumPlane, 5, v._14 - v._13, v._24 - v._23, v._34 - v._33, v._44 - v._43 ); // far
    }
} // namespace sw
