/**
 * surface_deform.hlsl — 표면 변형 층을 읽는 지면(SurfaceDeformMeshComponent 가 값을 싣는다). 발자국 · 끌린 자국 · 바퀴 자국 · 파묻힘 테두리 · 깔때기가
 * 정점을 내리고(월드 위치 오프셋) 법선을 기울인다. 충돌 · 내비는 그대로다 — 보이는 것만 꺼진다.
 *
 * 머티리얼 셰이더 뼈대(material_shader.hlsli · material_shader_main.hlsli)의 사용자다. 정점이 변형 텍스처를 읽어야 하므로 머티리얼은 **정점 셰이더만** 읽는다 —
 * GL(ARB_gl_spirv)은 구조버퍼를 정점 · 픽셀 두 단계에서 읽으면 링크를 거절한다(water.hlsl 과 같은 길). 픽셀이 쓸 값(색 · 거칠기 · 텍스처 번호 · 창)은 사용자 보간 칸으로 넘긴다.
 *
 * 변형 텍스처(C++ SurfaceDeformGrid 의 인코딩): R = 1 - 꺼짐 / 범위, G = 1 - 솟음 / 범위 — 높이 = (R - G) × 범위. 흰색이 평평이라 텍스처가 아직 없거나
 * 흰색으로 읽히는 자리가 땅을 옮기지 않는다. 창(_deformParams) = (최소 x, 최소 z, 1 / 너비, 깊이 범위)이고 창 밖은 0, 가장자리는 서서히 0 으로 줄인다
 * (창이 옮겨 갈 때 자국이 끊겨 보이지 않게). 알베도는 월드 xz 로 찍는다(한 칸 1 m — 프로토타입 격자와 같다).
 */
#include "lighting.hlsli"

// 창 가장자리에서 변형을 0 으로 줄이는 폭의 역수(창 너비 비율) — 1/32 안쪽부터 줄인다(20 m 창이면 0.6 m).
static const float kEdgeFadeScale = 32.0f;

// surfacedeform.material 의 프로퍼티와 같은 이름 · 순서 · 타입이다.
SW_MATERIAL_BEGIN
{
    float4 _color;
    float4 _deformParams; // x, y = 창 최소 (x, z) 월드, z = 1 / 창 너비(1/m), w = 깊이 범위(m). z 가 0 이면 변형이 없다
    float  _deformTexel;  // 변형 텍셀의 월드 크기(m) — 픽셀 법선의 차분 간격
    float  _roughness;
    float  _metallic;
    uint   _albedoMap;
    uint   _deformMap;
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::checkStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
    uint _swPad1;
    uint _swPad2;
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
    nointerpolation float4 _deformParams : TEXCOORD9;
    nointerpolation float4 _surface : TEXCOORD10; // x 거칠기, y 금속도, z 변형 텍셀(m)
    nointerpolation uint   _albedoMap : TEXCOORD11;
    nointerpolation uint   _deformMap : TEXCOORD12;
};
#include "material_shader.hlsli"

/** @brief 월드 (x, z) 의 변형 높이(m, 음수 = 꺼짐)입니다. 텍스처가 없거나 창 밖이면 0 이다. 이른 반환 없이 출구가 하나다. */
float sampleDeformHeight( uint deformMap, float4 deformParams, float2 worldXZ )
{
    float height = 0.0f;
    if ( deformMap != kInvalidIndex && deformParams.z > 0.0f )
    {
        const float2 uv    = ( worldXZ - deformParams.xy ) * deformParams.z;
        const float2 edge  = min( uv, 1.0f - uv );
        const float  fade  = saturate( min( edge.x, edge.y ) * kEdgeFadeScale );
        const float4 texel = MaterialTexture::sampleLevelZero( deformMap, saturate( uv ) );
        height             = ( texel.r - texel.g ) * deformParams.w * fade;
    }
    return height;
}

/** @brief 월드 위치 오프셋 — 변형 높이만큼 정점을 내린다(y 만). 그림자 패스도 같은 자리를 만든다. */
float3 computeWorldPositionOffset( VertexContext context )
{
    return float3( 0.0f, sampleDeformHeight( context._material._deformMap, context._material._deformParams, context._worldPosition.xz ), 0.0f );
}

MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    const MaterialData material = context._material;
    MaterialVaryings   varyings;
    varyings._color        = material._color;
    varyings._deformParams = material._deformParams;
    varyings._surface      = float4( material._roughness, material._metallic, material._deformTexel, 0.0f );
    varyings._albedoMap    = material._albedoMap;
    varyings._deformMap    = material._deformMap;
    return varyings;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialVaryings user  = context._user;
    const float2           place = context._worldPosition.xz;
    // 법선 — 변형 높이의 중앙 차분(한 텍셀 간격). 높이장 y = h(x, z) 의 법선은 (-dh/dx, 1, -dh/dz) 쪽이고 메시 법선에 그 기울기를 더한다.
    const float  step     = max( user._surface.z, 1e-3f );
    const float  left     = sampleDeformHeight( user._deformMap, user._deformParams, place - float2( step, 0.0f ) );
    const float  right    = sampleDeformHeight( user._deformMap, user._deformParams, place + float2( step, 0.0f ) );
    const float  back     = sampleDeformHeight( user._deformMap, user._deformParams, place - float2( 0.0f, step ) );
    const float  front    = sampleDeformHeight( user._deformMap, user._deformParams, place + float2( 0.0f, step ) );
    const float2 gradient = float2( right - left, front - back ) / ( 2.0f * step );

    const float4 albedoTexture = MaterialTexture::sampleIndex( user._albedoMap, place );

    MaterialSurface surface;
    surface._albedo    = float4( user._color.rgb * albedoTexture.rgb, 1.0f );
    surface._normal    = normalize( context._normal + float3( -gradient.x, 0.0f, -gradient.y ) );
    surface._roughness = user._surface.x;
    surface._metallic  = user._surface.y;
    return surface;
}

#include "material_shader_main.hlsli"
