#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/RenderView.h"

namespace sw
{
    void RenderView::setViewProjection( const float4x4& viewProj )
    {
        _viewProj = viewProj;
        // 평면은 정규화돼 있다 — 셰이더가 `dot( plane.xyz, center ) + plane.w < -radius` 로 바운드 반지름을 그대로 비교한다.
        _frustum = Frustum::fromViewProjection( viewProj );
    }
} // namespace sw
