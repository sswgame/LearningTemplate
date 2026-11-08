/**
 * @file hair.hlsl
 * @brief 머리카락 카드 머티리얼 셰이더 — 셰이딩은 hair_shading.hlsli(Marschner 근사 R · TT · TRT + 다중 산란 확산), 덮임은 정렬 없는 디더 알파다.
 * @details **가닥 방향:** 정점 탄젠트(dP/du)나 바이탄젠트(dP/dv) 중 가닥을 따라가는 쪽을 `_strandAxis`(0 · 1)로 고르고, 같은 축의 UV 가 가닥 좌표다 —
 *          `_strandRootUV`(뿌리) → `_strandTipUV`(끝)를 0 → 1 로 옮긴 값 s. 방향은 뿌리 → 끝(큐티클 이동의 부호가 이 방향을 따른다).
 *
 *          **덮임(카드 가장자리):** alphaMode="Mask" 의 알파 컷오프 길(깊이 · 그림자 · 본 패스가 같은 표면 훅으로 같은 픽셀을 버린다)을 쓰되, `_coverageDither` 가 1 이면
 *          컷오프 자리에 화면 픽셀의 노이즈 문턱(interleaved gradient noise)을 둔다 — 반투명 정렬 없이 불투명 목록에서 알파 0.3 은 픽셀 30 % 로 남고, TAA 가 그것을
 *          부드러운 가장자리로 모은다(언리얼 머리카락 카드의 Dithered Opacity Mask + TAA, 유니티 HDRP 헤어의 알파 테스트와 같은 자리). 문턱이 화면 픽셀의 순수
 *          함수라 깊이 프리패스와 본 패스가 같은 픽셀을 버린다. 반투명 앞머리 패스는 두지 않는다 — 디더 + TAA 로 충분하고, 정렬 · 깊이 쓰기 없는 패스는
 *          그림자 · SSAO 와 어긋난다(필요해지면 패스 종류 표에 MATERIAL_HAIR_FRINGE 를 요구하는 메시 패스로 더한다).
 *
 *          **꾸미기(외형이 머티리얼 값으로 건다):** `_length`(0..1 가닥 좌표에서 자르기 — 그 뒤는 알파 0 이라 모든 패스 · 모든 LOD 가 UV 로 같은 자리를 자른다),
 *          `_rootColor` · `_tipColor` · `_gradientPosition` · `_gradientWidth`(뿌리 → 끝 색 그라데이션), `_highlightColor`(a = 세기) · `_highlightMap`(r = 마스크).
 *          색은 기본 텍스처 rgb 에 곱한다 — 색을 온전히 바꾸려면 회색 텍스처를 쓴다.
 *
 *          머티리얼 define: `MATERIAL_ALPHA_CUTOFF`(alphaMode="Mask" — 늘 켠다) · `MATERIAL_TWO_SIDED`(카드는 양면). `MATERIAL_FORWARD_ONLY` 는 늘 있어야 한다 —
 *          G버퍼에 머리카락 셰이딩 모델 칸이 없어 G버퍼 퍼뮤테이션은 `#error` 로 쿠킹에서 멈춘다(툰과 같은 길).
 */
#include "lighting.hlsli"

// hair.material 의 프로퍼티와 이름 · 순서가 같다(머티리얼 패커는 리플렉션 오프셋으로 채운다).
SW_MATERIAL_BEGIN
{
    float4 _baseColor;        // 기본 텍스처에 곱하는 색(a = 알파)
    float4 _rootColor;        // 뿌리 색(그라데이션 시작)
    float4 _tipColor;         // 끝 색(그라데이션 끝)
    float4 _highlightColor;   // 하이라이트 색, a = 세기(0 이면 없음)
    float  _gradientPosition; // 그라데이션 가운데(가닥 좌표 0..1)
    float  _gradientWidth;    // 그라데이션 폭(가닥 좌표, 0 이면 계단)
    float  _length;           // 가닥 좌표가 이 값을 넘는 자리는 자른다(1 = 그대로)
    float  _strandRootUV;     // 가닥 축 UV 의 뿌리 값
    float  _strandTipUV;      // 가닥 축 UV 의 끝 값
    float  _strandAxis;       // 0 = 탄젠트(U 방향) · 1 = 바이탄젠트(V 방향)가 가닥
    float  _roughness;        // 세로 거칠기
    float  _shift;            // 큐티클 기울기(라디안)
    float  _specular;         // R 갈래 배율
    float  _scatter;          // 다중 산란 확산 세기
    float  _shiftJitter;      // 텍스처 밝기로 가닥마다 흩뜨리는 탄젠트 이동(Kajiya-Kay)
    float  _alphaCutoff;      // 디더가 아니면 이 알파보다 작은 픽셀을 버린다
    float  _coverageDither;   // 1 이면 화면 노이즈 문턱으로 버린다(정렬 없는 부드러운 가장자리 — TAA 가 모은다)
    uint   _albedoMap;        // 기본 텍스처(rgb 색 · a 덮임)
    uint   _highlightMap;     // 하이라이트 마스크(r)
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::checkStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
}
SW_MATERIAL_END

#if defined( SW_PASS_GBUFFER )
    #error "hair.hlsl is forward-only: the material must list MATERIAL_FORWARD_ONLY in _alwaysDefines (deferred draws it in ForwardOnlyOpaque)"
#endif

#define SW_MATERIAL_CUSTOM_LIGHTING
#define SW_MATERIAL_DEFAULT_VARYINGS
#if !defined( SW_PASS_SHADOW ) && !defined( SW_PASS_DEPTH_PREPASS )
    #define SW_MATERIAL_USES_TANGENT
#endif
#include "material_shader.hlsli"
#include "hair_shading.hlsli"

/** @brief 가닥 좌표 s(뿌리 0 → 끝 1)입니다. */
float computeStrandCoordinate( MaterialData material, float2 uv )
{
    const float axisUV = material._strandAxis > 0.5f ? uv.y : uv.x;
    const float span   = material._strandTipUV - material._strandRootUV;
    return saturate( ( axisUV - material._strandRootUV ) / ( abs( span ) > 1e-5f ? span : 1e-5f ) );
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialData material  = context._material;
    const float4       texel     = MaterialTexture::sampleIndex( material._albedoMap, context._uv );
    const float        strand    = computeStrandCoordinate( material, context._uv );
    const float        halfWidth = max( material._gradientWidth * 0.5f, 1e-4f );
    const float3       gradient  = lerp( material._rootColor.rgb, material._tipColor.rgb, smoothstep( material._gradientPosition - halfWidth, material._gradientPosition + halfWidth, strand ) );
    const float        highlight = MaterialTexture::sampleIndex( material._highlightMap, context._uv ).r * material._highlightColor.a;

    MaterialSurface surface;
    surface._albedo.rgb = lerp( context._color.rgb * material._baseColor.rgb * texel.rgb * gradient, material._highlightColor.rgb, saturate( highlight ) );
    // 길이 자르기는 알파 0 이다 — 깊이 · 그림자 · 본 패스가 같은 표면 훅으로 같은 자리를 버린다.
    surface._albedo.a  = context._color.a * material._baseColor.a * texel.a * ( strand <= material._length ? 1.0f : 0.0f );
    surface._normal    = context._normal;
    surface._roughness = material._roughness;
    surface._metallic  = 0.0f;
    surface.setAlphaCutoff( material._coverageDither > 0.5f ? CommonMath::computeInterleavedGradientNoise( context._position.xy ) : material._alphaCutoff );
    return surface;
}

// 깊이 패스에는 탄젠트 칸이 없고 조명 훅도 부르지 않는다 — 가닥 방향을 읽는 몸통은 색 패스에만 둔다.
#if !defined( SW_MATERIAL_USES_TANGENT )
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    return surface._albedo;
}
#elif SW_VIEWMODE_SKIPS_LIGHTING
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    return surface._albedo;
}
#else
/** @brief 가닥 방향(월드, 뿌리 → 끝)입니다. 바이탄젠트는 메시 규약(cross( N, T ) × w = −dP/dv)에서 dP/dv 로 뒤집는다. */
float3 computeStrandTangent( PixelContext context, float texelLuminance )
{
    const MaterialData material  = context._material;
    const float3       tangentU  = context._tangent.xyz;
    const float3       tangentV  = -cross( context._normal, tangentU ) * context._tangent.w;
    const float3       alongUV   = material._strandAxis > 0.5f ? tangentV : tangentU;
    const float        direction = material._strandTipUV >= material._strandRootUV ? 1.0f : -1.0f;
    return HairShading::shiftTangent( normalize( alongUV * direction ), context._normal, ( texelLuminance - 0.5f ) * material._shiftJitter );
}

float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    const MaterialData material = context._material;
    const float3       albedo   = ViewMode::overrideLightingAlbedo( surface._albedo.rgb );
    const float4       texel    = MaterialTexture::sampleIndex( material._albedoMap, context._uv );

    HairSurface hair;
    hair._baseColor = albedo;
    hair._roughness = material._roughness;
    hair._shift     = material._shift;
    hair._specular  = material._specular;
    hair._scatter   = material._scatter;
    hair._tangent   = computeStrandTangent( context, CommonMath::computeLuminance( texel.rgb ) );
    return float4( HairShading::shadeLights( hair, context._worldPosition, surface._normal ), surface._albedo.a );
}
#endif

#include "material_shader_main.hlsli"
