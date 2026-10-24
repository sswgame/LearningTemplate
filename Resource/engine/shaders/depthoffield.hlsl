/**
 * depthoffield.hlsl — 피사계 심도 패스(RenderPassType::DepthOfField).
 *
 * 입력은 HDR 색(SourceColor)과 장면 깊이(SceneDepth), 출력은 같은 형식의 색이다. 렌즈 값은 PassCB 의 g_CameraLens 이고 엔진이 뷰마다 채운다
 * (FrameRenderer::applyViewPassConstants — 카메라의 CameraDepthOfFieldValues 에서 얇은 렌즈 식으로 착란원 배율을 구한다).
 *
 * 방식: 픽셀마다 착란원 반지름(px) = y · |1 - 초점 거리 / 깊이| 를 z 로 자른다. 그 반지름의 원판에 탭 16 개(안쪽 고리 6 · 바깥 고리 10)를 놓고 색을 모은다.
 * 가까운 탭(앞의 물체)은 **자기** 반지름이 이 픽셀까지 닿을 때만 센다 — 초점이 맞은 앞 물체가 뒤의 흐린 배경으로 번져 후광이 생기지 않게 한다.
 * 흐린 앞 물체가 초점이 맞은 뒤 물체 위로 번지는 것(앞 번짐)은 중심 반지름이 0 이라 모으지 않는다 — 산란(scatter) 방식이 아니라서 생기는 한계다.
 *
 * 끈 뷰(조리개 0 · 직교 · 후처리 끔, g_CameraLens.w = 0)와 SW_PASS_FLAG_SKIP_POST 는 원본을 고른다. early-return 이 아니라 값 선택이다(postchain 과 같은 이유).
 */
#include "binding.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

static const uint  kTapCount      = 16u;
static const uint  kInnerTapCount = 6u;
static const float kInnerRadius   = 0.5f;

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

/** @brief 단위 원판 위 탭 하나의 자리 — 안쪽 고리(반지름 0.5) 6 개, 바깥 고리(반지름 1) 10 개. 바깥 고리는 반 칸 돌려 안쪽과 겹치지 않는다. */
float2 computeTapOffset( uint tapIndex )
{
    const bool  bInner    = tapIndex < kInnerTapCount;
    const float ringCount = bInner ? (float)kInnerTapCount : (float)( kTapCount - kInnerTapCount );
    const float ringIndex = bInner ? (float)tapIndex : ( (float)( tapIndex - kInnerTapCount ) + 0.5f );
    const float angle     = ringIndex * ( 2.0f * kPi / ringCount );
    const float radius    = bInner ? kInnerRadius : 1.0f;
    return float2( cos( angle ), sin( angle ) ) * radius;
}

/**
 * @brief 화면 UV 의 뷰 깊이(카메라 앞축 거리, m)입니다. 깊이 버퍼 값을 역뷰-투영으로 되돌린 동차 좌표의 w 의 역수가 클립 w(= 뷰 깊이)다.
 * @details 원근 투영에서 (ndc, depth, 1) · 역행렬 = 월드 / 클립 w 이므로 별도의 근 · 원 평면 상수가 필요 없다.
 */
float computeViewDepth( float2 uv )
{
    const float  deviceDepth = PassTexture::sampleDepthPoint( uv ).r;
    const float2 ndcXy       = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 world       = mul( float4( ndcXy, deviceDepth, 1.0f ), g_InvViewProj );
    return 1.0f / max( abs( world.w ), 1e-6f );
}

/** @brief 그 깊이의 착란원 반지름(px)입니다. 상한(g_CameraLens.z)으로 자른다. */
float computeBlurRadius( float viewDepth )
{
    const float focusRatio = g_CameraLens.x / max( viewDepth, 1e-4f );
    return min( g_CameraLens.y * abs( 1.0f - focusRatio ), g_CameraLens.z );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 source      = PassTexture::sampleSourcePoint( input._uv ).rgb;
    const float  centerDepth = computeViewDepth( input._uv );
    const float  centerBlur  = computeBlurRadius( centerDepth );

    float3 colorSum  = source;
    float  weightSum = 1.0f;
    SW_UNROLL for ( uint tapIndex = 0u; tapIndex < kTapCount; ++tapIndex )
    {
        const float2 offsetPixels = computeTapOffset( tapIndex ) * centerBlur;
        const float2 tapUv        = input._uv + offsetPixels * g_SourceTexel.xy;
        const float  tapDepth     = computeViewDepth( tapUv );
        const float  tapBlur      = computeBlurRadius( tapDepth );
        // 앞의 탭은 자기 착란원이 이 픽셀까지 닿는 만큼만 센다(1 px 에 걸쳐 부드럽게). 뒤의 탭은 이 픽셀의 착란원 안이므로 다 센다.
        const bool  bNearer = tapDepth < centerDepth;
        const float weight  = bNearer ? saturate( tapBlur - length( offsetPixels ) + 1.0f ) : 1.0f;
        colorSum += PassTexture::sampleSource( tapUv ).rgb * weight;
        weightSum += weight;
    }
    const float3 blurred = colorSum / weightSum;

    const bool bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    const bool bEnabled  = g_CameraLens.w > 0.5f && bSkipPost == false;
    return float4( bEnabled ? blurred : source, 1.0f );
}
