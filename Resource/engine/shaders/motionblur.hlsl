// motionblur.hlsl — 카메라 모션 블러(언리얼 카메라 모션 블러 · 유니티 URP Camera Motion Blur 와 같은 갈래).
// 깊이로 이 픽셀의 월드 위치를 복원하고(g_InvViewProj, lighting.hlsli Transform::computeWorldPositionFromDepth 와 같은 행벡터 규약) 지난 프레임 뷰-투영(g_PrevViewProj)으로
// 다시 투영해 화면 속도(UV)를 구한 뒤, 그 선분 위의 원본을 고르게 섞는다. 움직이는 물체의 속도 버퍼는 없다 — 카메라 움직임만 흐린다.
// 하늘(깊이 1)도 같은 식이라 돌면 흐려진다. 끈 경우(사용자 설정 · 후처리를 끈 뷰)는 C++ 가 원본을 복사하거나 SW_PASS_FLAG_SKIP_POST 로 원본을 고른다.
#include "binding.hlsli"

static const uint  kMotionBlurSampleCount = 8u;
static const float kMaxBlurUV             = 0.05f;   // 한 프레임 흐림 길이의 상한(화면 폭 비율) — 순간 이동 · 큰 회전이 화면을 통째로 문대지 않게
static const float kMinBlurUV             = 0.0005f; // 이보다 짧으면 섞지 않는다(정지 화면의 미세 떨림)

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
    const float  depth  = PassTexture::sampleDepth( input.uv ).r;

    const float2 ndcXy    = input.uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 world    = mul( float4( ndcXy, depth, 1.0f ), g_InvViewProj );
    const float4 prevClip = mul( float4( world.xyz / world.w, 1.0f ), g_PrevViewProj );
    const float2 prevNdc  = prevClip.xy / max( abs( prevClip.w ), 1e-5f ) * sign( prevClip.w );
    const float2 prevUv   = prevNdc * float2( 0.5f, -0.5f ) + 0.5f;
    float2       velocity = input.uv - prevUv;
    const float  speed    = length( velocity );
    velocity *= ( speed > kMaxBlurUV ) ? ( kMaxBlurUV / speed ) : 1.0f;

    // 선분은 이 픽셀을 가운데에 두고 앞뒤로 반씩 편다(지난 위치 쪽 · 앞으로 갈 쪽).
    float3 accumulated = 0.0f;
    SW_UNROLL for ( uint sampleIndex = 0u; sampleIndex < kMotionBlurSampleCount; ++sampleIndex )
    {
        const float ratio = ( float( sampleIndex ) + 0.5f ) / float( kMotionBlurSampleCount ) - 0.5f;
        accumulated += PassTexture::sampleSource( input.uv + velocity * ratio ).rgb;
    }
    const float3 blurred = accumulated / float( kMotionBlurSampleCount );

    // 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — binding.hlsli MorphPool::computeElement).
    const bool bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    const bool bStill    = speed < kMinBlurUV;
    return float4( ( bSkipPost || bStill ) ? source : blurred, 1.0f );
}
