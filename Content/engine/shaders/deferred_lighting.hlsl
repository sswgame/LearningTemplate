// 디퍼드는 셰이딩 모델 번호마다 겹 BRDF 의 엽을 켠다 — 모든 엽을 컴파일하고 번호가 무게를 정한다(꺼진 엽은 무게 0 이라 PBR 과 같다).
#define SW_SHADING_MODEL_LAYERED
#define MATERIAL_LOBE_COAT
#define MATERIAL_LOBE_SHEEN
#define MATERIAL_LOBE_SUBSURFACE
#define MATERIAL_LOBE_TRANSMISSION
#define MATERIAL_LOBE_ANISOTROPY
#include "lighting.hlsli"
#include "fullscreen.hlsli"

/**
 * deferred_lighting.hlsl — G버퍼를 읽어 씬의 모든 라이트로 셰이딩하는 풀스크린 패스.
 *
 * 조명 식은 포워드와 **같은 함수**(lighting.hlsli 의 SurfaceBRDF::shadeLights)를 부른다. 두 경로가 각자 식을 들고 있으면 반드시 갈라진다("경로마다 다른 그림").
 * 셰이딩 모델 번호(G버퍼 알베도 알파 — shading_model_ids.hlsli)마다 사용자 칸(GBufferCustomData)을 풀어 엽을 하나 켠다 — 번호 하나에 분기 하나다(언리얼 디퍼드의 ShadingModelID 분기).
 * 출력은 장면 색(LitColor)에 **더한다**(파이프라인 XML 의 Additive) — G버퍼 패스가 자체 발광을 먼저 적어 두었다.
 * shortcut: 모든 엽이 컴파일되어 기본 PBR 픽셀도 엽 식을 무게 0 으로 돈다(빛마다 GGX 하나 · 시엔 하나 더). 비싸지면 번호별 타일 분류(언리얼 Strata 타일)로 가른다.
 *
 * 월드 위치는 깊이에서 복원한다(Transform::computeWorldPositionFromDepth). G버퍼에 위치를 저장하지 않는 이유는
 * 첨부 하나를 통째로 아끼기 때문이고, 언리얼도 같은 선택을 한다.
 */

/** @brief 번호마다 사용자 칸을 엽으로 — 번호 하나에 분기 하나. 모르는 번호는 기본 PBR 이다(번호 표 밖 값은 G버퍼 패스가 쓰지 않는다). */
void applyShadingModel( inout SurfaceBRDF brdf, uint shadingModel, float4 customData, float3 albedo )
{
    if ( shadingModel == SW_SHADING_MODEL_ID_SUBSURFACE )
    {
        brdf.setSubsurface( customData.rgb );
        brdf.setTransmission( albedo * customData.a, max( customData.r, max( customData.g, customData.b ) ) );
    }
    else if ( shadingModel == SW_SHADING_MODEL_ID_CLOTH )
    {
        brdf.setSheen( customData.rgb, customData.a, 1.0f );
    }
    else if ( shadingModel == SW_SHADING_MODEL_ID_CLEAR_COAT )
    {
        brdf.setCoat( customData.r, customData.g );
    }
    else if ( shadingModel == SW_SHADING_MODEL_ID_EYE )
    {
        brdf.setCoat( customData.r, customData.g );
        brdf.setSubsurface( float3( customData.b, customData.b * 0.6f, customData.b * 0.5f ) * ( 1.0f - customData.a ) );
    }
    else if ( shadingModel == SW_SHADING_MODEL_ID_THIN_TRANSLUCENT )
    {
        brdf.setTransmission( customData.rgb, customData.a );
    }
    else if ( shadingModel == SW_SHADING_MODEL_ID_ANISOTROPIC )
    {
        brdf.setAnisotropy( Octahedral::decode( customData.rg * 2.0f - 1.0f ), customData.b * 2.0f - 1.0f );
    }
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float4 albedoSample = PassTexture::sampleAlbedo( input._uv );
    const float3 albedo       = albedoSample.rgb;
    const uint   shadingModel = SurfaceOutput::decodeShadingModel( albedoSample.a );
    // 옥타헤드럴 좌표(G버퍼 노멀 칸)를 단위 노멀로 푼 값이다. 클리어 값 (0, 0) 은 +z 로 풀려 안 그린 곳도 단위 길이다.
    const float3 normal = PassTexture::sampleNormal( input._uv );

    const float  depth         = PassTexture::sampleDepth( input._uv ).r;
    const float3 worldPosition = Transform::computeWorldPositionFromDepth( input._uv, depth );

    // 금속도 · 거칠기 · 반사율 · 가림은 G버퍼 머티리얼 칸이다(binding_output.hlsli SurfaceOutput::encodeGbufferMaterial 이 적는다).
    float roughness;
    float metallic;
    float specular;
    float occlusion;
    PassTexture::loadGbufferMaterial( input._uv, roughness, metallic, specular, occlusion );

    // 보기 모드 LightingOnly 는 표면 색을 회색으로 바꾼다(ViewMode::overrideLightingAlbedo — 포워드의 Lighting::shadeLights 와 같은 자리).
    SurfaceBRDF brdf = SurfaceBRDF::makePBR( ViewMode::overrideLightingAlbedo( albedo ), worldPosition, normal, roughness, metallic );
    // 유전체 F0 를 반사율 칸으로(언리얼 Specular — 0.5 → 0.04). 금속은 기본 색 그대로.
    const float dielectric   = 0.08f * specular;
    brdf._pbr._specularColor = lerp( float3( dielectric, dielectric, dielectric ), albedo, saturate( metallic ) );
    brdf._occlusion          = occlusion;
    applyShadingModel( brdf, shadingModel, PassTexture::sampleGbufferCustomData( input._uv ), albedo );

    // 그림자는 월드 위치를 **라이트 클립 공간으로 투영해** 읽는다(화면 UV 로 샘플하면 그늘이 물체를 따라오지 않고 화면에 붙는다).
    const float  shadow = Shadow::sampleAtWorld( worldPosition, normal );
    const float3 shaded = brdf.shadeLights( worldPosition, shadow ) + Lighting::computeRimLight( normal );
    // 보기 모드 Unlit · 조명 없는 머티리얼은 알베도 그대로.
    const float3 lit = ( shadingModel == SW_SHADING_MODEL_ID_UNLIT ) ? albedo : shaded;

    // 아무것도 안 그린 곳(번호 0 — 클리어 값)은 **쓰지 않고 버린다**(출력을 다 만든 뒤 — binding.hlsli 8-1 절). 셰이딩할 표면이 없다.
    // discard 하면 LitColor 는 자기 클리어 색 그대로 남는다 — 포워드의 SceneColor 와 같은 배경이다. 보기 모드 Overdraw 는 깊이를 쓰지 않아 번호로 본다.
    if ( shadingModel == SW_SHADING_MODEL_ID_NONE )
        discard;
    return float4( lit, 1.0f );
}
