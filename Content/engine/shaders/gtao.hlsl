/**
 * gtao.hlsl — 지평선 기반 앰비언트 오클루전(GTAO, Jimenez 2016)과 굽은 노멀. SSAO 패스(`_type="SSAO"`)가 `_shaderPath` 로 고른다 — 같은 출력(AOColor)을 쓰므로
 *             읽는 쪽(블룸 패스의 AO 적용, PassTexture::sampleAmbientOcclusion 의 r)은 그대로다. 예: engine/pipeline/deferredpipeline_gi.xml.
 *
 * 화면 방향 조각(slice)마다 양쪽으로 깊이를 걸어 가장 높은 지평선 각을 찾고, 노멀을 그 조각 평면에 투영한 각으로 보이는 몫을 적분한다(코사인 가중 — 4 절 식).
 * 굽은 노멀(가리지 않은 방향의 평균)은 조각마다 두 지평선의 가운데 방향을 투영 노멀 길이로 가중해 더한다 — gba 에 0.5 + 0.5 × n 으로 싣는다(읽는 쪽이 아직 없다).
 * 위치는 깊이에서 복원한 월드 좌표다(Transform::computeWorldPositionFromDepth) — 반경 · 감쇠가 미터 단위다.
 *
 * 값은 XML 퍼뮤테이션이다(기본값을 셰이더에 두지 않는다 — 없으면 컴파일이 멈춘다):
 *   SW_GTAO_RADIUS(m, 지평선을 찾는 월드 반경) · SW_GTAO_SLICES(조각 수) · SW_GTAO_STEPS(한쪽 걸음 수) · SW_GTAO_INTENSITY(가림을 키우는 지수, 1 이면 물리 값)
 * 언리얼 GTAO(r.AmbientOcclusion.Method=1)와 같은 식이다(기억에 의존한 비교). 언리얼은 시간 누적으로 조각을 늘리고, 여기는 픽셀마다 회전한 조각을 한 프레임에 쓴다(TAA 가 섞는다).
 */
#include "lighting.hlsli"
#include "fullscreen.hlsli"

#if !defined( SW_GTAO_RADIUS ) || !defined( SW_GTAO_SLICES ) || !defined( SW_GTAO_STEPS ) || !defined( SW_GTAO_INTENSITY )
    #error "gtao.hlsl needs SW_GTAO_RADIUS, SW_GTAO_SLICES, SW_GTAO_STEPS and SW_GTAO_INTENSITY in the pass _listPermutation"
#endif

static const float kGtaoHalfPi = 1.57079632679f;

/** @brief 픽셀마다 조각 회전(0 ~ 1) — 4×4 반복 무늬(인터리브 그래디언트 노이즈). 프레임마다 바뀌지 않아 골든이 정해진다. */
float computeSliceJitter( float2 pixel )
{
    return frac( 52.9829189f * frac( dot( floor( pixel ), float2( 0.06711056f, 0.00583715f ) ) ) );
}

/** @brief 조각 하나의 보이는 몫(코사인 가중, 0 ~ 1)과 굽은 방향입니다. @p cosHorizon0 · @p cosHorizon1 은 음 · 양 방향 지평선의 cos(시선과의 각)입니다. */
float integrateSlice( float cosHorizon0, float cosHorizon1, float normalAngle, out float outBentAngle )
{
    // 지평선 각(시선에서 잰 부호 있는 각)을 노멀이 보는 반구 안으로 자른다.
    const float horizon0 = normalAngle + max( -acos( cosHorizon0 ) - normalAngle, -kGtaoHalfPi );
    const float horizon1 = normalAngle + min( acos( cosHorizon1 ) - normalAngle, kGtaoHalfPi );
    outBentAngle         = ( horizon0 + horizon1 ) * 0.5f;
    const float sinN     = sin( normalAngle );
    const float cosN     = cos( normalAngle );
    const float arc0     = -cos( 2.0f * horizon0 - normalAngle ) + cosN + 2.0f * horizon0 * sinN;
    const float arc1     = -cos( 2.0f * horizon1 - normalAngle ) + cosN + 2.0f * horizon1 * sinN;
    return 0.25f * ( arc0 + arc1 );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 분기 없이 모두 계산하고 끝에서 하늘(깊이 1)만 바꾼다 — 기울기 읽기(Sample)를 갈라진 흐름 안에 두면 FXC(DX11)가 거절한다(X4121). 루프는 퍼뮤테이션 상수라 펼친다.
    const float depth = PassTexture::sampleDepthPoint( input._uv ).r;
    {
        const float3 position = Transform::computeWorldPositionFromDepth( input._uv, depth );
        const float3 normal   = PassTexture::sampleNormal( input._uv );
        const float3 toView   = View::computeToView( position );
        const float2 texel    = g_SourceTexel.xy;
        const float  jitter   = computeSliceJitter( input._uv * g_SourceTexel.zw );
        // 반경을 화면 길이로 — 시선에 수직으로 반경만큼 떨어진 점을 투영해 그 화면 거리(uv)를 걸음 수로 나눈다(멀수록 짧다).
        const float3 side         = normalize( cross( toView, abs( toView.y ) < 0.99f ? float3( 0.0f, 1.0f, 0.0f ) : float3( 1.0f, 0.0f, 0.0f ) ) );
        const float4 centerClip   = mul( float4( position, 1.0f ), g_ViewProj );
        const float4 sideClip     = mul( float4( position + side * SW_GTAO_RADIUS, 1.0f ), g_ViewProj );
        const float  screenRadius = length( sideClip.xy / max( sideClip.w, 1e-4f ) - centerClip.xy / max( centerClip.w, 1e-4f ) ) * 0.5f;
        const float  stepUv       = max( screenRadius / (float)SW_GTAO_STEPS, texel.y );

        float  visibility = 0.0f;
        float3 bentNormal = float3( 0.0f, 0.0f, 0.0f );
        SW_UNROLL for ( uint slice = 0u; slice < (uint)SW_GTAO_SLICES; ++slice )
        {
            const float  angle     = ( (float)slice + jitter ) * ( kPi / (float)SW_GTAO_SLICES );
            const float2 direction = float2( cos( angle ), sin( angle ) );
            // 조각의 월드 방향 — 한 걸음 옆 점으로 구하고 시선에 직교화한다.
            const float2 nextUv   = input._uv + direction * stepUv;
            const float3 nextPos  = Transform::computeWorldPositionFromDepth( nextUv, PassTexture::sampleDepthPoint( nextUv ).r );
            float3       sliceDir = nextPos - position;
            sliceDir              = sliceDir - toView * dot( sliceDir, toView );
            sliceDir              = sliceDir / max( length( sliceDir ), 1e-5f );
            const float3 axis     = cross( sliceDir, toView );
            // 노멀을 조각 평면에 투영 — 길이가 이 조각의 가중치, 시선과의 부호 있는 각이 노멀 각이다.
            const float3 projected    = normal - axis * dot( normal, axis );
            const float  projectedLen = length( projected );
            const float  cosNormal    = saturate( dot( projected, toView ) / max( projectedLen, 1e-5f ) );
            const float  normalAngle  = sign( dot( projected, sliceDir ) ) * acos( cosNormal );

            float cosHorizon0 = -1.0f;
            float cosHorizon1 = -1.0f;
            SW_UNROLL for ( uint stepIndex = 1u; stepIndex <= (uint)SW_GTAO_STEPS; ++stepIndex )
            {
                const float2 offset = direction * ( stepUv * (float)stepIndex );
                const float2 uv1    = input._uv + offset;
                const float2 uv0    = input._uv - offset;
                const float3 delta1 = Transform::computeWorldPositionFromDepth( uv1, PassTexture::sampleDepthPoint( uv1 ).r ) - position;
                const float3 delta0 = Transform::computeWorldPositionFromDepth( uv0, PassTexture::sampleDepthPoint( uv0 ).r ) - position;
                const float  dist1  = length( delta1 );
                const float  dist0  = length( delta0 );
                // 반경 밖은 지평선을 내린다(-1 쪽으로) — 먼 벽이 가까운 틈을 가리지 않게.
                const float falloff1 = saturate( 1.0f - dist1 / SW_GTAO_RADIUS );
                const float falloff0 = saturate( 1.0f - dist0 / SW_GTAO_RADIUS );
                cosHorizon1          = max( cosHorizon1, lerp( -1.0f, dot( delta1, toView ) / max( dist1, 1e-5f ), falloff1 ) );
                cosHorizon0          = max( cosHorizon0, lerp( -1.0f, dot( delta0, toView ) / max( dist0, 1e-5f ), falloff0 ) );
            }
            // 음 방향은 sliceDir 의 반대쪽이다 — integrateSlice 는 음의 각으로 받는다.
            float bentAngle;
            visibility += integrateSlice( cosHorizon0, cosHorizon1, normalAngle, bentAngle ) * projectedLen;
            bentNormal += ( toView * cos( bentAngle ) + sliceDir * sin( bentAngle ) ) * projectedLen;
        }
        const float  ambientOcclusion = pow( saturate( visibility / (float)SW_GTAO_SLICES ), SW_GTAO_INTENSITY );
        const float3 bent             = normalize( bentNormal + normal * 1e-4f );
        // 그리지 않은 하늘은 가림 없음 · 굽은 노멀 없음이다.
        return depth < 1.0f ? float4( ambientOcclusion, bent * 0.5f + 0.5f ) : float4( 1.0f, 0.5f, 0.5f, 1.0f );
    }
}
