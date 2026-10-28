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
    float4 _position : SV_POSITION;
};

PSInput VSMain( VertexInput input, uint vertexID : SV_VertexID )
{
    PSInput output;
    input._position           = input._position;
    const float2 clipPosition = float2( ( vertexID == 1u ) ? 3.0f : -1.0f, ( vertexID == 2u ) ? 3.0f : -1.0f );
    output._position          = float4( clipPosition, 0.0f, 1.0f );
    return output;
}

#if defined( OPENGL )
// GL 의 장면 깊이는 행이 아래부터다(0 행이 화면 아래). 읽는 쪽(vfxsimulate · occlusiontiles)은 D3D 처럼 0 행이 화면 위라고 보고 텍셀을 구하므로
// 여기서 원본의 행을 뒤집어 히스토리를 D3D 순서로 만든다(GL 에서만 깊이 충돌이 안 닿던 결함 — 입자가 바닥 아래로 떨어져 빛이 사라졌다).
int findSourceRow( int row )
{
    return int( Bindless::getEngineTextureSize( SW_ROOT( g_DepthHistorySource ), 0u ).y ) - 1 - row;
}
#else
int findSourceRow( int row )
{
    return row;
}
#endif

float4 PSMain( PSInput input ) : SV_TARGET
{
    int2 texel        = int2( input._position.xy );
    texel.y           = findSourceRow( texel.y );
    const float depth = Bindless::loadEngineTexel( SW_ROOT( g_DepthHistorySource ), 0u, texel ).r;
    return float4( depth, 0.0f, 0.0f, 1.0f );
}
