/**
 * @file surface_lit.hlsl
 * @brief 몸 표면 상태 머티리얼 셰이더 — forward_lit 의 PBR 에 젖음 · 흙 · 피 · 상처 · 찢김(알파 컷)을 얹는다.
 * @details 표면 상태(`CharacterSurfaceState`)가 머티리얼 인스턴스에 올리는 이름(`CharacterSurfaceBinding.h` 의 `SurfaceParameterNames`)을 읽는다.
 *          - 채널 스칼라 `_surface<채널>`: 모든 영역의 최댓값. 마스크가 없는 채널(젖음)은 이 값이 몸 전체에 곱해진다.
 *          - 마스크 `_surface<채널>Mask`: 부품 UV 공간의 도장(R 채널). 마스크가 있는 채널(흙 · 피 · 상처 · 찢김)은 마스크가 자리를 정하고 스칼라가 0 이면 마스크를 읽지 않는다.
 *            마스크를 올리지 않은 인스턴스(표면 상태가 없는 캐릭터)나 bindless 에뮬 백엔드(DX11/GL)는 텍스처가 흰색으로 읽히므로, 스칼라가 0 이면 효과가 없게 스칼라로 문을 건다.
 *          영역 벡터(`_surfaceRegion<영역>Chunk<덩어리>`)는 읽지 않는다 — 부위별로 다르게 그리는 자리는 마스크의 도장(맞은 UV 자리)이 정한다. 영역 번호를 정점으로 싣는 칸은 두지 않았다.
 *          채널 이름은 `engine/character/default.surfacechannels.xml` 의 `Wet · Dirt · Blood · Wound · Tear` 다. 다른 채널을 쓰려면 이 구조체와 `surfacelit.material` 에 같은 이름을 더한다.
 *          찢김(Tear)은 알파 컷(`MATERIAL_ALPHA_CUTOFF` — 머티리얼이 늘 켠다)으로 구멍을 낸다. 깊이 · 그림자 패스도 같은 표면 훅을 읽으므로 구멍으로 그림자가 샌다.
 *          알베도 · 거칠기 · 금속도 · 조명은 forward_lit.hlsl 과 같은 식이다(디퍼드 조명 패스와 같은 함수를 쓴다).
 */
#include "lighting.hlsli"

// surfacelit.material 의 프로퍼티 순서 · 타입과 1:1 이다.
SW_MATERIAL_BEGIN
{
    float4 _color;
    float  _roughness;
    float  _metallic;
    uint   _albedoMap;
    float  _surfaceWet;
    float  _surfaceDirt;
    float  _surfaceBlood;
    float  _surfaceWound;
    float  _surfaceTear;
    uint   _surfaceDirtMask;
    uint   _surfaceBloodMask;
    uint   _surfaceWoundMask;
    uint   _surfaceTearMask;
}
SW_MATERIAL_END

#define SW_MATERIAL_CUSTOM_LIGHTING
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "material_shader.hlsli"

static const float  kWetAlbedoScale     = 0.55f; // 젖음이 가득 찼을 때 알베도에 곱하는 값(젖으면 어두워진다)
static const float  kWetRoughness       = 0.15f; // 젖음이 가득 찼을 때의 거칠기(젖으면 번들거린다)
static const float3 kDirtColor          = float3( 0.28f, 0.20f, 0.12f );
static const float  kDirtRoughness      = 1.0f;
static const float3 kBloodColor         = float3( 0.30f, 0.01f, 0.01f );
static const float  kBloodRoughness     = 0.25f;
static const float3 kWoundColor         = float3( 0.18f, 0.02f, 0.04f );
static const float  kWoundRoughness     = 0.45f;
static const float  kTearCoverageCutoff = 0.5f; // 찢김 덮임이 이 값을 넘는 픽셀은 알파 컷으로 구멍이 된다

/** @brief 마스크 채널의 덮임 — 스칼라가 0 이면 0, 아니면 마스크 R 값이다(마스크가 흰색으로 읽히는 인스턴스에서 문 역할). */
float computeMaskedCoverage( PixelContext context, float channelValue, uint maskTexture )
{
    const float mask = MaterialTexture::sampleIndex( maskTexture, context._uv ).r;
    return ( channelValue > 0.0f ) ? saturate( mask ) : 0.0f;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialData material = context._material;

    const float4 surfaceTexture = MaterialTexture::sampleIndex( material._albedoMap, context._uv );
    float4       albedo         = context._color * material._color * surfaceTexture;
    float        roughness      = material._roughness;

    // 젖음 — 몸 전체. 어둡게 하고 거칠기를 낮춘다.
    const float wet = saturate( material._surfaceWet );
    albedo.rgb *= lerp( 1.0f, kWetAlbedoScale, wet );
    roughness = lerp( roughness, kWetRoughness, wet );

    // 흙 · 피 · 상처 — 마스크가 자리를 정하는 덮개. 나중 것이 위에 덮인다.
    const float dirt = computeMaskedCoverage( context, material._surfaceDirt, material._surfaceDirtMask );
    albedo.rgb       = lerp( albedo.rgb, kDirtColor, dirt );
    roughness        = lerp( roughness, kDirtRoughness, dirt );

    const float blood = computeMaskedCoverage( context, material._surfaceBlood, material._surfaceBloodMask );
    albedo.rgb        = lerp( albedo.rgb, kBloodColor, blood );
    roughness         = lerp( roughness, kBloodRoughness, blood );

    const float wound = computeMaskedCoverage( context, material._surfaceWound, material._surfaceWoundMask );
    albedo.rgb        = lerp( albedo.rgb, kWoundColor, wound );
    roughness         = lerp( roughness, kWoundRoughness, wound );

    // 찢김 — 덮임이 컷오프를 넘으면 알파 컷으로 구멍을 낸다(알파 1 → 0, 컷오프 0.5).
    const float tear = computeMaskedCoverage( context, material._surfaceTear, material._surfaceTearMask );
    albedo.a *= ( tear > kTearCoverageCutoff ) ? 0.0f : 1.0f;

    MaterialSurface surface;
    surface._albedo    = albedo;
    surface._normal    = context._normal;
    surface._roughness = roughness;
    surface._metallic  = material._metallic;
    surface.setAlphaCutoff( 0.5f );
    return surface;
}

#if SW_VIEWMODE_SKIPS_LIGHTING
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    // 보기 모드 Unlit — 조명 · 그림자 · 림 없이 알베도 그대로.
    return surface._albedo;
}
#else
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    // 조명 식은 디퍼드와 같은 함수다(lighting.hlsli) — forward_lit.hlsl 과 같은 조합.
    const float shadow = Shadow::sampleAtWorld( context._worldPosition, surface._normal );
    float3      lit    = Lighting::shadeLights( surface._albedo.rgb, context._worldPosition, surface._normal, shadow, surface._roughness, surface._metallic );
    lit += Lighting::computeRimLight( surface._normal );
    return float4( lit, surface._albedo.a );
}
#endif

#include "material_shader_main.hlsli"
