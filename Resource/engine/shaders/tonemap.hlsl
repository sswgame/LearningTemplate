#include "binding.hlsli"
#include "postgrade.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input.position      = input.position;
    float2 clipPosition = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    output.position     = float4( clipPosition, 0.0f, 1.0f );
    output.uv           = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 source = PassTexture::sampleSource( input.uv ).rgb;
    // 표면 값을 보이는 보기 모드(Normals · Depth · Overdraw)는 색을 바꾸지 않는다 — 뷰마다라 퍼뮤테이션이 아니라 패스 플래그다.
    // 고르기는 early-return 이 아니라 값 선택이다(postchain 과 같은 이유).
    const bool bSkipTonemap = ( g_Flags & SW_PASS_FLAG_SKIP_TONEMAP ) != 0u;
    // 카메라별 후처리 값(노출 · 채도 · 대비 · 비네트 · 레터박스)은 후처리를 끈 뷰(CCTV)에는 걸지 않는다 — 톤맵만 한다.
    const bool   bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    const float3 exposed   = bSkipPost ? source : CameraGrade::applyExposure( source );
    const float3 mapped    = exposed / ( exposed + 1.0f );
    const float3 graded    = bSkipPost ? mapped : CameraGrade::apply( input.uv, mapped );
    return float4( bSkipTonemap ? source : graded, 1.0f );
}
