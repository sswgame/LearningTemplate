/**
 * fullscreen.hlsli — 전체 화면 패스의 정점 단계. 정점 버퍼 없이 큰 삼각형 하나(정점 3)로 화면을 덮는다.
 * 사각형(삼각형 둘)과 달리 대각선 위 픽셀을 두 번 칠하지 않는다. `_uv` 는 DX 규약(위가 0)의 뷰포트 안 좌표다.
 */
#ifndef SW_ENGINE_FULLSCREEN_HLSLI
#define SW_ENGINE_FULLSCREEN_HLSLI

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

PSInput VSMain( uint vertexID : SV_VertexID )
{
    PSInput      output;
    const float2 clipPosition = float2( ( vertexID == 1u ) ? 3.0f : -1.0f, ( vertexID == 2u ) ? 3.0f : -1.0f );
    output._position          = float4( clipPosition, 0.0f, 1.0f );
    output._uv                = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

#endif // SW_ENGINE_FULLSCREEN_HLSLI
