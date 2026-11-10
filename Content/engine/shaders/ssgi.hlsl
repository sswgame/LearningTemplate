/**
 * ssgi.hlsl — 화면 공간 간접 확산광(RenderPassType::ScreenSpaceGI). 조명 첨부에 가산 블렌드(`_blendPreset="Additive"` — 출력 알파가 세기)로 더한다.
 *
 * 픽셀마다 화면 방향 몇 개로 깊이를 걸어, 반경 안에서 이 면을 마주 보는 가까운 면의 색(반투명 앞에 떠 둔 장면 색 사본)을 모은다:
 *   간접 조도 ≈ 평균( 색(표본) × max( N · L, 0 ) × max( N표본 · -L, 0 ) × 감쇠 ) — L 은 이 면에서 표본으로 가는 단위 방향이다.
 * 모은 빛 × G버퍼 알베도 × 세기가 더할 빛이다. 하늘(깊이 1) 표본은 빛으로 치지 않는다 — 하늘빛은 프로브 · 앰비언트가 이미 그렸다.
 * 먼 빛은 프로브 GI 가 조명 패스에서 그렸고 이 패스는 반경 안의 가까운 튐만 더한다(언리얼 UE4 r.SSGI 가 라이트맵 · 스카이 위에 더하는 것과 같은 자리, 기억에 의존한 비교).
 * shortcut: 화면 밖 · 가려진 면의 빛은 모른다(화면 공간의 한계) — 시간 누적 없이 픽셀마다 회전한 방향을 쓰고 TAA 가 섞는다. 루멘처럼 화면 밖을 프로브로 잇는 일은 프로브가 이미 맡는다.
 *
 * 값은 XML 퍼뮤테이션이다(기본값을 셰이더에 두지 않는다 — 없으면 컴파일이 멈춘다):
 *   SW_SSGI_RADIUS(m) · SW_SSGI_DIRECTIONS(방향 수) · SW_SSGI_STEPS(방향마다 걸음 수) · SW_SSGI_INTENSITY(더할 빛의 배율)
 */
#include "lighting.hlsli"
#include "fullscreen.hlsli"

#if !defined( SW_SSGI_RADIUS ) || !defined( SW_SSGI_DIRECTIONS ) || !defined( SW_SSGI_STEPS ) || !defined( SW_SSGI_INTENSITY )
    #error "ssgi.hlsl needs SW_SSGI_RADIUS, SW_SSGI_DIRECTIONS, SW_SSGI_STEPS and SW_SSGI_INTENSITY in the pass _listPermutation"
#endif

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 분기 없이 모두 계산하고 끝에서 하늘(깊이 1)만 바꾼다 — 기울기 읽기(Sample)를 갈라진 흐름 안에 두면 FXC(DX11)가 거절한다(X4121). 루프는 퍼뮤테이션 상수라 펼친다.
    const float depth = PassTexture::sampleDepthPoint( input._uv ).r;
    {
        const float3 position     = Transform::computeWorldPositionFromDepth( input._uv, depth );
        const float3 normal       = PassTexture::sampleNormal( input._uv );
        const float3 toView       = View::computeToView( position );
        const float3 side         = normalize( cross( toView, abs( toView.y ) < 0.99f ? float3( 0.0f, 1.0f, 0.0f ) : float3( 1.0f, 0.0f, 0.0f ) ) );
        const float4 centerClip   = mul( float4( position, 1.0f ), g_ViewProj );
        const float4 sideClip     = mul( float4( position + side * SW_SSGI_RADIUS, 1.0f ), g_ViewProj );
        const float  screenRadius = length( sideClip.xy / max( sideClip.w, 1e-4f ) - centerClip.xy / max( centerClip.w, 1e-4f ) ) * 0.5f;
        const float  stepUv       = max( screenRadius / (float)SW_SSGI_STEPS, g_SourceTexel.y );
        const float  jitter       = CommonMath::computeInterleavedGradientNoise( input._uv * g_SourceTexel.zw );

        float3 gathered    = float3( 0.0f, 0.0f, 0.0f );
        float  sampleCount = 0.0f;
        SW_UNROLL for ( uint directionIndex = 0u; directionIndex < (uint)SW_SSGI_DIRECTIONS; ++directionIndex )
        {
            const float  angle     = ( (float)directionIndex + jitter ) * ( 2.0f * kPi / (float)SW_SSGI_DIRECTIONS );
            const float2 direction = float2( cos( angle ), sin( angle ) );
            SW_UNROLL for ( uint stepIndex = 1u; stepIndex <= (uint)SW_SSGI_STEPS; ++stepIndex )
            {
                // 걸음마다 반 칸씩 비켜 같은 거리 띠가 겹치지 않게 한다.
                const float2 uv          = input._uv + direction * ( stepUv * ( (float)stepIndex - 0.5f + jitter * 0.5f ) );
                const float  sampleDepth = PassTexture::sampleDepthPoint( uv ).r;
                const float  onScreen    = ( all( uv >= 0.0f ) && all( uv <= 1.0f ) && sampleDepth < 1.0f ) ? 1.0f : 0.0f; // 화면 밖 · 하늘은 빛이 아니다
                const float3 delta       = Transform::computeWorldPositionFromDepth( uv, sampleDepth ) - position;
                const float  distance    = length( delta );
                const float3 toSample    = delta / max( distance, 1e-4f );
                const float  receive     = saturate( dot( normal, toSample ) );
                const float  emit        = saturate( dot( PassTexture::sampleNormal( uv ), -toSample ) );
                const float  attenuation = saturate( 1.0f - distance / SW_SSGI_RADIUS );
                gathered += Bindless::sampleIndex( SW_ENGINE_ROLE( SceneColorCopy ), uv ).rgb * ( receive * emit * attenuation * onScreen );
                sampleCount += 1.0f;
            }
        }
        const float3 albedo   = PassTexture::sampleAlbedo( input._uv ).rgb;
        const float3 indirect = gathered / max( sampleCount, 1.0f ) * albedo * SW_SSGI_INTENSITY;
        // 가산 블렌드(SrcAlpha, One)라 알파 1 이면 rgb 가 그대로 더해지고, 하늘(알파 0)은 아무것도 더하지 않는다.
        return depth < 1.0f ? float4( indirect, 1.0f ) : float4( 0.0f, 0.0f, 0.0f, 0.0f );
    }
}
