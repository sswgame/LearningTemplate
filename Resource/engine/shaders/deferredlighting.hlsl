#include "lighting.hlsli"

/**
 * deferredlighting.hlsl — G버퍼를 읽어 씬의 모든 라이트로 셰이딩하는 풀스크린 패스.
 *
 * 조명 식은 포워드와 **같은 함수**(lighting.hlsli 의 Lighting::shadeLights)를 부른다. 두 경로가 각자
 * 식을 들고 있으면 반드시 갈라진다("경로마다 다른 그림").
 *
 * 월드 위치는 깊이에서 복원한다(Transform::computeWorldPositionFromDepth). G버퍼에 위치를 저장하지 않는 이유는
 * 첨부 하나를 통째로 아끼기 때문이고, 언리얼도 같은 선택을 한다.
 */

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexID : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexID == 1 ) ? 3.0f : -1.0f, ( vertexID == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float4 albedoSample = PassTexture::sampleAlbedo( input._uv );
    float3       albedo       = albedoSample.rgb;
    // 옥타헤드럴 좌표(G버퍼 노멀 칸)를 단위 노멀로 푼 값이다. 클리어 값 (0, 0) 은 +z 로 풀려 안 그린 곳도 단위 길이다.
    const float3 normal = PassTexture::sampleNormal( input._uv );

    float  depth         = PassTexture::sampleDepth( input._uv ).r;
    float3 worldPosition = Transform::computeWorldPositionFromDepth( input._uv, depth );

    // 그림자는 월드 위치를 **라이트 클립 공간으로 투영해** 읽는다(화면 UV 로 샘플하면 그늘이 물체를 따라오지 않고 화면에 붙는다).
    // 금속도 · 거칠기는 G버퍼 머티리얼 칸이다(binding.hlsli SurfaceOutput::encodeGbufferMaterial 이 적는다).
    float roughness;
    float metallic;
    bool  bDrawn;
    PassTexture::loadGbufferMaterial( input._uv, roughness, metallic, bDrawn );

    float  shadow = Shadow::sampleAtWorld( worldPosition, normal );
    float3 lit    = Lighting::shadeLights( albedo, worldPosition, normal, shadow, roughness, metallic );

    // 아무것도 안 그린 곳(깊이 원경이고 머티리얼 칸의 알파가 클리어 값 0)은 **쓰지 않고 버린다**. G버퍼가 비어 있어 셰이딩할 표면이
    // 없고, 그대로 계산하면 검은 알베도에 림 라이트만 남아 배경이 이상한 색이 된다.
    // discard 하면 LitColor 는 자기 클리어 색 그대로 남는다 — 포워드의 SceneColor 와 같은 배경이다.
    // "그려졌다" 표시도 보는 이유: 보기 모드 Overdraw 는 깊이를 쓰지 않는다 — 깊이만 보면 겹친 면이 모두 버려진다.
    if ( depth >= 1.0f && bDrawn == false )
        discard;
    // 알베도 알파는 G버퍼 패스가 적은 셰이딩 모델이다(binding.hlsli SW_GBUFFER_SHADING) — 보기 모드 Unlit 의 표면은 알베도 그대로.
    if ( albedoSample.a < 0.5f * ( SW_GBUFFER_SHADING_LIT + SW_GBUFFER_SHADING_UNLIT ) )
        return float4( albedo, 1.0f );

    lit += Lighting::computeRimLight( normal );
    return float4( lit, 1.0f );
}
