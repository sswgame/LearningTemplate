// 하늘 — 스카이 패스(RenderPassType::Sky). 불투명 패스 뒤 · 반투명 앞에 풀스크린 삼각형을 깊이 1 에 그린다.
// 깊이 테스트(LessEqual)만 하고 쓰지 않으므로 아무것도 그리지 않은 곳(깊이 클리어 1)만 칠해진다. 씬에 하늘이 없으면 패스가 돌지 않는다(FrameRenderer).
// 색은 C++ SkyGradient::evaluateRadiance 와 같은 식이다 — 위는 천정 → 지평선, 아래는 땅 → 지평선, 지평선 쪽 비율 (1 - |y|)^지수, 마지막에 세기.
// 상수 배치: g_SkyZenith(rgb, w 지수) · g_SkyHorizon(rgb) · g_SkyGround(rgb, w 세기).
#include "binding.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 clipXy : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input.position      = input.position;
    const float2 clipXy = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    // z = w 라 깊이가 1 이다(네 백엔드 모두 0~1 깊이 — GL 은 glClipControl ZERO_TO_ONE).
    output.position = float4( clipXy, 1.0f, 1.0f );
    output.clipXy   = clipXy;
    return output;
}

float3 evaluateSky( float3 direction )
{
    const float  elevation     = clamp( direction.y, -1.0f, 1.0f );
    const float  horizonWeight = pow( max( 1.0f - abs( elevation ), 0.0f ), g_SkyZenith.w );
    const float3 poleColor     = ( elevation >= 0.0f ) ? g_SkyZenith.rgb : g_SkyGround.rgb;
    return lerp( poleColor, g_SkyHorizon.rgb, horizonWeight ) * g_SkyGround.w;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 이 픽셀을 지나는 시선 — 가까운 면과 먼 면의 월드 점을 잇는다(행벡터 규약, lighting.hlsli Transform::computeWorldPositionFromDepth 와 같은 순서). 직교 카메라도 같다.
    const float4 nearPoint = mul( float4( input.clipXy, 0.0f, 1.0f ), g_InvViewProj );
    const float4 farPoint  = mul( float4( input.clipXy, 1.0f, 1.0f ), g_InvViewProj );
    const float3 direction = normalize( farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w );
    return float4( evaluateSky( direction ), 1.0f );
}
