/**
 * terrain.hlsl — 지형(TerrainComponent). 스플랫 맵(RGBA = 레이어 0..3 가중치)으로 레이어 넷을 섞는다.
 *
 * 머티리얼 셰이더 뼈대(materialshader.hlsli · materialshadermain.hlsli)의 사용자다. 이 파일은 표면 훅만 정의한다 — 조명(기본 PBR) · 보기 모드 ·
 * G버퍼 서명 · 정점 변환은 뼈대가 한다.
 *
 * 레이어 하나 = 색(layerNColor) × 디테일 맵의 자기 채널(타일링 무늬, 0.5 가 중립). 디테일은 위에서 내려 찍거나(평면), 세 축에서 찍어
 * 노멀로 섞는다(3 축 — 절벽에서 늘어지지 않는다, layerTriplanar 가 레이어마다 고른다). 가파른 면은 절벽 레이어가 덮는다(terrainParams.zw).
 * 가중치 합을 1 로 나누는 식은 CPU `TerrainHeightfield::normalizeWeights` 와 같다 — 배치의 레이어 필터가 그려진 것과 같은 값을 본다
 * (절벽 덮기는 그림에만 있다 — 배치는 경사 필터로 가린다).
 * 정점 UV 는 지형 정규 좌표(0..1)다. 스플랫 텍셀 중심을 지형 가장자리에 맞추려고 너비로 반 텍셀 옮긴다(CPU 와 같은 규칙).
 */
#include "lighting.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 지형은 거친 유전체다(PBR 칸이 없다).
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

// terrain.material 의 프로퍼티와 같은 이름 · 타입이다.
SW_MATERIAL_BEGIN
{
    float4 _layer0Color;
    float4 _layer1Color;
    float4 _layer2Color;
    float4 _layer3Color;
    float4 _layerDetail;    // 레이어마다 디테일 세기(0..1)
    float4 _layerTriplanar; // 레이어마다 1 이면 3 축 투영
    float4 _terrainParams;  // x = 스플랫 너비(텍셀), y = 디테일 반복(1/m), z = 절벽 문턱(노멀 y), w = 절벽 레이어(−1 = 끔)
    uint   _splatMap;
    uint   _detailMap;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "terrain.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(materialshader.hlsli) — 기본 PBR 조명, 사용자 보간 칸 없음.
#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "materialshader.hlsli"

/** @brief 가중치 넷을 합 1 로 나눈다. 합이 0 이면 레이어 0 이다(C++ TerrainHeightfield::normalizeWeights 와 같다). */
float4 normalizeWeights( float4 weight )
{
    const float total = weight.x + weight.y + weight.z + weight.w;
    return ( total <= 1e-5f ) ? float4( 1.0f, 0.0f, 0.0f, 0.0f ) : weight / total;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const float3 normal   = context._normal;
    MaterialData material = context._material;

    // 스플랫 — 텍셀 중심을 지형 가장자리에 맞춘다.
    const float  splatWidth = material._terrainParams.x;
    const float2 splatUv    = ( splatWidth > 1.0f ) ? context._uv * ( ( splatWidth - 1.0f ) / splatWidth ) + 0.5f / splatWidth : context._uv;
    float4       weight     = ( material._splatMap == kInvalidIndex ) ? float4( 1.0f, 0.0f, 0.0f, 0.0f ) : MaterialTexture::sampleIndex( material._splatMap, splatUv );
    weight                  = normalizeWeights( weight );

    // 절벽 — 노멀 y 가 문턱 아래로 내려갈수록 절벽 레이어가 덮는다.
    const float cliffLayer = material._terrainParams.w;
    if ( cliffLayer >= 0.0f )
    {
        const float  cliff     = saturate( ( material._terrainParams.z - normal.y ) / 0.12f );
        const float4 onlyCliff = float4( cliffLayer < 0.5f ? 1.0f : 0.0f, ( cliffLayer >= 0.5f && cliffLayer < 1.5f ) ? 1.0f : 0.0f,
                                         ( cliffLayer >= 1.5f && cliffLayer < 2.5f ) ? 1.0f : 0.0f, cliffLayer >= 2.5f ? 1.0f : 0.0f );
        weight = lerp( weight, onlyCliff, cliff );
    }

    // 디테일 — 평면(위에서) 하나와 3 축 셋. 평면 표본은 3 축의 xz 면과 같다.
    const float  tiling      = material._terrainParams.y;
    const float4 detailTop   = MaterialTexture::sampleIndex( material._detailMap, context._worldPosition.xz * tiling );
    const float4 detailFront = MaterialTexture::sampleIndex( material._detailMap, context._worldPosition.xy * tiling );
    const float4 detailSide  = MaterialTexture::sampleIndex( material._detailMap, context._worldPosition.zy * tiling );
    float3       axisWeight  = pow( abs( normal ), 4.0f );
    axisWeight /= max( axisWeight.x + axisWeight.y + axisWeight.z, 1e-5f );
    const float4 detailTriplanar = detailSide * axisWeight.x + detailTop * axisWeight.y + detailFront * axisWeight.z;
    const float4 detail          = lerp( detailTop, detailTriplanar, material._layerTriplanar );
    // 0.5 가 중립 — 디테일 세기만큼 색을 0..2 배로 흔든다.
    const float4 shade = lerp( float4( 1.0f, 1.0f, 1.0f, 1.0f ), detail * 2.0f, material._layerDetail );

    float3 albedo = material._layer0Color.rgb * shade.x * weight.x + material._layer1Color.rgb * shade.y * weight.y + material._layer2Color.rgb * shade.z * weight.z +
                    material._layer3Color.rgb * shade.w * weight.w;
    // 큰 무늬 — 같은 디테일 맵을 훨씬 넓게 찍어 타일 반복을 깬다.
    const float macro = MaterialTexture::sampleIndex( material._detailMap, context._worldPosition.xz * tiling * 0.071f ).x;
    albedo *= lerp( 0.85f, 1.12f, macro );

    MaterialSurface surface;
    surface._albedo    = float4( albedo, 1.0f );
    surface._normal    = normal;
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

#include "materialshadermain.hlsli"
