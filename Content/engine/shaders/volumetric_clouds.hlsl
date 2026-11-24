#include "binding.hlsli"
#include "fullscreen.hlsli"

/**
 * volumetric_clouds.hlsl — 볼륨 구름 패스(RenderPassKind::VolumetricClouds).
 *
 * 하늘 위 두께 있는 층(g_SkyCloudShape.y 부터 g_SkyCloudVolume.x km)을 저해상도 첨부(SkyCloudColor)에 레이마치하고, 지난 기록과 섞어 (산란 rgb — 불투명도를 이미 곱했다, 불투명도 a)를 낸다.
 * 스카이 패스(sky.hlsl)가 깊이 1 인 픽셀에서 그 결과를 하늘 위에 얹는다 — 합성이 곧 하늘 그리기라 전 화면 합성 패스를 더하지 않고, 물체 가장자리는 전체 해상도 깊이 테스트가 자른다.
 * 층은 카메라를 따라온다(평면 구름과 같다) — 유니티 HDRP Volumetric Clouds 의 Local Clouds 를 끈 모양이고 늘 장면 뒤에 있다. 언리얼 Volumetric Cloud 도 작은 화면에 걷고 시간 누적으로 되살린다.
 *
 * 밀도: 정수 해시 값 노이즈 fbm(3D 텍스처가 RHI 에 없다 — 굽지 않아 네 백엔드 · 웹이 같은 무늬다). 덮는 비율의 문턱을 층 높이로 올려 바닥은 평평하고 위는 줄고, 잔 노이즈로 가장자리를 깎는다.
 * 빛: 해 쪽으로 몇 걸음 걸은 광학 두께의 비어 감쇠 × 두 엽 HG 위상. 여러 번 산란은 감쇠 · 이방성을 줄인 옥타브 합으로 어림한다(Wrenninge 2013 — 언리얼 · HDRP 와 같은 갈래).
 *     해 투과율(g_SkyCloudSun)과 하늘 앰비언트(g_SkyCloudAmbient)는 C++ 가 프레임에 한 번 구한다(`FrameRenderer::applyFrameSky`).
 * 누적: 층을 무한히 먼 것으로 보고 시선 방향만 지난 뷰-투영으로 옮긴다(카메라를 따라오는 층이라 이동은 시차가 없다). 프레임마다 텍셀 안 자리와 걸음 시작을 비켜 기록이 모은다.
 * 하늘이 보이지 않는 텍셀은 걷지 않는다 — 불투명도에 작은 음수를 적어 다음 프레임이 기록으로 쓰지 않는다.
 */

static const float kCloudHorizonFadeStart = 0.02f; // sky.hlsl 과 같다 — 이 y 아래는 구름이 없다
static const float kCloudHorizonFadeRange = 0.18f;
static const float kCloudMaxMarchKm       = 14.0f;  // 지평선 쪽 시선이 걷는 길이의 상한
static const float kCloudSkippedAlpha     = -1e-3f; // 걷지 않은 텍셀 표시(합성 쌍선형에 보이지 않을 만큼 작다)
static const float kCloudMinTransmittance = 0.01f;
static const uint  kCloudBaseOctaveCount  = 3u;
static const uint  kCloudEdgeOctaveCount  = 2u;
static const float kCloudEdgeFrequency    = 4.1f;  // 바탕 노이즈 대비 가장자리 노이즈의 배율
static const float kCloudEdgeErosion      = 0.4f;  // 가장자리를 깎는 세기
static const float kCloudBottomFade       = 0.12f; // 층 바닥이 나타나는 높이 비율
static const float kCloudTaperStart       = 0.35f; // 이 높이 비율부터 문턱이 오른다
static const float kCloudTopTaper         = 0.35f; // 층 꼭대기에서 문턱이 오른 폭
static const uint  kCloudLightStepCount   = 4u;
static const float kCloudLightReach       = 0.6f; // 해 쪽으로 걷는 길이(층 두께 비율)
static const uint  kCloudScatterOctaves   = 3u;
static const float kCloudForwardLobe      = 0.8f;
static const float kCloudBackwardLobe     = -0.2f;
static const float kCloudLobeBlend        = 0.5f;
static const float kCloudAmbientScale     = 0.5f;  // 앰비언트의 몫 — 평면 구름(sky.hlsl)과 같다
static const int   kCloudDepthProbeStride = 2;     // 하늘이 보이는지 텍셀 둘레 깊이를 이 간격으로 본다
static const float kCloudMinHistoryWeight = 0.05f; // 쓸 수 있는 기록 이웃의 가중 합이 이보다 작으면 이번 값만 쓴다

/** @brief 볼륨 구름의 밀도 · 빛 · 기록 읽기 묶음입니다. */
struct VolumetricClouds
{
    static float hashCell( int3 cell );
    static float computeNoise( float3 position, uint octaveCount );
    static float computeDensity( float3 positionKm, bool bErodeEdge );
    static float computeSunOpticalDepth( float3 positionKm, float3 toSun );
    static float computePhase( float cosine, float anisotropyScale );
    static float computeSunScattering( float cosine, float opticalDepth );
    static float3 computeViewDirection( float2 uv );
    static bool isSkyVisible( int2 texel );
    static float4 marchLayer( float3 direction, float3 toSun, float stepOffset );
    static float4 loadHistory( float3 direction, out float outWeight );
};

static float VolumetricClouds::hashCell( int3 cell )
{
    uint value = (uint)cell.x * 1597334673u ^ (uint)cell.y * 3812015801u ^ (uint)cell.z * 2798796415u;
    value      = ( value ^ ( value >> 16u ) ) * 0x7feb352du;
    value      = ( value ^ ( value >> 15u ) ) * 0x846ca68bu;
    value      = value ^ ( value >> 16u );
    return (float)( value >> 8u ) / 16777216.0f;
}

/** @brief 3D 값 노이즈 fbm(0..1)입니다. */
static float VolumetricClouds::computeNoise( float3 position, uint octaveCount )
{
    float  sum           = 0.0f;
    float  amplitude     = 0.5f;
    float  amplitudeSum  = 0.0f;
    float3 noisePosition = position;
    SW_LOOP for ( uint octave = 0u; octave < octaveCount; ++octave )
    {
        const float3 base     = floor( noisePosition );
        const float3 fraction = noisePosition - base;
        const float3 smoothed = fraction * fraction * ( 3.0f - 2.0f * fraction );
        const int3   cell     = int3( base );
        const float  lowNear  = lerp( VolumetricClouds::hashCell( cell ), VolumetricClouds::hashCell( cell + int3( 1, 0, 0 ) ), smoothed.x );
        const float  highNear = lerp( VolumetricClouds::hashCell( cell + int3( 0, 1, 0 ) ), VolumetricClouds::hashCell( cell + int3( 1, 1, 0 ) ), smoothed.x );
        const float  lowFar   = lerp( VolumetricClouds::hashCell( cell + int3( 0, 0, 1 ) ), VolumetricClouds::hashCell( cell + int3( 1, 0, 1 ) ), smoothed.x );
        const float  highFar  = lerp( VolumetricClouds::hashCell( cell + int3( 0, 1, 1 ) ), VolumetricClouds::hashCell( cell + int3( 1, 1, 1 ) ), smoothed.x );
        sum += lerp( lerp( lowNear, highNear, smoothed.y ), lerp( lowFar, highFar, smoothed.y ), smoothed.z ) * amplitude;
        amplitudeSum += amplitude;
        noisePosition = noisePosition * 2.03f + float3( 17.3f, 5.7f, 9.1f );
        amplitude *= 0.5f;
    }
    return sum / max( amplitudeSum, 1.0e-4f );
}

/** @brief 눈을 원점으로 한 자리(km)의 구름 밀도(0..1)입니다. 층 밖은 0 입니다. */
static float VolumetricClouds::computeDensity( float3 positionKm, bool bErodeEdge )
{
    const float heightRatio = ( positionKm.y - g_SkyCloudShape.y ) / g_SkyCloudVolume.x;
    float       density     = 0.0f;
    SW_BRANCH if ( heightRatio > 0.0f && heightRatio < 1.0f )
    {
        const float2 wind          = g_SkyCloudWind.xy * ( g_SkyCloudWind.z * g_SwMaterialTime );
        const float3 noisePosition = float3( positionKm.x + wind.x, positionKm.y, positionKm.z + wind.y ) * g_SkyCloudShape.z;
        const float  bottom        = smoothstep( 0.0f, kCloudBottomFade, heightRatio );
        const float  taper         = smoothstep( kCloudTaperStart, 1.0f, heightRatio ) * kCloudTopTaper;
        const float  threshold     = 1.0f - g_SkyCloudShape.x + taper;
        const float  softness      = max( g_SkyCloudWind.w, 1.0e-3f );
        density                    = smoothstep( threshold - softness, threshold + softness, VolumetricClouds::computeNoise( noisePosition, kCloudBaseOctaveCount ) ) * bottom;
        SW_BRANCH if ( bErodeEdge && density > 0.0f )
        {
            const float edge = VolumetricClouds::computeNoise( noisePosition * kCloudEdgeFrequency, kCloudEdgeOctaveCount );
            density          = saturate( density - edge * kCloudEdgeErosion * ( 1.0f - density ) );
        }
    }
    return density;
}

/** @brief 자리에서 해 쪽으로 층 안을 걸은 광학 두께입니다. */
static float VolumetricClouds::computeSunOpticalDepth( float3 positionKm, float3 toSun )
{
    const float stepLength = g_SkyCloudVolume.x * kCloudLightReach / (float)kCloudLightStepCount;
    float       density    = 0.0f;
    SW_LOOP for ( uint lightStep = 0u; lightStep < kCloudLightStepCount; ++lightStep )
    {
        density += VolumetricClouds::computeDensity( positionKm + toSun * ( stepLength * ( (float)lightStep + 0.5f ) ), false );
    }
    return density * stepLength * g_SkyCloudVolume.y;
}

/** @brief 앞 · 뒤 두 엽 Henyey-Greenstein 위상 함수입니다(구 전체 적분이 1 — 1/4π). */
static float VolumetricClouds::computePhase( float cosine, float anisotropyScale )
{
    const float forward       = kCloudForwardLobe * anisotropyScale;
    const float backward      = kCloudBackwardLobe * anisotropyScale;
    const float forwardBase   = max( 1.0f + forward * forward - 2.0f * forward * cosine, 1.0e-4f );
    const float backwardBase  = max( 1.0f + backward * backward - 2.0f * backward * cosine, 1.0e-4f );
    const float forwardPhase  = ( 1.0f - forward * forward ) / ( forwardBase * sqrt( forwardBase ) );
    const float backwardPhase = ( 1.0f - backward * backward ) / ( backwardBase * sqrt( backwardBase ) );
    return lerp( forwardPhase, backwardPhase, kCloudLobeBlend ) * ( kInvPi * 0.25f );
}

/** @brief 해 빛의 산란 비율 — 감쇠 · 이방성 · 세기를 반씩 줄인 옥타브를 더해 여러 번 산란을 어림합니다. 단위는 평면 구름(sky.hlsl)의 해 빛과 같다. */
static float VolumetricClouds::computeSunScattering( float cosine, float opticalDepth )
{
    float scattering = 0.0f;
    float scale      = 1.0f;
    SW_UNROLL for ( uint octave = 0u; octave < kCloudScatterOctaves; ++octave )
    {
        scattering += scale * exp( -opticalDepth * scale ) * VolumetricClouds::computePhase( cosine, scale );
        scale *= 0.5f;
    }
    return scattering * kPi;
}

/** @brief 화면 자리의 시선 방향입니다(sky.hlsl 과 같은 식 — 행벡터 규약). */
static float3 VolumetricClouds::computeViewDirection( float2 uv )
{
    const float2 clipXy    = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
    const float4 nearPoint = mul( float4( clipXy, 0.0f, 1.0f ), g_InvViewProj );
    const float4 farPoint  = mul( float4( clipXy, 1.0f, 1.0f ), g_InvViewProj );
    return normalize( farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w );
}

/** @brief 이 저해상도 텍셀을 읽는 전체 해상도 픽셀(합성의 쌍선형이 이웃 반 텍셀까지 닿는다) 가운데 하늘이 하나라도 보이면 true 입니다. */
static bool VolumetricClouds::isSkyVisible( int2 texel )
{
    const int  divisor  = max( (int)( g_SkyCloudSample.w + 0.5f ), 1 );
    const int2 origin   = texel * divisor - ( divisor >> 1 ); // 양수의 반 — 정수 나눗셈이 아니라 자리 옮김이다(FXC X3556)
    const int2 rectEnd  = int2( g_ViewRectSize.xy ) - 1;
    const int  count    = divisor; // 덮는 폭은 텍셀 두 개(divisor × 2)이고 간격이 kCloudDepthProbeStride 다
    float      farthest = 0.0f;
    SW_LOOP for ( int row = 0; row < count; ++row )
    {
        SW_LOOP for ( int column = 0; column < count; ++column )
        {
            const int2 probe = clamp( origin + int2( column, row ) * kCloudDepthProbeStride, int2( 0, 0 ), rectEnd );
            farthest         = max( farthest, Bindless::loadEngineTexel( SW_ENGINE_ROLE( SceneDepth ), probe ).r );
        }
    }
    return farthest >= 1.0f;
}

/** @brief 층을 걸어 (산란 rgb — 불투명도 곱함, 불투명도 a)를 냅니다. */
static float4 VolumetricClouds::marchLayer( float3 direction, float3 toSun, float stepOffset )
{
    const float horizonFade = saturate( ( direction.y - kCloudHorizonFadeStart ) / kCloudHorizonFadeRange );
    const float inverseY    = 1.0f / max( direction.y, kCloudHorizonFadeStart );
    const float marchStart  = g_SkyCloudShape.y * inverseY;
    const float marchLength = min( g_SkyCloudVolume.x * inverseY, kCloudMaxMarchKm );
    const uint  stepCount   = horizonFade > 0.0f ? (uint)( g_SkyCloudVolume.w + 0.5f ) : 0u;
    const float stepLength  = marchLength / max( g_SkyCloudVolume.w, 1.0f );
    const float cosine      = dot( direction, toSun );
    // 해 복사휘도 — 대기 모드는 g_SkyCloudSun 이 해 배율 · 투과율을 들고, 그 밖은 키 라이트 세기를 곱한다(sky.hlsl 의 두 갈래와 같다).
    const bool   bAtmosphere = g_SkyAtmosphereRayleigh.w > 0.5f && g_SkyAtmosphereRayleigh.w < 1.5f;
    const float3 sunRadiance = g_KeyLightColor.rgb * g_SkyCloudSun.rgb * ( bAtmosphere ? 1.0f : g_KeyLightDirIntensity.w );

    float3 scattered     = float3( 0.0f, 0.0f, 0.0f );
    float  transmittance = 1.0f;
    SW_LOOP for ( uint viewStep = 0u; viewStep < stepCount && transmittance > kCloudMinTransmittance; ++viewStep )
    {
        const float3 positionKm = direction * ( marchStart + stepLength * ( (float)viewStep + stepOffset ) );
        const float  density    = VolumetricClouds::computeDensity( positionKm, true );
        SW_BRANCH if ( density > 0.0f )
        {
            const float  sunDepth     = VolumetricClouds::computeSunOpticalDepth( positionKm, toSun );
            const float3 source       = sunRadiance * VolumetricClouds::computeSunScattering( cosine, sunDepth ) + g_SkyCloudAmbient.rgb * kCloudAmbientScale;
            const float  stepTransmit = exp( -density * g_SkyCloudVolume.y * stepLength );
            scattered += source * ( transmittance * ( 1.0f - stepTransmit ) ); // 걸음 안을 적분한 산란(Hillaire 2016 — 걸음 수가 바뀌어도 밝기가 같다)
            transmittance *= stepTransmit;
        }
    }
    const float coverage = horizonFade * g_SkyCloudShape.w;
    return float4( scattered * g_SkyCloudColor.rgb * coverage, ( 1.0f - transmittance ) * coverage );
}

/** @brief 이 시선이 지난 프레임에 있던 자리의 기록입니다. 걷지 않은 텍셀 · 화면 밖 이웃은 빼고 쌍선형으로 섞습니다. @p outWeight 는 쓴 이웃의 가중 합입니다. */
static float4 VolumetricClouds::loadHistory( float3 direction, out float outWeight )
{
    const float4 previousClip = mul( float4( direction, 0.0f ), g_PrevViewProj );
    const float2 previousUv   = previousClip.xy / max( previousClip.w, 1.0e-4f ) * float2( 0.5f, -0.5f ) + 0.5f;
    const float2 rectTexels   = float2( Bindless::getEngineTextureSize( SW_ENGINE_ROLE( GBufferAlbedo ) ) ) * g_ViewRectScale.zw;
    const float2 texelUv      = clamp( previousUv, -1.0f, 2.0f ) * rectTexels - 0.5f;
    const int2   base         = int2( floor( texelUv ) );
    const float2 blend        = texelUv - floor( texelUv );
    const int2   rectEnd      = max( int2( rectTexels + 0.5f ) - 1, int2( 0, 0 ) );
    const bool   bInFront     = previousClip.w > 1.0e-4f;
    float4       sum          = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    float        weightSum    = 0.0f;
    SW_UNROLL for ( int neighbor = 0; neighbor < 4; ++neighbor )
    {
        const int2   offset  = int2( neighbor % 2, neighbor / 2 );
        const int2   texel   = base + offset;
        const bool   bInside = bInFront && all( texel >= int2( 0, 0 ) ) && all( texel <= rectEnd );
        const float4 value   = Bindless::loadEngineTexel( SW_ENGINE_ROLE( GBufferAlbedo ), clamp( texel, int2( 0, 0 ), rectEnd ) );
        const float2 axis    = lerp( 1.0f - blend, blend, float2( offset ) );
        const float  weight  = ( bInside && value.a >= 0.0f ) ? axis.x * axis.y : 0.0f;
        sum += value * weight;
        weightSum += weight;
    }
    outWeight = weightSum;
    return sum / max( weightSum, 1.0e-4f );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 저해상도 텍셀 하나의 UV 크기 — 흐름 갈래 밖에서 잰다(미분).
    const float2 texelUv = float2( abs( ddx( input._uv.x ) ), abs( ddy( input._uv.y ) ) );
    float4       result  = float4( 0.0f, 0.0f, 0.0f, kCloudSkippedAlpha );
    SW_BRANCH if ( VolumetricClouds::isSkyVisible( int2( input._position.xy ) ) )
    {
        const float3 lightTravel  = g_KeyLightDirIntensity.xyz;
        const float  travelLength = length( lightTravel );
        const float3 toSun        = travelLength > 1.0e-6f ? -lightTravel / travelLength : float3( 0.0f, 1.0f, 0.0f );
        // 텍셀 안 자리(xy)와 걸음 시작(z)을 프레임마다 비킨다 — 기록이 모은다.
        const float3 direction  = VolumetricClouds::computeViewDirection( input._uv + g_SkyCloudSample.xy * texelUv );
        const float  stepOffset = frac( CommonMath::computeInterleavedGradientNoise( input._position.xy ) + g_SkyCloudSample.z );
        result                  = VolumetricClouds::marchLayer( direction, toSun, stepOffset );
        SW_BRANCH if ( g_SkyCloudVolume.z > 0.0f )
        {
            float        historyWeight = 0.0f;
            const float4 history       = VolumetricClouds::loadHistory( VolumetricClouds::computeViewDirection( input._uv ), historyWeight );
            result                     = lerp( result, history, historyWeight > kCloudMinHistoryWeight ? g_SkyCloudVolume.z : 0.0f );
        }
    }
    return result;
}
