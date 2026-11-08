/**
 * terrain.hlsl — 지형(TerrainComponent). 스플랫 맵 둘(RGBA 한 장에 레이어 넷 — splatMap 은 레이어 0..3, splatMap1 은 4..7)로 레이어 여덟을 섞는다.
 *
 * 머티리얼 셰이더 뼈대(material_shader.hlsli · material_shader_main.hlsli)의 사용자다. 이 파일은 표면 훅만 정의한다 — 조명(기본 PBR) · 보기 모드 ·
 * G버퍼 서명 · 정점 변환은 뼈대가 한다. 섞기 · 규칙 · 날씨 식은 함수 묶음(material_functions.hlsli)의 것이다.
 *
 * 한 점의 순서(언리얼 랜드스케이프 레이어 블렌드 + 오토 머티리얼과 같은 흐름)
 *   1. 칠한 가중치 — 스플랫 둘을 여덟 전체의 합 1 로 나눈다(CPU `SurfaceLayerWeights::computeNormalized` 와 같다).
 *   2. 자동 규칙 — 레이어 순서대로 경사 · 고도 띠가 그 레이어 쪽으로 덮는다(CPU `TerrainHeightfield::computeLayerWeightsAt` 와 같다 — 발소리 · 배치가 같은 땅을 본다).
 *      옛 "절벽 레이어"는 바위 레이어의 경사 규칙(예: 40..90 도)이다.
 *   3. 높이 섞기 — 디테일 맵의 레이어 채널을 높이로 써서 경계에서 높은 레이어(자갈 · 바위)가 먼저 드러난다(`_terrainParams.z` 깊이, 0 = 끔). 텍스처를 읽어야 해서 그림에만 있다.
 *   4. 색 · 거칠기 · 다공성 · 늘 젖음 · 반짝이를 가중치로 섞고, 큰 무늬로 타일 반복을 깬다.
 *   5. 날씨 — 머티리얼 칸 `_weather`(파라미터 묶음 engine/materials/weather.matparams.xml 의 Weather 에 묶인다: x 젖음, y 웅덩이, z 눈, w 설선 m)와
 *      레이어의 늘 젖음(진흙)으로 젖음 · 웅덩이를 얹고, 위를 보는 면에 눈을 쌓는다. 눈과 반짝이 레이어(눈 · 얼음 · 모래)는 작은 거울 칸이 해를 받아 반짝인다.
 *
 * 레이어 하나 = 색(layerNColor) × 디테일 맵의 자기 채널(타일링 무늬, 0.5 가 중립). 디테일은 위에서 내려 찍거나(평면), 세 축에서 찍어
 * 노멀로 섞는다(3 축 — 절벽에서 늘어지지 않는다, layerTriplanar · layerTriplanar1 이 레이어마다 고른다). 레이어 4..7 은 레이어 0..3 의 디테일 채널을
 * 다른 배율(kHighLayerDetailScale)로 다시 찍어 쓴다(디테일 맵 한 장으로 여덟 레이어 — 같은 무늬가 겹쳐 보이지 않게 배율을 어긋낸다).
 * 정점 UV 는 지형 정규 좌표(0..1)다. 스플랫 텍셀 중심을 지형 가장자리에 맞추려고 너비로 반 텍셀 옮긴다(CPU 와 같은 규칙).
 *
 * 지오모프(E4) — 지오모프 머티리얼(terrain_geomorph._material)만 MATERIAL_VERTEX_DEFORM 을 가져 `computeWorldPositionOffset` 이 불린다. 청크가 거친 LOD 로 바뀔 때의 튐을
 * 없애려고 높이를 한 걸음 거친 LOD 메시의 높이로 옮긴다. 섞는 값은 정점 색 · 탄젠트 칸에 CPU(`TerrainMeshBuilder::applyGeomorph`)가 구워 둔다:
 * 색 = (거친 높이 - 지금 높이, 섞기 시작 거리, 섞기 끝 거리, 청크 최소 x), 탄젠트 = (청크 최소 z, 청크 최대 x, 청크 최대 z, 청크 중앙 샘플 높이) - 월드 값이다.
 * 카메라와 청크 사이 거리는 컴포넌트가 LOD 를 고르는 식(청크 상자까지의 평면 거리와 중앙 샘플 높이 차)이고, 섞는 비율은 시작~끝 거리 사이의 0..1 이다.
 * 카메라 위치는 g_InvViewProj 로 구한다 - (0, 0, 1, 0) 의 역투영이 눈의 동차 좌표(원근)다. 직교 카메라와 그림자 패스(빛 행렬)는 눈이 없어 섞지 않는다.
 */
#include "lighting.hlsli"

// 지형은 유전체다(금속 레이어가 없다 — 웅덩이 · 눈도 금속도를 0 으로 둔다).
static const float kSurfaceMetallic = 0.0f;
// 레이어 4..7 의 디테일을 찍는 배율(레이어 0..3 의 무늬와 겹쳐 보이지 않게 어긋낸 값).
static const float kHighLayerDetailScale = 1.37f;
// 디테일의 3 축 날카로움 — C++ MaterialProjection 테스트의 지형 값(4)과 같다.
static const float kDetailTriplanarSharpness = 4.0f;
// 레이어의 늘 젖음이 웅덩이로 번지는 비율(진흙 1 이면 웅덩이 양 0.6).
static const float kLayerWetnessToPuddle = 0.6f;

// terrain._material 의 프로퍼티와 같은 이름 · 타입이다. 컴포넌트(TerrainComponent::acquireMaterial)가 레이어 데이터로 채운다.
SW_MATERIAL_BEGIN
{
    float4 _layer0Color;
    float4 _layer1Color;
    float4 _layer2Color;
    float4 _layer3Color;
    float4 _layer4Color;
    float4 _layer5Color;
    float4 _layer6Color;
    float4 _layer7Color;
    float4 _layer0Rule; // 레이어마다의 자동 규칙 띠: x · y = 경사(도) 아래 · 위, z · w = 높이(m) 아래 · 위
    float4 _layer1Rule;
    float4 _layer2Rule;
    float4 _layer3Rule;
    float4 _layer4Rule;
    float4 _layer5Rule;
    float4 _layer6Rule;
    float4 _layer7Rule;
    float4 _layer0Params; // x = 경사 페이드(도), y = 높이 페이드(m), z = 규칙 세기(0 = 끔), w = 반짝이(칸 비율 0..1)
    float4 _layer1Params;
    float4 _layer2Params;
    float4 _layer3Params;
    float4 _layer4Params;
    float4 _layer5Params;
    float4 _layer6Params;
    float4 _layer7Params;
    float4 _layerDetail;     // 레이어 0..3 의 디테일 세기(0..1)
    float4 _layerTriplanar;  // 레이어 0..3 마다 1 이면 3 축 투영
    float4 _layerDetail1;    // 레이어 4..7 의 디테일 세기(0..1)
    float4 _layerTriplanar1; // 레이어 4..7 마다 1 이면 3 축 투영
    float4 _layerRoughness;  // 레이어 0..3 의 거칠기
    float4 _layerRoughness1; // 레이어 4..7 의 거칠기
    float4 _layerPorosity;   // 레이어 0..3 의 다공성(젖으면 어두워지는 정도, 흙 1 · 바위 0.3)
    float4 _layerPorosity1;  // 레이어 4..7 의 다공성
    float4 _layerWetness;    // 레이어 0..3 의 늘 젖음(진흙 · 물가 — 날씨와 무관하게 젖어 웅덩이가 진다)
    float4 _layerWetness1;   // 레이어 4..7 의 늘 젖음
    float4 _terrainParams;   // x = 스플랫 너비(텍셀), y = 디테일 반복(1/m), z = 높이 섞기 깊이(0 = 끔), w = 0(예비)
    float4 _terrainSurface;  // x = 웅덩이 노이즈 크기(m), y = 반짝이 칸 밀도(칸/m), z = 날씨 눈의 반짝이(칸 비율), w = 0(예비)
    float4 _weather;         // 파라미터 묶음 Weather: x 젖음, y 웅덩이, z 눈, w 설선 높이(m)
    uint   _splatMap;        // 레이어 0..3 가중치(RGBA)
    uint   _splatMap1;       // 레이어 4..7 가중치(RGBA)
    uint   _detailMap;
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::checkStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "terrain.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(material_shader.hlsli) — 기본 PBR 조명, 사용자 보간 칸 없음.
#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "material_shader.hlsli"
#include "material_functions.hlsli"

/** @brief 가중치 여덟(낮은 넷 · 높은 넷)을 여덟 전체의 합 1 로 나눈다. 합이 0 이면 레이어 0 이다(C++ SurfaceLayerWeights::computeNormalized 와 같다). */
void normalizeWeights( inout float4 inoutLow, inout float4 inoutHigh )
{
    const float total  = inoutLow.x + inoutLow.y + inoutLow.z + inoutLow.w + inoutHigh.x + inoutHigh.y + inoutHigh.z + inoutHigh.w;
    const bool  bEmpty = total <= 1e-5f;
    inoutLow           = bEmpty ? float4( 1.0f, 0.0f, 0.0f, 0.0f ) : inoutLow / max( total, 1e-5f );
    inoutHigh          = bEmpty ? float4( 0.0f, 0.0f, 0.0f, 0.0f ) : inoutHigh / max( total, 1e-5f );
}

/** @brief 레이어 @p layer 가 @p firstLayer .. +3 의 어느 칸인지 원-핫 넷으로 돌려준다(규칙이 덮는 쪽). */
float4 oneHotLayer( float layer, float firstLayer )
{
    const float offset = layer - firstLayer;
    return float4( abs( offset ) < 0.5f ? 1.0f : 0.0f, abs( offset - 1.0f ) < 0.5f ? 1.0f : 0.0f, abs( offset - 2.0f ) < 0.5f ? 1.0f : 0.0f, abs( offset - 3.0f ) < 0.5f ? 1.0f : 0.0f );
}

/**
 * @brief 레이어 @p layer 의 자동 규칙을 덮는다 — 덮는 정도 c = 세기 × 경사 띠 × 고도 띠, 가중치 = lerp( 가중치, 원-핫, c ).
 * @details C++ `TerrainLayerRule::computeCoverage` + `SurfaceLayerWeights::computeCovered` 와 같은 식이다(세기 0 이면 c = 0 이라 그대로).
 */
void applyLayerRule( inout float4 inoutLow, inout float4 inoutHigh, float layer, float4 rule, float4 params, float slopeDegrees, float altitude )
{
    const float slope    = SurfaceFunction::computeBand( slopeDegrees, rule.x, rule.y, params.x );
    const float height   = SurfaceFunction::computeBand( altitude, rule.z, rule.w, params.y );
    const float coverage = saturate( params.z ) * slope * height;
    inoutLow             = lerp( inoutLow, oneHotLayer( layer, 0.0f ), coverage );
    inoutHigh            = lerp( inoutHigh, oneHotLayer( layer, 4.0f ), coverage );
}

/**
 * @brief 디테일 맵의 레이어 채널 넷을 찍는다 - 평면(위에서) 하나와 3 축 셋을 @p triplanar 마스크로 섞는다. 평면 표본은 3 축의 xz 면과 같다.
 * @param scale 월드 좌표에 곱하는 배율(레이어 0..3 은 1, 4..7 은 kHighLayerDetailScale)
 */
float4 sampleLayerDetail( uint detailMap, float3 worldPosition, float3 axisWeight, float tiling, float scale, float4 triplanar )
{
    const float4 detailTop       = MaterialTexture::sampleIndex( detailMap, worldPosition.xz * tiling * scale );
    const float4 detailTriplanar = SurfaceFunction::sampleTriplanar( detailMap, worldPosition, axisWeight, tiling * scale );
    return lerp( detailTop, detailTriplanar, triplanar );
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const float3 normal   = context._normal;
    const float3 position = context._worldPosition;
    MaterialData material = context._material;

    // 1. 칠한 가중치 — 텍셀 중심을 지형 가장자리에 맞춘다.
    const float  splatWidth = material._terrainParams.x;
    const float2 splatUv    = ( splatWidth > 1.0f ) ? context._uv * ( ( splatWidth - 1.0f ) / splatWidth ) + 0.5f / splatWidth : context._uv;
    float4       weightLow  = ( material._splatMap == kInvalidIndex ) ? float4( 1.0f, 0.0f, 0.0f, 0.0f ) : MaterialTexture::sampleIndex( material._splatMap, splatUv );
    float4       weightHigh = ( material._splatMap1 == kInvalidIndex ) ? float4( 0.0f, 0.0f, 0.0f, 0.0f ) : MaterialTexture::sampleIndex( material._splatMap1, splatUv );
    normalizeWeights( weightLow, weightHigh );

    // 2. 자동 규칙 — 레이어 순서대로(뒤 레이어가 앞을 덮는다).
    const float slopeDegrees = SurfaceFunction::computeSlopeDegrees( normal.y );
    applyLayerRule( weightLow, weightHigh, 0.0f, material._layer0Rule, material._layer0Params, slopeDegrees, position.y );
    applyLayerRule( weightLow, weightHigh, 1.0f, material._layer1Rule, material._layer1Params, slopeDegrees, position.y );
    applyLayerRule( weightLow, weightHigh, 2.0f, material._layer2Rule, material._layer2Params, slopeDegrees, position.y );
    applyLayerRule( weightLow, weightHigh, 3.0f, material._layer3Rule, material._layer3Params, slopeDegrees, position.y );
    applyLayerRule( weightLow, weightHigh, 4.0f, material._layer4Rule, material._layer4Params, slopeDegrees, position.y );
    applyLayerRule( weightLow, weightHigh, 5.0f, material._layer5Rule, material._layer5Params, slopeDegrees, position.y );
    applyLayerRule( weightLow, weightHigh, 6.0f, material._layer6Rule, material._layer6Params, slopeDegrees, position.y );
    applyLayerRule( weightLow, weightHigh, 7.0f, material._layer7Rule, material._layer7Params, slopeDegrees, position.y );

    // 디테일 — 평면(위에서) 하나와 3 축 셋. 평면 표본은 3 축의 xz 면과 같다.
    const float  tiling     = material._terrainParams.y;
    const float3 axisWeight = SurfaceFunction::computeTriplanarWeights( normal, kDetailTriplanarSharpness );
    const float4 detailLow  = sampleLayerDetail( material._detailMap, position, axisWeight, tiling, 1.0f, material._layerTriplanar );
    const float4 detailHigh = sampleLayerDetail( material._detailMap, position, axisWeight, tiling, kHighLayerDetailScale, material._layerTriplanar1 );

    // 3. 높이 섞기 — 디테일 채널이 레이어의 높이다(깊이 0 이면 그대로).
    SurfaceFunction::blendByHeight8( weightLow, weightHigh, detailLow, detailHigh, material._terrainParams.z );

    // 4. 레이어 값 섞기. 0.5 가 중립 — 디테일 세기만큼 색을 0..2 배로 흔든다.
    const float4 shadeLow  = lerp( float4( 1.0f, 1.0f, 1.0f, 1.0f ), detailLow * 2.0f, material._layerDetail );
    const float4 shadeHigh = lerp( float4( 1.0f, 1.0f, 1.0f, 1.0f ), detailHigh * 2.0f, material._layerDetail1 );
    float3       albedo    = material._layer0Color.rgb * shadeLow.x * weightLow.x + material._layer1Color.rgb * shadeLow.y * weightLow.y + material._layer2Color.rgb * shadeLow.z * weightLow.z +
                    material._layer3Color.rgb * shadeLow.w * weightLow.w + material._layer4Color.rgb * shadeHigh.x * weightHigh.x + material._layer5Color.rgb * shadeHigh.y * weightHigh.y +
                    material._layer6Color.rgb * shadeHigh.z * weightHigh.z + material._layer7Color.rgb * shadeHigh.w * weightHigh.w;
    float        roughness    = dot( weightLow, material._layerRoughness ) + dot( weightHigh, material._layerRoughness1 );
    const float  porosity     = dot( weightLow, material._layerPorosity ) + dot( weightHigh, material._layerPorosity1 );
    const float  layerWet     = dot( weightLow, material._layerWetness ) + dot( weightHigh, material._layerWetness1 );
    const float4 sparkleLow   = float4( material._layer0Params.w, material._layer1Params.w, material._layer2Params.w, material._layer3Params.w );
    const float4 sparkleHigh  = float4( material._layer4Params.w, material._layer5Params.w, material._layer6Params.w, material._layer7Params.w );
    const float  layerSparkle = dot( weightLow, sparkleLow ) + dot( weightHigh, sparkleHigh );
    // 큰 무늬 — 같은 디테일 맵을 훨씬 넓게 찍어 타일 반복을 깬다.
    const float macro = MaterialTexture::sampleIndex( material._detailMap, position.xz * tiling * 0.071f ).x;
    albedo *= lerp( 0.85f, 1.12f, macro );

    // 5. 날씨 — 젖음(날씨 · 레이어의 늘 젖음) → 웅덩이 → 눈 → 반짝이.
    const float4 weather       = material._weather;
    float3       surfaceNormal = normal;
    float        metallic      = kSurfaceMetallic;
    SurfaceFunction::applyWetness( albedo, roughness, porosity, max( weather.x, layerWet ) );
    const float puddle = SurfaceFunction::computePuddleMask( position, normal, max( weather.y, layerWet * kLayerWetnessToPuddle ), material._terrainSurface.x );
    SurfaceFunction::applyPuddle( albedo, roughness, metallic, surfaceNormal, puddle );
    const float snow = SurfaceFunction::computeSnowMask( position, normal, weather.z, weather.w );
    SurfaceFunction::applySnow( albedo, roughness, metallic, surfaceNormal, snow );
    SurfaceFunction::applySparkle( roughness, surfaceNormal, position, material._terrainSurface.y, max( layerSparkle * ( 1.0f - puddle ), snow * material._terrainSurface.z ) );

    MaterialSurface surface;
    surface._albedo    = float4( albedo, 1.0f );
    surface._normal    = surfaceNormal;
    surface._roughness = saturate( roughness );
    surface._metallic  = metallic;
    return surface;
}

#if defined( MATERIAL_VERTEX_DEFORM )
    #if defined( SW_PASS_SHADOW )
/** @brief 그림자 패스는 지오모프를 섞지 않는다(빛의 시점이라 눈 거리가 뜻이 없다) — 정의를 파일 수준에서 고른다. */
float computeGeomorphBlend( VertexContext context )
{
    return 0.0f;
}
    #else
/** @brief 눈과 청크 상자의 거리로 정한 지오모프 섞기 비율(0..1)입니다. 값의 배치는 파일 머리 주석. 이른 반환 없이 출구가 하나다. */
float computeGeomorphBlend( VertexContext context )
{
    float        morph          = 0.0f;
    const float4 eyeHomogeneous = mul( float4( 0.0f, 0.0f, 1.0f, 0.0f ), g_InvViewProj );
    if ( abs( eyeHomogeneous.w ) > 1.0e-6f )
    {
        const float3 eye      = eyeHomogeneous.xyz / eyeHomogeneous.w;
        const float4 bake     = context._input._color;
        const float4 bounds   = context._input._tangent;
        const float  deltaX   = max( max( bake.w - eye.x, eye.x - bounds.y ), 0.0f );
        const float  deltaZ   = max( max( bounds.x - eye.z, eye.z - bounds.z ), 0.0f );
        const float  deltaY   = eye.y - bounds.w;
        const float  distance = sqrt( deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ );
        morph                 = saturate( ( distance - bake.y ) / max( bake.z - bake.y, 1.0e-3f ) );
    }
    return morph;
}
    #endif

/**
 * @brief 지오모프 — 정점 높이를 거친 LOD 높이로 옮기는 월드 위치 오프셋(y 만)입니다.
 */
float3 computeWorldPositionOffset( VertexContext context )
{
    return float3( 0.0f, context._input._color.x * computeGeomorphBlend( context ), 0.0f );
}
#endif

#include "material_shader_main.hlsli"
