/**
 * meshmodifier.hlsli — 메시 모디파이어 스택의 GPU 실행기(uber). meshmorph.hlsl 이 include 한다.
 *
 * CPU 정본은 확장 모듈 `GF_EditorPlugIn_MeshModifiers`(Source/GameFramework/PlugIn/Editor/MeshModifiers)다. 여기 함수는 그 `deformPoint` 를
 * 한 줄씩 옮긴 것이고, 결과가 CPU 와 같은지는 MeshModifierGPUTest.ProgramMatchesCPU 가 네 백엔드에서 그림으로 본다.
 * 모디파이어마다 셰이더를 만들지 않고 종류 번호 + 매개변수 목록을 차례로 건다(uber 셰이더 하나).
 *
 * [프로그램 모양] float4 목록(`Mesh::setGPUDeformProgram`). 정수도 float 값으로 담는다(2^24 미만 정확, 비트 재해석은 비정규 수로 지워질 수 있다).
 *   머리   [0] x = 모디파이어 수, y = 시간(초)
 *   기록마다 SW_MODIFIER_RECORD_FLOAT4 개 + 매개변수:
 *     [0]  x = 종류 번호(아래 SW_MODIFIER_*), y = 매개변수 float4 수, z = 주축(0 X · 1 Y · 2 Z), w = 야코비안 걸음
 *     [1]  xyz = 입력 상자 최소(모디파이어 공간), w = 상자가 유효하면 1
 *     [2]  xyz = 입력 상자 최대
 *     [3..6]   로컬 → 모디파이어 행렬의 행(행 벡터 규약 v·M, 행 3 이 평행 이동)
 *     [7..10]  모디파이어 → 로컬
 *     [11..13] 로컬 → 모디파이어 법선 행렬(역전치)의 행 0..2
 *     [14..16] 모디파이어 → 로컬 법선 행렬의 행 0..2
 *     그 뒤 매개변수: 선언 순서대로 스칼라는 x, Vector3 는 xyz(`MeshModifier::packGPUParams` 기본 배치)
 *
 * 주의: 함수는 한 출구로 쓴다(early return 없음). OpenGL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다(RHIBackendInfo.h).
 */
#ifndef SW_ENGINE_MESHMODIFIER_HLSLI
#define SW_ENGINE_MESHMODIFIER_HLSLI

#define SW_MODIFIER_RECORD_FLOAT4 17

// 종류 번호 — C++ 표 `MeshModifierGPUProgram.cpp` 의 순서와 같다(테스트가 supportsGPU 집합과 맞는지 본다)
#define SW_MODIFIER_BEND           0
#define SW_MODIFIER_TWIST          1
#define SW_MODIFIER_TAPER          2
#define SW_MODIFIER_STRETCH        3
#define SW_MODIFIER_WAVE           4
#define SW_MODIFIER_RIPPLE         5
#define SW_MODIFIER_NOISE          6
#define SW_MODIFIER_MELT           7
#define SW_MODIFIER_PUSH           8
#define SW_MODIFIER_POKE           9
#define SW_MODIFIER_DISPLACE       10
#define SW_MODIFIER_BUBBLE         11
#define SW_MODIFIER_BULGE          12
#define SW_MODIFIER_CYLINDRIFY     13
#define SW_MODIFIER_GLOBE          14
#define SW_MODIFIER_HUMP           15
#define SW_MODIFIER_RADIALSKEW     16
#define SW_MODIFIER_SKEW           17
#define SW_MODIFIER_SPHERIFY       18
#define SW_MODIFIER_SQUEEZE        19
#define SW_MODIFIER_SCALE          20
#define SW_MODIFIER_SINUSCURVE     21
#define SW_MODIFIER_ROLLED         22
#define SW_MODIFIER_SIMPLEMOD      23
#define SW_MODIFIER_CRUMPLE        24
#define SW_MODIFIER_DYNAMICRIPPLE  25
#define SW_MODIFIER_PAGEFLIP       26
#define SW_MODIFIER_TREEBEND       27
#define SW_MODIFIER_VERTNOISE      28
#define SW_MODIFIER_ZSTRETCHNOISE  29
#define SW_MODIFIER_WAVING         30
#define SW_MODIFIER_PIVOTADJUST    31
#define SW_MODIFIER_DISPLACELIMITS 32

static const float kModifierEpsilon = 1e-6f;
// 원주율은 common.hlsli 의 kPi · kTwoPi 를 쓴다(meshmorph.hlsl 이 common.hlsli 를 먼저 포함한다).
static const float kDegToRad = kPi / 180.0f;

struct ModifierContext
{
    float3 boundsMin;
    float3 boundsMax;
    float  boundsValid; // 1 이면 유효
    uint   axis;
    float  time;
    uint   paramBase; // g_DeformPrograms 안 매개변수 시작 원소

    float param( uint index );
    float3 param3( uint index );
    int paramInt( uint index );
    uint paramAxis( uint index );
    float axisLength();
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
    static float3 deformPoint( uint kind, float3 location, float3 surfaceNormal, ModifierContext context );
    static float3 transformPoint( float3 location, uint row );
    static float3 transformVector( float3 direction, uint row );
    static void applyProgram( uint programBase, inout float3 inoutPosition, inout float3 inoutNormal );
};

float ModifierContext::param( uint index )
{
    return g_DeformPrograms[paramBase + index].x;
}

float3 ModifierContext::param3( uint index )
{
    return g_DeformPrograms[paramBase + index].xyz;
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
    const float length = MeshDeform::get( boundsMax - boundsMin, axis );
    return length > kModifierEpsilon ? length : 1.0f;
}

/** MeshModifierUtil::getAxisFraction — 자르지 않는다 */
static float MeshDeform::axisFraction( float3 position, ModifierContext context )
{
    return ( MeshDeform::get( position, context.axis ) - MeshDeform::get( context.boundsMin, context.axis ) ) / context.axisLength();
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
    const uint  axis         = context.axis;
    const uint  axisU        = ( axis + 1u ) % 3u;
    const uint  axisV        = ( axis + 2u ) % 3u;
    const float length       = context.axisLength();
    const float lowerBound   = MeshDeform::get( context.boundsMin, axis );
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
        const uint  axis       = context.axis;
        const uint  axisU      = ( axis + 1u ) % 3u;
        const uint  axisV      = ( axis + 2u ) % 3u;
        const float length     = context.axisLength();
        const float lowerBound = MeshDeform::get( context.boundsMin, axis );
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
        result               = MeshDeform::set( location * scale, context.axis, MeshDeform::get( location, context.axis ) );
    }
    return result;
}

static float3 MeshDeform::stretch( float3 location, ModifierContext context )
{
    const float alongScale = 1.0f + context.param( 0u );
    const float sideScale  = pow( alongScale, -0.5f * context.param( 1u ) );
    return MeshDeform::set( location * sideScale, context.axis, MeshDeform::get( location, context.axis ) * alongScale );
}

static float3 MeshDeform::wave( float3 location, ModifierContext context, bool bRadial )
{
    const float amplitude = context.param( 0u );
    float3      result    = location;
    if ( abs( amplitude ) >= kModifierEpsilon )
    {
        const uint  axis       = context.axis;
        const float componentU = MeshDeform::get( location, ( axis + 1u ) % 3u );
        const float componentV = MeshDeform::get( location, ( axis + 2u ) % 3u );
        const float distance   = bRadial ? sqrt( componentU * componentU + componentV * componentV ) : componentU;
        const float phase      = ( context.param( 2u ) + context.param( 4u ) * context.time ) * kDegToRad;
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
        const float  drift       = context.param( 4u ) * context.time;
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
        const uint  axis        = context.axis;
        const float floorHeight = MeshDeform::get( context.boundsMin, axis );
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
        result             = MeshDeform::set( location, context.axis, MeshDeform::get( location, context.axis ) + amount * weight );
    }
    return result;
}

static float3 MeshDeform::displace( float3 location, float3 surfaceNormal, ModifierContext context )
{
    const float amount = context.param( 0u );
    float3      result = location;
    if ( abs( amount ) >= kModifierEpsilon )
    {
        const uint  axis       = context.axis;
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
        result              = MeshDeform::set( location * scale, context.axis, MeshDeform::get( location, context.axis ) );
    }
    return result;
}

/** Cylindrify(축 성분을 뺀 반경) · Globe(전체 반경) */
static float3 MeshDeform::toRadius( float3 location, ModifierContext context, bool bCylinder )
{
    const float  percent  = context.param( 0u );
    const float3 radial   = bCylinder ? MeshDeform::set( location, context.axis, 0.0f ) : location;
    const float  distance = length( radial );
    float3       result   = location;
    if ( percent >= kModifierEpsilon && distance >= kModifierEpsilon )
    {
        const float target = distance + ( context.param( 1u ) - distance ) * percent;
        result             = radial * ( target / distance );
        result             = bCylinder ? MeshDeform::set( result, context.axis, MeshDeform::get( location, context.axis ) ) : result;
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
        const float3 offset = MeshDeform::set( location - context.param3( 2u ), context.axis, 0.0f );
        const float  ratio  = length( offset ) / radius;
        result              = MeshDeform::set( location, context.axis, MeshDeform::get( location, context.axis ) + height * exp( -ratio * ratio ) );
    }
    return result;
}

static float3 MeshDeform::radialSkew( float3 location, ModifierContext context )
{
    const float  angle    = context.param( 0u ) * kDegToRad;
    const float3 radial   = MeshDeform::set( location, context.axis, 0.0f );
    const float  distance = length( radial );
    float3       result   = location;
    if ( abs( angle ) >= kModifierEpsilon && distance >= kModifierEpsilon )
    {
        const float height = MeshDeform::get( location, context.axis ) - MeshDeform::get( context.boundsMin, context.axis );
        const float target = max( distance + tan( angle ) * height, 0.0f );
        result             = MeshDeform::set( radial * ( target / distance ), context.axis, MeshDeform::get( location, context.axis ) );
    }
    return result;
}

/** Skew · Sinus Curve 의 옆 밀기 */
static float3 MeshDeform::shiftSide( float3 location, ModifierContext context, float shift, float direction )
{
    const uint axisU  = ( context.axis + 1u ) % 3u;
    const uint axisV  = ( context.axis + 2u ) % 3u;
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
    const float3 size     = context.boundsMax - context.boundsMin;
    const float  radius   = 0.5f * max( size.x, max( size.y, size.z ) );
    const float3 center   = ( context.boundsMin + context.boundsMax ) * 0.5f;
    const float3 offset   = location - center;
    const float  distance = length( offset );
    const bool   bActive  = percent >= kModifierEpsilon && context.boundsValid > 0.5f && radius >= kModifierEpsilon && distance >= kModifierEpsilon;
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
        result                  = MeshDeform::set( location * radialScale, context.axis, MeshDeform::get( location, context.axis ) * axisScale );
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
    const uint  axis   = context.axis;
    const float start  = MeshDeform::get( context.boundsMin, axis ) + context.param( 1u ) * context.axisLength();
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
    const float time       = max( context.time, 0.0f );
    const uint  axis       = context.axis;
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
    const uint  liftAxis = context.axis;
    const uint  pickAxis = context.paramAxis( 2u );
    const uint  flipAxis = pickAxis == liftAxis ? ( liftAxis + 1u ) % 3u : pickAxis;
    const float minU     = MeshDeform::get( context.boundsMin, flipAxis );
    const float maxU     = MeshDeform::get( context.boundsMax, flipAxis );
    const float width    = maxU - minU;
    const float radius   = max( context.param( 1u ), 0.001f );
    const float curl     = maxU - saturate( progress ) * width;
    const float arc      = MeshDeform::get( location, flipAxis ) - curl;
    const bool  bActive  = progress >= kModifierEpsilon && context.boundsValid > 0.5f && width >= kModifierEpsilon && arc > 0.0f;
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
    const uint   axis       = context.axis;
    const float3 wind       = MeshDeform::set( context.param3( 1u ), axis, 0.0f );
    const float  windLength = length( wind );
    const float  root       = MeshDeform::get( context.boundsMin, axis ) + context.param( 4u );
    const float  height     = MeshDeform::get( location, axis ) - root;
    const bool   bActive    = abs( strength ) >= kModifierEpsilon && context.boundsValid > 0.5f && windLength >= kModifierEpsilon && height > 0.0f;
    float3       result     = location;
    if ( bActive )
    {
        const float ratio = height / context.axisLength();
        const float sway  = 1.0f + context.param( 2u ) * sin( kTwoPi * context.param( 3u ) * context.time );
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
        const float  drift       = context.param( 3u ) * context.time;
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
    const uint  moveAxis  = context.axis == fixedAxis ? ( fixedAxis + 1u ) % 3u : context.axis;
    const float start     = MeshDeform::get( context.boundsMin, fixedAxis ) + context.param( 4u );
    const float distance  = MeshDeform::get( location, fixedAxis ) - start;
    const bool  bActive   = abs( amplitude ) >= kModifierEpsilon && context.boundsValid > 0.5f && distance > 0.0f;
    float3      result    = location;
    if ( bActive )
    {
        const float span   = MeshDeform::get( context.boundsMax - context.boundsMin, fixedAxis );
        const float ratio  = distance / ( span > kModifierEpsilon ? span : 1.0f );
        const float phase  = distance / max( context.param( 1u ), 0.001f ) - context.param( 2u ) * context.time;
        const float offset = amplitude * ratio * sin( kTwoPi * phase );
        result             = MeshDeform::set( location, moveAxis, MeshDeform::get( location, moveAxis ) + offset );
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
        default:
            break;
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
 * 가중치 채널은 없다(모두 1). 위치와 법선은 메시 로컬 공간이다.
 */
static void MeshDeform::applyProgram( uint programBase, inout float3 inoutPosition, inout float3 inoutNormal )
{
    const float4 header = g_DeformPrograms[programBase];
    const uint   count  = (uint)header.x;
    uint         cursor = programBase + 1u;
    for ( uint modifierIndex = 0u; modifierIndex < count; ++modifierIndex )
    {
        const float4    record    = g_DeformPrograms[cursor];
        const float4    boundsMin = g_DeformPrograms[cursor + 1u];
        ModifierContext context;
        context.boundsMin   = boundsMin.xyz;
        context.boundsValid = boundsMin.w;
        context.boundsMax   = g_DeformPrograms[cursor + 2u].xyz;
        context.axis        = (uint)record.z;
        context.time        = header.y;
        context.paramBase   = cursor + SW_MODIFIER_RECORD_FLOAT4;
        const uint  kind    = (uint)record.x;
        const float step    = record.w;

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
            const float3 deformed = MeshDeform::deformPoint( kind, location + offset, surfaceNormal, context );
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
