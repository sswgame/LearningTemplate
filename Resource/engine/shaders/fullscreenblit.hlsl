// 원본을 그대로 화면에 옮기는 블릿이다(톤맵 없음). `_shaderPath` 없는 Present 의 기본 셰이더라, 앞 패스가 톤맵을 끝낸 파이프라인(디퍼드)이
// 여기서 한 번 더 톤맵을 걸면 화면이 c/(c+1) 로 눌린다. 톤맵이 필요한 Present 는 `tonemap.hlsl` · `postchain.hlsl` 을 지정한다.
#include "binding.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 정수 업스케일(픽셀 퍼펙트) 뷰는 점 샘플이다 — 선형이면 원본 픽셀 경계가 섞여 번진다. 그 밖은 해상도 배율로 키울 때 부드럽게 선형이다.
    // 플래그는 패스 상수라 분기가 고르다 — 한 번만 읽는다. early-return 이 아니라 값을 채우는 분기다(postchain 과 같은 이유).
    const bool bPointUpscale = ( g_Flags & SW_PASS_FLAG_POINT_UPSCALE ) != 0u;
    float3     color;
    SW_BRANCH if ( bPointUpscale )
    {
        color = PassTexture::sampleSourcePoint( input._uv ).rgb;
    }
    else
    {
        color = PassTexture::sampleSource( input._uv ).rgb;
    }
    return float4( color, 1.0f );
}
