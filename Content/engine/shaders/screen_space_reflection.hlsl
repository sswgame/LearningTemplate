/**
 * screen_space_reflection.hlsl — 화면 공간 반사 패스(RenderPassType::ScreenSpaceReflection).
 *
 * 디퍼드에서 불투명 · 하늘을 그린 뒤 · 반투명 앞에 돈다. G버퍼 노멀로 시선을 반사한 광선을 월드에서 같은 걸음으로 나아가며 화면에 투영해, 장면 깊이보다 뒤로
 * 들어간 첫 걸음(두께 g_SSRParams.z 안)을 맞은 자리로 보고 이분 탐색으로 다듬는다. 맞은 자리의 색은 장면 색 사본(SceneColorCopy — 반사가 자기 결과를 읽지 않는다)에서 읽는다.
 * 출력은 장면 색 위에 프리멀티플라이로 블렌드한다(반사색 × 무게, 무게) — 안개와 같은 모양이라 출력 첨부가 장면 색 그대로이고 반투명이 그 위에 그린다.
 * 무게 = 세기 × 프레넬(Schlick, 금속도로 F0) × 거칠기 감쇠(최대 거칠기에서 0) × 화면 가장자리 · 광선 끝 감쇠. 못 맞힌 픽셀 · 하늘 · G버퍼를 그리지 않은 픽셀은 (0, 0)이라 장면 그대로다.
 * 깊이 · 노멀은 Load 로 읽는다 — 반복 안의 기울기 샘플은 DX11(FXC)이 거절한다.
 * 언리얼 SSR 은 HZB(깊이 밉 사슬)로 빈 칸을 건너뛴다. 이 엔진에는 HZB 가 없어 같은 걸음으로 나아간다(걸음 수 kSsrStepCount) — 걸음이 굵어 얇은 물체를 건너뛸 수 있다.
 *
 * 바인딩 계약: 패스 상수 g_SSRParams(세기, 최대 거리 m, 두께 m, 최대 거칠기), 입력 역할 SceneColorCopy · SceneDepth · GBufferNormal(필수) · GBufferMaterial(선택).
 */
#include "binding.hlsli"
#include "fullscreen.hlsli"

/// @brief 광선 걸음 수와 이분 탐색 횟수입니다.
static const int kSsrStepCount   = 32;
static const int kSsrRefineCount = 4;

/** @brief 장면 깊이의 @p uv 텍셀 값입니다(점, Load). */
float loadSceneDepth( float2 uv, float2 depthSize )
{
    const int2 texel = clamp( int2( uv * depthSize ), int2( 0, 0 ), int2( depthSize ) - int2( 1, 1 ) );
    return Bindless::loadEngineTexel( g_SceneDepthIndex, SW_EMU_SLOT_SceneDepth, texel ).r;
}

/**
 * @brief 월드 점 @p position 이 장면 깊이보다 뒤(두께 안)인지 봅니다. 시선 거리는 그 화면 자리의 가까운 면 점에서 잰다(원근 · 직교 모두 같은 식).
 * @param outUv 그 점의 화면 UV
 * @return 1 맞음 · 0 앞 · -1 화면 밖이거나 두께보다 깊다(그 걸음은 버린다)
 */
int classifyRayPoint( float3 position, float2 depthSize, out float2 outUv )
{
    const float4 clip        = mul( float4( position, 1.0f ), g_ViewProj );
    const float  w           = max( clip.w, 1e-5f );
    outUv                    = clip.xy / w * float2( 0.5f, -0.5f ) + 0.5f;
    const bool   bOnScreen   = clip.w > 1e-5f && all( outUv >= 0.0f ) && all( outUv <= 1.0f );
    const float  sceneDepth  = loadSceneDepth( outUv, depthSize );
    const float3 nearPoint   = Transform::computeWorldPositionFromDepth( outUv, 0.0f );
    const float  rayDistance = distance( position, nearPoint );
    const float  sceneDist   = distance( Transform::computeWorldPositionFromDepth( outUv, sceneDepth ), nearPoint );
    const float  behind      = rayDistance - sceneDist;
    const int    result      = behind < 0.0f ? 0 : ( behind <= g_SSRParams.z && sceneDepth < 1.0f ? 1 : -1 );
    return bOnScreen ? result : -1;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float2 uv        = input._uv;
    const float2 depthSize = float2( Bindless::getEngineTextureSize( g_SceneDepthIndex, SW_EMU_SLOT_SceneDepth ) );
    const float  depth     = loadSceneDepth( uv, depthSize );
    float        roughness = 1.0f;
    float        metallic  = 0.0f;
    bool         bDrawn    = false;
    PassTexture::loadGbufferMaterial( uv, roughness, metallic, bDrawn );

    float4 result = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    SW_BRANCH if ( bDrawn && depth < 1.0f && roughness < g_SSRParams.w )
    {
        const float3 position  = Transform::computeWorldPositionFromDepth( uv, depth );
        const float3 viewDir   = normalize( Transform::computeWorldPositionFromDepth( uv, 1.0f ) - Transform::computeWorldPositionFromDepth( uv, 0.0f ) );
        const float3 normal    = PassTexture::sampleNormal( uv );
        const float3 reflected = reflect( viewDir, normal );
        const float  stepSize  = g_SSRParams.y / float( kSsrStepCount );

        // 같은 걸음으로 나아가 처음 뒤로 들어간 걸음을 찾는다. 화면 밖 · 두께보다 깊은 걸음에서 멈춘다(break 없이 표시로).
        bool  bSearching = true;
        bool  bHit       = false;
        float hitLower   = 0.0f;
        float hitUpper   = 0.0f;
        float hitStep    = 0.0f;
        SW_LOOP for ( int step = 1; step <= kSsrStepCount; ++step )
        {
            float2    stepUv;
            const int state = bSearching ? classifyRayPoint( position + reflected * ( stepSize * float( step ) ), depthSize, stepUv ) : 0;
            bHit            = bHit || ( bSearching && state == 1 );
            hitLower        = bSearching && state == 1 ? stepSize * float( step - 1 ) : hitLower;
            hitUpper        = bSearching && state == 1 ? stepSize * float( step ) : hitUpper;
            hitStep         = bSearching && state == 1 ? float( step ) : hitStep;
            bSearching      = bSearching && state == 0;
        }

        SW_BRANCH if ( bHit )
        {
            // 맞은 걸음과 그 앞 걸음 사이를 반씩 좁힌다.
            float2 hitUv = float2( 0.0f, 0.0f );
            SW_UNROLL for ( int refine = 0; refine < kSsrRefineCount; ++refine )
            {
                const float middle = ( hitLower + hitUpper ) * 0.5f;
                float2      middleUv;
                const int   state = classifyRayPoint( position + reflected * middle, depthSize, middleUv );
                hitUpper          = state == 1 ? middle : hitUpper;
                hitLower          = state == 1 ? hitLower : middle;
            }
            float2    finalUv;
            const int finalState = classifyRayPoint( position + reflected * hitUpper, depthSize, finalUv );
            hitUv                = finalUv;

            const float3 color       = Bindless::sampleIndex( g_SceneColorCopyIndex, hitUv ).rgb;
            const float2 edge        = min( hitUv, 1.0f - hitUv );
            const float  edgeFade    = saturate( min( edge.x, edge.y ) * 10.0f );
            const float  travelFade  = 1.0f - hitStep / float( kSsrStepCount );
            const float  roughFade   = 1.0f - saturate( roughness / max( g_SSRParams.w, 1e-4f ) );
            const float  cosine      = saturate( dot( -viewDir, normal ) );
            const float  baseReflect = lerp( 0.04f, 1.0f, metallic );
            const float  fresnel     = baseReflect + ( 1.0f - baseReflect ) * pow( 1.0f - cosine, 5.0f );
            const float  weight      = finalState == 1 ? saturate( g_SSRParams.x * fresnel * roughFade * edgeFade * travelFade ) : 0.0f;
            result                   = float4( color * weight, weight );
        }
    }
    return result;
}
