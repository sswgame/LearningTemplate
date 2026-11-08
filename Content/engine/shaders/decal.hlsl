/**
 * decal.hlsl — 디퍼드 데칼(RenderPassType::Decal). 데칼마다 풀스크린 삼각형 하나로 장면 깊이에서 월드 위치를 복원해 데칼 상자(-0.5..0.5) 안의 픽셀만
 * G버퍼 알베도 · 노멀 · 머티리얼 칸에 곧은 알파로 섞는다(언리얼 Deferred Decal · 유니티 HDRP Decal Projector 의 G버퍼 덮기).
 *
 * 투영: 상자 로컬 -Y 쪽(상자 축 1 이 위)이다. UV 는 로컬 (x, -z) + 0.5. 상자 행렬은 g_World(행 = 축), 역행렬은 g_DecalWorldToLocal.
 * 칸마다의 덮임은 출력 알파(채널 마스크 g_DecalChannels × 불투명도 × 텍스처 덮임 × 각도 · 끝 페이드)이고, 덮지 않는 채널은 파이프라인 XML 의 쓰기 마스크가 막는다
 * (알베도 알파 = 셰이딩 모델 번호, 머티리얼 b · a = 반사율 · 가림). 노멀은 옥타헤드럴 좌표를 섞는다 — shortcut: 각이 크게 다른 노멀 사이는 조금 휜다(언리얼 DBuffer 는 노멀을 따로 담는다),
 * 데칼 노멀이 아래 면과 크게 다른 일이 잦아지면 섞기 전에 단위 벡터로 풀어 섞는다(G버퍼를 읽어야 해 사본이 든다).
 * 면 노멀은 깊이의 화면 미분으로 구한다 — G버퍼 노멀은 이 패스가 쓰는 타깃이라 읽을 수 없다. 실루엣 픽셀은 각도 페이드가 흔들린다.
 * 텍스처 번호의 뜻은 MaterialTexture::sampleIndex 와 같다(네이티브 bindless 의 SRV · 에뮬의 머티리얼 칸 0 · 1 — C++ FrameRenderer::prepareFrameDecals). 없으면 흰색이다.
 */
#include "binding.hlsli"
#include "fullscreen.hlsli"

static const uint  kDecalChannelAlbedo   = 1u;   // C++ DecalChannels::Albedo
static const uint  kDecalChannelNormal   = 2u;   // C++ DecalChannels::Normal
static const uint  kDecalChannelMaterial = 4u;   // C++ DecalChannels::Material
static const float kDecalEdgeFadeScale   = 8.0f; // 투영 축 끝 1/16 동안 사라진다(상자 위아래 면에서 잘린 자국이 보이지 않게)
static const float kDecalSkyDepth        = 1.0f; // 깊이 클리어 값 — 그린 것이 없는 픽셀

struct DecalOutput
{
    float4 _albedo : SV_TARGET0;   // rgb 데칼 색, a 덮임(곧은 알파)
    float4 _normal : SV_TARGET1;   // xy 옥타헤드럴 노멀, a 덮임
    float4 _material : SV_TARGET2; // r 금속도, g 거칠기, a 덮임
};

/** @brief 채널 비트가 켜져 있으면 1 입니다. */
float isChannelOn( uint channel )
{
    return ( g_DecalChannels & channel ) != 0u ? 1.0f : 0.0f;
}

DecalOutput PSMain( PSInput input )
{
    const float  deviceDepth = PassTexture::sampleDepthPoint( input._uv ).r;
    const float3 world       = Transform::computeWorldPositionFromDepth( input._uv, deviceDepth );
    const float3 local       = mul( float4( world, 1.0f ), g_DecalWorldToLocal ).xyz;
    const float3 insideAxes  = step( abs( local ), float3( 0.5f, 0.5f, 0.5f ) );
    const float  inside      = insideAxes.x * insideAxes.y * insideAxes.z * ( deviceDepth < kDecalSkyDepth ? 1.0f : 0.0f );

    // 면 노멀(깊이의 화면 미분) — 눈 쪽을 보게 뒤집는다. 각도 페이드는 투영 축(상자 축 1)과의 cos 다.
    const float3 axisX         = normalize( g_World[0].xyz );
    const float3 axisY         = normalize( g_World[1].xyz );
    const float3 axisZ         = normalize( g_World[2].xyz );
    const float3 faceNormalRaw = normalize( cross( ddy( world ), ddx( world ) ) );
    const float3 faceNormal    = dot( faceNormalRaw, View::computeToView( world ) ) < 0.0f ? -faceNormalRaw : faceNormalRaw;
    const float  angleFade     = saturate( ( dot( faceNormal, axisY ) - g_DecalSurface.w ) / max( 1.0f - g_DecalSurface.w, 1e-3f ) );
    const float  edgeFade      = saturate( ( 0.5f - abs( local.y ) ) * kDecalEdgeFadeScale );

    const float2 decalUv  = float2( local.x + 0.5f, 0.5f - local.z );
    const float4 albedo   = MaterialTexture::sampleIndex( g_DecalAlbedoTexture, decalUv );
    const float  coverage = saturate( albedo.a * g_DecalColor.a ) * angleFade * edgeFade * inside;

    // 탄젠트 공간 노멀 — 데칼 X 가 u, -Z 가 v(UV 의 v 가 -Z 로 는다), 투영 축이 노멀이다. 노멀 텍스처가 없으면 C++ 가 노멀 칸을 껐다(흰색을 노멀로 읽지 않는다).
    float3 tangentNormal = float3( MaterialTexture::sampleIndex( g_DecalNormalTexture, decalUv ).xy * 2.0f - 1.0f, 0.0f );
    tangentNormal.xy *= g_DecalSurface.z;
    tangentNormal.z          = sqrt( saturate( 1.0f - dot( tangentNormal.xy, tangentNormal.xy ) ) );
    const float3 decalNormal = normalize( axisX * tangentNormal.x - axisZ * tangentNormal.y + axisY * tangentNormal.z );

    DecalOutput output;
    output._albedo   = float4( g_DecalColor.rgb * albedo.rgb, coverage * isChannelOn( kDecalChannelAlbedo ) );
    output._normal   = float4( Octahedral::encode( decalNormal ), 0.0f, coverage * isChannelOn( kDecalChannelNormal ) );
    output._material = float4( g_DecalSurface.y, g_DecalSurface.x, 0.0f, coverage * isChannelOn( kDecalChannelMaterial ) );
    return output;
}
