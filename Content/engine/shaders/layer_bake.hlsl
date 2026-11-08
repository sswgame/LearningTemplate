// layer_bake.hlsl — UV 공간 층 굽기(C++ TextureLayerBakeRequest · FramePassExecutor::drawTextureLayerBakes). 층 하나 = 전체 화면 한 번.
// 아래 결과(밑 알베도 · 밑 표면 덮어쓰기)를 읽어 층 하나를 섞고 두 타깃(알베도 · 표면)에 쓴다 — 오버레이 섞기는 아래 색이 있어야 해서 고정 블렌드로 못 한다.
// 층 값은 루트 상수 한 묶음이다(C++ TextureLayerBakeConstants 와 같은 배치 — TextureLayerBakeTest 가 맞춘다). 식은 skin_layer_composite.hlsli(C++ SkinSurfaceSample 과 같다).
// 번짐 텍스처는 RG = 이동(0.5 가 0, 범위 kLayerBakeSmearRange), B = 묽음이다(C++ SkinLayerSmearState::makeTexturePixels).

// 이 셰이더는 루트 상수 블록을 직접 선언한다(binding.hlsli 의 그래픽스 블록 g_SwMaterialCount 대신).
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"
#include "fullscreen.hlsli"
#include "skin_layer_composite.hlsli"

static const float kLayerBakeSmearRange = 0.25f; // C++ TextureLayerBakeConstants::kSmearOffsetRange
static const float kLayerBakeMinScale   = 0.01f;
static const float kLayerBakeMaxScale   = 4.0f;
static const float kLayerBakeMaxAngle   = 180.0f;
static const uint  kLayerBakeFlagPlaced = 1u << 18;
static const uint  kLayerBakeFlagMirror = 1u << 19;

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_SwBakeSourceAlbedo;  // 아래 알베도 — DX12/Vulkan 은 bindless 전역 번호, DX11/GL 은 서수(t16..)
    uint g_SwBakeSourceSurface; // 아래 표면 덮어쓰기(첫 층은 kInvalidIndex — 덮어쓰기 없음)
    uint g_SwBakeMask;          // 층 마스크(R) — kInvalidIndex 면 덮지 않는다(밑을 옮기기만)
    uint g_SwBakeSmear;         // 번짐(RG 이동 · B 묽음) — kInvalidIndex 면 번짐 없음
    uint g_SwBakeColor;         // RGBA8(r 가 낮은 바이트), a = 색 알파 × 불투명도
    uint g_SwBakeOffset;        // u · v 16 비트 [0,1]
    uint g_SwBakeScale;         // u · v 16 비트 [최소, 최대]
    uint g_SwBakeRotationFlags; // 회전 16 비트 [−180, 180], 16..17 섞기, 18 배치, 19 대칭
    uint g_SwBakeRegionMin;     // 영역 경계 16 비트 둘
    uint g_SwBakeRegionMax;
    uint g_SwBakeSurface; // 거칠기 · 거칠기 무게 · 금속 · 금속 무게 8 비트
};
SW_ROOT_CONSTANTS_END

struct LayerBakeOutput
{
    float4 _albedo : SV_TARGET0;
    float4 _surface : SV_TARGET1;
};

float decodeUnorm16( uint packed, uint shift, float minValue, float maxValue )
{
    return minValue + float( ( packed >> shift ) & 0xFFFFu ) / 65535.0f * ( maxValue - minValue );
}

float decodeUnorm8( uint packed, uint shift )
{
    return float( ( packed >> shift ) & 0xFFu ) / 255.0f;
}

float2 decodePair( uint packed, float minValue, float maxValue )
{
    return float2( decodeUnorm16( packed, 0u, minValue, maxValue ), decodeUnorm16( packed, 16u, minValue, maxValue ) );
}

// 층이 피부 UV skinUV 를 덮는 정도(마스크 R) — 배치 층은 층 UV 로, 대칭이면 짝도 보고 큰 쪽.
float sampleLayerMask( float2 skinUV, uint flags )
{
    const float2 regionMin = decodePair( SW_ROOT( g_SwBakeRegionMin ), 0.0f, 1.0f );
    const float2 regionMax = decodePair( SW_ROOT( g_SwBakeRegionMax ), 0.0f, 1.0f );
    if ( ( flags & kLayerBakeFlagPlaced ) == 0u )
    {
        const float inside = SkinLayerComposite::computeInside( float2( 0.5f, 0.5f ), skinUV, regionMin, regionMax );
        return inside * MaterialTexture::sampleIndex( SW_ROOT( g_SwBakeMask ), skinUV ).r;
    }
    const float2 offset   = decodePair( SW_ROOT( g_SwBakeOffset ), 0.0f, 1.0f );
    const float2 scale    = decodePair( SW_ROOT( g_SwBakeScale ), kLayerBakeMinScale, kLayerBakeMaxScale );
    const float  rotation = decodeUnorm16( flags, 0u, -kLayerBakeMaxAngle, kLayerBakeMaxAngle );
    const float2 layerUV  = SkinLayerComposite::toLayerUV( skinUV, offset, scale, rotation, false );
    float        coverage = SkinLayerComposite::computeInside( layerUV, skinUV, regionMin, regionMax ) * MaterialTexture::sampleIndex( SW_ROOT( g_SwBakeMask ), saturate( layerUV ) ).r;
    SW_BRANCH if ( ( flags & kLayerBakeFlagMirror ) != 0u )
    {
        const float2 mirrorUV = SkinLayerComposite::toLayerUV( skinUV, offset, scale, rotation, true );
        coverage              = max( coverage, SkinLayerComposite::computeInside( mirrorUV, skinUV, regionMin, regionMax ) * MaterialTexture::sampleIndex( SW_ROOT( g_SwBakeMask ), saturate( mirrorUV ) ).r );
    }
    return coverage;
}

LayerBakeOutput PSMain( PSInput input )
{
    const float2 uv      = input._uv;
    const float3 below   = MaterialTexture::sampleIndex( SW_ROOT( g_SwBakeSourceAlbedo ), uv ).rgb;
    float4       surface = float4( 0.0f, 0.0f, 0.0f, 0.0f ); // 덮어쓰기 없음(값 · 무게 0)
    if ( SW_ROOT( g_SwBakeSourceSurface ) != kInvalidIndex )
        surface = MaterialTexture::sampleIndex( SW_ROOT( g_SwBakeSourceSurface ), uv );

    LayerBakeOutput output;
    output._albedo  = float4( below, 1.0f );
    output._surface = surface;
    if ( SW_ROOT( g_SwBakeMask ) == kInvalidIndex )
        return output;

    // 번짐 — 층을 (UV − 이동)에서 읽고 덮임에 (1 − 묽음)을 곱한다(가짜 데칼이 아니라 원래 층이 옮겨지고 흐려진다).
    float2 layerSourceUV = uv;
    float  dilution      = 0.0f;
    if ( SW_ROOT( g_SwBakeSmear ) != kInvalidIndex )
    {
        const float4 smear = MaterialTexture::sampleIndex( SW_ROOT( g_SwBakeSmear ), uv );
        layerSourceUV      = uv - ( smear.rg * 2.0f - 1.0f ) * kLayerBakeSmearRange;
        dilution           = saturate( smear.b );
    }

    const uint   flags    = SW_ROOT( g_SwBakeRotationFlags );
    const uint   color    = SW_ROOT( g_SwBakeColor );
    const float3 tint     = float3( decodeUnorm8( color, 0u ), decodeUnorm8( color, 8u ), decodeUnorm8( color, 16u ) );
    const float  coverage = saturate( sampleLayerMask( layerSourceUV, flags ) * decodeUnorm8( color, 24u ) * ( 1.0f - dilution ) );
    const uint   blend    = ( flags >> 16u ) & 0x3u;
    output._albedo        = float4( SkinLayerComposite::blend( below, tint, coverage, blend ), 1.0f );

    // 표면 덮어쓰기는 프리멀티플라이로 쌓는다 — 값 V' = lerp( V, 층 값, a ), 무게 W' = lerp( W, 1, a ) (a = 덮임 × 층 무게). 머티리얼은 원래 × (1 − W) + V 로 읽는다 —
    // 층마다 lerp( 아래, 층 값, a ) 를 차례로 건 것(C++ SkinSurfaceSample::composite)과 같은 값이다.
    const uint  packedSurface   = SW_ROOT( g_SwBakeSurface );
    const float roughnessAmount = coverage * decodeUnorm8( packedSurface, 8u );
    const float metallicAmount  = coverage * decodeUnorm8( packedSurface, 24u );
    output._surface.r           = lerp( surface.r, decodeUnorm8( packedSurface, 0u ), roughnessAmount );
    output._surface.g           = lerp( surface.g, 1.0f, roughnessAmount );
    output._surface.b           = lerp( surface.b, decodeUnorm8( packedSurface, 16u ), metallicAmount );
    output._surface.a           = lerp( surface.a, 1.0f, metallicAmount );
    return output;
}
