// taa.hlsl — 시간 누적(TAA)과 시간 업스케일(TAAU) 해석. 이번 원본(SourceColor — 렌더 해상도)과 지난 출력(기록 — C++ 가 g_GBufferAlbedo 슬롯에 바인딩한다, 출력 해상도)을 섞는다.
// 기록은 이 픽셀이 지난 프레임에 있던 화면 자리에서 읽는다(재투영). 그 자리는 불투명 지오메트리 패스가 쓴 화면 속도(SceneVelocity — 물체마다 지난 월드 행렬 · 스킨 위치,
// binding_output.hlsli)로 찾는다 — 카메라에 붙어 함께 움직이는 물체(1인칭 총 · 캐릭터)는 속도가 0 에 가까워 기록이 제자리에 있다. 속도는 3x3 에서 가장 가까운 깊이의 것을 쓴다
// (가장자리 늘이기 — 언리얼 TAA 의 velocity dilation · 유니티 URP TAA 와 같은 갈래). 그리지 않은 픽셀(하늘, 깊이 1)은 속도가 없어 깊이를 지난 뷰-투영(g_PrevViewProj)으로 다시
// 투영한다(motion_blur.hlsl 과 같은 식). 재투영한 자리가 화면 밖이면 이번 원본만 쓴다. 컷 · 후처리를 끈 뷰는 C++ 가 기록 슬롯에 이번 원본을 바인딩한다(섞어도 이번 그림만 남는다).
// 모양은 Adria(MIT) · Salvi 2016 · Playdead INSIDE TAA · 언리얼 TAAU 를 옮겼다:
//   - 기록은 5 탭 Catmull-Rom 으로 읽는다(Jimenez — 바이리니어 재투영이 프레임마다 쌓여 흐려지는 것을 막는다).
//   - 이웃 색 클리핑은 YCoCg 공간의 분산 상자(3x3 평균 ± γσ, 이웃 최소 · 최대와 겹친 곳)로 자른다 — 상자 가운데를 향해 자르므로(clip) 색조가 덜 틀어진다.
//   - 기록 비율은 기록 신뢰도(자른 기록과 이번 휘도 차)와 화면 속도로 하한 · 상한 사이에서 고른다.
//   - 시간 업스케일(g_TemporalUpscale.x 1 — 출력이 렌더보다 크다)이면 이번 색을 출력 픽셀 중심 둘레 3x3 렌더 표본의 업샘플 가중 평균으로 짓는다. 가중은 지터한 표본 자리와
//     출력 픽셀 중심 사이 거리의 가우스(블랙먼-해리스 근사)이고, 가장 가까운 표본이 멀수록 기록을 더 믿는다. 0 이면 업스케일 없는 경로와 같은 값이다.
// 스칼라 식은 temporal_math.hlsli(C++ TemporalMath.h 가 같은 파일을 읽어 단위 테스트한다) 하나다.
#include "binding.hlsli"
#include "fullscreen.hlsli"
#include "temporal_math.hlsli"

// 값은 Config/Engine/EngineConfig.json `_render._taa*` 가 정본이고 엔진이 모든 컴파일에 define 으로 넣는다(ShaderCompiler::appendEngineDefines).
#if !defined( SW_TAA_HISTORY_WEIGHT ) || !defined( SW_TAA_MIN_HISTORY_WEIGHT ) || !defined( SW_TAA_VARIANCE_CLIP_GAMMA ) || !defined( SW_TAA_VELOCITY_REJECT_PIXELS )
    #error "SW_TAA_* is not defined - the engine injects it from EngineConfig _render._taa*"
#endif
static const float kMaxHistoryWeight     = SW_TAA_HISTORY_WEIGHT;         // 신뢰도가 높고 서 있는 픽셀의 기록 비율
static const float kMinHistoryWeight     = SW_TAA_MIN_HISTORY_WEIGHT;     // 기록을 믿기 어렵거나 빠른 픽셀의 기록 비율
static const float kVarianceClipGamma    = SW_TAA_VARIANCE_CLIP_GAMMA;    // 분산 상자의 폭(σ 의 배수)
static const float kVelocityRejectPixels = SW_TAA_VELOCITY_REJECT_PIXELS; // 이 화면 속도(출력 px)에서 기록 비율이 하한이 된다
static const float kSkyDepth             = 1.0f;                          // 깊이 클리어 값 — 아무것도 그리지 않은 픽셀(속도 출력이 없다)
static const float kClipEpsilon          = 1.0e-5f;                       // 상자 폭이 0 인 축(단색 이웃)에서 나누기를 막는다

struct TemporalAA
{
    static float3 toYCoCg( float3 rgb );
    static float3 toRgb( float3 yCoCg );
    static float3 sampleHistoryCatmullRom( float2 uv );
    static float3 clipToBox( float3 history, float3 boxMin, float3 boxMax );
};

static float3 TemporalAA::toYCoCg( float3 rgb )
{
    return float3( dot( rgb, float3( 0.25f, 0.5f, 0.25f ) ), dot( rgb, float3( 0.5f, 0.0f, -0.5f ) ), dot( rgb, float3( -0.25f, 0.5f, -0.25f ) ) );
}

static float3 TemporalAA::toRgb( float3 yCoCg )
{
    return float3( yCoCg.x + yCoCg.y - yCoCg.z, yCoCg.x + yCoCg.z, yCoCg.x - yCoCg.y - yCoCg.z );
}

/**
 * @brief 기록을 5 탭 Catmull-Rom 으로 읽습니다 — 가운데 두 탭을 바이리니어 한 번으로 합쳐 4x4(16 탭)를 9 탭으로, 네 모서리(가중이 가장 작다)를 버려 5 탭으로 줄인다(Jimenez, Filmic SMAA).
 * @details 좌표는 사각형 UV 다. 기록은 출력 해상도 공간이라 텍셀은 출력 풀 텍셀을 지난 사각형 배율로 나눈 크기다(g_OutputRectSize.zw / g_ViewRectScale.zw) — 사각형 UV 의 탭이 기록 텍스처의
 *          텍셀 중심에 떨어진다. 바깥 탭의 음수 가중이 만드는 링잉은 뒤의 클리핑이 자르고, 음수 색은 여기서 0 으로 자른다.
 */
static float3 TemporalAA::sampleHistoryCatmullRom( float2 uv )
{
    const float2 texel     = g_OutputRectSize.zw / max( g_ViewRectScale.zw, float2( kClipEpsilon, kClipEpsilon ) );
    const float2 samplePos = uv / texel;
    const float2 texPos1   = floor( samplePos - 0.5f ) + 0.5f;
    const float2 fraction  = samplePos - texPos1;
    const float2 w0        = float2( TemporalMath::computeCatmullRomWeight0( fraction.x ), TemporalMath::computeCatmullRomWeight0( fraction.y ) );
    const float2 w1        = float2( TemporalMath::computeCatmullRomWeight1( fraction.x ), TemporalMath::computeCatmullRomWeight1( fraction.y ) );
    const float2 w2        = float2( TemporalMath::computeCatmullRomWeight2( fraction.x ), TemporalMath::computeCatmullRomWeight2( fraction.y ) );
    const float2 w3        = float2( TemporalMath::computeCatmullRomWeight3( fraction.x ), TemporalMath::computeCatmullRomWeight3( fraction.y ) );
    const float2 w12       = w1 + w2;
    const float2 texPos0   = ( texPos1 - 1.0f ) * texel;
    const float2 texPos3   = ( texPos1 + 2.0f ) * texel;
    const float2 texPos12  = ( texPos1 + w2 / w12 ) * texel;

    const float weightTop    = w12.x * w0.y;
    const float weightLeft   = w0.x * w12.y;
    const float weightCenter = w12.x * w12.y;
    const float weightRight  = w3.x * w12.y;
    const float weightBottom = w12.x * w3.y;
    float3      sum          = PassTexture::sampleAlbedoPrevious( float2( texPos12.x, texPos0.y ) ).rgb * weightTop;
    sum += PassTexture::sampleAlbedoPrevious( float2( texPos0.x, texPos12.y ) ).rgb * weightLeft;
    sum += PassTexture::sampleAlbedoPrevious( texPos12 ).rgb * weightCenter;
    sum += PassTexture::sampleAlbedoPrevious( float2( texPos3.x, texPos12.y ) ).rgb * weightRight;
    sum += PassTexture::sampleAlbedoPrevious( float2( texPos12.x, texPos3.y ) ).rgb * weightBottom;
    const float weightSum = weightTop + weightLeft + weightCenter + weightRight + weightBottom;
    return max( sum / weightSum, float3( 0.0f, 0.0f, 0.0f ) );
}

/** @brief 기록(YCoCg)을 상자 가운데를 향해 상자 안으로 자릅니다(Playdead clip_aabb) — 축마다 자르는 clamp 와 달리 색이 상자 모서리 쪽으로 틀어지지 않는다. */
static float3 TemporalAA::clipToBox( float3 history, float3 boxMin, float3 boxMax )
{
    const float3 center   = 0.5f * ( boxMax + boxMin );
    const float3 extents  = 0.5f * ( boxMax - boxMin ) + kClipEpsilon;
    const float3 offset   = history - center;
    const float3 unit     = abs( offset / extents );
    const float  maxRatio = max( unit.x, max( unit.y, unit.z ) );
    return ( maxRatio > 1.0f ) ? center + offset / maxRatio : history;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    // 업스케일이면 출력 픽셀 중심을 렌더 픽셀 단위로 잰다. 래스터는 투영을 g_TemporalJitter 만큼 옮겨 그렸다 — 렌더 픽셀 중심 c 는 지터 없는 장면의 c − 지터 자리를 보였다.
    const bool   bUpscale    = g_TemporalUpscale.x > 0.5f;
    const float2 renderPos   = input._uv * g_ViewRectSize.xy;
    const float2 jitterPixel = g_TemporalJitter.xy * float2( 0.5f, -0.5f ) * g_ViewRectSize.xy;
    const float2 texelCenter = floor( renderPos ) + 0.5f;

    // 이웃 통계(YCoCg) — 평균 · 제곱 평균으로 분산 상자를, 최소 · 최대로 상자의 바깥 한계를 잡는다. 같은 3x3 에서 가장 가까운 깊이의 자리도 찾는다(속도 늘이기).
    // 같은 3x3 이 업샘플 표본이다(가중 = 지터한 표본 자리와 출력 픽셀 중심 거리의 가우스).
    const float3 center        = PassTexture::sampleSourcePoint( input._uv ).rgb;
    float3       neighborMin   = TemporalAA::toYCoCg( center );
    float3       neighborMax   = neighborMin;
    float3       moment1       = float3( 0.0f, 0.0f, 0.0f );
    float3       moment2       = float3( 0.0f, 0.0f, 0.0f );
    float3       upsampledSum  = float3( 0.0f, 0.0f, 0.0f );
    float        weightSum     = 0.0f;
    float        coverage      = 0.0f;
    float        closestDepth  = PassTexture::sampleDepthPoint( input._uv ).r;
    float2       closestOffset = float2( 0.0f, 0.0f );
    SW_UNROLL for ( int offsetY = -1; offsetY <= 1; ++offsetY )
    {
        SW_UNROLL for ( int offsetX = -1; offsetX <= 1; ++offsetX )
        {
            const float2 offset       = float2( offsetX, offsetY ) * g_SourceTexel.xy;
            const float3 color        = PassTexture::sampleSourcePoint( input._uv + offset ).rgb;
            const float3 neighbor     = TemporalAA::toYCoCg( color );
            const float  depth        = PassTexture::sampleDepthPoint( input._uv + offset ).r;
            const bool   bCloser      = depth < closestDepth;
            const float2 toSample     = texelCenter + float2( offsetX, offsetY ) - jitterPixel - renderPos;
            const float  sampleWeight = TemporalMath::computeUpsampleWeight( dot( toSample, toSample ) );
            neighborMin               = min( neighborMin, neighbor );
            neighborMax               = max( neighborMax, neighbor );
            moment1 += neighbor;
            moment2 += neighbor * neighbor;
            upsampledSum += color * sampleWeight;
            weightSum += sampleWeight;
            coverage      = max( coverage, sampleWeight );
            closestDepth  = bCloser ? depth : closestDepth;
            closestOffset = bCloser ? offset : closestOffset;
        }
    }
    const float3 mean   = moment1 * ( 1.0f / 9.0f );
    const float3 sigma  = sqrt( max( moment2 * ( 1.0f / 9.0f ) - mean * mean, float3( 0.0f, 0.0f, 0.0f ) ) );
    const float3 boxMin = max( mean - kVarianceClipGamma * sigma, neighborMin );
    const float3 boxMax = min( mean + kVarianceClipGamma * sigma, neighborMax );
    // 업스케일하지 않으면 이번 색은 이 픽셀의 표본 하나다(지터는 기록이 모은다) — 업스케일 없는 TAA 와 같은 값이다.
    const float3 current      = bUpscale ? upsampledSum / max( weightSum, kClipEpsilon ) : center;
    const float3 currentYCoCg = TemporalAA::toYCoCg( current );

    // 재투영 — 이 픽셀이 지난 프레임에 있던 화면 자리. 그린 픽셀은 화면 속도, 하늘은 카메라 재투영이다(고르기는 값 선택 — early-return 없음).
    float        prevW;
    const float  centerDepth = PassTexture::sampleDepthPoint( input._uv ).r;
    const float2 cameraUv    = Transform::computeCameraPreviousUv( input._uv, centerDepth, prevW );
    const float2 velocity    = PassTexture::sampleVelocityPoint( input._uv + closestOffset );
    const bool   bSky        = closestDepth >= kSkyDepth;
    const float2 prevUv      = bSky ? cameraUv : input._uv - velocity;
    const bool   bInFront    = bSky == false || prevW > 0.0f;
    const bool   bOnScreen   = bInFront && all( prevUv >= 0.0f ) && all( prevUv <= 1.0f );

    // 히스토리는 지난 프레임의 사각형 배율로 읽는다(sampleAlbedoPrevious). 자른 뒤 RGB 로 되돌려 섞는다. 속도는 출력 픽셀로 잰다.
    const float3 historyYCoCg   = TemporalAA::clipToBox( TemporalAA::toYCoCg( TemporalAA::sampleHistoryCatmullRom( prevUv ) ), boxMin, boxMax );
    const float3 history        = TemporalAA::toRgb( historyYCoCg );
    const float  velocityPixels = length( ( input._uv - prevUv ) * g_OutputRectSize.xy );
    // YCoCg 의 Y 가 휘도다. 업스케일이면 이번 표본이 출력 픽셀에서 먼 만큼 기록을 더 믿는다.
    const float blend  = TemporalMath::computeHistoryWeight( currentYCoCg.x, historyYCoCg.x, velocityPixels, kMinHistoryWeight, kMaxHistoryWeight, kVelocityRejectPixels );
    const float scaled = bUpscale ? TemporalMath::computeUpscaledHistoryWeight( blend, coverage ) : blend;
    // 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — motion_blur.hlsl).
    const float weight = bOnScreen ? scaled : 0.0f;
    return float4( lerp( current, history, weight ), 1.0f );
}
