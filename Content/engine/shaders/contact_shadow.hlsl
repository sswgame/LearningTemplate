#include "lighting.hlsli"
#include "fullscreen.hlsli"

/**
 * contact_shadow.hlsl — 접촉 그림자 패스(RenderPassType::ContactShadow).
 *
 * 장면 깊이에서 복원한 점마다 접촉 그림자를 켠 빛(채널 넷, g_ContactShadowLights — C++ FrameShadowData::populateContactShadows) 쪽으로
 * 월드 공간에서 짧게(빛의 _extra.y m) 걷는다. 걸음마다 그 점을 화면에 투영해 깊이 버퍼의 면이 그 점보다 앞(카메라 쪽)에 두께 안으로 있으면 가려졌다.
 * 그림자 맵 텍셀보다 작은 가림(발치 · 틈 · 접힌 곳)을 잡는다 — 언리얼 Contact Shadows · 유니티 HDRP Contact Shadows 와 같은 자리다.
 * 출력은 채널마다 1(빛 받음) · 0(가림)이고 조명(LightData::computeShadow)이 그 빛의 채널(_extra.z)을 곱한다.
 * 화면 밖으로 나간 걸음은 판단하지 않는다(깊이를 모른다) — 화면 가장자리에서 옅어진다.
 */

static const uint kContactShadowStepCount = 12u;
// 두께 — 깊이 버퍼 면 뒤 이만큼(걸음 길이의 배수 + 거리 비례)까지만 가린다고 본다. 얇은 물체 뒤의 먼 배경이 가려지는 것(헤일로)을 막는다.
static const float kContactShadowThicknessSteps = 3.0f;
static const float kContactShadowThicknessDepth = 0.01f;
// 바이어스 — 출발한 면 자신에 가려지지 않게(깊이 버퍼 양자화 · 노멀 기울기) 이만큼(거리 비례 + 고정) 앞의 면만 가린다고 본다.
static const float kContactShadowBiasDepth = 0.002f;
static const float kContactShadowBias      = 0.005f;
// 화면 가장자리에서 옅어지는 폭(uv)
static const float kContactShadowEdgeFade = 0.05f;

/** @brief 접촉 그림자 걷기 — 채널 하나(빛 하나)의 가림 판단. */
struct ContactShadow
{
    static float traceLight( float3 worldPosition, float viewDepth, float jitter, float lightIndexValue );
    static float computeEdgeFade( float2 uv );
};

/** @brief 화면 가장자리에 가까울수록 0 — 화면 밖을 걸은 결과를 믿지 않는다. */
static float ContactShadow::computeEdgeFade( float2 uv )
{
    const float2 edge = min( uv, 1.0f - uv );
    return saturate( min( edge.x, edge.y ) / kContactShadowEdgeFade );
}

/**
 * @brief 빛 하나(@p lightIndexValue — 빛 원소 번호, 음수면 빈 채널) 쪽으로 걸어 가려지면 0, 아니면 1 을 돌려준다.
 * @details 방향광은 빛이 오는 쪽(−방향), 점광 · 스폿은 빛 위치 쪽이다(빛까지 거리보다 멀리 걷지 않는다). early-return 이 없다(GL 드라이버 함정, binding.hlsli MorphPool::computeElement).
 */
static float ContactShadow::traceLight( float3 worldPosition, float viewDepth, float jitter, float lightIndexValue )
{
    float lit = 1.0f;
    if ( lightIndexValue >= 0.0f && (uint)lightIndexValue < g_SwLightCount )
    {
        const LightData light     = g_SwLights[(uint)( lightIndexValue + 0.5f )];
        const uint      lightType = (uint)( light._directionType.w + 0.5f );
        float3          toLight   = -light._directionType.xyz;
        float           rayLength = light._extra.y;
        if ( lightType != SW_LIGHT_TYPE_DIRECTIONAL )
        {
            const float3 offset   = light._positionRadius.xyz - worldPosition;
            const float  distance = max( length( offset ), 1e-4f );
            toLight               = offset / distance;
            rayLength             = min( rayLength, distance );
        }
        const float stepLength = rayLength / (float)kContactShadowStepCount;
        const float thickness  = stepLength * kContactShadowThicknessSteps + viewDepth * kContactShadowThicknessDepth;
        float       occlusion  = 0.0f;
        SW_UNROLL for ( uint stepIndex = 0u; stepIndex < kContactShadowStepCount; ++stepIndex )
        {
            const float3 samplePosition = worldPosition + toLight * ( stepLength * ( (float)stepIndex + jitter ) );
            const float4 sampleClip     = mul( float4( samplePosition, 1.0f ), g_ViewProj );
            const float2 sampleUv       = sampleClip.xy / max( sampleClip.w, 1e-4f ) * float2( 0.5f, -0.5f ) + 0.5f;
            const bool   bOnScreen      = sampleClip.w > 0.0f && all( sampleUv >= 0.0f ) && all( sampleUv <= 1.0f );
            const float  sceneDevice    = PassTexture::sampleDepthPoint( sampleUv ).r;
            const float3 scenePosition  = Transform::computeWorldPositionFromDepth( sampleUv, sceneDevice );
            const float  sceneDepth     = mul( float4( scenePosition, 1.0f ), g_ViewProj ).w;
            // 걸음 점이 깊이 버퍼 면보다 뒤(카메라에서 멀다)이고 두께 안이면 그 면이 빛을 가린다.
            const float behind = sampleClip.w - sceneDepth;
            const float bias   = sampleClip.w * kContactShadowBiasDepth + kContactShadowBias;
            const bool  bHit   = bOnScreen && sceneDevice < 1.0f && behind > bias && behind < thickness;
            occlusion          = max( occlusion, bHit ? ContactShadow::computeEdgeFade( sampleUv ) : 0.0f );
        }
        lit = 1.0f - occlusion;
    }
    return lit;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float  device        = PassTexture::sampleDepthPoint( input._uv ).r;
    const float3 worldPosition = Transform::computeWorldPositionFromDepth( input._uv, device );
    const float  viewDepth     = mul( float4( worldPosition, 1.0f ), g_ViewProj ).w;
    // 걸음 출발을 픽셀마다 조금씩 비켜 걸음 사이의 띠를 잡음으로 바꾼다(Jimenez 의 interleaved gradient noise — 픽셀 좌표만의 함수라 같은 프레임은 같은 그림).
    const float jitter = CommonMath::computeInterleavedGradientNoise( input._position.xy );
    float4      mask   = float4( 1.0f, 1.0f, 1.0f, 1.0f );
    if ( device < 1.0f )
    {
        mask.x = ContactShadow::traceLight( worldPosition, viewDepth, jitter, g_ContactShadowLights.x );
        mask.y = ContactShadow::traceLight( worldPosition, viewDepth, jitter, g_ContactShadowLights.y );
        mask.z = ContactShadow::traceLight( worldPosition, viewDepth, jitter, g_ContactShadowLights.z );
        mask.w = ContactShadow::traceLight( worldPosition, viewDepth, jitter, g_ContactShadowLights.w );
    }
    return mask;
}
