// autoexposure.hlsl — 자동 노출 측정 · 적응(풀스크린 1x1). HDR 원본의 평균 로그 휘도를 재어 지난 EV 에서 목표 EV 로 다가간 값을 R32 실수 RT 에 쓴다.
// EV 는 평균 휘도가 중간 회색(0.18)일 때 0 이다. 목표는 카메라의 범위(최소 · 최대 EV)로 자른 측정값이고, 밝아질 때와 어두워질 때 다가가는 비율이 다르다(언리얼 Speed Up · Down).
// 비율이 1 이면(처음 · 컷 프레임) 지난 값을 섞지 않는다 — 새 기록에는 값이 없어 NaN 일 수 있으므로 lerp 가 아니라 고르기다.
// 원본 · 지난 EV 는 네이티브(DX12 · Vulkan)는 bindless 번호(루트 상수), 에뮬은 엔진 텍스처 슬롯 0 · 1(t0 · t1 — C++ 가 bindShaderResource 로 건다)이다.
// 톤맵(postgrade.hlsli)이 이 RT 를 g_AutoExposureIndex 로 읽어 2^-EV 를 곱한다. C++: FrameRenderer::recordAutoExposure — 톤맵하는 패스 앞에 같은 리스트로 그린다.
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint  g_AutoExposureSource;   // HDR 원본의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
    uint  g_AutoExposureHistory;  // 지난 EV(1x1)의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
    float g_AutoExposureMinEv;    // 목표 EV 의 하한
    float g_AutoExposureMaxEv;    // 목표 EV 의 상한
    float g_AutoExposureRateUp;   // 장면이 밝아질 때(목표 EV > 지난 EV) 이번 프레임에 다가가는 비율(0..1)
    float g_AutoExposureRateDown; // 장면이 어두워질 때 다가가는 비율(0..1)
};
SW_ROOT_CONSTANTS_END

static const uint  kAutoExposureGrid = 32u;     // 화면을 32 x 32 칸으로 나눠 칸 가운데 텍셀 하나씩 읽는다(1024 번 — 한 픽셀이 다 읽는다)
static const float kMiddleGrey       = 0.18f;   // EV 0 의 평균 휘도
static const float kMinLuminance     = 1.0e-4f; // 검은 텍셀의 log2 가 -무한이 되지 않게 자르는 휘도

struct PSInput
{
    float4 _position : SV_POSITION;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input._position           = input._position;
    const float2 clipPosition = float2( ( vertexId == 1u ) ? 3.0f : -1.0f, ( vertexId == 2u ) ? 3.0f : -1.0f );
    output._position          = float4( clipPosition, 0.0f, 1.0f );
    return output;
}

float3 loadSource( int2 texel )
{
    return Bindless::loadEngineTexel( SW_ROOT( g_AutoExposureSource ), 0u, texel ).rgb;
}

float2 getSourceSize()
{
    return float2( Bindless::getEngineTextureSize( SW_ROOT( g_AutoExposureSource ), 0u ) );
}

float loadHistory()
{
    return Bindless::loadEngineTexel( SW_ROOT( g_AutoExposureHistory ), 1u, int2( 0, 0 ) ).r;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float2 size   = getSourceSize();
    float        sumLog = 0.0f;
    for ( uint gridY = 0u; gridY < kAutoExposureGrid; ++gridY )
    {
        for ( uint gridX = 0u; gridX < kAutoExposureGrid; ++gridX )
        {
            const int2   texel     = int2( ( float2( gridX, gridY ) + 0.5f ) * size / kAutoExposureGrid );
            const float3 color     = loadSource( texel );
            const float  luminance = dot( color, float3( 0.2126f, 0.7152f, 0.0722f ) );
            sumLog += log2( max( luminance, kMinLuminance ) );
        }
    }
    const float measuredEv = sumLog / float( kAutoExposureGrid * kAutoExposureGrid ) - log2( kMiddleGrey );
    const float targetEv   = clamp( measuredEv, SW_ROOT( g_AutoExposureMinEv ), SW_ROOT( g_AutoExposureMaxEv ) );
    const float previousEv = loadHistory();
    const float rate       = ( targetEv > previousEv ) ? SW_ROOT( g_AutoExposureRateUp ) : SW_ROOT( g_AutoExposureRateDown );
    const float adaptedEv  = ( rate >= 1.0f ) ? targetEv : lerp( previousEv, targetEv, rate );
    return float4( adaptedEv, 0.0f, 0.0f, 1.0f );
}
