/**
 * surface_deform.hlsl — 표면 변형 층을 읽는 지면. 발자국 · 끌린 자국 · 바퀴 자국 · 파묻힘 테두리 · 깔때기가
 * 정점을 내리고(월드 위치 오프셋) 법선을 기울인다. 충돌 · 내비는 그대로다 — 보이는 것만 꺼진다.
 *
 * 층(텍스처 · 창)은 머티리얼 칸이 아니라 패스가 거는 값이다(surface_deform_layer.hlsli — 엔진 텍스처 칸 SurfaceDeform · 패스 상수 g_SurfaceDeformParams).
 * 지형(terrain.hlsl)도 같은 층을 읽는다.
 *
 * 머티리얼 셰이더 뼈대(material_shader.hlsli · material_shader_main.hlsli)의 사용자다. 머티리얼은 **정점 셰이더만** 읽는다 —
 * GL(ARB_gl_spirv)은 구조버퍼를 정점 · 픽셀 두 단계에서 읽으면 링크를 거절한다(water.hlsl 과 같은 길). 픽셀이 쓸 값(색 · 거칠기 · 텍스처 번호)은 사용자 보간 칸으로 넘긴다.
 * 알베도는 월드 xz 로 찍는다(한 칸 1 m — 프로토타입 격자와 같다).
 */
#include "lighting.hlsli"
#include "surface_deform_layer.hlsli"

// surfacedeform.material 의 프로퍼티와 같은 이름 · 순서 · 타입이다.
SW_MATERIAL_BEGIN
{
    float4 _color;
    float  _roughness;
    float  _metallic;
    uint   _albedoMap;
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::checkStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "surface_deform.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(material_shader.hlsli) — 머티리얼은 정점에서만 읽고 픽셀이 쓸 값은 사용자 보간 칸으로 넘긴다. 조명은 기본 PBR.
#define SW_MATERIAL_READ_IN_VERTEX
#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    nointerpolation float4 _color : TEXCOORD8;
    nointerpolation float4 _surface : TEXCOORD9; // x 거칠기, y 금속도
    nointerpolation uint   _albedoMap : TEXCOORD10;
};
#include "material_shader.hlsli"

/** @brief 월드 위치 오프셋 — 변형 높이만큼 정점을 내린다(y 만). 그림자 패스도 같은 층을 읽어 같은 자리를 만든다. */
float3 computeWorldPositionOffset( VertexContext context )
{
    return float3( 0.0f, SurfaceDeformLayer::sampleHeight( context._worldPosition.xz ), 0.0f );
}

MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    const MaterialData material = context._material;
    MaterialVaryings   varyings;
    varyings._color     = material._color;
    varyings._surface   = float4( material._roughness, material._metallic, 0.0f, 0.0f );
    varyings._albedoMap = material._albedoMap;
    return varyings;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialVaryings user          = context._user;
    const float2           place         = context._worldPosition.xz;
    const float4           albedoTexture = MaterialTexture::sampleIndex( user._albedoMap, place );

    MaterialSurface surface;
    surface._albedo    = float4( user._color.rgb * albedoTexture.rgb, 1.0f );
    surface._normal    = SurfaceDeformLayer::tiltNormal( context._normal, place );
    surface._roughness = user._surface.x;
    surface._metallic  = user._surface.y;
    return surface;
}

#include "material_shader_main.hlsli"
