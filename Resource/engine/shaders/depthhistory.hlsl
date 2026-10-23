// depthhistory.hlsl — 뷰의 깊이 히스토리 복사(풀스크린). 지난 프레임에 그 뷰가 남긴 장면 깊이를 R32 실수 RT(뷰마다의 히스토리)로 옮긴다.
// 깊이 텍스처는 RW 텍스처가 될 수 없고 에뮬 백엔드(DX11 · GL)의 컴퓨트는 샘플 텍스처를 읽지 못한다 — 그래서 그래픽스로 한 번 옮겨 두고, 읽는 컴퓨트
// (vfxsimulate.hlsl 의 깊이 충돌)는 그 RT 를 RW 텍스처로 읽는다. 같은 크기 · 같은 텍셀이라 Load 로 텍셀을 그대로 옮긴다(필터 없음).
// 원본은 네이티브(DX12 · Vulkan)는 bindless 번호(루트 상수), 에뮬은 엔진 텍스처 슬롯 0(t0 — C++ 가 bindShaderResource 로 건다)이다.
// C++: FrameRenderer::recordDepthHistories — 컴퓨트 프리패스 리스트에서 뷰마다 drawInstanced 하나(정점 셋).
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_DepthHistorySource; // 원본 깊이의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
};
SW_ROOT_CONSTANTS_END

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain( SwVertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input.position            = input.position;
    const float2 clipPosition = float2( ( vertexId == 1u ) ? 3.0f : -1.0f, ( vertexId == 2u ) ? 3.0f : -1.0f );
    output.position           = float4( clipPosition, 0.0f, 1.0f );
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const int3 texel = int3( int2( input.position.xy ), 0 );
#if defined( SW_NATIVE_BINDLESS )
    const float depth = g_SwBindlessTex2D[NonUniformResourceIndex( swBindlessTextureSlot( SW_ROOT( g_DepthHistorySource ) ) )].Load( texel ).r;
#else
    const float depth = g_SwSlot0.Load( texel ).r;
#endif
    return float4( depth, 0.0f, 0.0f, 1.0f );
}
