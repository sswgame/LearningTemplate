/**
 * @file pbr_lit.hlsl
 * @brief 텍스처 PBR 머티리얼 셰이더 — glTF 금속도 · 거칠기 머티리얼 그대로(알베도 · 노멀 · ORM 맵). 스캔 · 사진 기반 에셋(Poly Haven 등)이 쓴다.
 * @details 머티리얼 셰이더 뼈대(material_shader.hlsli · material_shader_main.hlsli)의 사용자다. `forward_lit.hlsl`(색 · 알베도 하나)과 조명 식이 같다(lighting.hlsli).
 *          ORM 맵은 glTF 배치다 — R 가림(AO) · G 거칠기 · B 금속도. 거칠기 · 금속도는 맵 × 머티리얼 값(glTF 의 factor)이다.
 *          `_worldTileSize` 가 0 보다 크면 메시 UV 대신 월드 좌표를 축 정렬로 투영한다(그 길이 m 마다 한 번 반복) — 늘린 큐브 · 평면(바닥 · 벽)이 늘어나지 않고 타일링된다
 *          (언리얼 WorldAlignedTexture 와 같은 생각, 축 하나만 골라 섞지는 않는다).
 *          머티리얼 define: `MATERIAL_ALPHA_CUTOFF`(glTF alphaMode MASK — 잎 · 꽃) · `MATERIAL_TWO_SIDED`(glTF doubleSided — 컬링은 PSO 변형이 끄고 뒷면 노멀은 뼈대가 뒤집는다) ·
 *          `MATERIAL_DISPLACEMENT`(높이 맵 변위 — 헐 · 도메인이 붙은 변형으로 윤곽까지 바뀐다, material_displacement.hlsli).
 *          shortcut: 가림은 알베도에 곱한다 — 직접광도 조금 어두워진다. G버퍼에 가림 칸이 생기면 간접광(앰비언트 · 프로브)에만 곱한다.
 */
#include "lighting.hlsli"

// pbrmaterial.material 의 프로퍼티 순서 · 타입과 1:1 이다(머티리얼 패커는 리플렉션 오프셋으로 채운다).
SW_MATERIAL_BEGIN
{
    float4 _color;
    float  _roughness;
    float  _metallic;
    float  _normalScale;
    float  _occlusionStrength;
    float  _worldTileSize;
    float  _alphaCutoff; // MATERIAL_ALPHA_CUTOFF 일 때 이 값보다 작은 알파를 버린다(glTF alphaMode MASK 의 alphaCutoff)
    uint   _albedoMap;
    uint   _normalMap;
    uint   _ormMap;
    // 변위(MATERIAL_DISPLACEMENT 일 때만 읽는다 — material_displacement.hlsli 머리말). 높이 맵 R 이 _displacementMidLevel 이면 제자리, 위는 노멀 방향으로 _displacementScale m 까지
    uint  _displacementMap;
    float _displacementScale;
    float _displacementMidLevel;
    float _tessellationDensity;   // 각 1 rad 당 분할 수 — 1 m 변을 10 m 에서 보면 0.1 rad × 밀도
    float _tessellationMaxFactor; // 변 계수 상한(64 이하)
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::checkStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
    uint _swPad1;
}
SW_MATERIAL_END

#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_RIM_LIGHT
#define SW_MATERIAL_DEFAULT_VARYINGS
#if !defined( SW_PASS_SHADOW ) && !defined( SW_PASS_DEPTH_PREPASS )
    #define SW_MATERIAL_USES_TANGENT
#endif
#include "material_shader.hlsli"
#include "normal_map.hlsli"

/**
 * @brief 월드 축 정렬 투영 UV 와 그 탄젠트입니다. 노멀이 가장 큰 축의 면에 투영하고, 벽은 위가 이미지 위다.
 * @details 탄젠트 손잡이는 메시와 같은 규약이다 — cross( N, T ) × w 가 -dP/dv(수면: UV = xz, T = +x, w = +1 — `WaterBodyComponent`).
 *          shortcut: 반대쪽 면(-x · -z)은 이미지가 좌우로 뒤집힌다 — 콘크리트 · 블록 같은 무늬엔 보이지 않는다. 글자 · 방향 있는 무늬를 쓰면 면마다 u 부호를 고른다.
 */
void computeWorldAlignedUV( float3 worldPosition, float3 normal, float tileSize, out float2 outUV, out float4 outTangent )
{
    const float3 axisWeight = abs( normal );
    float3       tangent;
    float3       downV;
    if ( axisWeight.y >= axisWeight.x && axisWeight.y >= axisWeight.z )
    {
        outUV   = worldPosition.xz;
        tangent = float3( 1.0f, 0.0f, 0.0f );
        downV   = float3( 0.0f, 0.0f, 1.0f );
    }
    else if ( axisWeight.x >= axisWeight.z )
    {
        outUV   = float2( worldPosition.z, -worldPosition.y );
        tangent = float3( 0.0f, 0.0f, 1.0f );
        downV   = float3( 0.0f, -1.0f, 0.0f );
    }
    else
    {
        outUV   = float2( worldPosition.x, -worldPosition.y );
        tangent = float3( 1.0f, 0.0f, 0.0f );
        downV   = float3( 0.0f, -1.0f, 0.0f );
    }
    outUV /= tileSize;
    outTangent = float4( tangent, dot( cross( normal, tangent ), downV ) > 0.0f ? -1.0f : 1.0f );
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
#if defined( SW_MATERIAL_USES_TANGENT )
    if ( context._material._worldTileSize > 0.0f )
        computeWorldAlignedUV( context._worldPosition, context._normal, context._material._worldTileSize, context._uv, context._tangent );
#endif
    const float4 albedoTexture = MaterialTexture::sampleIndex( context._material._albedoMap, context._uv );
    // ORM 이 없으면(kInvalidIndex · bindless 에뮬 백엔드) 흰색이라 가림 1 · 거칠기 · 금속도는 머티리얼 값 그대로다.
    const float3 orm       = MaterialTexture::sampleIndex( context._material._ormMap, context._uv ).rgb;
    const float  occlusion = lerp( 1.0f, orm.r, saturate( context._material._occlusionStrength ) );

    MaterialSurface surface;
    surface._albedo = context._color * context._material._color * albedoTexture;
    surface._albedo.rgb *= occlusion;
#if defined( SW_MATERIAL_USES_TANGENT )
    surface._normal = applyNormalMap( context._material._normalMap, context._material._normalScale, context._uv, context._normal, context._tangent );
#else
    surface._normal = context._normal;
#endif
    surface._roughness = saturate( orm.g * context._material._roughness );
    surface._metallic  = saturate( orm.b * context._material._metallic );
    surface.setAlphaCutoff( context._material._alphaCutoff ); // 알파 컷오프 머티리얼만 싣는다 — 깊이 · 본 패스가 같은 값으로 오린다
    return surface;
}

#include "material_shader_main.hlsli"
