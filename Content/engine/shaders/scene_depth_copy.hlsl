// scene_depth_copy.hlsl — 장면 깊이 사본(풀스크린). 반투명 패스가 읽을 수 있게 장면 깊이를 R32 실수 RT 로 텍셀 그대로 옮긴다.
// 반투명 패스는 장면 깊이를 깊이 붙임으로 쥐고 있어 같은 텍스처를 샘플할 수 없다(백엔드마다 읽기 전용 깊이 + SRV 를 보장하지 못한다).
// 같은 크기 · 같은 텍셀이라 필터 없는 점 샘플로 옮긴다(PassTexture::sampleDepthPoint — GL 의 행 방향은 슬롯 읽기가 이미 맞춘다).
// C++: RenderPassType::SceneDepthCopy(일반 풀스크린 패스) — 입력 SceneDepth, 출력 SceneDepthCopy(R32_FLOAT).
#include "binding.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexID : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexID == 1 ) ? 3.0f : -1.0f, ( vertexID == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    return float4( PassTexture::sampleDepthPoint( input._uv ).r, 0.0f, 0.0f, 1.0f );
}
