/**
 * meshmodifier.hlsli — 메시 모디파이어 스택의 GPU 실행기(uber). meshmorph.hlsl 이 include 한다.
 *
 * CPU 정본은 확장 모듈 `GF_EditorPlugIn_MeshModifiers`(Source/GameFramework/PlugIn/Editor/MeshModifiers)다. 여기 함수는 그 `deformPoint` 를
 * 한 줄씩 옮긴 것이고, 결과가 CPU 와 같은지는 MeshModifierGPUTest.ProgramMatchesCPU 가 네 백엔드에서 그림으로 본다.
 * 모디파이어마다 셰이더를 만들지 않고 종류 번호 + 매개변수 목록을 차례로 건다(uber 셰이더 하나).
 *
 * [프로그램 모양] float4 목록(`Mesh::setGPUDeformProgram`). 정수도 float 값으로 담는다(2^24 미만 정확, 비트 재해석은 비정규 수로 지워질 수 있다).
 *   머리   [0] x = 모디파이어 수, y = 시간(초), z = 정점 UV 표 시작(프로그램 시작 기준 float4 번호 · 없으면 0), w = UV 표의 정점 수
 *   기록마다 SW_MODIFIER_RECORD_FLOAT4 개 + 매개변수:
 *     [0]  x = 종류 번호(아래 SW_MODIFIER_*), y = 매개변수 float4 수, z = 주축(0 X · 1 Y · 2 Z), w = 야코비안 걸음
 *     [1]  xyz = 입력 상자 최소(모디파이어 공간), w = 상자가 유효하면 1
 *     [2]  xyz = 입력 상자 최대
 *     [3..6]   로컬 → 모디파이어 행렬의 행(행 벡터 규약 v·M, 행 3 이 평행 이동)
 *     [7..10]  모디파이어 → 로컬
 *     [11..13] 로컬 → 모디파이어 법선 행렬(역전치)의 행 0..2
 *     [14..16] 모디파이어 → 로컬 법선 행렬의 행 0..2
 *     그 뒤 매개변수: 선언 순서대로 스칼라는 x, Vector3 는 xyz(`MeshModifier::packGPUParams` 기본 배치)
 *       곡선(Curve)은 [0,1] 을 SW_MODIFIER_CURVE_SAMPLES 등분한 표본이 float4 SW_MODIFIER_CURVE_SAMPLES/4 개에 들어간다(`ModifierContext::curve` 가 선형 보간)
 *       수 목록(NumberList)은 고정 매개변수가 모두 끝난 뒤에 싣는다(길이가 가변이라 앞에 두면 뒤 매개변수 번호가 밀린다): float4(항목 수, 항목 원소 수) 뒤에 원소가 float4 하나에 넷씩
 *         평탄하게 이어진다(`ModifierContext::listCount` · `listValue`)
 *       그 뒤(수 목록 다음)가 외부 입력 블록 - 스플라인 표본 · 텍스처 격자 · 격자 제어점 · 층 곡선(`MeshModifier::packGPUInputs`). 모양은 모디파이어마다 다르고(아래 각 함수 주석)
 *         첫 float4 의 x 가 "입력을 얻었다 1 · 못 얻었다 0" 인 것은 못 얻으면 항등으로 둔다. 별도 GPU 버퍼가 아니라 같은 프로그램 목록 안이라 바인딩이 늘지 않는다
 *   기록 [2].w = 이 기록에 들어오는 정점별 선택 가중치 표의 위치(기록 시작부터의 float4 수, 0 이면 가중치 없음 = 모두 1). 표는 매개변수 영역 끝에 float4 하나에 넷씩 정점 순서로 있고 셰이더가 변형 결과를 그 세기로 섞는다
 *   묶음(Deformable · World Warp)은 기록이 없다 - 하위 모디파이어의 기록이 같은 자리에 이어지고 행렬에 묶음의 공간 변환이 곱해져 있다(C++ 쪽이 만든다)
 *   끝     정점 UV 표(텍스처 모디파이어가 있을 때만): float4 하나에 정점 둘의 UV(xy · zw). 정점 번호는 메시 안 번호다 - 컴퓨트의 풀 정점 번호에서 칸 표 y(구간 시작)를 뺀 값
 *
 * 주의: 함수는 한 출구로 쓴다(early return 없음). OpenGL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다(RHIBackendInfo.h).
 */
#ifndef SW_ENGINE_MESHMODIFIER_HLSLI
#define SW_ENGINE_MESHMODIFIER_HLSLI

#define SW_MODIFIER_RECORD_FLOAT4 17
// 텍스처 입력의 격자 한 변 텍셀 수 — C++ `MeshModifier::kGPUTextureResolution` 와 같다(4 의 배수)
#define SW_MODIFIER_TEXTURE_RESOLUTION 64
// 스플라인 입력의 표본 구간 수 — C++ `MeshModifier::kGPUPathSegmentCount` 와 같다(표본은 하나 더, 하나에 float4 셋)
#define SW_MODIFIER_PATH_SEGMENTS 128
// 곡선 매개변수의 표본 수 — C++ `MeshModifier::kGPUCurveSampleCount` 와 같다(4 의 배수)
#define SW_MODIFIER_CURVE_SAMPLES 32

// 종류 번호 — C++ 표 `MeshModifierGPUProgram.cpp` 의 순서와 같다(테스트가 supportsGPU 집합과 맞는지 본다)
#define SW_MODIFIER_BEND                 0
#define SW_MODIFIER_TWIST                1
#define SW_MODIFIER_TAPER                2
#define SW_MODIFIER_STRETCH              3
#define SW_MODIFIER_WAVE                 4
#define SW_MODIFIER_RIPPLE               5
#define SW_MODIFIER_NOISE                6
#define SW_MODIFIER_MELT                 7
#define SW_MODIFIER_PUSH                 8
#define SW_MODIFIER_POKE                 9
#define SW_MODIFIER_DISPLACE             10
#define SW_MODIFIER_BUBBLE               11
#define SW_MODIFIER_BULGE                12
#define SW_MODIFIER_CYLINDRIFY           13
#define SW_MODIFIER_GLOBE                14
#define SW_MODIFIER_HUMP                 15
#define SW_MODIFIER_RADIALSKEW           16
#define SW_MODIFIER_SKEW                 17
#define SW_MODIFIER_SPHERIFY             18
#define SW_MODIFIER_SQUEEZE              19
#define SW_MODIFIER_SCALE                20
#define SW_MODIFIER_SINUSCURVE           21
#define SW_MODIFIER_ROLLED               22
#define SW_MODIFIER_SIMPLEMOD            23
#define SW_MODIFIER_CRUMPLE              24
#define SW_MODIFIER_DYNAMICRIPPLE        25
#define SW_MODIFIER_PAGEFLIP             26
#define SW_MODIFIER_TREEBEND             27
#define SW_MODIFIER_VERTNOISE            28
#define SW_MODIFIER_ZSTRETCHNOISE        29
#define SW_MODIFIER_WAVING               30
#define SW_MODIFIER_PIVOTADJUST          31
#define SW_MODIFIER_DISPLACELIMITS       32
#define SW_MODIFIER_BEZIERATTRACTOR      33
#define SW_MODIFIER_RUBBER               34
#define SW_MODIFIER_CURVEDEFORM          35
#define SW_MODIFIER_CURVESCULPT          36
#define SW_MODIFIER_CURVESCULPTLAYERED   37
#define SW_MODIFIER_ROPEDEFORM           38
#define SW_MODIFIER_PATHDEFORM           39
#define SW_MODIFIER_WORLDPATHDEFORM      40
#define SW_MODIFIER_DISPLACERENDERTARGET 41
#define SW_MODIFIER_DISPLACEWEBCAM       42
#define SW_MODIFIER_LATTICE              43
#define SW_MODIFIER_FFD2                 44
#define SW_MODIFIER_FFD3                 45
#define SW_MODIFIER_FFD4                 46
#define SW_MODIFIER_FFD2D                47
#define SW_MODIFIER_COLLISIONDEFORM      48
#define SW_MODIFIER_HITDEFORM            49
#define SW_MODIFIER_CONFORM              50
#define SW_MODIFIER_CONFORMMULTI         51
#define SW_MODIFIER_POINTCACHE           52
#define SW_MODIFIER_VERTEXANIM           53
#define SW_MODIFIER_PAINT                54

static const float kModifierEpsilon = 1e-6f;
// 원주율은 common.hlsli 의 kPi · kTwoPi 를 쓴다(meshmorph.hlsl 이 common.hlsli 를 먼저 포함한다).
static const float kDegToRad = kPi / 180.0f;

struct ModifierContext
{
    float3 _boundsMin;
    float3 _boundsMax;
    float  _boundsValid; // 1 이면 유효
    uint   _axis;
    float  _time;
    uint   _paramBase;   // g_DeformPrograms 안 매개변수 시작 원소
    uint   _vertexIndex; // 이 메시 안의 정점 번호(정점별 위상 · 정점 단위 표)
    float2 _uv;          // 이 정점의 UV(프로그램 끝 UV 표 — 표가 없으면 0)

    float param( uint index );
    float3 param3( uint index );
    int paramInt( uint index );
    uint paramAxis( uint index );
    float axisLength();
    float curve( uint index, float x );
    float4 raw( uint index );
    uint listCount( uint listBase );
    float listValue( uint listBase, uint flat );
    float3 listVec3( uint listBase, uint flat );
    float textureGrid( uint gridBase, float2 uv );
};

/** @brief 모디파이어 프로그램의 점 변형 — C++ 모디파이어(`deformPoint`)를 한 줄씩 옮긴 것이다. 진입은 applyProgram 하나다. */
struct MeshDeform
{
    static float get( float3 value, uint axis );
    static float3 set( float3 value, uint axis, float component );
    static float3 normalizeOr( float3 value, float3 fallback );
    static float axisFraction( float3 position, ModifierContext context );
    static uint hashLattice( int cellX, int cellY, int cellZ, uint seed );
    static float latticeValue( int cellX, int cellY, int cellZ, uint seed );
    static float smooth( float ratio );
    static float valueNoise3D( float3 position, uint seed );
    static float3 bendLike( float3 location, ModifierContext context, float angle, float direction, float lowerParam, float upperParam );
    static float3 bend( float3 location, ModifierContext context );
    static float3 twist( float3 location, ModifierContext context );
    static float3 taper( float3 location, ModifierContext context );
    static float3 stretch( float3 location, ModifierContext context );
    static float3 wave( float3 location, ModifierContext context, bool bRadial );
    static float3 noise( float3 location, ModifierContext context );
    static float3 melt( float3 location, ModifierContext context );
    static float3 poke( float3 location, ModifierContext context );
    static float3 displace( float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 bubble( float3 location, ModifierContext context );
    static float3 bulge( float3 location, ModifierContext context );
    static float3 toRadius( float3 location, ModifierContext context, bool bCylinder );
    static float3 hump( float3 location, ModifierContext context );
    static float3 radialSkew( float3 location, ModifierContext context );
    static float3 shiftSide( float3 location, ModifierContext context, float shift, float direction );
    static float3 skew( float3 location, ModifierContext context );
    static float3 spherify( float3 location, ModifierContext context );
    static float3 squeeze( float3 location, ModifierContext context );
    static float3 sinusCurve( float3 location, ModifierContext context );
    static float3 rolled( float3 location, ModifierContext context );
    static float3 crumple( float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 dynamicRipple( float3 location, ModifierContext context );
    static float3 pageFlip( float3 location, ModifierContext context );
    static float3 treeBend( float3 location, ModifierContext context );
    static float3 vertNoise( float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 zstretchNoise( float3 location, ModifierContext context );
    static float3 waving( float3 location, ModifierContext context );
    static float3 displaceLimits( float3 location, ModifierContext context );
    static float3 bezierAttractor( float3 location, ModifierContext context );
    static float3 rubber( float3 location, ModifierContext context );
    static float3 curveDeform( float3 location, ModifierContext context );
    static float3 curveSculpt( float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 curveSculptLayered( float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 placeOnPath( ModifierContext context, uint pathBase, float distance, bool bLoop, float coordU, float coordV );
    static float3 ropeDeform( float3 location, ModifierContext context );
    static float3 pathDeform( float3 location, ModifierContext context, bool bWorld );
    static float3 displaceByGrid( float3 location, float3 surfaceNormal, ModifierContext context, float amount, float midLevel, float2 uv, uint headerParam );
    static void bernstein( uint degree, float t, out float outWeight[8] );
    static float3 lattice( float3 location, ModifierContext context );
    static float3 ffd2D( float3 location, ModifierContext context );
    static float3 pushOutOfSphere( float3 pos, float3 center, float radius );
    static float3 pushOutOfBox( float3 pos, float3 center, float3 halfSize );
    static float3 pushOutOfPlane( float3 pos, float3 center, float3 normalDirection, float margin );
    static float3 collisionDeform( float3 location, ModifierContext context );
    static float3 hitDeform( float3 location, ModifierContext context );
    static float hashToUnit( uint value );
    static float valueNoise1D( uint seed, float time );
    static float3 vertexAnim( float3 location, ModifierContext context );
    static float3 pointCache( float3 location, ModifierContext context );
    static float3 closestOnTriangle( float3 pos, float3 a, float3 b, float3 c );
    static bool intersectRayTriangle( float3 origin, float3 direction, float3 a, float3 b, float3 c, out float outDistance );
    static float pointBoxDistanceSq( float3 pos, float3 boxMin, float3 boxMax );
    static bool rayHitsBox( float3 origin, float3 direction, float3 boxMin, float3 boxMax, float maxDistance );
    static float3 conform( float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 deformPoint( uint kind, float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 transformPoint( float3 location, uint row );
    static float3 transformVector( float3 direction, uint row );
    static void applyProgram( uint programBase, uint vertexIndex, inout float3 inoutPosition, inout float3 inoutNormal );
};

float ModifierContext::param( uint index )
{
    return g_DeformPrograms[_paramBase + index].x;
}

float3 ModifierContext::param3( uint index )
{
    return g_DeformPrograms[_paramBase + index].xyz;
}

/**
 * MeshModifier::evaluateCurve 의 표본판 — 매개변수 index(float4 단위)부터 SW_MODIFIER_CURVE_SAMPLES 개 표본을 선형 보간한다. x 는 [0,1] 로 자른다.
 * 곡선을 균등 표본으로 싣는 것은 키 목록(가변 길이 + 접선)을 싣는 것보다 모양이 고정이라 기록 길이를 미리 안다. 키 사이 3차 보간은 32 등분으로 근사한다.
 */
float ModifierContext::curve( uint index, float x )
{
    const float position = saturate( x ) * (float)( SW_MODIFIER_CURVE_SAMPLES - 1 );
    const uint  lower    = min( (uint)position, (uint)( SW_MODIFIER_CURVE_SAMPLES - 2 ) );
    const float ratio    = position - (float)lower;
    const uint  base     = _paramBase + index;
    const float low      = g_DeformPrograms[base + lower / 4u][lower % 4u];
    const float high     = g_DeformPrograms[base + ( lower + 1u ) / 4u][( lower + 1u ) % 4u];
    return lerp( low, high, ratio );
}

/** 매개변수 영역의 float4 하나(index 는 float4 단위) — 외부 입력 블록을 읽는 길이다 */
float4 ModifierContext::raw( uint index )
{
    return g_DeformPrograms[_paramBase + index];
}

/** 수 목록(listBase 에서 시작)의 항목 수 — 머리 float4 의 x */
uint ModifierContext::listCount( uint listBase )
{
    return (uint)( g_DeformPrograms[_paramBase + listBase].x + 0.5f );
}

/** 수 목록의 평탄한 원소 flat(= 항목 * 항목 원소 수 + 열) — 머리 float4 하나 뒤에 원소가 float4 하나에 넷씩 이어진다 */
float ModifierContext::listValue( uint listBase, uint flat )
{
    return g_DeformPrograms[_paramBase + listBase + 1u + flat / 4u][flat % 4u];
}

float3 ModifierContext::listVec3( uint listBase, uint flat )
{
    return float3( listValue( listBase, flat ), listValue( listBase, flat + 1u ), listValue( listBase, flat + 2u ) );
}

/**
 * MeshModifierUtil::packGPUTextureGrid 가 실은 격자(gridBase 부터, 행 우선 · float4 하나에 넷)를 이중선형으로 읽는다 —
 * `MeshModifierTextureSampler` 와 같은 규약: 텍셀 중심은 (i + 0.5) / 해상도, 범위 밖은 반복.
 */
float ModifierContext::textureGrid( uint gridBase, float2 uv )
{
    const int    resolution = SW_MODIFIER_TEXTURE_RESOLUTION;
    const float2 scaled     = uv * (float)resolution - 0.5f;
    const float2 cell       = floor( scaled );
    const float2 ratio      = scaled - cell;
    const int    x0         = ( ( (int)cell.x % resolution ) + resolution ) % resolution;
    const int    y0         = ( ( (int)cell.y % resolution ) + resolution ) % resolution;
    const int    x1         = ( x0 + 1 ) % resolution;
    const int    y1         = ( y0 + 1 ) % resolution;
    const uint   base       = _paramBase + gridBase;
    const uint   i00        = (uint)( y0 * resolution + x0 );
    const uint   i10        = (uint)( y0 * resolution + x1 );
    const uint   i01        = (uint)( y1 * resolution + x0 );
    const uint   i11        = (uint)( y1 * resolution + x1 );
    const float  v00        = g_DeformPrograms[base + i00 / 4u][i00 % 4u];
    const float  v10        = g_DeformPrograms[base + i10 / 4u][i10 % 4u];
    const float  v01        = g_DeformPrograms[base + i01 / 4u][i01 % 4u];
    const float  v11        = g_DeformPrograms[base + i11 / 4u][i11 % 4u];
    return lerp( lerp( v00, v10, ratio.x ), lerp( v01, v11, ratio.x ), ratio.y );
}

/** MeshModifier::getInt — floor(x + 0.5) */
int ModifierContext::paramInt( uint index )
{
    return (int)floor( param( index ) + 0.5f );
}

/** MeshModifier::getAxisIndex — [0, 2] 로 자른 정수 */
uint ModifierContext::paramAxis( uint index )
{
    return (uint)clamp( paramInt( index ), 0, 2 );
}

static float MeshDeform::get( float3 value, uint axis )
{
    return axis == 0u ? value.x : ( axis == 1u ? value.y : value.z );
}

static float3 MeshDeform::set( float3 value, uint axis, float component )
{
    return float3( axis == 0u ? component : value.x, axis == 1u ? component : value.y, axis == 2u ? component : value.z );
}

static float3 MeshDeform::normalizeOr( float3 value, float3 fallback )
{
    const float length = sqrt( dot( value, value ) );
    const bool  bValid = length > kModifierEpsilon && length < 3.0e38f;
    return bValid ? value / max( length, kModifierEpsilon ) : fallback;
}

/** MeshModifierUtil::getAxisLength */
float ModifierContext::axisLength()
{
    const float length = MeshDeform::get( _boundsMax - _boundsMin, _axis );
    return length > kModifierEpsilon ? length : 1.0f;
}

/** MeshModifierUtil::getAxisFraction — 자르지 않는다 */
static float MeshDeform::axisFraction( float3 position, ModifierContext context )
{
    return ( MeshDeform::get( position, context._axis ) - MeshDeform::get( context._boundsMin, context._axis ) ) / context.axisLength();
}

// ------------------------------------------------------------------------------
// 값 노이즈 — Stack/MeshModifierNoise.h 와 비트 단위로 같은 정수 해시
// ------------------------------------------------------------------------------
static uint MeshDeform::hashLattice( int cellX, int cellY, int cellZ, uint seed )
{
    uint hash = seed * 0x9E3779B1u;
    hash ^= asuint( cellX ) * 0x85EBCA6Bu;
    hash = ( hash << 13 ) | ( hash >> 19 );
    hash ^= asuint( cellY ) * 0xC2B2AE35u;
    hash = ( hash << 13 ) | ( hash >> 19 );
    hash ^= asuint( cellZ ) * 0x27D4EB2Fu;
    hash ^= hash >> 16;
    hash *= 0x85EBCA6Bu;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35u;
    hash ^= hash >> 16;
    return hash;
}

static float MeshDeform::latticeValue( int cellX, int cellY, int cellZ, uint seed )
{
    return (float)( MeshDeform::hashLattice( cellX, cellY, cellZ, seed ) & 0xFFFFu ) / 32767.5f - 1.0f;
}

static float MeshDeform::smooth( float ratio )
{
    return ratio * ratio * ( 3.0f - 2.0f * ratio );
}

static float MeshDeform::valueNoise3D( float3 position, uint seed )
{
    const float3 cell    = floor( position );
    const int    cellX   = (int)cell.x;
    const int    cellY   = (int)cell.y;
    const int    cellZ   = (int)cell.z;
    const float  tx      = MeshDeform::smooth( position.x - cell.x );
    const float  ty      = MeshDeform::smooth( position.y - cell.y );
    const float  tz      = MeshDeform::smooth( position.z - cell.z );
    const float  v000    = MeshDeform::latticeValue( cellX, cellY, cellZ, seed );
    const float  v100    = MeshDeform::latticeValue( cellX + 1, cellY, cellZ, seed );
    const float  v010    = MeshDeform::latticeValue( cellX, cellY + 1, cellZ, seed );
    const float  v110    = MeshDeform::latticeValue( cellX + 1, cellY + 1, cellZ, seed );
    const float  v001    = MeshDeform::latticeValue( cellX, cellY, cellZ + 1, seed );
    const float  v101    = MeshDeform::latticeValue( cellX + 1, cellY, cellZ + 1, seed );
    const float  v011    = MeshDeform::latticeValue( cellX, cellY + 1, cellZ + 1, seed );
    const float  v111    = MeshDeform::latticeValue( cellX + 1, cellY + 1, cellZ + 1, seed );
    const float  value00 = v000 + ( v100 - v000 ) * tx;
    const float  value10 = v010 + ( v110 - v010 ) * tx;
    const float  value01 = v001 + ( v101 - v001 ) * tx;
    const float  value11 = v011 + ( v111 - v011 ) * tx;
    const float  lower   = value00 + ( value10 - value00 ) * ty;
    const float  upper   = value01 + ( value11 - value01 ) * ty;
    return lower + ( upper - lower ) * tz;
}

// ------------------------------------------------------------------------------
// 모디파이어 — 각 함수는 Modifiers/<이름>Modifier.cpp 의 deformPoint 와 같은 식이다(매개변수 번호 = 선언 순서)
// ------------------------------------------------------------------------------
static float3 MeshDeform::bendLike( float3 location, ModifierContext context, float angle, float direction, float lowerParam, float upperParam )
{
    const uint  axis         = context._axis;
    const uint  axisU        = ( axis + 1u ) % 3u;
    const uint  axisV        = ( axis + 2u ) % 3u;
    const float length       = context.axisLength();
    const float lowerBound   = MeshDeform::get( context._boundsMin, axis );
    const float lower        = lowerBound + min( lowerParam, upperParam ) * length;
    const float upper        = lowerBound + max( lowerParam, upperParam ) * length;
    const float cosDirection = cos( direction );
    const float sinDirection = sin( direction );
    const float componentU   = MeshDeform::get( location, axisU );
    const float componentV   = MeshDeform::get( location, axisV );
    const float bendX        = componentU * cosDirection + componentV * sinDirection;
    const float sideY        = -componentU * sinDirection + componentV * cosDirection;
    const float height       = MeshDeform::get( location, axis );
    const float clamped      = clamp( height, lower, upper );
    const float radius       = length / angle;
    const float phi          = clamped / radius;
    const float cosPhi       = cos( phi );
    const float sinPhi       = sin( phi );
    const float extra        = height - clamped;
    const float newX         = radius - ( radius - bendX ) * cosPhi + extra * sinPhi;
    const float newH         = ( radius - bendX ) * sinPhi + extra * cosPhi;
    float3      result       = MeshDeform::set( location, axis, newH );
    result                   = MeshDeform::set( result, axisU, newX * cosDirection - sideY * sinDirection );
    result                   = MeshDeform::set( result, axisV, newX * sinDirection + sideY * cosDirection );
    return result;
}

static float3 MeshDeform::bend( float3 location, ModifierContext context )
{
    const float angle  = context.param( 0u ) * kDegToRad;
    float3      result = location;
    if ( abs( angle ) >= kModifierEpsilon )
        result = MeshDeform::bendLike( location, context, angle, context.param( 1u ) * kDegToRad, context.param( 2u ), context.param( 3u ) );
    return result;
}

static float3 MeshDeform::twist( float3 location, ModifierContext context )
{
    const float angle  = context.param( 0u ) * kDegToRad;
    float3      result = location;
    if ( abs( angle ) >= kModifierEpsilon )
    {
        const uint  axis       = context._axis;
        const uint  axisU      = ( axis + 1u ) % 3u;
        const uint  axisV      = ( axis + 2u ) % 3u;
        const float length     = context.axisLength();
        const float lowerBound = MeshDeform::get( context._boundsMin, axis );
        const float lower      = lowerBound + min( context.param( 1u ), context.param( 2u ) ) * length;
        const float upper      = lowerBound + max( context.param( 1u ), context.param( 2u ) ) * length;
        const float height     = clamp( MeshDeform::get( location, axis ), lower, upper );
        const float phi        = angle * height / length;
        const float cosPhi     = cos( phi );
        const float sinPhi     = sin( phi );
        const float componentU = MeshDeform::get( location, axisU );
        const float componentV = MeshDeform::get( location, axisV );
        result                 = MeshDeform::set( result, axisU, componentU * cosPhi - componentV * sinPhi );
        result                 = MeshDeform::set( result, axisV, componentU * sinPhi + componentV * cosPhi );
    }
    return result;
}

static float3 MeshDeform::taper( float3 location, ModifierContext context )
{
    const float amount = context.param( 0u );
    float3      result = location;
    if ( abs( amount ) >= kModifierEpsilon )
    {
        const float fraction = saturate( MeshDeform::axisFraction( location, context ) );
        const float scale    = 1.0f + amount * pow( fraction, context.param( 1u ) );
        result               = MeshDeform::set( location * scale, context._axis, MeshDeform::get( location, context._axis ) );
    }
    return result;
}

static float3 MeshDeform::stretch( float3 location, ModifierContext context )
{
    const float alongScale = 1.0f + context.param( 0u );
    const float sideScale  = pow( alongScale, -0.5f * context.param( 1u ) );
    return MeshDeform::set( location * sideScale, context._axis, MeshDeform::get( location, context._axis ) * alongScale );
}

static float3 MeshDeform::wave( float3 location, ModifierContext context, bool bRadial )
{
    const float amplitude = context.param( 0u );
    float3      result    = location;
    if ( abs( amplitude ) >= kModifierEpsilon )
    {
        const uint  axis       = context._axis;
        const float componentU = MeshDeform::get( location, ( axis + 1u ) % 3u );
        const float componentV = MeshDeform::get( location, ( axis + 2u ) % 3u );
        const float distance   = bRadial ? sqrt( componentU * componentU + componentV * componentV ) : componentU;
        const float phase      = ( context.param( 2u ) + context.param( 4u ) * context._time ) * kDegToRad;
        const float angle      = kTwoPi * distance / context.param( 1u ) + phase;
        const float offset     = amplitude * sin( angle ) * exp( -context.param( 3u ) * abs( distance ) );
        result                 = MeshDeform::set( location, axis, MeshDeform::get( location, axis ) + offset );
    }
    return result;
}

static float3 MeshDeform::noise( float3 location, ModifierContext context )
{
    const float amplitude = context.param( 0u );
    float3      result    = location;
    if ( amplitude >= kModifierEpsilon )
    {
        const uint   seed        = (uint)context.paramInt( 2u );
        const float  drift       = context.param( 4u ) * context._time;
        const float3 samplePoint = location * context.param( 1u ) + float3( drift, drift, drift );
        const float3 strength    = context.param3( 3u );
        const float3 offset      = float3( MeshDeform::valueNoise3D( samplePoint, seed ) * strength.x, MeshDeform::valueNoise3D( samplePoint + float3( 31.416f, 0.0f, 0.0f ), seed ) * strength.y,
                                           MeshDeform::valueNoise3D( samplePoint + float3( 0.0f, 0.0f, 67.123f ), seed ) * strength.z );
        result                   = location + offset * amplitude;
    }
    return result;
}

static float3 MeshDeform::melt( float3 location, ModifierContext context )
{
    const float amount = context.param( 0u );
    float3      result = location;
    if ( amount >= kModifierEpsilon )
    {
        const uint  axis        = context._axis;
        const float floorHeight = MeshDeform::get( context._boundsMin, axis );
        const float height      = MeshDeform::get( location, axis );
        const float fraction    = saturate( MeshDeform::axisFraction( location, context ) );
        const float spread      = 1.0f + context.param( 1u ) * amount * ( 1.0f - fraction );
        result                  = MeshDeform::set( location * spread, axis, floorHeight + ( height - floorHeight ) * ( 1.0f - amount ) );
    }
    return result;
}

static float3 MeshDeform::poke( float3 location, ModifierContext context )
{
    const float amount   = context.param( 0u );
    const float radius   = context.param( 1u );
    const float distance = length( location );
    float3      result   = location;
    if ( abs( amount ) >= kModifierEpsilon && distance < radius )
    {
        const float ratio  = distance / radius;
        const float weight = pow( 1.0f - ratio * ratio, context.param( 2u ) );
        result             = MeshDeform::set( location, context._axis, MeshDeform::get( location, context._axis ) + amount * weight );
    }
    return result;
}

static float3 MeshDeform::displace( float3 location, float3 surfaceNormal, ModifierContext context )
{
    const float amount = context.param( 0u );
    float3      result = location;
    if ( abs( amount ) >= kModifierEpsilon )
    {
        const uint  axis       = context._axis;
        const float componentU = MeshDeform::get( location, ( axis + 1u ) % 3u );
        const float componentV = MeshDeform::get( location, ( axis + 2u ) % 3u );
        const float frequency  = kTwoPi * context.param( 1u );
        const float phase      = context.param( 2u ) * kDegToRad;
        const float height     = sin( frequency * componentU + phase ) * cos( frequency * componentV + phase );
        result                 = location + surfaceNormal * ( amount * height );
    }
    return result;
}

static float3 MeshDeform::bubble( float3 location, ModifierContext context )
{
    const float  strength = context.param( 0u );
    const float  radius   = context.param( 1u );
    const float3 offset   = location - context.param3( 2u );
    const float  distance = length( offset );
    const bool   bActive  = abs( strength ) >= kModifierEpsilon && radius >= kModifierEpsilon && distance < radius && distance >= kModifierEpsilon;
    float3       result   = location;
    if ( bActive )
    {
        const float ratio   = distance / radius;
        const float falloff = ( 1.0f - ratio * ratio ) * ( 1.0f - ratio * ratio );
        result              = location + offset * ( strength * falloff / distance );
    }
    return result;
}

static float3 MeshDeform::bulge( float3 location, ModifierContext context )
{
    const float amount   = context.param( 0u );
    const float lower    = min( context.param( 2u ), context.param( 3u ) );
    const float upper    = max( context.param( 2u ), context.param( 3u ) );
    const float fraction = MeshDeform::axisFraction( location, context );
    const bool  bActive  = abs( amount ) >= kModifierEpsilon && upper - lower >= kModifierEpsilon && fraction > lower && fraction < upper;
    float3      result   = location;
    if ( bActive )
    {
        const float ratio   = ( fraction - lower ) / ( upper - lower );
        const float profile = 1.0f - pow( abs( 2.0f * ratio - 1.0f ), context.param( 1u ) );
        const float scale   = max( 1.0f + amount * profile, 0.0f );
        result              = MeshDeform::set( location * scale, context._axis, MeshDeform::get( location, context._axis ) );
    }
    return result;
}

/** Cylindrify(축 성분을 뺀 반경) · Globe(전체 반경) */
static float3 MeshDeform::toRadius( float3 location, ModifierContext context, bool bCylinder )
{
    const float  percent  = context.param( 0u );
    const float3 radial   = bCylinder ? MeshDeform::set( location, context._axis, 0.0f ) : location;
    const float  distance = length( radial );
    float3       result   = location;
    if ( percent >= kModifierEpsilon && distance >= kModifierEpsilon )
    {
        const float target = distance + ( context.param( 1u ) - distance ) * percent;
        result             = radial * ( target / distance );
        result             = bCylinder ? MeshDeform::set( result, context._axis, MeshDeform::get( location, context._axis ) ) : result;
    }
    return result;
}

static float3 MeshDeform::hump( float3 location, ModifierContext context )
{
    const float height = context.param( 0u );
    const float radius = context.param( 1u );
    float3      result = location;
    if ( abs( height ) >= kModifierEpsilon && radius >= kModifierEpsilon )
    {
        const float3 offset = MeshDeform::set( location - context.param3( 2u ), context._axis, 0.0f );
        const float  ratio  = length( offset ) / radius;
        result              = MeshDeform::set( location, context._axis, MeshDeform::get( location, context._axis ) + height * exp( -ratio * ratio ) );
    }
    return result;
}

static float3 MeshDeform::radialSkew( float3 location, ModifierContext context )
{
    const float  angle    = context.param( 0u ) * kDegToRad;
    const float3 radial   = MeshDeform::set( location, context._axis, 0.0f );
    const float  distance = length( radial );
    float3       result   = location;
    if ( abs( angle ) >= kModifierEpsilon && distance >= kModifierEpsilon )
    {
        const float height = MeshDeform::get( location, context._axis ) - MeshDeform::get( context._boundsMin, context._axis );
        const float target = max( distance + tan( angle ) * height, 0.0f );
        result             = MeshDeform::set( radial * ( target / distance ), context._axis, MeshDeform::get( location, context._axis ) );
    }
    return result;
}

/** Skew · Sinus Curve 의 옆 밀기 */
static float3 MeshDeform::shiftSide( float3 location, ModifierContext context, float shift, float direction )
{
    const uint axisU  = ( context._axis + 1u ) % 3u;
    const uint axisV  = ( context._axis + 2u ) % 3u;
    float3     result = MeshDeform::set( location, axisU, MeshDeform::get( location, axisU ) + shift * cos( direction ) );
    return MeshDeform::set( result, axisV, MeshDeform::get( result, axisV ) + shift * sin( direction ) );
}

static float3 MeshDeform::skew( float3 location, ModifierContext context )
{
    const float amount = context.param( 0u );
    float3      result = location;
    if ( abs( amount ) >= kModifierEpsilon )
    {
        const float lower    = min( context.param( 2u ), context.param( 3u ) );
        const float upper    = max( context.param( 2u ), context.param( 3u ) );
        const float fraction = MeshDeform::axisFraction( location, context );
        const float ratio    = upper - lower < kModifierEpsilon ? ( fraction >= lower ? 1.0f : 0.0f ) : saturate( ( fraction - lower ) / ( upper - lower ) );
        result               = MeshDeform::shiftSide( location, context, amount * ratio, context.param( 1u ) * kDegToRad );
    }
    return result;
}

static float3 MeshDeform::spherify( float3 location, ModifierContext context )
{
    const float  percent  = context.param( 0u );
    const float3 size     = context._boundsMax - context._boundsMin;
    const float  radius   = 0.5f * max( size.x, max( size.y, size.z ) );
    const float3 center   = ( context._boundsMin + context._boundsMax ) * 0.5f;
    const float3 offset   = location - center;
    const float  distance = length( offset );
    const bool   bActive  = percent >= kModifierEpsilon && context._boundsValid > 0.5f && radius >= kModifierEpsilon && distance >= kModifierEpsilon;
    float3       result   = location;
    if ( bActive )
    {
        const float target = distance + ( radius - distance ) * percent;
        result             = center + offset * ( target / distance );
    }
    return result;
}

static float3 MeshDeform::squeeze( float3 location, ModifierContext context )
{
    const float amount = context.param( 0u );
    float3      result = location;
    if ( abs( amount ) >= kModifierEpsilon )
    {
        const float axisScale   = max( 1.0f - amount, 0.1f );
        const float radialScale = 1.0f + context.param( 1u ) * ( 1.0f / sqrt( axisScale ) - 1.0f );
        result                  = MeshDeform::set( location * radialScale, context._axis, MeshDeform::get( location, context._axis ) * axisScale );
    }
    return result;
}

static float3 MeshDeform::sinusCurve( float3 location, ModifierContext context )
{
    const float amplitude = context.param( 0u );
    float3      result    = location;
    if ( abs( amplitude ) >= kModifierEpsilon )
    {
        const float fraction = MeshDeform::axisFraction( location, context );
        const float shift    = amplitude * sin( kTwoPi * context.param( 1u ) * fraction + context.param( 2u ) * kDegToRad );
        result               = MeshDeform::shiftSide( location, context, shift, context.param( 3u ) * kDegToRad );
    }
    return result;
}

static float3 MeshDeform::rolled( float3 location, ModifierContext context )
{
    const float radius = context.param( 0u );
    const float turns  = context.param( 2u );
    const uint  axis   = context._axis;
    const float start  = MeshDeform::get( context._boundsMin, axis ) + context.param( 1u ) * context.axisLength();
    const float height = MeshDeform::get( location, axis );
    float3      result = location;
    if ( radius >= kModifierEpsilon && turns >= kModifierEpsilon && height > start )
    {
        const uint  axisU        = ( axis + 1u ) % 3u;
        const uint  axisV        = ( axis + 2u ) % 3u;
        const float direction    = context.param( 3u ) * kDegToRad;
        const float cosDirection = cos( direction );
        const float sinDirection = sin( direction );
        const float componentU   = MeshDeform::get( location, axisU );
        const float componentV   = MeshDeform::get( location, axisV );
        const float rollX        = componentU * cosDirection + componentV * sinDirection;
        const float sideY        = -componentU * sinDirection + componentV * cosDirection;
        const float arc          = height - start;
        const float rolled       = min( arc, turns * kTwoPi * radius );
        const float extra        = arc - rolled;
        const float phi          = rolled / radius;
        const float cosPhi       = cos( phi );
        const float sinPhi       = sin( phi );
        const float newX         = radius - ( radius - rollX ) * cosPhi + extra * sinPhi;
        const float newH         = start + ( radius - rollX ) * sinPhi + extra * cosPhi;
        result                   = MeshDeform::set( result, axis, newH );
        result                   = MeshDeform::set( result, axisU, newX * cosDirection - sideY * sinDirection );
        result                   = MeshDeform::set( result, axisV, newX * sinDirection + sideY * cosDirection );
    }
    return result;
}

static float3 MeshDeform::crumple( float3 location, float3 surfaceNormal, ModifierContext context )
{
    const float amplitude = context.param( 0u );
    float3      result    = location;
    if ( amplitude >= kModifierEpsilon )
    {
        const uint   seed        = (uint)context.paramInt( 2u );
        const float3 samplePoint = location * context.param( 1u );
        const float3 noiseVector = float3( MeshDeform::valueNoise3D( samplePoint, seed ), MeshDeform::valueNoise3D( samplePoint + float3( 31.416f, 0.0f, 0.0f ), seed ),
                                           MeshDeform::valueNoise3D( samplePoint + float3( 0.0f, 0.0f, 67.123f ), seed ) );
        const float3 along       = surfaceNormal * noiseVector.x;
        const float  blend       = saturate( context.param( 3u ) );
        const float3 offset      = noiseVector * ( 1.0f - blend ) + along * blend;
        result                   = location + offset * amplitude;
    }
    return result;
}

static float3 MeshDeform::dynamicRipple( float3 location, ModifierContext context )
{
    const float amplitude  = context.param( 0u );
    const float time       = max( context._time, 0.0f );
    const uint  axis       = context._axis;
    const float componentU = MeshDeform::get( location, ( axis + 1u ) % 3u );
    const float componentV = MeshDeform::get( location, ( axis + 2u ) % 3u );
    const float radius     = sqrt( componentU * componentU + componentV * componentV );
    const float front      = context.param( 2u ) * time;
    float3      result     = location;
    if ( abs( amplitude ) >= kModifierEpsilon && radius <= front )
    {
        const float limit   = context.param( 4u );
        const float falloff = limit > kModifierEpsilon ? saturate( 1.0f - radius / limit ) : 1.0f;
        const float angle   = kTwoPi * ( radius - front ) / max( context.param( 1u ), 0.001f );
        const float offset  = amplitude * sin( angle ) * exp( -context.param( 3u ) * time ) * falloff;
        result              = MeshDeform::set( location, axis, MeshDeform::get( location, axis ) + offset );
    }
    return result;
}

static float3 MeshDeform::pageFlip( float3 location, ModifierContext context )
{
    const float progress = context.param( 0u );
    const uint  liftAxis = context._axis;
    const uint  pickAxis = context.paramAxis( 2u );
    const uint  flipAxis = pickAxis == liftAxis ? ( liftAxis + 1u ) % 3u : pickAxis;
    const float minU     = MeshDeform::get( context._boundsMin, flipAxis );
    const float maxU     = MeshDeform::get( context._boundsMax, flipAxis );
    const float width    = maxU - minU;
    const float radius   = max( context.param( 1u ), 0.001f );
    const float curl     = maxU - saturate( progress ) * width;
    const float arc      = MeshDeform::get( location, flipAxis ) - curl;
    const bool  bActive  = progress >= kModifierEpsilon && context._boundsValid > 0.5f && width >= kModifierEpsilon && arc > 0.0f;
    float3      result   = location;
    if ( bActive )
    {
        const float height   = MeshDeform::get( location, liftAxis );
        const float halfTurn = kPi * radius;
        const float theta    = arc / radius;
        const bool  bCurled  = arc <= halfTurn;
        const float newU     = bCurled ? curl + ( radius - height ) * sin( theta ) : curl - ( arc - halfTurn );
        const float newH     = bCurled ? radius - ( radius - height ) * cos( theta ) : 2.0f * radius - height;
        result               = MeshDeform::set( result, flipAxis, newU );
        result               = MeshDeform::set( result, liftAxis, newH );
    }
    return result;
}

static float3 MeshDeform::treeBend( float3 location, ModifierContext context )
{
    const float  strength   = context.param( 0u );
    const uint   axis       = context._axis;
    const float3 wind       = MeshDeform::set( context.param3( 1u ), axis, 0.0f );
    const float  windLength = length( wind );
    const float  root       = MeshDeform::get( context._boundsMin, axis ) + context.param( 4u );
    const float  height     = MeshDeform::get( location, axis ) - root;
    const bool   bActive    = abs( strength ) >= kModifierEpsilon && context._boundsValid > 0.5f && windLength >= kModifierEpsilon && height > 0.0f;
    float3       result     = location;
    if ( bActive )
    {
        const float ratio = height / context.axisLength();
        const float sway  = 1.0f + context.param( 2u ) * sin( kTwoPi * context.param( 3u ) * context._time );
        result            = location + wind * ( 1.0f / windLength ) * ( strength * sway * ratio * ratio );
    }
    return result;
}

static float3 MeshDeform::vertNoise( float3 location, float3 surfaceNormal, ModifierContext context )
{
    const float amplitude = context.param( 0u );
    float3      result    = location;
    if ( abs( amplitude ) >= kModifierEpsilon )
    {
        const uint   seed        = (uint)context.paramInt( 2u );
        const float  drift       = context.param( 3u ) * context._time;
        const float3 samplePoint = location * context.param( 1u ) + float3( drift, drift * 0.5f, drift * 0.25f );
        result                   = location + surfaceNormal * ( MeshDeform::valueNoise3D( samplePoint, seed ) * amplitude );
    }
    return result;
}

static float3 MeshDeform::zstretchNoise( float3 location, ModifierContext context )
{
    const float amount = context.param( 0u );
    const float noise  = context.param( 1u );
    float3      result = location;
    if ( abs( amount ) >= kModifierEpsilon || abs( noise ) >= kModifierEpsilon )
    {
        const uint  axis   = context.paramAxis( 4u );
        const uint  seed   = (uint)context.paramInt( 3u );
        const float wobble = MeshDeform::valueNoise3D( location * context.param( 2u ), seed );
        result             = MeshDeform::set( location, axis, MeshDeform::get( location, axis ) * ( 1.0f + amount ) + noise * wobble );
    }
    return result;
}

static float3 MeshDeform::waving( float3 location, ModifierContext context )
{
    const float amplitude = context.param( 0u );
    const uint  fixedAxis = context.paramAxis( 3u );
    const uint  moveAxis  = context._axis == fixedAxis ? ( fixedAxis + 1u ) % 3u : context._axis;
    const float start     = MeshDeform::get( context._boundsMin, fixedAxis ) + context.param( 4u );
    const float distance  = MeshDeform::get( location, fixedAxis ) - start;
    const bool  bActive   = abs( amplitude ) >= kModifierEpsilon && context._boundsValid > 0.5f && distance > 0.0f;
    float3      result    = location;
    if ( bActive )
    {
        const float span   = MeshDeform::get( context._boundsMax - context._boundsMin, fixedAxis );
        const float ratio  = distance / ( span > kModifierEpsilon ? span : 1.0f );
        const float phase  = distance / max( context.param( 1u ), 0.001f ) - context.param( 2u ) * context._time;
        const float offset = amplitude * ratio * sin( kTwoPi * phase );
        result             = MeshDeform::set( location, moveAxis, MeshDeform::get( location, moveAxis ) + offset );
    }
    return result;
}

/** Bezier Attractor — 3차 베지어(제어점 4 = 매개변수 0..3)에서 가장 가까운 점 쪽으로 반경 안의 정점을 끌어당긴다. CPU 와 같이 64 선분으로 나눠 찾는다 */
static float3 MeshDeform::bezierAttractor( float3 location, ModifierContext context )
{
    const float  strength = context.param( 4u );
    const float  radius   = context.param( 5u );
    const float3 p0       = context.param3( 0u );
    const float3 p1       = context.param3( 1u );
    const float3 p2       = context.param3( 2u );
    const float3 p3       = context.param3( 3u );
    float3       previous = p0;
    float3       best     = p0;
    float        bestDist = dot( p0 - location, p0 - location );
    for ( uint segment = 1u; segment <= 64u; ++segment )
    {
        const float  t        = (float)segment / 64.0f;
        const float  inverse  = 1.0f - t;
        const float3 current  = p0 * ( inverse * inverse * inverse ) + p1 * ( 3.0f * inverse * inverse * t ) + p2 * ( 3.0f * inverse * t * t ) + p3 * ( t * t * t );
        const float3 edge     = current - previous;
        const float  edgeLen  = dot( edge, edge );
        const float  ratio    = edgeLen > kModifierEpsilon ? saturate( dot( location - previous, edge ) / edgeLen ) : 0.0f;
        const float3 cand     = previous + edge * ratio;
        const float  candDist = dot( cand - location, cand - location );
        if ( candDist < bestDist )
        {
            bestDist = candDist;
            best     = cand;
        }
        previous = current;
    }
    const float3 delta    = best - location;
    const float  distance = length( delta );
    float3       result   = location;
    if ( strength > kModifierEpsilon && distance < radius )
    {
        const float weight = strength * pow( 1.0f - distance / radius, context.param( 6u ) );
        result             = location + delta * saturate( weight );
    }
    return result;
}

/** Displace Limits — 위치를 상자(Mode 1) · 구(Mode 2) 안으로 자른다. "Clamp Displacement" 를 켜면 CPU 전용이라 여기 오지 않는다 */
static float3 MeshDeform::displaceLimits( float3 location, ModifierContext context )
{
    const int    mode    = context.paramInt( 0u );
    const float3 low     = context.param3( 1u );
    const float3 high    = context.param3( 2u );
    const float3 boxed   = clamp( location, min( low, high ), max( low, high ) );
    const float  radius  = context.param( 3u );
    const float  len     = length( location );
    const bool   bShrink = len > radius && len >= kModifierEpsilon;
    const float3 sphered = bShrink ? location * ( radius / max( len, kModifierEpsilon ) ) : location;
    return mode == 1 ? boxed : ( mode == 2 ? sphered : location );
}

/** 모디파이어 하나의 점 변형(모디파이어 공간). 모르는 종류는 그대로 둔다 */
static float3 MeshDeform::deformPoint( uint kind, float3 location, float3 surfaceNormal, ModifierContext context )
{
    float3 result = location;
    SW_BRANCH switch ( kind )
    {
        case SW_MODIFIER_BEND:
            result = MeshDeform::bend( location, context );
            break;
        case SW_MODIFIER_TWIST:
            result = MeshDeform::twist( location, context );
            break;
        case SW_MODIFIER_TAPER:
            result = MeshDeform::taper( location, context );
            break;
        case SW_MODIFIER_STRETCH:
            result = MeshDeform::stretch( location, context );
            break;
        case SW_MODIFIER_WAVE:
            result = MeshDeform::wave( location, context, false );
            break;
        case SW_MODIFIER_RIPPLE:
            result = MeshDeform::wave( location, context, true );
            break;
        case SW_MODIFIER_NOISE:
            result = MeshDeform::noise( location, context );
            break;
        case SW_MODIFIER_MELT:
            result = MeshDeform::melt( location, context );
            break;
        case SW_MODIFIER_PUSH:
            result = location + surfaceNormal * context.param( 0u );
            break;
        case SW_MODIFIER_POKE:
            result = MeshDeform::poke( location, context );
            break;
        case SW_MODIFIER_DISPLACE:
            result = MeshDeform::displace( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_BUBBLE:
            result = MeshDeform::bubble( location, context );
            break;
        case SW_MODIFIER_BULGE:
            result = MeshDeform::bulge( location, context );
            break;
        case SW_MODIFIER_CYLINDRIFY:
            result = MeshDeform::toRadius( location, context, true );
            break;
        case SW_MODIFIER_GLOBE:
            result = MeshDeform::toRadius( location, context, false );
            break;
        case SW_MODIFIER_HUMP:
            result = MeshDeform::hump( location, context );
            break;
        case SW_MODIFIER_RADIALSKEW:
            result = MeshDeform::radialSkew( location, context );
            break;
        case SW_MODIFIER_SKEW:
            result = MeshDeform::skew( location, context );
            break;
        case SW_MODIFIER_SPHERIFY:
            result = MeshDeform::spherify( location, context );
            break;
        case SW_MODIFIER_SQUEEZE:
            result = MeshDeform::squeeze( location, context );
            break;
        case SW_MODIFIER_SCALE:
            result = location * context.param( 0u ) * context.param3( 1u );
            break;
        case SW_MODIFIER_SINUSCURVE:
            result = MeshDeform::sinusCurve( location, context );
            break;
        case SW_MODIFIER_ROLLED:
            result = MeshDeform::rolled( location, context );
            break;
        case SW_MODIFIER_SIMPLEMOD:
            result = location + context.param3( 0u ) + context.param3( 1u ) * location + context.param3( 2u ) * location * location;
            break;
        case SW_MODIFIER_CRUMPLE:
            result = MeshDeform::crumple( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_DYNAMICRIPPLE:
            result = MeshDeform::dynamicRipple( location, context );
            break;
        case SW_MODIFIER_PAGEFLIP:
            result = MeshDeform::pageFlip( location, context );
            break;
        case SW_MODIFIER_TREEBEND:
            result = MeshDeform::treeBend( location, context );
            break;
        case SW_MODIFIER_VERTNOISE:
            result = MeshDeform::vertNoise( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_ZSTRETCHNOISE:
            result = MeshDeform::zstretchNoise( location, context );
            break;
        case SW_MODIFIER_WAVING:
            result = MeshDeform::waving( location, context );
            break;
        case SW_MODIFIER_PIVOTADJUST:
            result = location - context.param3( 0u );
            break;
        case SW_MODIFIER_DISPLACELIMITS:
            result = MeshDeform::displaceLimits( location, context );
            break;
        case SW_MODIFIER_BEZIERATTRACTOR:
            result = MeshDeform::bezierAttractor( location, context );
            break;
        case SW_MODIFIER_RUBBER:
            result = MeshDeform::rubber( location, context );
            break;
        case SW_MODIFIER_CURVEDEFORM:
            result = MeshDeform::curveDeform( location, context );
            break;
        case SW_MODIFIER_CURVESCULPT:
            result = MeshDeform::curveSculpt( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_CURVESCULPTLAYERED:
            result = MeshDeform::curveSculptLayered( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_ROPEDEFORM:
            result = MeshDeform::ropeDeform( location, context );
            break;
        case SW_MODIFIER_PATHDEFORM:
            result = MeshDeform::pathDeform( location, context, false );
            break;
        case SW_MODIFIER_WORLDPATHDEFORM:
            result = MeshDeform::pathDeform( location, context, true );
            break;
        case SW_MODIFIER_DISPLACERENDERTARGET:
            // 매개변수 0 양 · 1 중간값 · 2 채널(격자가 이미 한 채널이라 안 쓴다) · 3 타일링 · 4 오프셋, 격자 머리는 5
            result = MeshDeform::displaceByGrid( location, surfaceNormal, context, context.param( 0u ), context.param( 1u ),
                                                 context._uv * context.param3( 3u ).xy + context.param3( 4u ).xy, 5u );
            break;
        case SW_MODIFIER_DISPLACEWEBCAM:
            // 매개변수 0 양 · 1 중간값 · 2 채널 · 3 좌우 뒤집기, 격자 머리는 4
            result = MeshDeform::displaceByGrid( location, surfaceNormal, context, context.param( 0u ), context.param( 1u ),
                                                 float2( context.param( 3u ) > 0.5f ? 1.0f - context._uv.x : context._uv.x, context._uv.y ), 4u );
            break;
        case SW_MODIFIER_LATTICE:
        case SW_MODIFIER_FFD2:
        case SW_MODIFIER_FFD3:
        case SW_MODIFIER_FFD4:
            result = MeshDeform::lattice( location, context );
            break;
        case SW_MODIFIER_FFD2D:
            result = MeshDeform::ffd2D( location, context );
            break;
        case SW_MODIFIER_COLLISIONDEFORM:
            result = MeshDeform::collisionDeform( location, context );
            break;
        case SW_MODIFIER_HITDEFORM:
            result = MeshDeform::hitDeform( location, context );
            break;
        case SW_MODIFIER_CONFORM:
        case SW_MODIFIER_CONFORMMULTI:
            result = MeshDeform::conform( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_POINTCACHE:
            result = MeshDeform::pointCache( location, context );
            break;
        case SW_MODIFIER_VERTEXANIM:
            result = MeshDeform::vertexAnim( location, context );
            break;
        case SW_MODIFIER_PAINT:
            // 위치 · 법선을 안 바꾼다 - 색은 세션이 GPU 메시의 정점 스트림에 CPU 결과로 덮는다(MeshModifier::writesGPUVertexColor)
            break;
        default:
            break;
    }
    return result;
}

/** Rubber — 주축 밀림 = 양 · w(t), 단면 배율 = 1/√(1 + |양 · w| / L). w 는 감쇠 곡선(매개변수 1 부터 곡선 표본) */
static float3 MeshDeform::rubber( float3 location, ModifierContext context )
{
    const float amount = context.param( 0u );
    float3      result = location;
    if ( abs( amount ) >= kModifierEpsilon )
    {
        const float weight   = context.curve( 1u, saturate( MeshDeform::axisFraction( location, context ) ) );
        const float thinning = 1.0f / sqrt( 1.0f + abs( amount * weight ) / context.axisLength() );
        result               = MeshDeform::set( location * thinning, context._axis, MeshDeform::get( location, context._axis ) + amount * weight );
    }
    return result;
}

/**
 * CurveDeform — 주축 비율 t(자름)에서 변위 곡선(param 2..9)과 배율 곡선(param 10..17). 수직 두 성분을 배율로 키우고 방향 각(param 1)으로 양(param 0) · 변위(t) 만큼 민다.
 */
static float3 MeshDeform::curveDeform( float3 location, ModifierContext context )
{
    const uint  axis      = context._axis;
    const uint  axisU     = ( axis + 1u ) % 3u;
    const uint  axisV     = ( axis + 2u ) % 3u;
    const float fraction  = saturate( MeshDeform::axisFraction( location, context ) );
    const float scale     = context.curve( 10u, fraction );
    const float offset    = context.param( 0u ) * context.curve( 2u, fraction );
    const float direction = context.param( 1u ) * kDegToRad;
    float3      result    = location;
    result                = MeshDeform::set( result, axisU, MeshDeform::get( location, axisU ) * scale + offset * cos( direction ) );
    result                = MeshDeform::set( result, axisV, MeshDeform::get( location, axisV ) * scale + offset * sin( direction ) );
    return result;
}

/** CurveSculpt — 주축에서 떨어진 거리 비율을 곡선(param 0..7)에 넣은 높이 × 세기(param 8)만큼 법선으로 민다 */
static float3 MeshDeform::curveSculpt( float3 location, float3 surfaceNormal, ModifierContext context )
{
    const uint   axis   = context._axis;
    const uint   axisU  = ( axis + 1u ) % 3u;
    const uint   axisV  = ( axis + 2u ) % 3u;
    const float3 size   = context._boundsMax - context._boundsMin;
    const float3 center = ( context._boundsMax + context._boundsMin ) * 0.5f;
    float        radius = 0.5f * max( MeshDeform::get( size, axisU ), MeshDeform::get( size, axisV ) );
    radius              = radius > kModifierEpsilon ? radius : 1.0f;
    const float coordU  = MeshDeform::get( location, axisU ) - MeshDeform::get( center, axisU );
    const float coordV  = MeshDeform::get( location, axisV ) - MeshDeform::get( center, axisV );
    const float ratio   = saturate( sqrt( coordU * coordU + coordV * coordV ) / radius );
    const float height  = context.param( 8u ) * context.curve( 0u, ratio );
    return location + surfaceNormal * height;
}

/**
 * CurveSculptLayered — 공용 Shape 곡선(param 0..7) 뒤에 층 표(param 8: 항목 수 · 원소 4 = u v 반경 높이 · 평탄한 값), 그 뒤에 층마다 낙하 곡선 표본(float4 8 개)이 층 수만큼 이어진다.
 * 자기 곡선이 없는 층은 CPU 쪽 싣기가 공용 곡선을 채워 놓았다.
 */
static float3 MeshDeform::curveSculptLayered( float3 location, float3 surfaceNormal, ModifierContext context )
{
    const uint layerCount = context.listCount( 8u );
    float3     result     = location;
    if ( layerCount > 0u )
    {
        const uint   axis      = context._axis;
        const uint   axisU     = ( axis + 1u ) % 3u;
        const uint   axisV     = ( axis + 2u ) % 3u;
        const float3 size      = context._boundsMax - context._boundsMin;
        const float  sizeU     = max( MeshDeform::get( size, axisU ), kModifierEpsilon );
        const float  sizeV     = max( MeshDeform::get( size, axisV ), kModifierEpsilon );
        const float  ratioU    = ( MeshDeform::get( location, axisU ) - MeshDeform::get( context._boundsMin, axisU ) ) / sizeU;
        const float  ratioV    = ( MeshDeform::get( location, axisV ) - MeshDeform::get( context._boundsMin, axisV ) ) / sizeV;
        const uint   curveBase = 9u + layerCount; // 층 표 머리(1) + 원소 layerCount * 4 / 4 개 뒤
        float        height    = 0.0f;
        for ( uint layer = 0u; layer < layerCount; ++layer )
        {
            const uint  item     = layer * 4u;
            const float radius   = context.listValue( 8u, item + 2u );
            const float deltaU   = ratioU - context.listValue( 8u, item );
            const float deltaV   = ratioV - context.listValue( 8u, item + 1u );
            const float distance = sqrt( deltaU * deltaU + deltaV * deltaV ) / max( radius, kModifierEpsilon );
            if ( radius > kModifierEpsilon && distance < 1.0f )
                height += context.listValue( 8u, item + 3u ) * context.curve( curveBase + layer * ( SW_MODIFIER_CURVE_SAMPLES / 4u ), distance );
        }
        result = location + surfaceNormal * height;
    }
    return result;
}

/**
 * 경로 표(`PathModifierUtil::packGPUPathTable`: float4(얻음, 길이) 뒤에 표본마다 위치 · 접선 · 단면 축 V 셋)에서 호 길이 distance 자리에 단면 좌표 (u, v) 를 놓는다 — `tryPlaceOnPath`.
 * 이웃 표본을 선형 보간하고 접선에 V 를 다시 직교화한다. 경로 밖은 끝 접선을 따라 곧게 잇는다.
 */
static float3 MeshDeform::placeOnPath( ModifierContext context, uint pathBase, float distance, bool bLoop, float coordU, float coordV )
{
    const float  length    = max( context.raw( pathBase ).y, kModifierEpsilon );
    const float  along     = bLoop ? distance - floor( distance / length ) * length : distance;
    const float  clamped   = clamp( along, 0.0f, length );
    const float  scaled    = saturate( clamped / length ) * (float)SW_MODIFIER_PATH_SEGMENTS;
    const uint   lower     = min( (uint)scaled, (uint)( SW_MODIFIER_PATH_SEGMENTS - 1 ) );
    const float  ratio     = scaled - (float)lower;
    const uint   sampleAt  = pathBase + 1u + lower * 3u;
    const float3 positionA = context.raw( sampleAt ).xyz;
    const float3 positionB = context.raw( sampleAt + 3u ).xyz;
    const float3 tangentA  = context.raw( sampleAt + 1u ).xyz;
    const float3 tangentB  = context.raw( sampleAt + 4u ).xyz;
    const float3 axisVA    = context.raw( sampleAt + 2u ).xyz;
    const float3 axisVB    = context.raw( sampleAt + 5u ).xyz;
    const float3 position  = lerp( positionA, positionB, ratio );
    const float3 tangent   = MeshDeform::normalizeOr( lerp( tangentA, tangentB, ratio ), tangentA );
    const float3 blendedV  = lerp( axisVA, axisVB, ratio );
    const float3 axisV     = MeshDeform::normalizeOr( blendedV - tangent * dot( blendedV, tangent ), axisVA );
    const float3 axisU     = MeshDeform::normalizeOr( cross( axisV, tangent ), float3( 1.0f, 0.0f, 0.0f ) );
    return position + tangent * ( along - clamped ) + axisU * coordU + axisV * coordV;
}

/** RopeDeform — 매개변수 0 반경 · 1..8 반경 곡선 · 9 시작 · 10 끝, 경로 표는 11 부터(월드 공간 경로) */
static float3 MeshDeform::ropeDeform( float3 location, ModifierContext context )
{
    const float4 pathHeader = context.raw( 11u );
    float3       result     = location;
    if ( pathHeader.x > 0.5f )
    {
        const uint  axis     = context._axis;
        const float fraction = MeshDeform::axisFraction( location, context );
        const float start    = context.param( 9u );
        const float end      = context.param( 10u );
        const float along    = ( start + ( end - start ) * fraction ) * pathHeader.y;
        const float scale    = context.param( 0u ) * context.curve( 1u, saturate( fraction ) );
        const float coordU   = MeshDeform::get( location, ( axis + 1u ) % 3u ) * scale;
        const float coordV   = MeshDeform::get( location, ( axis + 2u ) % 3u ) * scale;
        result               = MeshDeform::placeOnPath( context, 11u, along, false, coordU, coordV );
    }
    return result;
}

/**
 * PathDeform · WorldPathDeform — 매개변수 0 늘이기 · 1 밀기(경로 변형은 길이 비율, 월드 변형은 거리) · 2 회전 · 3 비틀기 · 4 반복, 경로 표는 5 부터.
 * 경로 변형의 표는 경로 자기 공간, 월드 변형의 표는 월드 공간 좌표를 모디파이어 공간으로 옮긴 것이다(CPU 가 싣는다 - 셰이더는 같다).
 */
static float3 MeshDeform::pathDeform( float3 location, ModifierContext context, bool bWorld )
{
    const float4 pathHeader = context.raw( 5u );
    float3       result     = location;
    if ( pathHeader.x > 0.5f )
    {
        const uint  axis     = context._axis;
        const float height   = MeshDeform::get( location, axis ) - MeshDeform::get( context._boundsMin, axis );
        const float distance = ( bWorld ? context.param( 1u ) : context.param( 1u ) * pathHeader.y ) + height * context.param( 0u );
        const float fraction = MeshDeform::axisFraction( location, context );
        const float radian   = ( context.param( 2u ) + context.param( 3u ) * fraction ) * kDegToRad;
        const float baseU    = MeshDeform::get( location, ( axis + 1u ) % 3u );
        const float baseV    = MeshDeform::get( location, ( axis + 2u ) % 3u );
        const float coordU   = baseU * cos( radian ) - baseV * sin( radian );
        const float coordV   = baseU * sin( radian ) + baseV * cos( radian );
        result               = MeshDeform::placeOnPath( context, 5u, distance, context.param( 4u ) > 0.5f, coordU, coordV );
    }
    return result;
}

/** 텍스처 격자 변위 — 격자 머리(얻음)가 headerParam, 격자는 그 다음. 값 = 격자(uv) - 중간값, 법선으로 amount 만큼 민다. 못 얻었거나 양이 0 이면 그대로. */
static float3 MeshDeform::displaceByGrid( float3 location, float3 surfaceNormal, ModifierContext context, float amount, float midLevel, float2 uv, uint headerParam )
{
    float3 result = location;
    if ( abs( amount ) >= kModifierEpsilon && context.raw( headerParam ).x > 0.5f )
        result = location + surfaceNormal * ( amount * ( context.textureGrid( headerParam + 1u, uv ) - midLevel ) );
    return result;
}

/** 베른슈타인 기저(드 카스텔조) — degree + 1 개를 채운다(나머지는 0). Lattice · FFD2D 의 `computeBernstein` */
static void MeshDeform::bernstein( uint degree, float t, out float outWeight[8] )
{
    outWeight[0]          = 1.0f;
    outWeight[1]          = 0.0f;
    outWeight[2]          = 0.0f;
    outWeight[3]          = 0.0f;
    outWeight[4]          = 0.0f;
    outWeight[5]          = 0.0f;
    outWeight[6]          = 0.0f;
    outWeight[7]          = 0.0f;
    const float oneMinusT = 1.0f - t;
    for ( uint order = 1u; order <= degree; ++order )
    {
        float carry = 0.0f;
        for ( uint index = 0u; index < order; ++index )
        {
            const float value = outWeight[index];
            outWeight[index]  = carry + oneMinusT * value;
            carry             = t * value;
        }
        outWeight[order] = carry;
    }
}

/**
 * Lattice(ffd2 · ffd3 · ffd4 포함) — 매개변수 0..2 해상도, 입력 블록 3 부터: float4(격자 수 x y z, 1) · 상자 최소 · 상자 최대, 뒤에 제어점(정규 좌표)이
 * x 가 가장 빨리 도는 순서로 하나씩. 3 중 베른슈타인 합이다.
 */
static float3 MeshDeform::lattice( float3 location, ModifierContext context )
{
    const float4 counts     = context.raw( 3u );
    const uint   countX     = clamp( (uint)( counts.x + 0.5f ), 2u, 8u );
    const uint   countY     = clamp( (uint)( counts.y + 0.5f ), 2u, 8u );
    const uint   countZ     = clamp( (uint)( counts.z + 0.5f ), 2u, 8u );
    const float3 boxMin     = context.raw( 4u ).xyz;
    const float3 size       = context.raw( 5u ).xyz - boxMin;
    const float3 normalized = float3( size.x > kModifierEpsilon ? ( location.x - boxMin.x ) / size.x : 0.0f, size.y > kModifierEpsilon ? ( location.y - boxMin.y ) / size.y : 0.0f,
                                      size.z > kModifierEpsilon ? ( location.z - boxMin.z ) / size.z : 0.0f );
    float weightX[8];
    float weightY[8];
    float weightZ[8];
    MeshDeform::bernstein( countX - 1u, normalized.x, weightX );
    MeshDeform::bernstein( countY - 1u, normalized.y, weightY );
    MeshDeform::bernstein( countZ - 1u, normalized.z, weightZ );
    float3 moved = float3( 0.0f, 0.0f, 0.0f );
    for ( uint indexZ = 0u; indexZ < countZ; ++indexZ )
    {
        for ( uint indexY = 0u; indexY < countY; ++indexY )
        {
            const float weightYZ = weightY[indexY] * weightZ[indexZ];
            for ( uint indexX = 0u; indexX < countX; ++indexX )
                moved += context.raw( 6u + indexX + countX * ( indexY + countY * indexZ ) ).xyz * ( weightX[indexX] * weightYZ );
        }
    }
    return float3( size.x > kModifierEpsilon ? boxMin.x + moved.x * size.x : location.x, size.y > kModifierEpsilon ? boxMin.y + moved.y * size.y : location.y,
                   size.z > kModifierEpsilon ? boxMin.z + moved.z * size.z : location.z );
}

/** FFD2D — 매개변수 0 · 1 해상도, 오프셋 목록(항목 = dx dy, 상자 크기 비율)은 2 부터. XY 평면만 움직이고 Z 는 그대로 */
static float3 MeshDeform::ffd2D( float3 location, ModifierContext context )
{
    const uint   pointCount = context.listCount( 2u );
    const float3 size       = context._boundsMax - context._boundsMin;
    const bool   bValid     = context._boundsValid > 0.5f;
    const bool   bUseX      = bValid && size.x > kModifierEpsilon;
    const bool   bUseY      = bValid && size.y > kModifierEpsilon;
    float3       result     = location;
    if ( pointCount > 0u && ( bUseX || bUseY ) )
    {
        const uint  countX = clamp( (uint)max( context.paramInt( 0u ), 0 ), 2u, 8u );
        const uint  countY = clamp( (uint)max( context.paramInt( 1u ), 0 ), 2u, 8u );
        const float coordS = bUseX ? ( location.x - context._boundsMin.x ) / size.x : 0.0f;
        const float coordT = bUseY ? ( location.y - context._boundsMin.y ) / size.y : 0.0f;
        float       weightX[8];
        float       weightY[8];
        MeshDeform::bernstein( countX - 1u, coordS, weightX );
        MeshDeform::bernstein( countY - 1u, coordT, weightY );
        float deltaS = 0.0f;
        float deltaT = 0.0f;
        for ( uint indexY = 0u; indexY < countY; ++indexY )
        {
            for ( uint indexX = 0u; indexX < countX; ++indexX )
            {
                const uint controlIndex = indexX + countX * indexY;
                if ( controlIndex < pointCount )
                {
                    const float weight = weightX[indexX] * weightY[indexY];
                    deltaS += context.listValue( 2u, controlIndex * 2u ) * weight;
                    deltaT += context.listValue( 2u, controlIndex * 2u + 1u ) * weight;
                }
            }
        }
        result = float3( bUseX ? location.x + deltaS * size.x : location.x, bUseY ? location.y + deltaT * size.y : location.y, location.z );
    }
    return result;
}

/** CollisionDeform 의 구 · 상자 · 평면 밀어내기 — `CollisionDeformModifierInternal` 과 같은 식(한 출구) */
static float3 MeshDeform::pushOutOfSphere( float3 pos, float3 center, float radius )
{
    const float3 offset = pos - center;
    return length( offset ) >= radius ? pos : center + MeshDeform::normalizeOr( offset, float3( 0.0f, 1.0f, 0.0f ) ) * radius;
}

static float3 MeshDeform::pushOutOfBox( float3 pos, float3 center, float3 halfSize )
{
    const float3 localPoint = pos - center;
    float        bestDepth  = 3.0e38f;
    uint         bestAxis   = 0u;
    bool         bOutside   = false;
    for ( uint axis = 0u; axis < 3u; ++axis )
    {
        const float depth = MeshDeform::get( halfSize, axis ) - abs( MeshDeform::get( localPoint, axis ) );
        bOutside          = bOutside || depth <= 0.0f; // 한 축이라도 밖이면 상자 밖이다
        if ( depth < bestDepth )
        {
            bestDepth = depth;
            bestAxis  = axis;
        }
    }
    float3      result = pos;
    const float value  = MeshDeform::get( localPoint, bestAxis );
    const float sign   = value >= 0.0f ? 1.0f : -1.0f;
    if ( bOutside == false )
        result = MeshDeform::set( pos, bestAxis, MeshDeform::get( center, bestAxis ) + sign * MeshDeform::get( halfSize, bestAxis ) );
    return result;
}

static float3 MeshDeform::pushOutOfPlane( float3 pos, float3 center, float3 normalDirection, float margin )
{
    const float3 planeNormal = MeshDeform::normalizeOr( normalDirection, float3( 0.0f, 1.0f, 0.0f ) );
    const float  distance    = dot( pos - center, planeNormal );
    return distance >= margin ? pos : pos + planeNormal * ( margin - distance );
}

/** CollisionDeform — 매개변수 0 여유 · 1 부드러움, 충돌체 목록(항목 = 종류 0 구 · 1 상자 · 2 평면, 중심 xyz, 크기 xyz)은 2 부터. 목록 순서대로 차례로 건다 */
static float3 MeshDeform::collisionDeform( float3 location, ModifierContext context )
{
    const uint  count    = context.listCount( 2u );
    const float margin   = context.param( 0u );
    const float keep     = saturate( 1.0f - context.param( 1u ) );
    float3      position = location;
    for ( uint index = 0u; index < count; ++index )
    {
        const uint   item   = index * 7u;
        const int    kind   = clamp( (int)floor( context.listValue( 2u, item ) + 0.5f ), 0, 2 );
        const float3 center = context.listVec3( 2u, item + 1u );
        const float3 size   = context.listVec3( 2u, item + 4u );
        float3       target = position;
        if ( kind == 0 )
            target = MeshDeform::pushOutOfSphere( position, center, abs( size.x ) + margin );
        else if ( kind == 1 )
            target = MeshDeform::pushOutOfBox( position, center, abs( size ) + margin );
        else
            target = MeshDeform::pushOutOfPlane( position, center, size, margin );
        position = position + ( target - position ) * keep;
    }
    return position;
}

/** HitDeform — 매개변수 0 배율, 충돌점 목록(항목 = 위치 xyz, 방향 xyz, 반경, 세기, 감쇠)은 1 부터. 반경 안 점마다 방향으로 세기 × 감쇠를 더한다 */
static float3 MeshDeform::hitDeform( float3 location, ModifierContext context )
{
    const uint count  = context.listCount( 1u );
    float3     offset = float3( 0.0f, 0.0f, 0.0f );
    for ( uint index = 0u; index < count; ++index )
    {
        const uint   item      = index * 9u;
        const float3 center    = context.listVec3( 1u, item );
        const float3 direction = MeshDeform::normalizeOr( context.listVec3( 1u, item + 3u ), float3( 0.0f, -1.0f, 0.0f ) );
        const float  radius    = abs( context.listValue( 1u, item + 6u ) );
        const float  distance  = length( location - center );
        if ( radius >= kModifierEpsilon && distance < radius )
        {
            const float exponent = 1.0f + max( context.listValue( 1u, item + 8u ), 0.0f );
            offset += direction * ( context.listValue( 1u, item + 7u ) * pow( 1.0f - distance / radius, exponent ) );
        }
    }
    return location + offset * context.param( 0u );
}

/** MeshModifierNoise::hashToUnit — 정수 하나를 [0, 1) 로 섞는다(정점 위상 · 1 차원 노이즈) */
static float MeshDeform::hashToUnit( uint value )
{
    uint mixed = value;
    mixed ^= mixed >> 16;
    mixed *= 0x7feb352du;
    mixed ^= mixed >> 15;
    mixed *= 0x846ca68bu;
    mixed ^= mixed >> 16;
    return (float)( mixed & 0x00ffffffu ) / 16777216.0f;
}

/** MeshModifierNoise::valueNoise1D — 씨앗별 1 차원 값 노이즈 [-1, 1] */
static float MeshDeform::valueNoise1D( uint seed, float time )
{
    const float safeTime = clamp( time, -1.0e6f, 1.0e6f );
    const float cell     = floor( safeTime );
    const float ratio    = safeTime - cell;
    const int   index    = (int)cell;
    const float valueA   = MeshDeform::hashToUnit( seed * 0x9E3779B9u + asuint( index ) );
    const float valueB   = MeshDeform::hashToUnit( seed * 0x9E3779B9u + asuint( index + 1 ) );
    const float weight   = ratio * ratio * ( 3.0f - 2.0f * ratio );
    return ( valueA + ( valueB - valueA ) * weight ) * 2.0f - 1.0f;
}

/** VertexAnim — 매개변수 0 진폭(vec3) · 1 속도 · 2 위상 변화 · 3 노이즈 섞기 · 4 씨앗. 정점 번호(메시 안)로 정점마다 다른 위상을 준다 */
static float3 MeshDeform::vertexAnim( float3 location, ModifierContext context )
{
    const float3 amplitude = context.param3( 0u );
    float3       result    = location;
    if ( dot( amplitude, amplitude ) >= kModifierEpsilon * kModifierEpsilon )
    {
        const uint  seed     = (uint)max( context.paramInt( 4u ), 0 ) * 7919u + context._vertexIndex;
        const float phase    = MeshDeform::hashToUnit( seed ) * context.param( 2u );
        const float cycle    = context._time * context.param( 1u ) + phase;
        const float noiseMix = context.param( 3u );
        float3      offset   = float3( 0.0f, 0.0f, 0.0f );
        for ( uint axis = 0u; axis < 3u; ++axis )
        {
            const float axisCycle = cycle + (float)axis * 0.25f;
            const float sine      = sin( kTwoPi * axisCycle );
            const float noise     = MeshDeform::valueNoise1D( seed * 3u + axis, axisCycle );
            offset                = MeshDeform::set( offset, axis, MeshDeform::get( amplitude, axis ) * ( sine + ( noise - sine ) * noiseMix ) );
        }
        result = location + offset;
    }
    return result;
}

/** PointCache — 매개변수 0 속도 · 1 시간 오프셋 · 2 반복, 입력 3: float4(얻음) 뒤에 정점마다 이 시각의 목표 위치(모디파이어 공간). 정점 번호로 읽는다 */
static float3 MeshDeform::pointCache( float3 location, ModifierContext context )
{
    return context.raw( 3u ).x > 0.5f ? context.raw( 4u + context._vertexIndex ).xyz : location;
}

/** 점에서 삼각형 위 가장 가까운 점 — `PathModifierUtil::findClosestOnTriangle`(Ericson 5.1.5)와 같은 영역 순서(a · b · ab · c · ac · bc · 안)다 */
static float3 MeshDeform::closestOnTriangle( float3 pos, float3 a, float3 b, float3 c )
{
    const float3 ab          = b - a;
    const float3 ac          = c - a;
    const float  d1          = dot( ab, pos - a );
    const float  d2          = dot( ac, pos - a );
    const float  d3          = dot( ab, pos - b );
    const float  d4          = dot( ac, pos - b );
    const float  d5          = dot( ab, pos - c );
    const float  d6          = dot( ac, pos - c );
    const float  vc          = d1 * d4 - d3 * d2;
    const float  vb          = d5 * d2 - d1 * d6;
    const float  va          = d3 * d6 - d5 * d4;
    const float  denominator = va + vb + vc;
    float3       result      = abs( denominator ) < kModifierEpsilon ? a : a + ab * ( vb / denominator ) + ac * ( vc / denominator );
    // 우선순위가 낮은 영역부터 덮어 쓴다(마지막에 덮인 것이 CPU 의 첫 번째 반환이다)
    result = ( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f ) ? b + ( c - b ) * ( ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) ) ) : result;
    result = ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f ) ? a + ac * ( d2 / ( d2 - d6 ) ) : result;
    result = ( d6 >= 0.0f && d5 <= d6 ) ? c : result;
    result = ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f ) ? a + ab * ( d1 / ( d1 - d3 ) ) : result;
    result = ( d3 >= 0.0f && d4 <= d3 ) ? b : result;
    result = ( d1 <= 0.0f && d2 <= 0.0f ) ? a : result;
    return result;
}

/** 반직선과 삼각형(양면)의 교차 — `CharacterGeometryUtil::intersectRayTriangle`(Möller-Trumbore, 무게중심 여유 1e-5). 맞으면 true 와 거리 */
static bool MeshDeform::intersectRayTriangle( float3 origin, float3 direction, float3 a, float3 b, float3 c, out float outDistance )
{
    const float  slack       = 1.0e-5f;
    const float3 edge1       = b - a;
    const float3 edge2       = c - a;
    const float3 pVector     = cross( direction, edge2 );
    const float  determinant = dot( edge1, pVector );
    const float  inverseDet  = 1.0f / ( abs( determinant ) < 1.0e-12f ? 1.0f : determinant );
    const float3 tVector     = origin - a;
    const float  u           = dot( tVector, pVector ) * inverseDet;
    const float3 qVector     = cross( tVector, edge1 );
    const float  v           = dot( direction, qVector ) * inverseDet;
    const float  distance    = dot( edge2, qVector ) * inverseDet;
    outDistance              = distance;
    return abs( determinant ) >= 1.0e-12f && u >= -slack && u <= 1.0f + slack && v >= -slack && u + v <= 1.0f + slack && distance >= 0.0f;
}

/** 점과 상자의 거리 제곱 — BVH 최근접점 질의의 가지치기 */
static float MeshDeform::pointBoxDistanceSq( float3 pos, float3 boxMin, float3 boxMax )
{
    const float3 outside = max( max( boxMin - pos, pos - boxMax ), float3( 0.0f, 0.0f, 0.0f ) );
    return dot( outside, outside );
}

/** 반직선이 상자를 maxDistance 안에서 지나는가(슬랩 판정) — BVH 광선 질의의 가지치기 */
static bool MeshDeform::rayHitsBox( float3 origin, float3 direction, float3 boxMin, float3 boxMax, float maxDistance )
{
    const float3 safeDirection = float3( abs( direction.x ) < 1.0e-20f ? 1.0e-20f : direction.x, abs( direction.y ) < 1.0e-20f ? 1.0e-20f : direction.y,
                                         abs( direction.z ) < 1.0e-20f ? 1.0e-20f : direction.z );
    const float3 inverse = 1.0f / safeDirection;
    const float3 t0      = ( boxMin - origin ) * inverse;
    const float3 t1      = ( boxMax - origin ) * inverse;
    const float3 nearT   = min( t0, t1 );
    const float3 farT    = max( t0, t1 );
    const float  tNear   = max( max( nearT.x, nearT.y ), max( nearT.z, 0.0f ) );
    const float  tFar    = min( min( farT.x, farT.y ), farT.z );
    return tNear <= tFar && tNear <= maxDistance;
}

/**
 * Conform · ConformMulti — 매개변수 0 방식(0 가장 가까운 점 · 1 주축 음의 방향 · 2 법선 음의 방향) · 1 띄우기 · 2 최대 거리 · 3 양, 입력 4: float4(얻음, 삼각형 수, 노드 수)
 * 뒤에 노드 표(노드마다 float4 둘 - 상자 최소 · w 첫 자식/첫 삼각형, 상자 최대 · w 잎의 삼각형 수)와 삼각형 표(삼각형마다 float4 셋). `ConformGPUTable` 이 짓는 BVH 를 스택으로 훑는다 -
 * CPU `ConformSurface::findClosestPoint` · `findNearestHit` 와 같은 질의다(남은 거리를 줄여 가며 가지치기).
 */
static float3 MeshDeform::conform( float3 location, float3 surfaceNormal, ModifierContext context )
{
    const float4 header = context.raw( 4u );
    const float  amount = context.param( 3u );
    float3       result = location;
    if ( header.x > 0.5f && amount > 0.0f )
    {
        const uint   nodeBase      = 5u;
        const uint   triangleBase  = nodeBase + (uint)( header.z + 0.5f ) * 2u;
        const int    mode          = clamp( context.paramInt( 0u ), 0, 2 );
        const float3 axisDirection = MeshDeform::set( float3( 0.0f, 0.0f, 0.0f ), context._axis, -1.0f );
        const float3 rayDirection  = mode == 2 ? MeshDeform::normalizeOr( -surfaceNormal, axisDirection ) : axisDirection;
        float        bestDistance  = context.param( 2u ); // 최대 거리로 시작해 찾을 때마다 줄어든다
        bool         bFound        = false;
        float3       bestPoint     = float3( 0.0f, 0.0f, 0.0f );
        float3       bestNormal    = float3( 0.0f, 1.0f, 0.0f );
        uint         stack[32];
        uint         stackSize = 1u;
        stack[0]               = 0u;
        while ( stackSize > 0u )
        {
            stackSize -= 1u;
            const uint   nodeIndex = stack[stackSize];
            const float4 nodeA     = context.raw( nodeBase + nodeIndex * 2u );
            const float4 nodeB     = context.raw( nodeBase + nodeIndex * 2u + 1u );
            const bool   bVisit    = mode == 0 ? MeshDeform::pointBoxDistanceSq( location, nodeA.xyz, nodeB.xyz ) <= bestDistance * bestDistance
                                               : MeshDeform::rayHitsBox( location, rayDirection, nodeA.xyz, nodeB.xyz, bestDistance );
            const uint   first     = (uint)( nodeA.w + 0.5f );
            const uint   count     = (uint)( nodeB.w + 0.5f );
            if ( bVisit && count == 0u )
            {
                // 안쪽 노드 - 둘째 자식을 쌓고 첫째 자식을 먼저 본다(스택 깊이는 트리 깊이 정도다)
                stack[stackSize]      = first + 1u;
                stack[stackSize + 1u] = first;
                stackSize += 2u;
            }
            if ( bVisit && count > 0u )
            {
                for ( uint leafIndex = 0u; leafIndex < count; ++leafIndex )
                {
                    const uint   at        = triangleBase + ( first + leafIndex ) * 3u;
                    const float3 a         = context.raw( at ).xyz;
                    const float3 b         = context.raw( at + 1u ).xyz;
                    const float3 c         = context.raw( at + 2u ).xyz;
                    float3       candidate = location;
                    float        distance  = 0.0f;
                    bool         bHit      = true;
                    if ( mode == 0 )
                    {
                        candidate = MeshDeform::closestOnTriangle( location, a, b, c );
                        distance  = length( candidate - location );
                    }
                    else
                    {
                        bHit      = MeshDeform::intersectRayTriangle( location, rayDirection, a, b, c, distance );
                        candidate = location + rayDirection * distance;
                    }
                    if ( bHit && distance <= bestDistance )
                    {
                        bestDistance = distance;
                        bFound       = true;
                        bestPoint    = candidate;
                        bestNormal   = cross( b - a, c - a );
                    }
                }
            }
        }
        if ( bFound )
        {
            float3 normal = MeshDeform::normalizeOr( bestNormal, float3( 0.0f, 1.0f, 0.0f ) );
            normal        = dot( normal, location - bestPoint ) < 0.0f ? -normal : normal;
            result        = lerp( location, bestPoint + normal * context.param( 1u ), saturate( amount ) );
        }
    }
    return result;
}

static float3 MeshDeform::transformPoint( float3 location, uint row )
{
    return location.x * g_DeformPrograms[row].xyz + location.y * g_DeformPrograms[row + 1u].xyz + location.z * g_DeformPrograms[row + 2u].xyz + g_DeformPrograms[row + 3u].xyz;
}

static float3 MeshDeform::transformVector( float3 direction, uint row )
{
    return direction.x * g_DeformPrograms[row].xyz + direction.y * g_DeformPrograms[row + 1u].xyz + direction.z * g_DeformPrograms[row + 2u].xyz;
}

/**
 * 프로그램을 정점 하나에 차례로 건다 — MeshModifier::apply(공간 변환) + 기본 applyToMesh(점 변형 · 야코비안 법선)와 같은 순서다.
 * 가중치 채널은 없다(모두 1). 위치와 법선은 메시 로컬 공간이다. vertexIndex 는 이 메시 안의 정점 번호다(UV 표를 읽는다).
 */
static void MeshDeform::applyProgram( uint programBase, uint vertexIndex, inout float3 inoutPosition, inout float3 inoutNormal )
{
    const float4 header = g_DeformPrograms[programBase];
    const uint   count  = (uint)header.x;
    uint         cursor = programBase + 1u;
    // 정점 UV - 텍스처 모디파이어가 있을 때만 프로그램 끝에 표가 있다(머리 z). vertexIndex 는 메시 안 정점 번호다
    float2 vertexUV = float2( 0.0f, 0.0f );
    if ( header.z > 0.5f && vertexIndex < (uint)header.w )
    {
        const float4 uvPair = g_DeformPrograms[programBase + (uint)header.z + vertexIndex / 2u];
        vertexUV            = ( vertexIndex & 1u ) != 0u ? uvPair.zw : uvPair.xy;
    }
    for ( uint modifierIndex = 0u; modifierIndex < count; ++modifierIndex )
    {
        const float4    record    = g_DeformPrograms[cursor];
        const float4    boundsMin = g_DeformPrograms[cursor + 1u];
        ModifierContext context;
        context._boundsMin   = boundsMin.xyz;
        context._boundsValid = boundsMin.w;
        context._boundsMax   = g_DeformPrograms[cursor + 2u].xyz;
        context._axis        = (uint)record.z;
        context._time        = header.y;
        context._uv          = vertexUV;
        context._vertexIndex = vertexIndex;
        context._paramBase   = cursor + SW_MODIFIER_RECORD_FLOAT4;
        const uint  kind     = (uint)record.x;
        const float step     = record.w;
        // 이 기록에 들어오는 정점별 선택 가중치(상자 최대 float4 의 w = 기록 시작부터의 표 위치, 0 이면 모두 1) - CPU `DeformJob` 처럼 섞는 세기다
        const float weightStart = g_DeformPrograms[cursor + 2u].w;
        const float weight      = weightStart > 0.5f ? saturate( g_DeformPrograms[cursor + (uint)weightStart + vertexIndex / 4u][vertexIndex % 4u] ) : 1.0f;

        const float3 location      = MeshDeform::transformPoint( inoutPosition, cursor + 3u );
        const float3 surfaceNormal = MeshDeform::normalizeOr( MeshDeform::transformVector( inoutNormal, cursor + 11u ), float3( 0.0f, 1.0f, 0.0f ) );

        // 법선에 수직인 접선 둘을 조금 밀어 변형된 접선을 얻고, 그 외적이 새 법선이다(t1 × t2 = surfaceNormal).
        // 점 셋(원래 · 접선 1 · 접선 2)을 루프 하나로 건다 - 펼쳐 부르면 종류 분기가 세 벌 복사돼 셰이더가 세 배로 커진다
        const float3 helper   = abs( surfaceNormal.x ) < 0.9f ? float3( 1.0f, 0.0f, 0.0f ) : float3( 0.0f, 1.0f, 0.0f );
        const float3 tangent1 = MeshDeform::normalizeOr( cross( surfaceNormal, helper ), float3( 0.0f, 0.0f, 1.0f ) );
        const float3 tangent2 = cross( surfaceNormal, tangent1 );
        float3       moved    = location;
        float3       shifted1 = location;
        float3       shifted2 = location;
        SW_LOOP for ( uint sampleIndex = 0u; sampleIndex < 3u; ++sampleIndex )
        {
            const float3 offset   = sampleIndex == 1u ? tangent1 * step : ( sampleIndex == 2u ? tangent2 * step : float3( 0.0f, 0.0f, 0.0f ) );
            const float3 sampleAt = location + offset;
            const float3 deformed = lerp( sampleAt, MeshDeform::deformPoint( kind, sampleAt, surfaceNormal, context ), weight );
            moved                 = sampleIndex == 0u ? deformed : moved;
            shifted1              = sampleIndex == 1u ? deformed : shifted1;
            shifted2              = sampleIndex == 2u ? deformed : shifted2;
        }
        const float3 movedN = MeshDeform::normalizeOr( cross( shifted1 - moved, shifted2 - moved ), surfaceNormal );

        inoutPosition = MeshDeform::transformPoint( moved, cursor + 7u );
        inoutNormal   = MeshDeform::normalizeOr( MeshDeform::transformVector( movedN, cursor + 14u ), float3( 0.0f, 1.0f, 0.0f ) );
        cursor += SW_MODIFIER_RECORD_FLOAT4 + (uint)record.y;
    }
}

#endif // SW_ENGINE_MESHMODIFIER_HLSLI
