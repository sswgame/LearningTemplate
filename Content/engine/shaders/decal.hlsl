/**
 * decal.hlsl — DBuffer 데칼(RenderPassKind::Decal — 언리얼 DBuffer Decal · 유니티 HDRP DBuffer). 깊이 프리패스 뒤 · 베이스 패스 앞에 데칼마다 상자 볼륨의 뒷면을 그려,
 * 장면 깊이에서 월드 위치를 복원하고 데칼 상자(-0.5..0.5) 안의 픽셀만 DBufferA · B · C 에 프리멀티플라이로 쌓는다. 베이스 패스(G버퍼 · 포워드 불투명)가 그 셋을 읽어
 * 셰이딩 전에 표면에 섞는다(material_shader_main.hlsli MaterialSurface::applyDBuffer) — 디퍼드 · 포워드가 같은 데칼을 받는다.
 *
 * DBuffer 인코딩(언리얼과 같다): 칸마다 rgb = 값 × 덮임, a = 덮임. 블렌드는 색 One · InvSrcAlpha, 알파 Zero · InvSrcAlpha(파이프라인 XML)라 쌓인 a 는 "남은 비율" Π(1 - 덮임)이고
 * 클리어 (0, 0, 0, 1)은 덮지 않음이다. 읽는 쪽은 값 × a + rgb 한 줄이다(노멀은 rgb × 2 - (1 - a) 가 덮인 노멀의 합).
 *   A: rgb 데칼 색 · B: rgb 월드 노멀 × 0.5 + 0.5 · C: r 거칠기, g 금속도. 채널 마스크(g_DecalChannels)는 칸마다의 덮임에 곱한다 — 칸이 따로라 쓰기 마스크가 필요 없다.
 *
 * 투영: 상자 로컬 -Y 쪽(상자 축 1 이 위)이다. UV 는 로컬 (x, -z) + 0.5. 상자 행렬은 g_World(행 = 축), 역행렬은 g_DecalWorldToLocal.
 * 면 노멀은 깊이의 화면 미분으로 구한다(G버퍼는 아직 없다). 실루엣 픽셀은 각도 페이드가 흔들린다.
 * 텍스처 번호의 뜻은 MaterialTexture::sampleIndex 와 같다(네이티브 bindless 의 SRV · 에뮬의 머티리얼 칸 0 · 1 — C++ FrameRenderer::prepareFrameDecals). 없으면 흰색이다.
 */
// 머티리얼 텍스처 칸을 빌려 쓰는 비머티리얼 셰이더 — 알베도 0 · 노멀 1(FrameRenderer 데칼 그리기)(웹 에뮬이 이 칸만 남긴다, binding_texture.hlsli SW_MATERIAL_TEXTURE_SLOT_LIMIT).
#if !defined( MATERIAL_TEXTURE_COUNT )
    #define MATERIAL_TEXTURE_COUNT 2
#elif MATERIAL_TEXTURE_COUNT != 2
    #error "MATERIAL_TEXTURE_COUNT from the compile defines differs from this shader's material texture slots"
#endif
#include "binding.hlsli"

static const uint  kDecalChannelAlbedo   = 1u;   // C++ DecalChannels::Albedo
static const uint  kDecalChannelNormal   = 2u;   // C++ DecalChannels::Normal
static const uint  kDecalChannelMaterial = 4u;   // C++ DecalChannels::Material
static const float kDecalEdgeFadeScale   = 8.0f; // 투영 축 끝 1/16 동안 사라진다(상자 위아래 면에서 잘린 자국이 보이지 않게)
static const float kDecalSkyDepth        = 1.0f; // 깊이 클리어 값 — 그린 것이 없는 픽셀

/**
 * 상자 모서리 번호(비트 0 = +x, 1 = +y, 2 = +z) — 면 여섯 × 삼각형 둘. 감김은 바깥에서 볼 때 시계 방향(엔진 앞면 — 왼손 · FrontFace::Clockwise)이라
 * XML 의 컬 Front 가 카메라 쪽 면을 버리고 뒷면만 남긴다. 수는 C++ kDecalBoxVertexCount 와 같다.
 */
static const uint kDecalBoxCorner[36] = { 1, 3, 5, 5, 3, 7,   // +x
                                          0, 4, 2, 4, 6, 2,   // -x
                                          2, 6, 3, 3, 6, 7,   // +y
                                          0, 1, 4, 4, 1, 5,   // -y
                                          4, 5, 6, 6, 5, 7,   // +z
                                          0, 2, 1, 1, 2, 3 }; // -z

struct DecalVSOutput
{
    float4 _position : SV_POSITION;
};

struct DecalOutput
{
    float4 _color : SV_TARGET0;    // DBufferA — rgb 색 × 덮임, a 덮임
    float4 _normal : SV_TARGET1;   // DBufferB — rgb (월드 노멀 × 0.5 + 0.5) × 덮임, a 덮임
    float4 _material : SV_TARGET2; // DBufferC — r 거칠기 × 덮임, g 금속도 × 덮임, a 덮임
};

/** @brief 상자 뒷면 한 정점 — 로컬 모서리(±0.5)를 상자 월드 행렬 g_World 로 옮겨 카메라 클립으로. */
DecalVSOutput VSMain( uint vertexID : SV_VertexID )
{
    const uint    corner = kDecalBoxCorner[vertexID];
    const float3  local  = float3( ( corner & 1u ) != 0u ? 0.5f : -0.5f, ( corner & 2u ) != 0u ? 0.5f : -0.5f, ( corner & 4u ) != 0u ? 0.5f : -0.5f );
    DecalVSOutput output;
    output._position = Transform::computeClipPosition( mul( float4( local, 1.0f ), g_World ), View::getViewProjection() );
    return output;
}

/** @brief 채널 비트가 켜져 있으면 1 입니다. */
float isChannelOn( uint channel )
{
    return ( g_DecalChannels & channel ) != 0u ? 1.0f : 0.0f;
}

DecalOutput PSMain( DecalVSOutput input )
{
    // 뷰 사각형 안의 화면 UV(0..1) — 픽셀 좌표 / 사각형 크기. 깊이 읽기와 위치 복원이 같은 UV 를 받는다(PassTexture 가 풀 UV 로 바꾼다).
    const float2 screenUv    = input._position.xy / g_ViewRectSize.xy;
    const float  deviceDepth = PassTexture::sampleDepthPoint( screenUv ).r;
    const float3 world       = Transform::computeWorldPositionFromDepth( screenUv, deviceDepth );
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

    const float colorCoverage    = coverage * isChannelOn( kDecalChannelAlbedo );
    const float normalCoverage   = coverage * isChannelOn( kDecalChannelNormal );
    const float materialCoverage = coverage * isChannelOn( kDecalChannelMaterial );
    DecalOutput output;
    output._color    = float4( g_DecalColor.rgb * albedo.rgb * colorCoverage, colorCoverage );
    output._normal   = float4( ( decalNormal * 0.5f + 0.5f ) * normalCoverage, normalCoverage );
    output._material = float4( float3( g_DecalSurface.x, g_DecalSurface.y, 0.0f ) * materialCoverage, materialCoverage );
    return output;
}
