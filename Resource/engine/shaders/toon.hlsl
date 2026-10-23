/**
 * @file toon.hlsl
 * @brief 셀 셰이딩(툰) 머티리얼 셰이더 — 파라미터 체계는 VRM MToon 1.0(`VRMC_materials_mtoon`)을 따른다.
 * @details 셰이딩은 MToon 과 같은 식이다.
 *            shading = linearstep( -1 + toony, 1 - toony, dot( N, L ) + shift ) × 그림자
 *            색      = lerp( 그림자색, 기본색, shading ) × 빛 + 기본색 × 환경광 + 림(프레넬 · 맷캡) + 발광
 *          빛마다 같은 계단을 쓴다(주광 · 점광 · 스폿, 포워드 경로가 보는 빛 목록 그대로). 그림자 맵은 그림자를 드리우는 빛 하나에만 곱한다.
 *
 *          **외곽선은 같은 파일의 다른 퍼뮤테이션이다**(`SW_PASS_MESH_OUTLINE` — 메시 외곽선 패스가 얹는다). 뒤집은 껍질(inverted hull):
 *          정점을 노멀 방향으로 밀어 앞면을 컬링해 그린다(컬 모드는 패스가 정한다). 두께는 월드(미터) 또는 화면(화면 높이 비율, 거리와 무관)이다.
 *          정점은 모프 · 스키닝 결과를 읽으므로(swLoadMorphedVertex) 움직이는 메시에서도 껍질이 따라간다. 외곽선 패스는 퍼뮤테이션에
 *          `MATERIAL_OUTLINE` 이 있는 배치만 그린다(RenderPassTypeInfo 의 `_pRequiredMaterialDefine`).
 *
 *          **머티리얼은 한 스테이지에서만 읽는다**(GL 은 구조버퍼를 두 단계가 읽으면 링크를 거절한다). 기본 퍼뮤테이션은 픽셀이,
 *          외곽선 퍼뮤테이션은 정점이 읽어(두께가 정점에 필요하다) 픽셀이 쓸 값을 보간 칸으로 넘긴다.
 *
 *          **알파 컷오프 머티리얼은 깊이 패스도 이 파일로 그린다**(`SW_PASS_SHADOW` · `SW_PASS_DEPTH_PREPASS` — `FrameRendererUtil::drawsDepthWithMaterialShader`).
 *          shadowdepth.hlsl 은 머티리얼을 몰라 머리카락 카드가 사각형 그림자를 드리우므로, 같은 정점 경로(swLoadMorphedVertex)로 투영하고 픽셀에서 같은 알파로 버린다.
 *          컬러 출력이 없어 픽셀 셰이더는 `clip` 만 한다.
 *
 *          머티리얼 define: `MATERIAL_OUTLINE`(외곽선 패스에 들어간다) · `MATERIAL_ALPHA_CUTOFF`(알파 컷오프) · `MATERIAL_TWO_SIDED`(양면 —
 *          컬링을 끄는 것은 PSO 변형이 하고, 셰이더는 뒷면의 노멀을 뒤집는다) · `MATERIAL_BLEND_TRANSLUCENT`(반투명, 머티리얼 blendMode 와 짝).
 *          `MATERIAL_FORWARD_ONLY` 는 늘 있어야 한다 — G버퍼 퍼뮤테이션은 `#error` 로 쿠킹에서 멈춘다.
 *
 *          UV 애니메이션(MToon uvAnimation*)은 PassCB `g_SwMaterialTime` 으로 픽셀에서 UV 를 스크롤 · 회전한다(computeAnimatedUv).
 */
#include "lighting.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 툰은 PBR 칸이 없는 거친 유전체로 본다.
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

// toon.material 의 프로퍼티와 이름 · 순서가 같다. 머티리얼 패커는 리플렉션 오프셋(이름)으로 채운다.
SW_MATERIAL_BEGIN
{
    float4 baseColor;                // 빛을 받는 쪽 색(MToon baseColorFactor · 0.x _Color). a 는 알파
    float4 shadeColor;               // 빛을 못 받는 쪽 색(shadeColorFactor · 0.x _ShadeColor)
    float4 emissiveColor;            // 발광 색(emissiveFactor · 0.x _EmissionColor)
    float4 rimColor;                 // 프레넬 림 색(parametricRimColorFactor · 0.x _RimColor)
    float4 matcapColor;              // 맷캡에 곱하는 색(matcapFactor). 맷캡 텍스처가 없으면 맷캡이 없다
    float4 outlineColor;             // 외곽선 색(outlineColorFactor · 0.x _OutlineColor)
    float  shadingShift;             // 계단 위치(shadingShiftFactor). 음수일수록 빛 쪽이 넓다
    float  shadingToony;             // 계단의 날카로움(shadingToonyFactor). 1 이면 칼같이, 0 이면 램버트처럼 부드럽다
    float  shadowReceive;            // 그림자 맵을 얼마나 받는가(0.x _ReceiveShadowRate). 1.0 은 늘 1
    float  emissiveStrength;         // 발광 배율(KHR_materials_emissive_strength)
    float  rimFresnelPower;          // 림 지수(parametricRimFresnelPowerFactor)
    float  rimLift;                  // 림 들어올림(parametricRimLiftFactor)
    float  rimLightingMix;           // 림 · 맷캡에 빛을 섞는 정도(rimLightingMixFactor). 0 이면 빛과 무관하게 빛난다
    float  outlineWidth;             // 외곽선 두께 — 월드 모드는 미터, 화면 모드는 화면 높이 비율(outlineWidthFactor)
    float  outlineWidthMode;         // 0 월드 · 1 화면(outlineWidthMode)
    float  outlineLightingMix;       // 외곽선 색에 빛을 곱하는 정도(outlineLightingMixFactor)
    float  outlineMaxDistance;       // 화면 모드 두께가 거리와 무관하게 유지되는 거리 상한(0.x _OutlineScaledMaxDistance). 넘으면 멀수록 가늘어진다
    float  alphaCutoff;              // MATERIAL_ALPHA_CUTOFF 일 때 이 값보다 작은 알파를 버린다(alphaCutoff · 0.x _Cutoff)
    float  normalScale;              // 노멀 맵 xy 배율(normalTexture.scale · 0.x _BumpScale)
    float  shadingShiftTextureScale; // 셰이딩 시프트 텍스처 배율(shadingShiftTexture.scale) — 시프트 += r × 배율
    uint   baseColorMap;             // 기본 텍스처(baseColorTexture · 0.x _MainTex)
    uint   shadeMap;                 // 그림자 텍스처(shadeMultiplyTexture · 0.x _ShadeTexture)
    uint   emissiveMap;              // 발광 텍스처(emissiveTexture · 0.x _EmissionMap)
    uint   matcapMap;                // 맷캡 텍스처(matcapTexture · 0.x _SphereAdd)
    uint   normalMap;                // 탄젠트 공간 노멀 맵(normalTexture · 0.x _BumpMap) — glTF 규약(초록이 위). 없으면 정점 노멀 그대로
    uint   shadingShiftMap;          // 셰이딩 시프트 텍스처(shadingShiftTexture) — r 을 쓴다. 없으면 0 을 더한다
    uint   rimMultiplyMap;           // 림 곱 텍스처(rimMultiplyTexture · 0.x _RimTexture) — 림(프레넬 + 맷캡)에 rgb 를 곱한다
    uint   outlineWidthMap;          // 외곽선 두께 곱 텍스처(outlineWidthMultiplyTexture · 0.x _OutlineWidthTexture) — 정점에서 g 를 두께에 곱한다
    float  uvAnimationScrollSpeedX;  // UV 스크롤 u 속도(uvAnimationScrollXSpeedFactor, UV / 초)
    float  uvAnimationScrollSpeedY;  // UV 스크롤 v 속도(uvAnimationScrollYSpeedFactor, glTF UV — v 는 아래가 +)
    float  uvAnimationRotationSpeed; // UV 회전 속도(uvAnimationRotationSpeedFactor, 라디안 / 초, 중심 (0.5, 0.5))
    uint   uvAnimationMaskMap;       // UV 애니메이션 마스크(uvAnimationMaskTexture · 0.x _UvAnimMaskTexture) — b 를 쓴다. 없으면 흰색이라 전체가 움직인다
}
SW_MATERIAL_END

// 포워드 전용이다 — 계단 · 림 · 맷캡은 G버퍼로 나타낼 수 없다. 머티리얼에 MATERIAL_FORWARD_ONLY 가 있으면 디퍼드의 G버퍼 패스가 이 배치를 빼고
// ForwardOnlyOpaque 패스가 포워드로 그린다. 그 define 을 빠뜨린 머티리얼이 G버퍼에서 조용히 램버트로 칠해지지 않게 쿠킹에서 멈춘다.
#if defined( SW_PASS_GBUFFER )
    #error "toon.hlsl is forward-only: the material must list MATERIAL_FORWARD_ONLY in _alwaysDefines (deferred draws it in ForwardOnlyOpaque)"
#endif

// 훅 선택(materialshader.hlsli). 조명은 툰 식이라 직접 정의한다. 외곽선 퍼뮤테이션은 머티리얼을 정점에서만 읽고(GL 이 구조버퍼를 두 단계에서 읽으면 링크를 거절한다)
// 픽셀이 쓸 값을 사용자 보간 칸으로 넘긴다. 노멀 맵의 탄젠트는 색을 내는 퍼뮤테이션(깊이 · 외곽선이 아닌 것)만 월드로 옮긴다.
#define SW_MATERIAL_CUSTOM_LIGHTING
#if defined( SW_PASS_MESH_OUTLINE )
    #define SW_MATERIAL_READ_IN_VERTEX
    #define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    nointerpolation float4 outlineColor : TEXCOORD8; // rgb 색 · a 빛 섞기
    nointerpolation float4 alphaParams : TEXCOORD9;  // x 기본색 알파 · y 컷오프
    nointerpolation uint   baseColorMap : TEXCOORD10;
};
#else
    #define SW_MATERIAL_DEFAULT_VARYINGS
#endif
#if !defined( SW_PASS_SHADOW ) && !defined( SW_PASS_DEPTH_PREPASS ) && !defined( SW_PASS_MESH_OUTLINE )
    #define SW_MATERIAL_USES_TANGENT
#endif
#include "materialshader.hlsli"

/**
 * @brief 노멀 맵을 적용한 월드 노멀입니다. 맵이 없으면 @p normal 그대로입니다.
 * @details 탄젠트를 노멀에 다시 직교화하고 바이탄젠트는 cross( N, T ) × w 다(glTF · MikkTSpace — C++ `Mesh::computeTangents`).
 */
float3 applyNormalMap( uint normalMap, float normalScale, float2 uv, float3 normal, float4 tangent )
{
    if ( normalMap == kInvalidIndex )
        return normal;
    float3 tangentNormal = swSampleMaterialTexture( normalMap, uv ).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalScale;
    const float3 orthogonalTangent = tangent.xyz - normal * dot( normal, tangent.xyz );
    const float  tangentLength     = length( orthogonalTangent );
    if ( tangentLength < 1e-5f )
        return normal;
    const float3 unitTangent = orthogonalTangent / tangentLength;
    const float3 bitangent   = cross( normal, unitTangent ) * ( tangent.w < 0.0f ? -1.0f : 1.0f );
    return normalize( unitTangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z );
}

/** @brief MToon 의 linearstep — [a, b] 를 [0, 1] 로 옮기고 자른다. a == b 면 계단 하나다. */
float linearStep( float lower, float upper, float value )
{
    return saturate( ( value - lower ) / max( upper - lower, 1e-4f ) );
}

/** @brief 환경광(PassCB 키라이트의 앰비언트 — swShadeLights 와 같은 값)입니다. */
float3 computeAmbient()
{
    return g_KeyLightColor.rgb * g_KeyLightColor.a;
}

/**
 * @brief 빛 목록 전부의 툰 셰이딩 합과 직접광의 합을 구합니다.
 * @param outDirectLighting 빛마다의 색 × 세기 × 감쇠 합(그림자 · 노멀과 무관) — 림 · 외곽선이 빛을 섞을 때 쓴다
 * @details 빛은 커서(lighting.hlsli swBeginLights)로 돈다. 3D 빛이 없으면 0 이다(swShadeLights 와 같은 규칙 — 키라이트로 대신하지 않는다).
 */
float3 shadeToonLights( float3 litColor, float3 shadeColor, float3 worldPosition, float3 normal, float shadow, float shadingShift, float shadingToony,
                        float shadowReceive, out float3 outDirectLighting )
{
    float3        color          = float3( 0.0f, 0.0f, 0.0f );
    float3        directLighting = float3( 0.0f, 0.0f, 0.0f );
    const float   lower          = -1.0f + shadingToony;
    const float   upper          = 1.0f - shadingToony;
    SwLightCursor cursor         = swBeginLights( worldPosition );
    SwLightData   light;
    while ( swNextLight( cursor, light ) )
    {
        float3 toLight;
        float  attenuation;
        if ( swComputeLightIncidence( light, worldPosition, toLight, attenuation ) == false )
            continue;

        // 그림자는 그것을 받는 빛(params.x)마다다 — 방향광은 캐스케이드, 점광 · 스폿은 자기 타일(swComputeLightShadow). 계단을 지난 뒤에 곱한다(MToon 1.0) — 그림자 경계도 그림자색으로 떨어진다.
        const float  shadowTerm = lerp( 1.0f, swComputeLightShadow( light, worldPosition, normal, shadow ), shadowReceive );
        const float  shading    = linearStep( lower, upper, dot( normal, toLight ) + shadingShift ) * shadowTerm;
        const float3 radiance   = light.colorIntensity.rgb * light.colorIntensity.a * attenuation;
        color += lerp( shadeColor, litColor, shading ) * radiance;
        directLighting += radiance;
    }
    outDirectLighting = directLighting;
    return color;
}

/**
 * @brief 빛마다의 색 × 세기 × 감쇠 합입니다(노멀 · 그림자와 무관) — 외곽선이 빛을 섞을 때 쓴다. 3D 빛이 없으면 0 입니다.
 */
float3 computeDirectLighting( float3 worldPosition )
{
    float3        directLighting = float3( 0.0f, 0.0f, 0.0f );
    SwLightCursor cursor         = swBeginLights( worldPosition );
    SwLightData   light;
    while ( swNextLight( cursor, light ) )
    {
        float3 toLight;
        float  attenuation;
        if ( swComputeLightIncidence( light, worldPosition, toLight, attenuation ) == false )
            continue;
        directLighting += light.colorIntensity.rgb * light.colorIntensity.a * attenuation;
    }
    return directLighting;
}

/**
 * @brief 맷캡 UV — 시선 공간의 노멀 xy 입니다(MToon 과 같은 식). 텍스처는 위가 v = 0 이라 v 를 뒤집는다.
 */
float2 computeMatcapUv( float3 normal, float3 viewDirection )
{
    const float3 worldUp   = float3( 0.0f, 1.0f, 0.0f );
    const float3 viewUp    = normalize( worldUp - viewDirection * dot( viewDirection, worldUp ) + float3( 0.0f, 0.0f, 1e-5f ) );
    const float3 viewRight = normalize( cross( viewDirection, viewUp ) );
    const float2 matcapUv  = float2( dot( viewRight, normal ), dot( viewUp, normal ) ) * 0.5f + 0.5f;
    return float2( matcapUv.x, 1.0f - matcapUv.y );
}

/**
 * @brief MToon UV 애니메이션을 적용한 UV 입니다 — (0.5, 0.5) 를 중심으로 돌린 뒤 스크롤한다. 마스크(b)가 0 인 텍셀은 움직이지 않습니다.
 * @details 시계는 PassCB g_SwMaterialTime(장면 애니메이션 시계)이다. 맷캡 UV 와 외곽선(정점이 머티리얼을 읽는 퍼뮤테이션)에는 적용하지 않는다.
 */
float2 computeAnimatedUv( SwMaterialData material, float2 uv )
{
    // VRMC_materials_mtoon-1.0 "Transform Order" 의 식 그대로다(three-vrm mtoon.frag 와 같다): 먼저 (0.5, 0.5) 중심으로 돌리고 그 뒤에 스크롤한다.
    // 회전 행렬은 [ cos sin ; -sin cos ] — U 오른쪽 · V 아래 공간에서 반시계(KHR_texture_transform 과 같은 방향)다. 마스크는 각과 스크롤 양 모두에 곱한다.
    const float  mask     = swSampleMaterialTexture( material.uvAnimationMaskMap, uv ).b;
    const float  angle    = material.uvAnimationRotationSpeed * g_SwMaterialTime * mask;
    const float  cosAngle = cos( angle );
    const float  sinAngle = sin( angle );
    const float2 centered = uv - 0.5f;
    const float2 rotated  = float2( cosAngle * centered.x + sinAngle * centered.y, -sinAngle * centered.x + cosAngle * centered.y ) + 0.5f;
    return rotated + float2( material.uvAnimationScrollSpeedX, material.uvAnimationScrollSpeedY ) * ( g_SwMaterialTime * mask );
}

// ------------------------------------------------------------------------------
// 훅 — 정점 · 픽셀 진입점은 materialshadermain.hlsli 가 가진다(깊이 · 외곽선 · 색 퍼뮤테이션 모두).
// ------------------------------------------------------------------------------
#if defined( SW_PASS_MESH_OUTLINE )

/**
 * @brief 외곽선 — 정점을 노멀 방향으로 민다. 월드 모드는 월드 위치를, 화면 모드는 클립 xy 를 밀어 두께가 거리와 무관하다.
 * @details 화면 모드는 노멀을 클립 공간에 투영해 그 방향으로 밀어낸다. 클립 xy 에 w 를 곱해 더하므로 나눗셈 뒤 화면 크기가 거리와 무관하다
 *          (outlineMaxDistance 를 넘으면 멀수록 가늘어진다). 화면 가로세로 비를 맞춰야 위아래 · 좌우 두께가 같다. 시선을 바로 보는 면(노멀이 카메라 쪽)은
 *          밀어도 실루엣이 안 생기고 안쪽에 얼룩만 남으니 줄인다(MToon 과 같다).
 */
SwOutlineOffset computeMaterialOutline( SwVertexContext context )
{
    // 두께 곱 텍스처(MToon outlineWidthMultiplyTexture)는 g 를 쓴다. 정점이라 밉 0 으로 읽는다. 없으면 흰색이라 1 이다.
    const float     outlineWidth = context.material.outlineWidth * swSampleMaterialTextureLevelZero( context.material.outlineWidthMap, context.input.uv ).g;
    const bool      bScreenWidth = context.material.outlineWidthMode > 0.5f;
    SwOutlineOffset offset;
    offset.worldOffset = bScreenWidth ? float3( 0.0f, 0.0f, 0.0f ) : context.worldNormal * outlineWidth;
    offset.clipOffset  = float2( 0.0f, 0.0f );
    if ( bScreenWidth )
    {
        const float4 clipPosition = swComputeClipPosition( float4( context.worldPosition, 1.0f ), g_ViewProj );
        const float2 clipNormal   = mul( float4( context.worldNormal, 0.0f ), g_ViewProj ).xy;
        const float  normalLength = length( clipNormal );
        const float2 direction    = ( normalLength > 1e-6f ) ? clipNormal / normalLength : float2( 0.0f, 0.0f );
        const float  aspect       = g_OutlineParams.y / max( g_OutlineParams.z, 1e-6f ); // (1/W) / (1/H) = H / W
        const float  facing       = saturate( 1.0f - abs( dot( context.worldNormal, swComputeToView( context.worldPosition ) ) ) );
        const float  scale        = outlineWidth * 2.0f * min( clipPosition.w, context.material.outlineMaxDistance ) * facing;
        offset.clipOffset         = direction * float2( aspect, 1.0f ) * scale;
    }
    return offset;
}

MaterialVaryings computeMaterialVaryings( SwVertexContext context )
{
    MaterialVaryings varyings;
    varyings.outlineColor = float4( context.material.outlineColor.rgb, context.material.outlineLightingMix );
    varyings.alphaParams  = float4( context.material.baseColor.a, context.material.alphaCutoff, 0.0f, 0.0f );
    varyings.baseColorMap = context.material.baseColorMap;
    return varyings;
}

SwMaterialSurface computeMaterialSurface( SwPixelContext context )
{
    // 머리카락 카드처럼 알파로 오린 면은 외곽선도 같은 모양으로 오린다. 알파를 쓰지 않는 변형은 텍스처를 읽지 않는다.
    float alpha = context.color.a * context.user.alphaParams.x;
    #if defined( MATERIAL_ALPHA_CUTOFF )
    alpha *= swSampleMaterialTexture( context.user.baseColorMap, context.uv ).a;
    #endif
    SwMaterialSurface surface;
    surface.albedo    = float4( context.user.outlineColor.rgb, alpha );
    surface.normal    = context.normal;
    surface.roughness = kSurfaceRoughness;
    surface.metallic  = kSurfaceMetallic;
    #if defined( MATERIAL_ALPHA_CUTOFF )
    surface.alphaCutoff = context.user.alphaParams.y;
    #endif
    return surface;
}

float4 computeMaterialLighting( SwPixelContext context, SwMaterialSurface surface )
{
    // 빛 섞기 0 이면 색 그대로, 1 이면 직접광 + 환경광을 곱한다(MToon outlineLightingMixFactor). 1 을 넘는 빛은 잘라 외곽선이 밝아지지 않게 한다.
    const float3 lighting = saturate( computeDirectLighting( context.worldPosition ) + computeAmbient() );
    return float4( context.user.outlineColor.rgb * lerp( float3( 1.0f, 1.0f, 1.0f ), lighting, context.user.outlineColor.a ), 1.0f );
}

#else

SwMaterialSurface computeMaterialSurface( SwPixelContext context )
{
    // UV 애니메이션(MToon uvAnimation*)은 모든 머티리얼 텍스처에 같은 UV 로 적용한다 — 맷캡만 시선 UV 다.
    const float2      uv = computeAnimatedUv( context.material, context.uv );
    SwMaterialSurface surface;
    // 알파 컷오프의 알파는 깊이 패스와 같은 값(정점 색 × 기본색 × 기본 텍스처)이다 — 뼈대가 같은 값으로 오린다.
    surface.albedo = context.color * context.material.baseColor * swSampleMaterialTexture( context.material.baseColorMap, uv );
    surface.normal = context.normal;
    #if defined( SW_MATERIAL_USES_TANGENT )
    surface.normal = applyNormalMap( context.material.normalMap, context.material.normalScale, uv, context.normal, context.tangent );
    #endif
    surface.roughness = kSurfaceRoughness;
    surface.metallic  = kSurfaceMetallic;
    #if defined( MATERIAL_ALPHA_CUTOFF )
    surface.alphaCutoff = context.material.alphaCutoff;
    #endif
    return surface;
}

float4 computeMaterialLighting( SwPixelContext context, SwMaterialSurface surface )
{
    float4 result;
    #if SW_VIEWMODE_SKIPS_LIGHTING
    result = surface.albedo;
    #else
    const SwMaterialData material = context.material;
    const float2         uv       = computeAnimatedUv( material, context.uv );
    float4               litColor = surface.albedo;
    // 셰이딩 시프트 텍스처(MToon shadingShiftTexture) — 없으면 더하지 않는다(흰색을 더하면 배율만큼 밀린다).
    const float shadingShift = material.shadingShift + ( ( material.shadingShiftMap == kInvalidIndex ) ? 0.0f : swSampleMaterialTexture( material.shadingShiftMap, uv ).r * material.shadingShiftTextureScale );
        #if defined( SW_VIEWMODE_LIGHTINGONLY )
    // 보기 모드 LightingOnly — 계단 셰이딩은 그대로 두고 기본색 · 그늘색만 회색으로 바꾼다(swShadeLights 를 쓰지 않으므로 여기서).
    litColor.rgb            = SW_VIEWMODE_LIGHTINGONLY_ALBEDO;
    const float3 shadeColor = SW_VIEWMODE_LIGHTINGONLY_ALBEDO;
        #else
    const float3 shadeColor = context.color.rgb * material.shadeColor.rgb * swSampleMaterialTexture( material.shadeMap, uv ).rgb;
        #endif
    const float shadow = swSampleShadowAtWorld( context.worldPosition, surface.normal );

    float3       directLighting;
    float3       color   = shadeToonLights( litColor.rgb, shadeColor, context.worldPosition, surface.normal, shadow, shadingShift, material.shadingToony,
                                            material.shadowReceive, directLighting );
    const float3 ambient = computeAmbient();
    color += litColor.rgb * ambient;

    // 림 = (프레넬 + 맷캡) × lerp( 1, 빛, rimLightingMix ) — MToon 1.0 과 같다. 맷캡은 텍스처가 있을 때만 더한다(없으면 흰색이 되어 화면이 바랜다).
    const float3 viewDirection = swComputeToView( context.worldPosition );
    const float  fresnelBase   = saturate( 1.0f - dot( surface.normal, viewDirection ) + material.rimLift );
    float3       rim           = pow( fresnelBase, max( material.rimFresnelPower, 1e-4f ) ) * material.rimColor.rgb;
    if ( material.matcapMap != kInvalidIndex )
        rim += material.matcapColor.rgb * swSampleMaterialTexture( material.matcapMap, computeMatcapUv( surface.normal, viewDirection ) ).rgb;
    rim *= swSampleMaterialTexture( material.rimMultiplyMap, uv ).rgb; // 림 곱 텍스처(rimMultiplyTexture) — 없으면 흰색이라 그대로
    color += rim * lerp( float3( 1.0f, 1.0f, 1.0f ), directLighting + ambient, material.rimLightingMix );

    color += material.emissiveColor.rgb * material.emissiveStrength * swSampleMaterialTexture( material.emissiveMap, uv ).rgb;
    result = float4( color, litColor.a );
    #endif
    return result;
}

#endif // SW_PASS_MESH_OUTLINE

#include "materialshadermain.hlsli"
