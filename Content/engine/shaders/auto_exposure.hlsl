// auto_exposure.hlsl — 자동 노출 측정 · 적응. HDR 원본의 휘도 히스토그램(log2 휘도 64 칸)을 모아 아래 · 위 몫을 버린 평균 로그 휘도(백분위 컷 — 언리얼 Auto Exposure Histogram 의
// Low · High Percent, Adria)를 재고, 지난 EV 에서 목표 EV 로 다가간 값을 R32G32 실수 기록 칸의 R 에, 목표 EV 를 G 에 쓴다(G 는 CPU 가 되읽어 적응이 남은 정도를 본다 — FrameAutoExposure::readBackAdaptation).
// EV 는 평균 휘도가 중간 회색(0.18)일 때 0 이다. 목표는 카메라의 범위(최소 · 최대 EV)로 자른 측정값이고, 밝아질 때와 어두워질 때 다가가는 비율이 다르다(언리얼 Speed Up · Down).
// 비율이 1 이면(처음 · 컷 프레임) 지난 값을 섞지 않는다 — 새 기록에는 값이 없어 NaN 일 수 있으므로 lerp 가 아니라 고르기다.
// 두 구현이 같은 식(아래 AutoExposure)을 쓴다:
//   - SW_PASS_AUTO_EXPOSURE_HISTOGRAM(컴퓨트, RenderPassKind::AutoExposureHistogram) — 네이티브 bindless(DX12 · Vulkan)만. 한 그룹 256 스레드가 128² 표본을 그룹 공유 히스토그램에 모으고
//     한 스레드가 컷 · 적응해 이번 칸에 RW 번호(g_AutoExposureOutput)로 쓴다. 에뮬 백엔드(DX11 · GL · WebGPU)는 컴퓨트가 샘플 텍스처를 못 읽어 쿠커가 컴파일만 하는 빈 진입점이다.
//   - 풀스크린 1x1(PSMain, RenderPassKind::AutoExposure) — 에뮬 백엔드의 길. 한 픽셀이 32² 표본으로 같은 히스토그램을 지역 배열에 모은다.
// 원본 · 지난 EV 는 네이티브는 bindless 번호(루트 상수), 에뮬은 SourceColor · AutoExposure 칸(C++ 가 shaderslot::emulated::getSlot 의 레지스터에 건다)이다.
// 톤맵(post_grade.hlsli)이 이 칸을 g_AutoExposureIndex 로 읽어 2^-EV 를 곱한다. C++: FrameAutoExposure::recordAutoExposure — 톤맵하는 패스 앞에 같은 리스트로 기록한다.
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"
#include "fullscreen.hlsli"
#include "temporal_math.hlsli"

// 백분위 · 히스토그램 범위는 Config/Engine/EngineConfig.json `_render._autoExposure*` 가 정본이고 엔진이 모든 컴파일에 define 으로 넣는다(ShaderCompiler::appendEngineDefines).
#if !defined( SW_AUTO_EXPOSURE_LOW_PERCENT ) || !defined( SW_AUTO_EXPOSURE_HIGH_PERCENT ) || !defined( SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN ) || !defined( SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX )
    #error "SW_AUTO_EXPOSURE_* is not defined - the engine injects it from EngineConfig _render._autoExposure*"
#endif

SW_ROOT_CONSTANTS_BEGIN
{
    uint  g_AutoExposureSource;      // HDR 원본의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
    uint  g_AutoExposureHistory;     // 지난 EV(1x1)의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
    float g_AutoExposureMinEv;       // 목표 EV 의 하한
    float g_AutoExposureMaxEv;       // 목표 EV 의 상한
    float g_AutoExposureRateUp;      // 장면이 밝아질 때(목표 EV > 지난 EV) 이번 프레임에 다가가는 비율(0..1)
    float g_AutoExposureRateDown;    // 장면이 어두워질 때 다가가는 비율(0..1)
    uint  g_AutoExposureSourceWidth; // 원본에서 뷰 사각형이 덮는 크기(px) — 텍스처 전체가 아니다(자동 렌더 배율)
    uint  g_AutoExposureSourceHeight;
    uint  g_AutoExposureOutput; // 이번 기록 칸의 RW 번호(히스토그램 컴퓨트). 풀스크린은 쓰지 않는다
};
SW_ROOT_CONSTANTS_END

static const uint  kHistogramBinCount = 64u;   // C++ FrameAutoExposure::kHistogramBinCount 와 같다
static const float kMiddleGrey        = 0.18f; // EV 0 의 평균 휘도

// 원본을 읽을 수 있는 변형 — 에뮬 백엔드의 컴퓨트 변형은 엔진 텍스처가 선언되지 않아(binding_texture.hlsli) 쿠커가 컴파일만 하는 빈 진입점이다.
#if defined( SW_NATIVE_BINDLESS ) || !defined( SW_STAGE_COMPUTE )
    #define SW_AUTO_EXPOSURE_READS_SOURCE 1
#endif

#if defined( SW_AUTO_EXPOSURE_READS_SOURCE )

struct AutoExposure
{
    static float3 loadSource( int2 texel );
    static float2 getSourceSize();
    static float loadHistory();
    static uint computeBin( float luminance );
    static float computeBinCenterLog( uint bin );
    static float2 computeAdaptedEv( float meanLog );
};

static float3 AutoExposure::loadSource( int2 texel )
{
    return Bindless::loadEngineTexel( SW_ROOT( g_AutoExposureSource ), SW_EMU_SLOT_SourceColor, texel ).rgb;
}

static float2 AutoExposure::getSourceSize()
{
    return float2( SW_ROOT( g_AutoExposureSourceWidth ), SW_ROOT( g_AutoExposureSourceHeight ) );
}

static float AutoExposure::loadHistory()
{
    return Bindless::loadEngineTexel( SW_ROOT( g_AutoExposureHistory ), SW_EMU_SLOT_AutoExposure, int2( 0, 0 ) ).r;
}

/** @brief 휘도가 드는 칸 — log2 휘도의 [LOG_MIN, LOG_MAX] 를 고르게 나눈다. 범위 밖은 끝 칸이다(temporal_math.hlsli — C++ 단위 테스트가 같은 식을 잰다). */
static uint AutoExposure::computeBin( float luminance )
{
    return uint( TemporalMath::computeHistogramBin( luminance, SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN, SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX, float( kHistogramBinCount ) ) );
}

static float AutoExposure::computeBinCenterLog( uint bin )
{
    return TemporalMath::computeHistogramBinCenterLog( float( bin ), SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN, SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX, float( kHistogramBinCount ) );
}

/** @brief 백분위 컷 평균 log2 휘도에서 (적응한 EV, 목표 EV)입니다. 목표는 카메라 범위로 자르고, 밝아질 때 · 어두워질 때 다른 비율로 지난 EV 에서 다가간다. */
static float2 AutoExposure::computeAdaptedEv( float meanLog )
{
    const float measuredEv = meanLog - log2( kMiddleGrey );
    const float targetEv   = clamp( measuredEv, SW_ROOT( g_AutoExposureMinEv ), SW_ROOT( g_AutoExposureMaxEv ) );
    const float previousEv = AutoExposure::loadHistory();
    const float rate       = ( targetEv > previousEv ) ? SW_ROOT( g_AutoExposureRateUp ) : SW_ROOT( g_AutoExposureRateDown );
    const float adaptedEv  = ( rate >= 1.0f ) ? targetEv : lerp( previousEv, targetEv, rate );
    return float2( adaptedEv, targetEv );
}

    // 백분위 컷 평균(칸마다의 식은 temporal_math.hlsli, 고리는 C++ FrameAutoExposure::computeHistogramPercentileMeanLog 와 같은 모양) — 표본을 어두운 순으로 놓고 아래 LOW % 와 위 (100 − HIGH) % 를 버린 나머지의 칸 가운데 평균.
    // 칸이 컷 경계에 걸리면 걸친 몫만 센다. 남는 몫이 없으면 아래 경계가 든 칸의 가운데다. 히스토그램 그릇(그룹 공유 · 지역 배열)이 달라 매크로로 둔다 — HLSL 은 배열을 참조로 넘기지 못한다.
    #define SW_AUTO_EXPOSURE_PERCENTILE_MEAN_LOG( arrBin, outMeanLog )                                                                                \
        {                                                                                                                                             \
            float total = 0.0f;                                                                                                                       \
            for ( uint sumBin = 0u; sumBin < kHistogramBinCount; ++sumBin )                                                                           \
                total += float( arrBin[sumBin] );                                                                                                     \
            const float lowCount     = TemporalMath::computeHistogramLowCount( total, SW_AUTO_EXPOSURE_LOW_PERCENT );                                 \
            const float highCount    = TemporalMath::computeHistogramHighCount( total, SW_AUTO_EXPOSURE_LOW_PERCENT, SW_AUTO_EXPOSURE_HIGH_PERCENT ); \
            float       sumLog       = 0.0f;                                                                                                          \
            float       sumWeight    = 0.0f;                                                                                                          \
            float       cumulative   = 0.0f;                                                                                                          \
            float       lowBinCenter = 0.5f * ( SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN + SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX );                            \
            bool        bLowBinFound = false;                                                                                                         \
            for ( uint bin = 0u; bin < kHistogramBinCount; ++bin )                                                                                    \
            {                                                                                                                                         \
                const float count  = float( arrBin[bin] );                                                                                            \
                const float start  = cumulative;                                                                                                      \
                const float end    = cumulative + count;                                                                                              \
                const float center = AutoExposure::computeBinCenterLog( bin );                                                                        \
                const float kept   = TemporalMath::computeHistogramKeptCount( start, end, lowCount, highCount );                                      \
                sumLog += kept * center;                                                                                                              \
                sumWeight += kept;                                                                                                                    \
                const bool bLowBin = bLowBinFound == false && count > 0.0f && end >= lowCount;                                                        \
                lowBinCenter       = bLowBin ? center : lowBinCenter;                                                                                 \
                bLowBinFound       = bLowBinFound || bLowBin;                                                                                         \
                cumulative         = end;                                                                                                             \
            }                                                                                                                                         \
            outMeanLog = sumWeight > 0.0f ? sumLog / sumWeight : lowBinCenter;                                                                        \
        }
#endif // SW_AUTO_EXPOSURE_READS_SOURCE

#if defined( SW_PASS_AUTO_EXPOSURE_HISTOGRAM )

static const uint kHistogramGroupSize  = 256u; // 스레드 수(16 x 16)
static const uint kHistogramSampleGrid = 128u; // 화면을 128 x 128 칸으로 나눠 칸 가운데 텍셀 하나씩 읽는다(16384 표본 — 스레드마다 64)

    #if defined( SW_NATIVE_BINDLESS )
groupshared uint s_arrHistogram[kHistogramBinCount];
    #endif

SW_NUMTHREADS( 16, 16, 1 )
void CSMain( uint groupIndex : SV_GroupIndex )
{
    #if defined( SW_NATIVE_BINDLESS )
    if ( groupIndex < kHistogramBinCount )
        s_arrHistogram[groupIndex] = 0u;
    GroupMemoryBarrierWithGroupSync();

    const float2 size = AutoExposure::getSourceSize();
    for ( uint sampleIndex = groupIndex; sampleIndex < kHistogramSampleGrid * kHistogramSampleGrid; sampleIndex += kHistogramGroupSize )
    {
        const uint2 grid      = uint2( sampleIndex % kHistogramSampleGrid, sampleIndex / kHistogramSampleGrid );
        const int2  texel     = int2( ( float2( grid ) + 0.5f ) * size / float( kHistogramSampleGrid ) );
        const float luminance = CommonMath::computeLuminance( AutoExposure::loadSource( texel ) );
        InterlockedAdd( s_arrHistogram[AutoExposure::computeBin( luminance )], 1u );
    }
    GroupMemoryBarrierWithGroupSync();

    if ( groupIndex != 0u )
        return;
    float meanLog = 0.0f;
    SW_AUTO_EXPOSURE_PERCENTILE_MEAN_LOG( s_arrHistogram, meanLog );
    const float2 ev = AutoExposure::computeAdaptedEv( meanLog );
    Bindless::storeRwTexture2D( SW_ROOT( g_AutoExposureOutput ), uint2( 0, 0 ), float4( ev, 0.0f, 1.0f ) );
    #endif
}

#else

static const uint kAutoExposureGrid = 32u; // 화면을 32 x 32 칸으로 나눠 칸 가운데 텍셀 하나씩 읽는다(1024 번 — 한 픽셀이 다 읽는다)

float4 PSMain( PSInput input ) : SV_TARGET
{
    uint arrBin[kHistogramBinCount];
    for ( uint clearBin = 0u; clearBin < kHistogramBinCount; ++clearBin )
        arrBin[clearBin] = 0u;
    const float2 size = AutoExposure::getSourceSize();
    for ( uint gridY = 0u; gridY < kAutoExposureGrid; ++gridY )
    {
        for ( uint gridX = 0u; gridX < kAutoExposureGrid; ++gridX )
        {
            const int2  texel     = int2( ( float2( gridX, gridY ) + 0.5f ) * size / kAutoExposureGrid );
            const float luminance = CommonMath::computeLuminance( AutoExposure::loadSource( texel ) );
            arrBin[AutoExposure::computeBin( luminance )] += 1u;
        }
    }
    float meanLog = 0.0f;
    SW_AUTO_EXPOSURE_PERCENTILE_MEAN_LOG( arrBin, meanLog );
    return float4( AutoExposure::computeAdaptedEv( meanLog ), 0.0f, 1.0f );
}

#endif
