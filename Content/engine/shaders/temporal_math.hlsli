/**
 * temporal_math.hlsli — 시간 누적(TAA · TAAU)과 자동 노출 히스토그램의 스칼라 식 한 벌. 셰이더와 C++ 단위 테스트가 **같은 파일**을 읽는다(shading_math.hlsli 와 같은 길).
 *
 * 셰이더는 taa.hlsl · auto_exposure.hlsl 이 include 하고, C++ 는 `Engine/Renderer/Frame/PostProcess/TemporalMath.h` 가 include 해 값을 잰다(두 벌로 두면 테스트가 셰이더가 아닌 사본을 잰다).
 * 그래서 두 언어가 함께 받는 모양만 쓴다:
 *   - 인자와 반환은 스칼라 `float` 뿐(벡터 · `out` · 배열이 없다 — 벡터 식은 부르는 쪽이 채널마다, 히스토그램 고리는 부르는 쪽이 칸마다 부른다).
 *   - 내장 함수는 C++ 쪽 `sw::shadermath`(ShadingMath.h)가 같은 이름을 둔 것만(`exp2` · `log2` · `abs` · `floor` · `saturate` · `lerp` · `max` · `min` · `clamp`).
 *   - 바깥 정의의 `static` 은 C++ 에서 `inline` 이어야 하므로 `SW_TEMPORAL_MATH_DEFINITION` 이 고른다.
 * 비교(기억에 의존): Catmull-Rom 5 탭은 Jimenez(Filmic SMAA), 기록 비율은 Playdead INSIDE TAA 의 feedback min · max 와 Adria(MIT), 업샘플 가중은 언리얼 TAAU 의
 * 블랙먼-해리스 근사(가우스 exp(-2.29 d²)), 히스토그램 백분위는 언리얼 Auto Exposure Histogram 의 Low · High Percent 다.
 */

#ifndef SW_ENGINE_TEMPORALMATH_HLSLI
#define SW_ENGINE_TEMPORALMATH_HLSLI

#if defined( __cplusplus )
    #define SW_TEMPORAL_MATH_DEFINITION inline // 헤더의 바깥 정의 — 번역 단위마다 한 벌
namespace sw::shadermath
{
#else
    #define SW_TEMPORAL_MATH_DEFINITION static // FXC 는 선언과 정의의 static 이 다르면 거절한다
#endif

    // 기록 신뢰도의 분모 하한 — 어두운 픽셀에서 작은 휘도 차가 신뢰도를 0 으로 끌어내리지 않게 한다(Playdead TAA 의 0.2).
    static const float kTemporalTrustLuminanceFloor = 0.2f;
    // 나누기의 하한 — 0 으로 나누지 않는다.
    static const float kTemporalMathEpsilon = 1e-6f;
    // Catmull-Rom 은 장력 0.5 의 카디널 스플라인이다 — 네 탭 가중의 계수가 모두 이 값에서 나온다.
    static const float kCatmullRomTension = 0.5f;
    // 업샘플 가중의 밑 2 지수 계수 — exp(-2.29 d²) = exp2(-3.3 d²)(언리얼 TAAU 의 블랙먼-해리스 근사, d 는 렌더 픽셀 거리).
    static const float kUpsampleWeightExponent = 3.3f;
    // 히스토그램 컷의 백분율 → 몫.
    static const float kPercentToFraction = 0.01f;
    // 히스토그램이 log2 를 잴 때 자르는 휘도 — 검은 텍셀의 log2 가 -무한이 되지 않는다(첫 칸에 든다).
    static const float kHistogramMinLuminance = 1e-6f;

    struct TemporalMath
    {
        // --- 기록 표본(Catmull-Rom) — 텍셀 0 의 중심에서 잰 자리 f(0 이상 1 미만)의 네 탭(텍셀 -1, 0, +1, +2) 가중. 합은 늘 1, f 0 이면 (0, 1, 0, 0) ---
        static float computeCatmullRomWeight0( float fraction );
        static float computeCatmullRomWeight1( float fraction );
        static float computeCatmullRomWeight2( float fraction );
        static float computeCatmullRomWeight3( float fraction );
        // --- 블렌드 ---
        /// 이번 기록 비율 — lerp( min, max, 신뢰도² × (1 − 속도 몫) ). 신뢰도 = 1 − |현재 − 자른 기록| / max(둘, 하한), 속도 몫 = 화면 속도(px) / 기준 속도(0..1).
        /// min 이 max 보다 크면 max 를 하한으로도 쓴다(범위가 점).
        static float computeHistoryWeight( float currentLuminance, float historyLuminance, float velocityPixels, float minWeight, float maxWeight, float velocityRejectPixels );
        // --- 시간 업스케일(TAAU) ---
        /// 렌더 표본 하나의 업샘플 가중 — 출력 픽셀 중심과 지터한 렌더 표본 자리 사이 거리²(렌더 픽셀 단위)의 가우스. 거리 0 이면 1 이다.
        static float computeUpsampleWeight( float distanceSquared );
        /// 업스케일 프레임의 기록 비율 — 이번 표본이 출력 픽셀에서 멀수록(가장 가까운 표본의 가중 @p currentCoverage 가 작을수록) 기록을 더 믿는다. 덮임 1 이면 그대로다.
        static float computeUpscaledHistoryWeight( float historyWeight, float currentCoverage );
        // --- 자동 노출 히스토그램 ---
        /// 휘도가 드는 칸 번호(0 .. binCount − 1, 실수로 낸다) — log2 휘도의 [logMin, logMax] 를 고르게 나누고 범위 밖은 끝 칸이다.
        static float computeHistogramBin( float luminance, float logMin, float logMax, float binCount );
        /// 칸 @p bin 의 가운데 log2 휘도.
        static float computeHistogramBinCenterLog( float bin, float logMin, float logMax, float binCount );
        /// 누적 표본 [start, end) 를 차지한 칸이 컷 [lowCount, highCount) 안에 남기는 표본 수(걸친 몫만).
        static float computeHistogramKeptCount( float start, float end, float lowCount, float highCount );
        /// 백분위(%)를 표본 수로 — 아래 컷 · 위 컷(위 컷은 아래 컷보다 작지 않다).
        static float computeHistogramLowCount( float total, float lowPercent );
        static float computeHistogramHighCount( float total, float lowPercent, float highPercent );
    };

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeCatmullRomWeight0( float fraction )
    {
        const float t = kCatmullRomTension;
        return fraction * ( -t + fraction * ( 2.0f * t - t * fraction ) );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeCatmullRomWeight1( float fraction )
    {
        const float t = kCatmullRomTension;
        return 1.0f + fraction * fraction * ( ( t - 3.0f ) + ( 2.0f - t ) * fraction );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeCatmullRomWeight2( float fraction )
    {
        const float t = kCatmullRomTension;
        return fraction * ( t + fraction * ( ( 3.0f - 2.0f * t ) + ( t - 2.0f ) * fraction ) );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeCatmullRomWeight3( float fraction )
    {
        const float t = kCatmullRomTension;
        return fraction * fraction * ( -t + t * fraction );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeHistoryWeight( float currentLuminance, float historyLuminance, float velocityPixels, float minWeight, float maxWeight,
                                                                          float velocityRejectPixels )
    {
        const float lowWeight  = min( minWeight, maxWeight );
        const float difference = abs( currentLuminance - historyLuminance );
        const float trust      = 1.0f - saturate( difference / max( max( currentLuminance, historyLuminance ), kTemporalTrustLuminanceFloor ) );
        const float motion     = saturate( velocityPixels / max( velocityRejectPixels, kTemporalMathEpsilon ) );
        return lerp( lowWeight, maxWeight, trust * trust * ( 1.0f - motion ) );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeUpsampleWeight( float distanceSquared )
    {
        return exp2( -kUpsampleWeightExponent * max( distanceSquared, 0.0f ) );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeUpscaledHistoryWeight( float historyWeight, float currentCoverage )
    {
        return 1.0f - ( 1.0f - historyWeight ) * saturate( currentCoverage );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeHistogramBin( float luminance, float logMin, float logMax, float binCount )
    {
        const float range    = max( logMax - logMin, kTemporalMathEpsilon );
        const float position = ( log2( max( luminance, kHistogramMinLuminance ) ) - logMin ) / range * binCount;
        return clamp( floor( position ), 0.0f, max( binCount - 1.0f, 0.0f ) );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeHistogramBinCenterLog( float bin, float logMin, float logMax, float binCount )
    {
        const float binWidth = ( logMax - logMin ) / max( binCount, 1.0f );
        return logMin + ( bin + 0.5f ) * binWidth;
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeHistogramKeptCount( float start, float end, float lowCount, float highCount )
    {
        return max( 0.0f, min( end, highCount ) - max( start, lowCount ) );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeHistogramLowCount( float total, float lowPercent )
    {
        return total * saturate( lowPercent * kPercentToFraction );
    }

    SW_TEMPORAL_MATH_DEFINITION float TemporalMath::computeHistogramHighCount( float total, float lowPercent, float highPercent )
    {
        return total * max( saturate( lowPercent * kPercentToFraction ), saturate( highPercent * kPercentToFraction ) );
    }

#if defined( __cplusplus )
} // namespace sw::shadermath
#endif

#endif // SW_ENGINE_TEMPORALMATH_HLSLI
