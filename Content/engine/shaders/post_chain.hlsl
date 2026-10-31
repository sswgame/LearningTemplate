/**
 * @file post_chain.hlsl
 * @brief 전체화면 후처리를 **한 패스**로 합친다 — 무엇을 적용할지는 파이프라인 XML 의 퍼뮤테이션이 정한다.
 * @details 전체화면 패스는 픽셀당 비용이 지배적이고(해상도를 1/4 로 줄이면 비용도 1/4), 그 비용의
 *          대부분은 계산이 아니라 **중간 타깃에 쓰고 다시 읽는 왕복**이다. 720p 왕복 하나가 약
 *          57 us 다 — 블룸·외곽선·톤맵을 따로 두면 그 왕복을 두 번 더 낸다.
 *
 *          그렇다고 셰이더 하나에 효과를 박아 넣으면 파이프라인 XML 로 패스를 끼우고 빼는 조립이
 *          깨진다. 그래서 효과는 각자 `.hlsli` 의 **함수**로 두고, 이 셰이더는 퍼뮤테이션으로 부를
 *          것만 부른다 — 끄고 켜는 일은 여전히 XML 의 `_listPermutation` 한 줄이다.
 *
 *          `SW_POST_*` 를 하나도 안 켜면 이 셰이더는 `fullscreen_blit.hlsl` 과 같다(원본을 그대로 낸다).
 */
#include "binding.hlsli"

#if defined( SW_POST_BLOOM )
    #include "post_bloom.hlsli"
#endif
#if defined( SW_POST_OUTLINE )
    #include "post_outline.hlsli"
#endif
#if defined( SW_POST_TONEMAP )
    #include "post_grade.hlsli"
#endif

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

// 단계마다 정의 하나 — 이 변형이 켠 단계만 일하고, 끈 단계는 색을 그대로 돌려준다(PSMain 본문에는 `#if` 가 없다).
#if defined( SW_POST_BLOOM )
float3 applyBloom( float2 uv, float2 sourceTexel, float3 color )
{
    // 자르지 않는다 — 장면 색 · 단계 파이프라인의 중간 타깃이 모두 `R16G16B16A16_FLOAT` 라 1.0 을 넘는 값이 톤맵까지 간다.
    // 여기서 [0,1] 로 자르면 톤맵 `x / (x + 1)` 의 최대가 0.5 가 되어 밝은 바닥 위의 더하기 블렌드(입자)가 보이지 않는다.
    return Bloom::apply( uv, sourceTexel, color );
}
#else
float3 applyBloom( float2 uv, float2 sourceTexel, float3 color )
{
    return color;
}
#endif

#if defined( SW_POST_OUTLINE )
float3 applyOutline( float2 uv, float2 depthTexel, float3 color )
{
    return Outline::apply( uv, depthTexel, color );
}
#else
float3 applyOutline( float2 uv, float2 depthTexel, float3 color )
{
    return color;
}
#endif

#if defined( SW_POST_TONEMAP )
float3 applyTonemap( float2 uv, float3 color )
{
    // 카메라별 후처리 값은 톤맵과 함께 간다 — 노출은 톤맵 앞, 색 보정 · 비네트 · 레터박스는 뒤(post_grade.hlsli). 후처리를 끈 뷰는 PSMain 에서 원본을 고른다.
    color = CameraGrade::applyExposure( color );
    color = color / ( color + 1.0f );
    return CameraGrade::apply( uv, color );
}
#else
float3 applyTonemap( float2 uv, float3 color )
{
    return color;
}
#endif

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float2 sourceTexel = g_SourceTexel.xy;   // 원본을 비켜 읽는 블룸 — 원본(반해상도일 수 있다)의 텍셀
    const float2 depthTexel  = g_OutlineParams.yz; // 깊이를 비켜 읽는 외곽선 — 깊이는 나누지 않으므로 프레임 텍셀
    // 화면과 1:1 이라 UV 가 텍셀 중심에 정확히 떨어진다 — 섞을 것이 없으니 점 샘플러로 읽는다.
    float3 source = PassTexture::sampleSourcePoint( input._uv ).rgb;
    float3 color  = source;

    color = applyBloom( input._uv, sourceTexel, color );
    color = applyOutline( input._uv, depthTexel, color );
    color = applyTonemap( input._uv, color );
    // 후처리를 끈 뷰(CCTV · 미니맵)는 원본을 낸다 — 뷰마다 달라서 퍼뮤테이션이 아니라 패스 플래그다. 고르기는 early-return 이 아니라
    // 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — binding.hlsli MorphPool::computeElement).
    const bool bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    return float4( bSkipPost ? source : color, 1.0f );
}
