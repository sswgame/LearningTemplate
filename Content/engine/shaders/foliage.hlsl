/**
 * foliage.hlsl — 식생(FoliageComponent). GPU 인스턴스마다 바람에 흔들고, 휘게 하는 구(캐릭터)에서 밀려나고, 먼 인스턴스는 뿌리 쪽으로 줄여 사라지게 한다.
 *
 * 머티리얼 셰이더 뼈대(materialshader.hlsli · materialshadermain.hlsli)의 사용자다. 이 파일은 월드 위치 오프셋 · 사용자 보간 칸 · 표면 훅만 정의한다.
 * 위치는 뼈대가 만든다 — 훅은 오프셋(줄이기 + 바람 + 휘기)만 돌려준다(깊이 · 그림자와 같은 위치 함수).
 *
 * 머티리얼은 **정점 셰이더만** 읽는다(GL 은 구조버퍼를 두 단계에서 읽으면 링크를 거절한다) — 색 · 텍스처 인덱스는 사용자 보간 칸으로 넘긴다.
 * 흔들림 무게는 로컬 높이 / swayHeight 의 제곱이라 뿌리는 땅에 붙어 있다. 인스턴스마다 위상은 인스턴스 위치의 해시라 CPU 가 따로 싣지 않는다.
 * 밝기 흔들기는 인스턴스 색 칸(InstanceData::computeTint)이다.
 * 흔드는 머티리얼(MATERIAL_VERTEX_DEFORM — 두 머티리얼이 늘 가진다)의 그림자는 이 파일의 그림자 퍼뮤테이션(SW_PASS_SHADOW)이 같은 흔들림으로 빛 행렬에 투영한다 —
 * 정점 스테이지뿐이다(FrameRendererUtil::drawsDepthWithMaterialShader). 깊이 프리패스에서는 빠진다(뼈대가 머티리얼 없는 사용자의 깊이 프리패스에서 뺀다 —
 * 흔들린 정점이 프리패스 깊이와 어긋나지 않게). 풀(grass.material)은 MATERIAL_SHADOW_CAST_OFF 로 그림자를 끈다(그림자 패스에서 shadowdepth.hlsl 이 뺀다).
 */
#include "lighting.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 잎은 거친 유전체다(PBR 칸이 없다).
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

// foliage.material · grass.material 의 프로퍼티와 같은 이름 · 타입이다.
SW_MATERIAL_BEGIN
{
    float4 _tint;
    float4 _windParams;  // xy = 바람 방향(xz), z = 세기(m), w = 시간(초)
    float4 _windWave;    // x = 흔들림 주파수(Hz), y = 돌풍 세기, z = 돌풍 주파수(Hz), w = 흔들림이 다 차는 높이(m)
    float4 _viewParams;  // xyz = 카메라 위치, w = 휘게 하는 세기
    float4 _fadeParams;  // x = 줄어들기 시작하는 거리, y = 다 사라지는 거리
    float4 _influencer0; // xyz = 중심, w = 반지름(0 이면 빈 칸)
    float4 _influencer1;
    float4 _influencer2;
    float4 _influencer3;
    uint   _albedoMap;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "foliage.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(materialshader.hlsli) — 머티리얼은 정점에서만 읽고 픽셀이 쓸 값(색 · 텍스처 인덱스)은 사용자 보간 칸으로 넘긴다. 조명은 기본 PBR.
#define SW_MATERIAL_READ_IN_VERTEX
#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    float4               _tintedColor : TEXCOORD8; // 정점 색 × 머티리얼 색 × 인스턴스 밝기 × 뿌리 그늘
    nointerpolation uint _albedoMap : TEXCOORD9;   // 머티리얼 텍스처 인덱스(픽셀은 머티리얼을 읽지 않는다)
};
#include "materialshader.hlsli"

/**
 * @brief 위치를 [0, 1) 로 섞는다 — 인스턴스마다 다른 흔들림 위상.
 * @details 정수 해시다. `frac( sin( 큰 수 ) )` 꼴은 백엔드 · 드라이버마다 sin 정밀도가 달라 같은 장면이 백엔드마다 다르게 흔들린다.
 */
float hashPosition( float2 position )
{
    const int2 cell = (int2)floor( position * 8.0f );
    uint       seed = (uint)cell.x * 73856093u ^ (uint)cell.y * 19349663u;
    seed            = ( seed ^ 61u ) ^ ( seed >> 16 );
    seed *= 9u;
    seed = seed ^ ( seed >> 4 );
    seed *= 0x27d4eb2du;
    seed = seed ^ ( seed >> 15 );
    return (float)( seed >> 8 ) * ( 1.0f / 16777216.0f );
}

/** @brief 휘게 하는 구 하나가 이 점을 밀어내는 양(xz)이다. 구 안쪽일수록 세다. */
float2 computeBend( float4 influencer, float3 worldPosition )
{
    if ( influencer.w <= 0.0f )
        return float2( 0.0f, 0.0f );
    const float2 away     = worldPosition.xz - influencer.xz;
    const float  distance = length( away );
    const float  strength = saturate( 1.0f - distance / influencer.w );
    return ( distance > 1e-4f ) ? away / distance * strength : float2( 0.0f, 0.0f );
}

/** @brief 흔들림 무게 — 로컬 높이 / swayHeight(뿌리 0, swayHeight 에서 1). 월드 위치 오프셋과 뿌리 그늘이 같은 값을 쓴다. */
float computeSwayWeight( VertexContext context )
{
    return saturate( context._localPosition.y / max( context._material._windWave.w, 1e-3f ) );
}

/**
 * @brief 월드 위치 오프셋 — 거리 페이드(뿌리 쪽으로 줄이기) + 바람 + 휘게 하는 구. 뼈대가 이 오프셋을 더해 위치를 만든다.
 * @details 줄인 위치 = 줄인 로컬 위치의 월드 변환(Transform::computeWorldPosition)이다. 바람 · 휘기는 그 위에 쌓인다.
 */
float3 computeWorldPositionOffset( VertexContext context )
{
    const MaterialData material = context._material;

    const float3 instancePosition = context._instance._world[3].xyz;
    const float3 toView           = material._viewParams.xyz - instancePosition;
    const float  viewDistance     = length( toView );

    // 거리 페이드 — 페이드 구간에서 인스턴스를 뿌리(로컬 원점) 쪽으로 줄인다. 끝을 넘으면 점 하나가 되어 그려지지 않는다.
    const float fadeRange     = max( material._fadeParams.y - material._fadeParams.x, 1e-3f );
    const float fade          = saturate( ( material._fadeParams.y - viewDistance ) / fadeRange );
    float4      worldPosition = Transform::computeWorldPosition( context._localPosition * fade, context._instance._world );

    // 바람 — 높이 무게(뿌리 0, swayHeight 에서 1) × (흔들림 + 돌풍). 위상은 인스턴스 위치 해시와 바람 방향 거리.
    const float  swayWeight    = computeSwayWeight( context );
    const float  weight        = swayWeight * swayWeight;
    const float2 windDirection = material._windParams.xy;
    const float  time          = material._windParams.w;
    const float  phase         = hashPosition( instancePosition.xz ) * kTwoPi + dot( instancePosition.xz, windDirection ) * 0.15f;
    const float  sway          = sin( time * material._windWave.x * kTwoPi + phase ) * 0.5f + 0.5f;
    const float  gust          = sin( time * material._windWave.z * kTwoPi + dot( instancePosition.xz, windDirection ) * 0.05f ) * 0.5f + 0.5f;
    const float  push          = ( material._windParams.z * sway + material._windWave.y * gust ) * weight;
    worldPosition.xz += windDirection * push;

    // 휘게 하는 구(캐릭터 · 탈것) — 바깥으로 밀고 그만큼 눕힌다.
    float2 bend = computeBend( material._influencer0, worldPosition.xyz ) + computeBend( material._influencer1, worldPosition.xyz ) +
                  computeBend( material._influencer2, worldPosition.xyz ) + computeBend( material._influencer3, worldPosition.xyz );
    bend *= material._viewParams.w * weight;
    worldPosition.xz += bend;
    worldPosition.y -= length( bend ) * 0.5f;

    return worldPosition.xyz - context._worldPosition;
}

MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    // 뿌리는 어둡고 끝은 밝게 — 정점 색 × 머티리얼 색 × 인스턴스 밝기.
    const float      rootShade = lerp( 0.55f, 1.0f, computeSwayWeight( context ) );
    MaterialVaryings varyings;
    varyings._tintedColor = context._input._color * context._material._tint * context._instance.computeTint() * float4( rootShade, rootShade, rootShade, 1.0f );
    varyings._albedoMap   = context._material._albedoMap;
    return varyings;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    MaterialSurface surface;
    surface._albedo    = context._user._tintedColor * MaterialTexture::sampleIndex( context._user._albedoMap, context._uv );
    surface._normal    = context._normal;
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

#include "materialshadermain.hlsli"
