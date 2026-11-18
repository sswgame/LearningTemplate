// motion_blur.hlsl — 모션 블러(언리얼 모션 블러 · 유니티 HDRP Motion Blur 와 같은 갈래). 화면 속도(SceneVelocity — 불투명 지오메트리 패스가 물체마다
// 지난 월드 행렬 · 스킨 위치로 쓴 이번 UV - 지난 UV, binding_output.hlsli)를 타일로 모은 이웃 최대 속도(VelocityNeighborMax — motion_blur_tile_max · motion_blur_neighbor_max)
// 방향으로 원본을 모으는 재구성 필터다(McGuire 외 2012 "A Reconstruction Filter for Plausible Motion Blur"). 표본마다 앞 · 뒤(뷰 깊이)와 두 속도의 원뿔로 무게를 매겨
// 빠른 물체의 흐림이 윤곽 밖 배경 위로 번지고, 서 있는 배경이 물체 위로 번지지 않는다. 카메라에 붙어 함께 움직이는 물체(1인칭 총)는 속도가 0 에 가까워 흐려지지 않는다.
// 그리지 않은 픽셀(하늘, 깊이 1)은 속도 출력이 없어 깊이로 복원한 월드 위치를 지난 뷰-투영(g_PrevViewProj)으로 다시 투영한다(카메라 움직임 — 돌면 하늘이 흐려진다).
// 끈 경우(사용자 설정 · 후처리를 끈 뷰 · 품질 미달 · 컷 · 카메라와 물체가 모두 서 있음)는 이 패스를 기록하지 않고 출력 이름이 원본을 가리킨다(RenderPassTraitFlag::kBypassesToSource).
#include "binding.hlsli"
#include "fullscreen.hlsli"

static const uint  kMotionBlurSampleCount = 8u;
static const float kMaxBlurUV             = 0.05f;   // 한 프레임 흐림 길이의 상한(화면 폭 비율) — motion_blur_tile_max.hlsl 과 같다
static const float kMinBlurUV             = 0.0005f; // 이웃 최대 속도가 이보다 짧으면 섞지 않는다(정지 화면의 미세 떨림)
static const float kSkyDepth              = 1.0f;    // 깊이 클리어 값 — 아무것도 그리지 않은 픽셀(속도 출력이 없다)
static const float kSoftDepthExtent       = 0.5f;    // 앞 · 뒤를 부드럽게 가르는 뷰 깊이 폭(m) — McGuire 의 SOFT_Z_EXTENT

/** @brief 화면 UV @p uv 의 화면 속도(UV) — 하늘은 카메라 재투영, 그 밖은 속도 첨부. 상한으로 자른다. */
float2 samplePixelVelocity( float2 uv, float depth )
{
    float        previousClipW;
    const float2 prevUv   = Transform::computeCameraPreviousUv( uv, depth, previousClipW );
    const float2 velocity = depth >= kSkyDepth ? uv - prevUv : PassTexture::sampleVelocityPoint( uv );
    const float  speed    = length( velocity );
    return velocity * ( ( speed > kMaxBlurUV ) ? ( kMaxBlurUV / speed ) : 1.0f );
}

/** @brief 화면 UV 와 장치 깊이의 뷰 깊이(m)입니다(depth_of_field.hlsl computeViewDepth 와 같은 식). */
float computeViewDepth( float2 uv, float deviceDepth )
{
    const float2 ndcXy = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 world = mul( float4( ndcXy, deviceDepth, 1.0f ), g_InvViewProj );
    return 1.0f / max( abs( world.w ), 1e-6f );
}

/** @brief 거리 @p distance 를 속도 길이 @p speed 가 덮는 몫(원뿔) — 둘 다 픽셀. */
float computeCone( float distance, float speed )
{
    return saturate( 1.0f - distance / max( speed, 1e-4f ) );
}

/** @brief 둘 다 같은 속도 길이 안인지(원기둥 — 같이 움직이는 면의 안쪽) — 둘 다 픽셀. */
float computeCylinder( float distance, float speed )
{
    return 1.0f - smoothstep( 0.95f * speed, 1.05f * speed, distance );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 source        = PassTexture::sampleSource( input._uv ).rgb;
    const float  depth         = PassTexture::sampleDepthPoint( input._uv ).r;
    const float2 neighborMax   = Bindless::sampleIndexWith( SW_ENGINE_ROLE( VelocityNeighborMax ), SW_SAMPLER_POINT_CLAMP, PassTexture::toViewRectUV( input._uv ) ).rg;
    const float2 pixelScale    = g_ViewRectSize.xy; // UV → 픽셀
    const float  centerSpeed   = length( samplePixelVelocity( input._uv, depth ) * pixelScale );
    const float  centerDepth   = computeViewDepth( input._uv, depth );
    const float  neighborSpeed = length( neighborMax );

    // 이 픽셀 자신 — McGuire 처럼 1 / 속도(픽셀)로 무게를 둔다(빠를수록 이웃 표본 몫이 커진다).
    float  totalWeight = 1.0f / max( centerSpeed, 1.0f );
    float3 accumulated = source * totalWeight;
    // 선분은 이 픽셀을 가운데에 두고 이웃 최대 속도 방향으로 앞뒤 반씩 편다.
    SW_UNROLL for ( uint sampleIndex = 0u; sampleIndex < kMotionBlurSampleCount; ++sampleIndex )
    {
        const float  ratio        = ( float( sampleIndex ) + 0.5f ) / float( kMotionBlurSampleCount ) - 0.5f;
        const float2 sampleUv     = input._uv + neighborMax * ratio;
        const float  sampleDepth  = PassTexture::sampleDepthPoint( sampleUv ).r;
        const float  sampleSpeed  = length( samplePixelVelocity( sampleUv, sampleDepth ) * pixelScale );
        const float  sampleView   = computeViewDepth( sampleUv, sampleDepth );
        const float  distance     = length( neighborMax * ratio * pixelScale );
        const float  sampleFront  = saturate( 1.0f - ( sampleView - centerDepth ) / kSoftDepthExtent ); // 표본이 이 픽셀보다 앞(또는 같은 깊이)
        const float  sampleBehind = saturate( 1.0f - ( centerDepth - sampleView ) / kSoftDepthExtent ); // 표본이 이 픽셀보다 뒤(또는 같은 깊이)
        // 앞의 표본은 자기 속도가 여기까지 덮을 때, 뒤의 표본은 이 픽셀의 속도가 거기까지 덮을 때, 같이 움직이면 둘 다 — 셋의 합이 무게다.
        const float weight = sampleFront * computeCone( distance, sampleSpeed ) + sampleBehind * computeCone( distance, centerSpeed ) +
                             computeCylinder( distance, sampleSpeed ) * computeCylinder( distance, centerSpeed ) * 2.0f;
        accumulated += PassTexture::sampleSource( sampleUv ).rgb * weight;
        totalWeight += weight;
    }
    const float3 blurred = accumulated / totalWeight;

    // 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — binding.hlsli MorphPool::computeElement).
    const bool bStill = neighborSpeed < kMinBlurUV;
    return float4( bStill ? source : blurred, 1.0f );
}
