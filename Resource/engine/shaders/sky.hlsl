// 하늘 — 스카이 패스(RenderPassType::Sky). 불투명 패스 뒤 · 반투명 앞에 풀스크린 삼각형을 깊이 1 에 그린다.
// 깊이 테스트(LessEqual)만 하고 쓰지 않으므로 아무것도 그리지 않은 곳(깊이 클리어 1)만 칠해진다. 씬에 하늘이 없으면 패스가 돌지 않는다(FrameRenderer).
//
// 모드 둘 — 그러데이션(g_SkyAtmosphereRayleigh.w = 0)과 대기 산란(= 1).
//   그러데이션: C++ SkyGradient::evaluateRadiance 와 같은 식이다 — 위는 천정 → 지평선, 아래는 땅 → 지평선, 지평선 쪽 비율 (1 - |y|)^지수, 마지막에 세기.
//     상수 배치: g_SkyZenith(rgb, w 지수) · g_SkyHorizon(rgb) · g_SkyGround(rgb, w 세기).
//   대기 산란: C++ SkyAtmosphere::evaluateScattering 와 같은 식 · 같은 구간 수(kAtmosphereViewSteps · kAtmosphereLightSteps) · 같은 상수다(식을 고치면 두 곳을 함께).
//     눈은 행성 중심 위 (행성 반지름 + 눈 고도) 에 있고 시선을 대기 끝(또는 행성 표면)까지 걸으며 레일리 · 미 단일 산란을 센다. 해 방향은 키 라이트 상수의 반대(-g_KeyLightDirIntensity.xyz),
//     해 복사휘도는 키 라이트 색 × 해 배율(g_SkyAtmosphereMie.w) × 세기(g_SkyGround.w)다. 땅(시선이 행성에 닿는 방향)의 반사율은 g_SkyGround.rgb 다.
//     해 원반(g_SkyAtmosphereGeometry.w 가 0 보다 크면)은 셰이더만 그린다 — 프로브 베이크는 원반을 넣지 않는다(방향광이 따로 비춘다).
// 구름(g_SkyCloudColor.w = 1): 눈 위 고정 고도의 평면(g_SkyCloudShape.y km)에 값 노이즈 fbm 을 놓고 바람으로 흘린다(시간 = g_SwMaterialTime). 레이마치 없음. 평면 위 점 = 눈 xz 를 원점으로 한 시선의 평면 교점이라
//     구름은 카메라를 따라온다(하늘 상자와 같다). 해 쪽으로 한 걸음 옮긴 밀도로 그늘을 어림하고, 지평선 근처는 평면이 너무 멀어 지운다. 구름은 프로브 베이크에 보이지 않는다.
#include "binding.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _clipXy : TEXCOORD0;
};

static const uint  kAtmosphereViewSteps   = 16u;     // C++ SkyAtmosphere::kViewStepCount
static const uint  kAtmosphereLightSteps  = 8u;      // C++ SkyAtmosphere::kLightStepCount
static const float kRayleighScaleHeightKm = 8.0f;    // C++ SkyAtmosphere::kRayleighScaleHeightKm
static const float kMieExtinctionRatio    = 1.1f;    // C++ SkyAtmosphere::kMieExtinctionRatio
static const float kSurfaceEpsilonKm      = 1.0e-3f; // C++ SkyGradientInternal::kSurfaceEpsilonKm
static const float kSunDiscRadianceScale  = 50.0f;   // 원반이 번지도록 해 복사휘도에 곱하는 배율(셰이더만)
static const float kCloudHorizonFadeStart = 0.02f;   // 이 y 아래는 구름이 없다
static const float kCloudHorizonFadeRange = 0.18f;   // 이 폭으로 서서히 나타난다
static const float kCloudSunStepKm        = 0.35f;   // 그늘을 읽으려고 해 쪽으로 옮기는 평면 거리(km)
static const uint  kCloudOctaveCount      = 4u;

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    const float2 clipXy = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    // z = w 라 깊이가 1 이다(네 백엔드 모두 0~1 깊이 — GL 은 glClipControl ZERO_TO_ONE).
    output._position = float4( clipXy, 1.0f, 1.0f );
    output._clipXy   = clipXy;
    return output;
}

float3 evaluateSky( float3 direction )
{
    const float  elevation     = clamp( direction.y, -1.0f, 1.0f );
    const float  horizonWeight = pow( max( 1.0f - abs( elevation ), 0.0f ), g_SkyZenith.w );
    const float3 poleColor     = ( elevation >= 0.0f ) ? g_SkyZenith.rgb : g_SkyGround.rgb;
    return lerp( poleColor, g_SkyHorizon.rgb, horizonWeight ) * g_SkyGround.w;
}

// ------------------------------------------------------------------------------
// 대기 산란 — C++ SkyAtmosphere::evaluateScattering 와 같다(거리 단위 km).
// ------------------------------------------------------------------------------
/** @brief 반직선 origin + t · direction 과 중심이 원점인 구의 교점 거리입니다. 만나면 true 이고 outNear <= outFar 입니다. */
bool intersectSphere( float3 origin, float3 direction, float radius, out float outNear, out float outFar )
{
    const float halfB        = dot( origin, direction );
    const float centerTerm   = dot( origin, origin ) - radius * radius;
    const float discriminant = halfB * halfB - centerTerm;
    const float root         = sqrt( max( discriminant, 0.0f ) );
    outNear                  = -halfB - root;
    outFar                   = -halfB + root;
    return discriminant >= 0.0f;
}

/**
 * @brief 점에서 해 쪽으로 대기 끝까지의 레일리 · 미 광학 두께(밀도 적분)입니다. 행성이 해를 가리거나 대기를 벗어나지 못하면 false 입니다(그때 값은 쓰지 않는다).
 */
bool computeSunOpticalDepth( float3 position, float3 toSun, out float outRayleigh, out float outMie )
{
    const float planetRadius = g_SkyAtmosphereGeometry.x;
    const float topRadius    = planetRadius + g_SkyAtmosphereGeometry.y;
    float       planetNear;
    float       planetFar;
    float       topNear;
    float       topFar;
    const bool  bPlanetOverlap = intersectSphere( position, toSun, planetRadius, planetNear, planetFar );
    const bool  bBlocked       = bPlanetOverlap && planetFar > kSurfaceEpsilonKm && planetNear > kSurfaceEpsilonKm;
    const bool  bTopHit        = intersectSphere( position, toSun, topRadius, topNear, topFar );
    const float stepLength     = topFar / (float)kAtmosphereLightSteps;
    float       rayleigh       = 0.0f;
    float       mie            = 0.0f;
    SW_LOOP for ( uint step = 0u; step < kAtmosphereLightSteps; ++step )
    {
        const float3 samplePosition = position + toSun * ( ( (float)step + 0.5f ) * stepLength );
        const float  height         = max( length( samplePosition ) - planetRadius, 0.0f );
        rayleigh += exp( -height / kRayleighScaleHeightKm ) * stepLength;
        mie += exp( -height / g_SkyAtmosphereMie.y ) * stepLength;
    }
    outRayleigh = rayleigh;
    outMie      = mie;
    return bBlocked == false && bTopHit;
}

/** @brief 방향에서 오는 단일 산란 복사휘도(해 복사휘도 1 당)입니다. 시선이 행성에 닿으면 땅 반사를 더한다. */
float3 computeAtmosphere( float3 direction, float3 toSun, float3 groundAlbedo )
{
    const float  planetRadius = g_SkyAtmosphereGeometry.x;
    const float  topRadius    = planetRadius + g_SkyAtmosphereGeometry.y;
    const float3 origin       = float3( 0.0f, planetRadius + g_SkyAtmosphereGeometry.z, 0.0f );
    float        atmosphereNear;
    float        atmosphereFar;
    const bool   bInAtmosphere = intersectSphere( origin, direction, topRadius, atmosphereNear, atmosphereFar );

    float      planetNear;
    float      planetFar;
    const bool bPlanetOverlap = intersectSphere( origin, direction, planetRadius, planetNear, planetFar );
    // 고도 0 의 눈은 아래 시선의 가까운 교점이 0 근처라 먼 교점으로 땅을 판정한다(C++ SkyAtmosphere::evaluateScattering 과 같다).
    const bool  bHitsGround = bPlanetOverlap && planetFar > kSurfaceEpsilonKm;
    const float segmentEnd  = bHitsGround ? min( atmosphereFar, max( planetNear, 0.0f ) ) : atmosphereFar;

    const float3 betaRayleigh      = g_SkyAtmosphereRayleigh.rgb;
    const float  betaMie           = g_SkyAtmosphereMie.x;
    const float  betaMieExtinction = betaMie * kMieExtinctionRatio;
    const float  stepLength        = segmentEnd / (float)kAtmosphereViewSteps;
    float        viewRayleigh      = 0.0f;
    float        viewMie           = 0.0f;
    float3       sumRayleigh       = float3( 0.0f, 0.0f, 0.0f );
    float3       sumMie            = float3( 0.0f, 0.0f, 0.0f );
    SW_LOOP for ( uint step = 0u; step < kAtmosphereViewSteps; ++step )
    {
        const float3 samplePosition  = origin + direction * ( ( (float)step + 0.5f ) * stepLength );
        const float  height          = max( length( samplePosition ) - planetRadius, 0.0f );
        const float  densityRayleigh = exp( -height / kRayleighScaleHeightKm ) * stepLength;
        const float  densityMie      = exp( -height / g_SkyAtmosphereMie.y ) * stepLength;
        viewRayleigh += densityRayleigh;
        viewMie += densityMie;
        float        sunRayleigh;
        float        sunMie;
        const bool   bSunVisible = computeSunOpticalDepth( samplePosition, toSun, sunRayleigh, sunMie );
        const float3 attenuation = exp( -( betaRayleigh * ( viewRayleigh + sunRayleigh ) + betaMieExtinction * ( viewMie + sunMie ) ) );
        const float  visible     = bSunVisible ? 1.0f : 0.0f;
        sumRayleigh += attenuation * ( densityRayleigh * visible );
        sumMie += attenuation * ( densityMie * visible );
    }

    const float cosine        = dot( direction, toSun );
    const float phaseRayleigh = 3.0f / ( 16.0f * kPi ) * ( 1.0f + cosine * cosine );
    const float squaredG      = g_SkyAtmosphereMie.z * g_SkyAtmosphereMie.z;
    const float phaseMie      = 3.0f / ( 8.0f * kPi ) * ( ( 1.0f - squaredG ) * ( 1.0f + cosine * cosine ) ) /
                           ( ( 2.0f + squaredG ) * pow( max( 1.0f + squaredG - 2.0f * g_SkyAtmosphereMie.z * cosine, 1.0e-4f ), 1.5f ) );
    float3 radiance = sumRayleigh * betaRayleigh * phaseRayleigh + sumMie * ( betaMie * phaseMie );

    // 땅 — 해 빛을 받은 알베도가 시선 투과율만큼 줄어 눈에 온다(램버트, 1/π).
    const float3 groundPosition = origin + direction * segmentEnd;
    const float3 normal         = groundPosition / max( length( groundPosition ), 1.0e-4f );
    const float  normalDotLight = max( dot( normal, toSun ), 0.0f );
    float        groundRayleigh;
    float        groundMie;
    const bool   bGroundLit     = computeSunOpticalDepth( groundPosition, toSun, groundRayleigh, groundMie );
    const float3 groundTransmit = exp( -( betaRayleigh * ( viewRayleigh + groundRayleigh ) + betaMieExtinction * ( viewMie + groundMie ) ) );
    const bool   bAddsGround    = bHitsGround && normalDotLight > 0.0f && bGroundLit;
    radiance += bAddsGround ? groundAlbedo * ( normalDotLight / kPi ) * groundTransmit : float3( 0.0f, 0.0f, 0.0f );
    return bInAtmosphere ? radiance : float3( 0.0f, 0.0f, 0.0f );
}

/** @brief 해 원반의 복사휘도 배율입니다(해 복사휘도에 곱하기 전, 눈에서 해까지의 투과율 포함). 원반이 없거나 해가 행성에 가려지면 0 입니다. */
float3 computeSunDisc( float3 direction, float3 toSun )
{
    const float  discAngle = g_SkyAtmosphereGeometry.w;
    const float  cosAngle  = dot( direction, toSun );
    const float  cosOuter  = cos( discAngle );
    const float  cosInner  = cos( discAngle * 0.8f );
    const float  edge      = discAngle > 0.0f ? smoothstep( cosOuter, cosInner, cosAngle ) : 0.0f;
    const float3 origin    = float3( 0.0f, g_SkyAtmosphereGeometry.x + g_SkyAtmosphereGeometry.z, 0.0f );
    float        sunRayleigh;
    float        sunMie;
    const bool   bVisible = computeSunOpticalDepth( origin, toSun, sunRayleigh, sunMie );
    const float3 transmit = exp( -( g_SkyAtmosphereRayleigh.rgb * sunRayleigh + g_SkyAtmosphereMie.x * kMieExtinctionRatio * sunMie ) );
    return bVisible ? transmit * ( edge * kSunDiscRadianceScale ) : float3( 0.0f, 0.0f, 0.0f );
}

// ------------------------------------------------------------------------------
// 구름 — 평면 값 노이즈. 정수 해시라 백엔드마다 같은 무늬다.
// ------------------------------------------------------------------------------
float hashCloud( int2 cell )
{
    uint value = (uint)cell.x * 1597334673u ^ (uint)cell.y * 3812015801u;
    value      = ( value ^ ( value >> 16u ) ) * 0x7feb352du;
    value      = ( value ^ ( value >> 15u ) ) * 0x846ca68bu;
    value      = value ^ ( value >> 16u );
    return (float)( value >> 8u ) / 16777216.0f;
}

float valueNoise( float2 position )
{
    const float2 base     = floor( position );
    const float2 fraction = position - base;
    const float2 smoothed = fraction * fraction * ( 3.0f - 2.0f * fraction );
    const int2   cell     = int2( base );
    const float  bottom   = lerp( hashCloud( cell ), hashCloud( cell + int2( 1, 0 ) ), smoothed.x );
    const float  top      = lerp( hashCloud( cell + int2( 0, 1 ) ), hashCloud( cell + int2( 1, 1 ) ), smoothed.x );
    return lerp( bottom, top, smoothed.y );
}

float computeCloudNoise( float2 position )
{
    float  sum           = 0.0f;
    float  amplitude     = 0.5f;
    float2 noisePosition = position;
    SW_UNROLL for ( uint octave = 0u; octave < kCloudOctaveCount; ++octave )
    {
        sum += valueNoise( noisePosition ) * amplitude;
        noisePosition = noisePosition * 2.03f + float2( 17.3f, 9.1f );
        amplitude *= 0.5f;
    }
    return sum / 0.9375f; // 0.5 + 0.25 + 0.125 + 0.0625
}

float computeCloudDensity( float2 planePosition )
{
    const float threshold = 1.0f - g_SkyCloudShape.x;
    const float softness  = max( g_SkyCloudWind.w, 1.0e-3f );
    return smoothstep( threshold - softness, threshold + softness, computeCloudNoise( planePosition ) );
}

/**
 * @brief 구름 층의 색(rgb)과 불투명도(a)입니다. @p sunRadiance 는 해 복사휘도(투과율 곱함), @p skyBehind 는 이 방향의 하늘빛(앰비언트 근사)입니다.
 */
float4 evaluateClouds( float3 direction, float3 toSun, float3 sunRadiance, float3 skyBehind )
{
    const float  altitude         = g_SkyCloudShape.y;
    const float  planeDistance    = altitude / max( direction.y, 1.0e-3f );
    const float2 wind             = g_SkyCloudWind.xy * ( g_SkyCloudWind.z * g_SwMaterialTime );
    const float2 planePosition    = ( direction.xz * planeDistance + wind ) * g_SkyCloudShape.z;
    const float  density          = computeCloudDensity( planePosition );
    const float  densityTowardSun = computeCloudDensity( planePosition + toSun.xz * ( kCloudSunStepKm * g_SkyCloudShape.z ) );
    const float  horizonFade      = saturate( ( direction.y - kCloudHorizonFadeStart ) / kCloudHorizonFadeRange );
    const float  alpha            = density * g_SkyCloudShape.w * horizonFade;
    const float  lit              = lerp( 0.45f, 1.0f, saturate( 1.0f - densityTowardSun ) );
    const float3 sunLight         = sunRadiance * ( saturate( toSun.y ) * lit / kPi );
    const float3 color            = g_SkyCloudColor.rgb * ( sunLight + skyBehind * 0.5f );
    return float4( color, alpha );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 이 픽셀을 지나는 시선 — 가까운 면과 먼 면의 월드 점을 잇는다(행벡터 규약, lighting.hlsli Transform::computeWorldPositionFromDepth 와 같은 순서). 직교 카메라도 같다.
    const float4 nearPoint = mul( float4( input._clipXy, 0.0f, 1.0f ), g_InvViewProj );
    const float4 farPoint  = mul( float4( input._clipXy, 1.0f, 1.0f ), g_InvViewProj );
    const float3 direction = normalize( farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w );

    const bool   bAtmosphere  = g_SkyAtmosphereRayleigh.w > 0.5f;
    const float3 lightTravel  = g_KeyLightDirIntensity.xyz;
    const float  travelLength = length( lightTravel );
    const float3 toSun        = travelLength > 1.0e-6f ? -lightTravel / travelLength : float3( 0.0f, 1.0f, 0.0f );
    // 해 복사휘도: 대기 모드는 키 라이트 색 × 해 배율 × 세기(C++ SkyGradient::evaluateRadiance 와 같다). 그러데이션 모드는 키 라이트의 색 × 세기다(구름만 쓴다).
    const float3 atmosphereSun = g_KeyLightColor.rgb * ( g_SkyAtmosphereMie.w * g_SkyGround.w );
    const float3 gradientSun   = g_KeyLightColor.rgb * g_KeyLightDirIntensity.w;

    float3 sky = float3( 0.0f, 0.0f, 0.0f );
    float3 sun = gradientSun;
    SW_BRANCH if ( bAtmosphere )
    {
        sun = atmosphereSun;
        sky = ( computeAtmosphere( direction, toSun, g_SkyGround.rgb ) + computeSunDisc( direction, toSun ) ) * atmosphereSun;
    }
    else
    {
        sky = evaluateSky( direction );
    }

    const bool bClouds = g_SkyCloudColor.w > 0.5f;
    SW_BRANCH if ( bClouds )
    {
        // 구름이 받는 해 빛은 눈에서 해까지의 투과율로 어림한다(대기 모드만 — 그러데이션은 1).
        float3 sunTransmit = float3( 1.0f, 1.0f, 1.0f );
        SW_BRANCH if ( bAtmosphere )
        {
            float        sunRayleigh;
            float        sunMie;
            const float3 observer = float3( 0.0f, g_SkyAtmosphereGeometry.x + g_SkyAtmosphereGeometry.z, 0.0f );
            const bool   bVisible = computeSunOpticalDepth( observer, toSun, sunRayleigh, sunMie );
            const float3 transmit = exp( -( g_SkyAtmosphereRayleigh.rgb * sunRayleigh + g_SkyAtmosphereMie.x * kMieExtinctionRatio * sunMie ) );
            sunTransmit           = bVisible ? transmit : float3( 0.0f, 0.0f, 0.0f );
        }
        const float4 cloud = evaluateClouds( direction, toSun, sun * sunTransmit, sky );
        sky                = lerp( sky, cloud.rgb, cloud.a );
    }
    return float4( sky, 1.0f );
}
