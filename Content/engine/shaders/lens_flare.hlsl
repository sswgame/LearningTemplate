/**
 * lens_flare.hlsl — 빛마다의 렌즈 플레어 스프라이트(RenderPassType::LensFlare). 스프라이트 하나 = 모서리 여섯을 SV_VertexID 로 만든다(정점 버퍼 없음, canvas.hlsl 과 같은 방식).
 *
 * 스프라이트 원소는 C++ `LensFlareSprite`(LensFlareSpriteBuffer.h)와 같은 배치다 — 렌더 스레드가 빛을 주 시점에 투영하고 가장자리 감쇠를 곱해 올린다.
 * 모양은 절차적(`LensFlareShape` 의 번호): 빛무리 · 원판 · 고리 · 줄. 가림은 픽셀 셰이더가 빛 자리 둘레 장면 깊이 표본 아홉으로 본다 —
 * 표본 깊이가 빛 깊이 - 여유 이상이면 가리지 않은 것이다(C++ `LensFlareSpriteBuffer::computeVisibility` 와 같은 식). 방향광의 깊이는 1 이라 하늘만 보인다.
 * 구조 버퍼는 정점 단계에서만 읽는다 — GL 은 두 단계가 같은 구조 버퍼를 읽으면 링크를 거절한다(binding.hlsli 9 절). 픽셀이 쓸 값은 보간하지 않는 칸으로 넘긴다.
 * 출력은 가산 블렌드(SrcAlpha, One — XML `_blendPreset="Additive"`)라 알파 1 이면 rgb 가 그대로 더해진다.
 */
#include "binding.hlsli"

struct LensFlareSprite
{
    float4 _centerHalfSize; // xy NDC 가운데, zw NDC 반 크기
    float4 _colorShape;     // rgb 색, a 모양
    float4 _lightUvKernel;  // xy 빛의 화면 UV, zw 가림 표본 반지름(UV)
    float4 _depthFade;      // x 빛의 장치 깊이, y 가장자리 감쇠
};

SW_DECLARE_STRUCTURED_BUFFER( LensFlareSprite, g_LensFlareSprites, SW_SLOT_PASS_SRV );

static const uint  kLensFlareShapeGlow   = 0u;      // LensFlareShape::Glow
static const uint  kLensFlareShapeDisc   = 1u;      // LensFlareShape::Disc
static const uint  kLensFlareShapeRing   = 2u;      // LensFlareShape::Ring
static const float kLensFlareDepthBias   = 1.0e-4f; // LensFlareSpriteBuffer::kDepthBias 와 같다
static const uint  kLensFlareSampleCount = 9u;

struct PSInput
{
    float4                 _position : SV_POSITION;
    float2                 _local : TEXCOORD0; // 스프라이트 안 좌표(-1 ~ 1)
    nointerpolation float4 _colorShape : TEXCOORD1;
    nointerpolation float4 _lightUvKernel : TEXCOORD2;
    nointerpolation float2 _depthFade : TEXCOORD3;
};

static const float2 kCornerTable[6] = { float2( -1.0f, -1.0f ), float2( 1.0f, -1.0f ), float2( -1.0f, 1.0f ),
                                        float2( 1.0f, -1.0f ), float2( 1.0f, 1.0f ), float2( -1.0f, 1.0f ) };

// 가림 표본의 자리(반지름 1 기준) — 가운데 하나와 둘레 여덟.
static const float2 kKernelTable[9] = { float2( 0.0f, 0.0f ), float2( 1.0f, 0.0f ), float2( -1.0f, 0.0f ), float2( 0.0f, 1.0f ), float2( 0.0f, -1.0f ),
                                        float2( 0.7071f, 0.7071f ), float2( -0.7071f, 0.7071f ), float2( 0.7071f, -0.7071f ), float2( -0.7071f, -0.7071f ) };

PSInput VSMain( uint vertexID : SV_VertexID )
{
    const LensFlareSprite sprite = g_LensFlareSprites[vertexID / 6u];
    const float2          corner = kCornerTable[vertexID % 6u];
    PSInput               output;
    output._position      = float4( sprite._centerHalfSize.xy + corner * sprite._centerHalfSize.zw, 0.0f, 1.0f );
    output._local         = corner;
    output._colorShape    = sprite._colorShape;
    output._lightUvKernel = sprite._lightUvKernel;
    output._depthFade     = sprite._depthFade.xy;
    return output;
}

/** @brief 모양의 세기(0 ~ 1)입니다. @p local 은 스프라이트 안 좌표(-1 ~ 1)입니다. */
float computeShapeMask( float2 local, uint shape )
{
    const float radius = length( local );
    const float glow   = exp( -radius * radius * 6.0f ) * saturate( 1.0f - radius );
    const float disc   = saturate( ( 1.0f - radius ) * 8.0f );
    const float ring   = saturate( 1.0f - abs( radius - 0.85f ) * 10.0f );
    const float streak = exp( -abs( local.y ) * 16.0f ) * saturate( 1.0f - abs( local.x ) );
    return shape == kLensFlareShapeGlow ? glow : ( shape == kLensFlareShapeDisc ? disc : ( shape == kLensFlareShapeRing ? ring : streak ) );
}

/** @brief 빛 자리 둘레 깊이 표본 중 가리지 않은 몫입니다(C++ LensFlareSpriteBuffer::computeVisibility 와 같은 식). */
float computeVisibility( float2 lightUv, float2 kernelRadius, float lightDepth )
{
    float visible = 0.0f;
    SW_UNROLL for ( uint sampleIndex = 0u; sampleIndex < kLensFlareSampleCount; ++sampleIndex )
    {
        const float sceneDepth = PassTexture::sampleDepthPoint( lightUv + kKernelTable[sampleIndex] * kernelRadius ).r;
        visible += sceneDepth >= lightDepth - kLensFlareDepthBias ? 1.0f : 0.0f;
    }
    return visible / (float)kLensFlareSampleCount;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float mask       = computeShapeMask( input._local, (uint)( input._colorShape.a + 0.5f ) );
    const float visibility = computeVisibility( input._lightUvKernel.xy, input._lightUvKernel.zw, input._depthFade.x );
    return float4( input._colorShape.rgb * ( mask * visibility * input._depthFade.y ), 1.0f );
}
