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

static const float kSwModEpsilon = 1e-6f;
// 원주율은 common.hlsli 의 kPi · kTwoPi 를 쓴다(meshmorph.hlsl 이 common.hlsli 를 먼저 포함한다).
static const float kSwModPi       = kPi;
static const float kSwModTwoPi    = kTwoPi;
static const float kSwModDegToRad = kPi / 180.0f;

struct SwModifierContext
{
    float3 boundsMin;
    float3 boundsMax;
    float  boundsValid; // 1 이면 유효
    uint   axis;
    float  time;
    uint   paramBase; // g_DeformPrograms 안 매개변수 시작 원소
};

float swModParam( SwModifierContext context, uint index )
{
    return g_DeformPrograms[context.paramBase + index].x;
}

float3 swModParam3( SwModifierContext context, uint index )
{
    return g_DeformPrograms[context.paramBase + index].xyz;
}

/** MeshModifier::getInt — floor(x + 0.5) */
int swModParamInt( SwModifierContext context, uint index )
{
    return (int)floor( swModParam( context, index ) + 0.5f );
}

/** MeshModifier::getAxisIndex — [0, 2] 로 자른 정수 */
uint swModParamAxis( SwModifierContext context, uint index )
{
    return (uint)clamp( swModParamInt( context, index ), 0, 2 );
}

float swModGet( float3 value, uint axis )
{
    return axis == 0u ? value.x : ( axis == 1u ? value.y : value.z );
}

float3 swModSet( float3 value, uint axis, float component )
{
    return float3( axis == 0u ? component : value.x, axis == 1u ? component : value.y, axis == 2u ? component : value.z );
}

float3 swModNormalizeOr( float3 value, float3 fallback )
{
    const float length = sqrt( dot( value, value ) );
    const bool  bValid = length > kSwModEpsilon && length < 3.0e38f;
    return bValid ? value / max( length, kSwModEpsilon ) : fallback;
}

/** MeshModifierUtil::getAxisLength */
float swModAxisLength( SwModifierContext context )
{
    const float length = swModGet( context.boundsMax - context.boundsMin, context.axis );
    return length > kSwModEpsilon ? length : 1.0f;
}

/** MeshModifierUtil::getAxisFraction — 자르지 않는다 */
float swModAxisFraction( float3 position, SwModifierContext context )
{
    return ( swModGet( position, context.axis ) - swModGet( context.boundsMin, context.axis ) ) / swModAxisLength( context );
}

// ------------------------------------------------------------------------------
// 값 노이즈 — Stack/MeshModifierNoise.h 와 비트 단위로 같은 정수 해시
// ------------------------------------------------------------------------------
uint swModHashLattice( int cellX, int cellY, int cellZ, uint seed )
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

float swModLatticeValue( int cellX, int cellY, int cellZ, uint seed )
{
    return (float)( swModHashLattice( cellX, cellY, cellZ, seed ) & 0xFFFFu ) / 32767.5f - 1.0f;
}

float swModSmooth( float ratio )
{
    return ratio * ratio * ( 3.0f - 2.0f * ratio );
}

float swModValueNoise3D( float3 position, uint seed )
{
    const float3 cell    = floor( position );
    const int    cellX   = (int)cell.x;
    const int    cellY   = (int)cell.y;
    const int    cellZ   = (int)cell.z;
    const float  tx      = swModSmooth( position.x - cell.x );
    const float  ty      = swModSmooth( position.y - cell.y );
    const float  tz      = swModSmooth( position.z - cell.z );
    const float  v000    = swModLatticeValue( cellX, cellY, cellZ, seed );
    const float  v100    = swModLatticeValue( cellX + 1, cellY, cellZ, seed );
    const float  v010    = swModLatticeValue( cellX, cellY + 1, cellZ, seed );
    const float  v110    = swModLatticeValue( cellX + 1, cellY + 1, cellZ, seed );
    const float  v001    = swModLatticeValue( cellX, cellY, cellZ + 1, seed );
    const float  v101    = swModLatticeValue( cellX + 1, cellY, cellZ + 1, seed );
    const float  v011    = swModLatticeValue( cellX, cellY + 1, cellZ + 1, seed );
    const float  v111    = swModLatticeValue( cellX + 1, cellY + 1, cellZ + 1, seed );
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
float3 swModBendLike( float3 location, SwModifierContext context, float angle, float direction, float lowerParam, float upperParam )
{
    const uint  axis         = context.axis;
    const uint  axisU        = ( axis + 1u ) % 3u;
    const uint  axisV        = ( axis + 2u ) % 3u;
    const float length       = swModAxisLength( context );
    const float lowerBound   = swModGet( context.boundsMin, axis );
    const float lower        = lowerBound + min( lowerParam, upperParam ) * length;
    const float upper        = lowerBound + max( lowerParam, upperParam ) * length;
    const float cosDirection = cos( direction );
    const float sinDirection = sin( direction );
    const float componentU   = swModGet( location, axisU );
    const float componentV   = swModGet( location, axisV );
    const float bendX        = componentU * cosDirection + componentV * sinDirection;
    const float sideY        = -componentU * sinDirection + componentV * cosDirection;
    const float height       = swModGet( location, axis );
    const float clamped      = clamp( height, lower, upper );
    const float radius       = length / angle;
    const float phi          = clamped / radius;
    const float cosPhi       = cos( phi );
    const float sinPhi       = sin( phi );
    const float extra        = height - clamped;
    const float newX         = radius - ( radius - bendX ) * cosPhi + extra * sinPhi;
    const float newH         = ( radius - bendX ) * sinPhi + extra * cosPhi;
    float3      result       = swModSet( location, axis, newH );
    result                   = swModSet( result, axisU, newX * cosDirection - sideY * sinDirection );
    result                   = swModSet( result, axisV, newX * sinDirection + sideY * cosDirection );
    return result;
}

float3 swModBend( float3 location, SwModifierContext context )
{
    const float angle  = swModParam( context, 0u ) * kSwModDegToRad;
    float3      result = location;
    if ( abs( angle ) >= kSwModEpsilon )
        result = swModBendLike( location, context, angle, swModParam( context, 1u ) * kSwModDegToRad, swModParam( context, 2u ), swModParam( context, 3u ) );
    return result;
}

float3 swModTwist( float3 location, SwModifierContext context )
{
    const float angle  = swModParam( context, 0u ) * kSwModDegToRad;
    float3      result = location;
    if ( abs( angle ) >= kSwModEpsilon )
    {
        const uint  axis       = context.axis;
        const uint  axisU      = ( axis + 1u ) % 3u;
        const uint  axisV      = ( axis + 2u ) % 3u;
        const float length     = swModAxisLength( context );
        const float lowerBound = swModGet( context.boundsMin, axis );
        const float lower      = lowerBound + min( swModParam( context, 1u ), swModParam( context, 2u ) ) * length;
        const float upper      = lowerBound + max( swModParam( context, 1u ), swModParam( context, 2u ) ) * length;
        const float height     = clamp( swModGet( location, axis ), lower, upper );
        const float phi        = angle * height / length;
        const float cosPhi     = cos( phi );
        const float sinPhi     = sin( phi );
        const float componentU = swModGet( location, axisU );
        const float componentV = swModGet( location, axisV );
        result                 = swModSet( result, axisU, componentU * cosPhi - componentV * sinPhi );
        result                 = swModSet( result, axisV, componentU * sinPhi + componentV * cosPhi );
    }
    return result;
}

float3 swModTaper( float3 location, SwModifierContext context )
{
    const float amount = swModParam( context, 0u );
    float3      result = location;
    if ( abs( amount ) >= kSwModEpsilon )
    {
        const float fraction = saturate( swModAxisFraction( location, context ) );
        const float scale    = 1.0f + amount * pow( fraction, swModParam( context, 1u ) );
        result               = swModSet( location * scale, context.axis, swModGet( location, context.axis ) );
    }
    return result;
}

float3 swModStretch( float3 location, SwModifierContext context )
{
    const float alongScale = 1.0f + swModParam( context, 0u );
    const float sideScale  = pow( alongScale, -0.5f * swModParam( context, 1u ) );
    return swModSet( location * sideScale, context.axis, swModGet( location, context.axis ) * alongScale );
}

float3 swModWave( float3 location, SwModifierContext context, bool bRadial )
{
    const float amplitude = swModParam( context, 0u );
    float3      result    = location;
    if ( abs( amplitude ) >= kSwModEpsilon )
    {
        const uint  axis       = context.axis;
        const float componentU = swModGet( location, ( axis + 1u ) % 3u );
        const float componentV = swModGet( location, ( axis + 2u ) % 3u );
        const float distance   = bRadial ? sqrt( componentU * componentU + componentV * componentV ) : componentU;
        const float phase      = ( swModParam( context, 2u ) + swModParam( context, 4u ) * context.time ) * kSwModDegToRad;
        const float angle      = kSwModTwoPi * distance / swModParam( context, 1u ) + phase;
        const float offset     = amplitude * sin( angle ) * exp( -swModParam( context, 3u ) * abs( distance ) );
        result                 = swModSet( location, axis, swModGet( location, axis ) + offset );
    }
    return result;
}

float3 swModNoise( float3 location, SwModifierContext context )
{
    const float amplitude = swModParam( context, 0u );
    float3      result    = location;
    if ( amplitude >= kSwModEpsilon )
    {
        const uint   seed        = (uint)swModParamInt( context, 2u );
        const float  drift       = swModParam( context, 4u ) * context.time;
        const float3 samplePoint = location * swModParam( context, 1u ) + float3( drift, drift, drift );
        const float3 strength    = swModParam3( context, 3u );
        const float3 offset      = float3( swModValueNoise3D( samplePoint, seed ) * strength.x, swModValueNoise3D( samplePoint + float3( 31.416f, 0.0f, 0.0f ), seed ) * strength.y,
                                           swModValueNoise3D( samplePoint + float3( 0.0f, 0.0f, 67.123f ), seed ) * strength.z );
        result                   = location + offset * amplitude;
    }
    return result;
}

float3 swModMelt( float3 location, SwModifierContext context )
{
    const float amount = swModParam( context, 0u );
    float3      result = location;
    if ( amount >= kSwModEpsilon )
    {
        const uint  axis        = context.axis;
        const float floorHeight = swModGet( context.boundsMin, axis );
        const float height      = swModGet( location, axis );
        const float fraction    = saturate( swModAxisFraction( location, context ) );
        const float spread      = 1.0f + swModParam( context, 1u ) * amount * ( 1.0f - fraction );
        result                  = swModSet( location * spread, axis, floorHeight + ( height - floorHeight ) * ( 1.0f - amount ) );
    }
    return result;
}

float3 swModPoke( float3 location, SwModifierContext context )
{
    const float amount   = swModParam( context, 0u );
    const float radius   = swModParam( context, 1u );
    const float distance = length( location );
    float3      result   = location;
    if ( abs( amount ) >= kSwModEpsilon && distance < radius )
    {
        const float ratio  = distance / radius;
        const float weight = pow( 1.0f - ratio * ratio, swModParam( context, 2u ) );
        result             = swModSet( location, context.axis, swModGet( location, context.axis ) + amount * weight );
    }
    return result;
}

float3 swModDisplace( float3 location, float3 surfaceNormal, SwModifierContext context )
{
    const float amount = swModParam( context, 0u );
    float3      result = location;
    if ( abs( amount ) >= kSwModEpsilon )
    {
        const uint  axis       = context.axis;
        const float componentU = swModGet( location, ( axis + 1u ) % 3u );
        const float componentV = swModGet( location, ( axis + 2u ) % 3u );
        const float frequency  = kSwModTwoPi * swModParam( context, 1u );
        const float phase      = swModParam( context, 2u ) * kSwModDegToRad;
        const float height     = sin( frequency * componentU + phase ) * cos( frequency * componentV + phase );
        result                 = location + surfaceNormal * ( amount * height );
    }
    return result;
}

float3 swModBubble( float3 location, SwModifierContext context )
{
    const float  strength = swModParam( context, 0u );
    const float  radius   = swModParam( context, 1u );
    const float3 offset   = location - swModParam3( context, 2u );
    const float  distance = length( offset );
    const bool   bActive  = abs( strength ) >= kSwModEpsilon && radius >= kSwModEpsilon && distance < radius && distance >= kSwModEpsilon;
    float3       result   = location;
    if ( bActive )
    {
        const float ratio   = distance / radius;
        const float falloff = ( 1.0f - ratio * ratio ) * ( 1.0f - ratio * ratio );
        result              = location + offset * ( strength * falloff / distance );
    }
    return result;
}

float3 swModBulge( float3 location, SwModifierContext context )
{
    const float amount   = swModParam( context, 0u );
    const float lower    = min( swModParam( context, 2u ), swModParam( context, 3u ) );
    const float upper    = max( swModParam( context, 2u ), swModParam( context, 3u ) );
    const float fraction = swModAxisFraction( location, context );
    const bool  bActive  = abs( amount ) >= kSwModEpsilon && upper - lower >= kSwModEpsilon && fraction > lower && fraction < upper;
    float3      result   = location;
    if ( bActive )
    {
        const float ratio   = ( fraction - lower ) / ( upper - lower );
        const float profile = 1.0f - pow( abs( 2.0f * ratio - 1.0f ), swModParam( context, 1u ) );
        const float scale   = max( 1.0f + amount * profile, 0.0f );
        result              = swModSet( location * scale, context.axis, swModGet( location, context.axis ) );
    }
    return result;
}

/** Cylindrify(축 성분을 뺀 반경) · Globe(전체 반경) */
float3 swModToRadius( float3 location, SwModifierContext context, bool bCylinder )
{
    const float  percent  = swModParam( context, 0u );
    const float3 radial   = bCylinder ? swModSet( location, context.axis, 0.0f ) : location;
    const float  distance = length( radial );
    float3       result   = location;
    if ( percent >= kSwModEpsilon && distance >= kSwModEpsilon )
    {
        const float target = distance + ( swModParam( context, 1u ) - distance ) * percent;
        result             = radial * ( target / distance );
        result             = bCylinder ? swModSet( result, context.axis, swModGet( location, context.axis ) ) : result;
    }
    return result;
}

float3 swModHump( float3 location, SwModifierContext context )
{
    const float height = swModParam( context, 0u );
    const float radius = swModParam( context, 1u );
    float3      result = location;
    if ( abs( height ) >= kSwModEpsilon && radius >= kSwModEpsilon )
    {
        const float3 offset = swModSet( location - swModParam3( context, 2u ), context.axis, 0.0f );
        const float  ratio  = length( offset ) / radius;
        result              = swModSet( location, context.axis, swModGet( location, context.axis ) + height * exp( -ratio * ratio ) );
    }
    return result;
}

float3 swModRadialSkew( float3 location, SwModifierContext context )
{
    const float  angle    = swModParam( context, 0u ) * kSwModDegToRad;
    const float3 radial   = swModSet( location, context.axis, 0.0f );
    const float  distance = length( radial );
    float3       result   = location;
    if ( abs( angle ) >= kSwModEpsilon && distance >= kSwModEpsilon )
    {
        const float height = swModGet( location, context.axis ) - swModGet( context.boundsMin, context.axis );
        const float target = max( distance + tan( angle ) * height, 0.0f );
        result             = swModSet( radial * ( target / distance ), context.axis, swModGet( location, context.axis ) );
    }
    return result;
}

/** Skew · Sinus Curve 의 옆 밀기 */
float3 swModShiftSide( float3 location, SwModifierContext context, float shift, float direction )
{
    const uint axisU  = ( context.axis + 1u ) % 3u;
    const uint axisV  = ( context.axis + 2u ) % 3u;
    float3     result = swModSet( location, axisU, swModGet( location, axisU ) + shift * cos( direction ) );
    return swModSet( result, axisV, swModGet( result, axisV ) + shift * sin( direction ) );
}

float3 swModSkew( float3 location, SwModifierContext context )
{
    const float amount = swModParam( context, 0u );
    float3      result = location;
    if ( abs( amount ) >= kSwModEpsilon )
    {
        const float lower    = min( swModParam( context, 2u ), swModParam( context, 3u ) );
        const float upper    = max( swModParam( context, 2u ), swModParam( context, 3u ) );
        const float fraction = swModAxisFraction( location, context );
        const float ratio    = upper - lower < kSwModEpsilon ? ( fraction >= lower ? 1.0f : 0.0f ) : saturate( ( fraction - lower ) / ( upper - lower ) );
        result               = swModShiftSide( location, context, amount * ratio, swModParam( context, 1u ) * kSwModDegToRad );
    }
    return result;
}

float3 swModSpherify( float3 location, SwModifierContext context )
{
    const float  percent  = swModParam( context, 0u );
    const float3 size     = context.boundsMax - context.boundsMin;
    const float  radius   = 0.5f * max( size.x, max( size.y, size.z ) );
    const float3 center   = ( context.boundsMin + context.boundsMax ) * 0.5f;
    const float3 offset   = location - center;
    const float  distance = length( offset );
    const bool   bActive  = percent >= kSwModEpsilon && context.boundsValid > 0.5f && radius >= kSwModEpsilon && distance >= kSwModEpsilon;
    float3       result   = location;
    if ( bActive )
    {
        const float target = distance + ( radius - distance ) * percent;
        result             = center + offset * ( target / distance );
    }
    return result;
}

float3 swModSqueeze( float3 location, SwModifierContext context )
{
    const float amount = swModParam( context, 0u );
    float3      result = location;
    if ( abs( amount ) >= kSwModEpsilon )
    {
        const float axisScale   = max( 1.0f - amount, 0.1f );
        const float radialScale = 1.0f + swModParam( context, 1u ) * ( 1.0f / sqrt( axisScale ) - 1.0f );
        result                  = swModSet( location * radialScale, context.axis, swModGet( location, context.axis ) * axisScale );
    }
    return result;
}

float3 swModSinusCurve( float3 location, SwModifierContext context )
{
    const float amplitude = swModParam( context, 0u );
    float3      result    = location;
    if ( abs( amplitude ) >= kSwModEpsilon )
    {
        const float fraction = swModAxisFraction( location, context );
        const float shift    = amplitude * sin( kSwModTwoPi * swModParam( context, 1u ) * fraction + swModParam( context, 2u ) * kSwModDegToRad );
        result               = swModShiftSide( location, context, shift, swModParam( context, 3u ) * kSwModDegToRad );
    }
    return result;
}

float3 swModRolled( float3 location, SwModifierContext context )
{
    const float radius = swModParam( context, 0u );
    const float turns  = swModParam( context, 2u );
    const uint  axis   = context.axis;
    const float start  = swModGet( context.boundsMin, axis ) + swModParam( context, 1u ) * swModAxisLength( context );
    const float height = swModGet( location, axis );
    float3      result = location;
    if ( radius >= kSwModEpsilon && turns >= kSwModEpsilon && height > start )
    {
        const uint  axisU        = ( axis + 1u ) % 3u;
        const uint  axisV        = ( axis + 2u ) % 3u;
        const float direction    = swModParam( context, 3u ) * kSwModDegToRad;
        const float cosDirection = cos( direction );
        const float sinDirection = sin( direction );
        const float componentU   = swModGet( location, axisU );
        const float componentV   = swModGet( location, axisV );
        const float rollX        = componentU * cosDirection + componentV * sinDirection;
        const float sideY        = -componentU * sinDirection + componentV * cosDirection;
        const float arc          = height - start;
        const float rolled       = min( arc, turns * kSwModTwoPi * radius );
        const float extra        = arc - rolled;
        const float phi          = rolled / radius;
        const float cosPhi       = cos( phi );
        const float sinPhi       = sin( phi );
        const float newX         = radius - ( radius - rollX ) * cosPhi + extra * sinPhi;
        const float newH         = start + ( radius - rollX ) * sinPhi + extra * cosPhi;
        result                   = swModSet( result, axis, newH );
        result                   = swModSet( result, axisU, newX * cosDirection - sideY * sinDirection );
        result                   = swModSet( result, axisV, newX * sinDirection + sideY * cosDirection );
    }
    return result;
}

float3 swModCrumple( float3 location, float3 surfaceNormal, SwModifierContext context )
{
    const float amplitude = swModParam( context, 0u );
    float3      result    = location;
    if ( amplitude >= kSwModEpsilon )
    {
        const uint   seed        = (uint)swModParamInt( context, 2u );
        const float3 samplePoint = location * swModParam( context, 1u );
        const float3 noiseVector = float3( swModValueNoise3D( samplePoint, seed ), swModValueNoise3D( samplePoint + float3( 31.416f, 0.0f, 0.0f ), seed ),
                                           swModValueNoise3D( samplePoint + float3( 0.0f, 0.0f, 67.123f ), seed ) );
        const float3 along       = surfaceNormal * noiseVector.x;
        const float  blend       = saturate( swModParam( context, 3u ) );
        const float3 offset      = noiseVector * ( 1.0f - blend ) + along * blend;
        result                   = location + offset * amplitude;
    }
    return result;
}

float3 swModDynamicRipple( float3 location, SwModifierContext context )
{
    const float amplitude  = swModParam( context, 0u );
    const float time       = max( context.time, 0.0f );
    const uint  axis       = context.axis;
    const float componentU = swModGet( location, ( axis + 1u ) % 3u );
    const float componentV = swModGet( location, ( axis + 2u ) % 3u );
    const float radius     = sqrt( componentU * componentU + componentV * componentV );
    const float front      = swModParam( context, 2u ) * time;
    float3      result     = location;
    if ( abs( amplitude ) >= kSwModEpsilon && radius <= front )
    {
        const float limit   = swModParam( context, 4u );
        const float falloff = limit > kSwModEpsilon ? saturate( 1.0f - radius / limit ) : 1.0f;
        const float angle   = kSwModTwoPi * ( radius - front ) / max( swModParam( context, 1u ), 0.001f );
        const float offset  = amplitude * sin( angle ) * exp( -swModParam( context, 3u ) * time ) * falloff;
        result              = swModSet( location, axis, swModGet( location, axis ) + offset );
    }
    return result;
}

float3 swModPageFlip( float3 location, SwModifierContext context )
{
    const float progress = swModParam( context, 0u );
    const uint  liftAxis = context.axis;
    const uint  pickAxis = swModParamAxis( context, 2u );
    const uint  flipAxis = pickAxis == liftAxis ? ( liftAxis + 1u ) % 3u : pickAxis;
    const float minU     = swModGet( context.boundsMin, flipAxis );
    const float maxU     = swModGet( context.boundsMax, flipAxis );
    const float width    = maxU - minU;
    const float radius   = max( swModParam( context, 1u ), 0.001f );
    const float curl     = maxU - saturate( progress ) * width;
    const float arc      = swModGet( location, flipAxis ) - curl;
    const bool  bActive  = progress >= kSwModEpsilon && context.boundsValid > 0.5f && width >= kSwModEpsilon && arc > 0.0f;
    float3      result   = location;
    if ( bActive )
    {
        const float height   = swModGet( location, liftAxis );
        const float halfTurn = kSwModPi * radius;
        const float theta    = arc / radius;
        const bool  bCurled  = arc <= halfTurn;
        const float newU     = bCurled ? curl + ( radius - height ) * sin( theta ) : curl - ( arc - halfTurn );
        const float newH     = bCurled ? radius - ( radius - height ) * cos( theta ) : 2.0f * radius - height;
        result               = swModSet( result, flipAxis, newU );
        result               = swModSet( result, liftAxis, newH );
    }
    return result;
}

float3 swModTreeBend( float3 location, SwModifierContext context )
{
    const float  strength   = swModParam( context, 0u );
    const uint   axis       = context.axis;
    const float3 wind       = swModSet( swModParam3( context, 1u ), axis, 0.0f );
    const float  windLength = length( wind );
    const float  root       = swModGet( context.boundsMin, axis ) + swModParam( context, 4u );
    const float  height     = swModGet( location, axis ) - root;
    const bool   bActive    = abs( strength ) >= kSwModEpsilon && context.boundsValid > 0.5f && windLength >= kSwModEpsilon && height > 0.0f;
    float3       result     = location;
    if ( bActive )
    {
        const float ratio = height / swModAxisLength( context );
        const float sway  = 1.0f + swModParam( context, 2u ) * sin( kSwModTwoPi * swModParam( context, 3u ) * context.time );
        result            = location + wind * ( 1.0f / windLength ) * ( strength * sway * ratio * ratio );
    }
    return result;
}

float3 swModVertNoise( float3 location, float3 surfaceNormal, SwModifierContext context )
{
    const float amplitude = swModParam( context, 0u );
    float3      result    = location;
    if ( abs( amplitude ) >= kSwModEpsilon )
    {
        const uint   seed        = (uint)swModParamInt( context, 2u );
        const float  drift       = swModParam( context, 3u ) * context.time;
        const float3 samplePoint = location * swModParam( context, 1u ) + float3( drift, drift * 0.5f, drift * 0.25f );
        result                   = location + surfaceNormal * ( swModValueNoise3D( samplePoint, seed ) * amplitude );
    }
    return result;
}

float3 swModZstretchNoise( float3 location, SwModifierContext context )
{
    const float amount = swModParam( context, 0u );
    const float noise  = swModParam( context, 1u );
    float3      result = location;
    if ( abs( amount ) >= kSwModEpsilon || abs( noise ) >= kSwModEpsilon )
    {
        const uint  axis   = swModParamAxis( context, 4u );
        const uint  seed   = (uint)swModParamInt( context, 3u );
        const float wobble = swModValueNoise3D( location * swModParam( context, 2u ), seed );
        result             = swModSet( location, axis, swModGet( location, axis ) * ( 1.0f + amount ) + noise * wobble );
    }
    return result;
}

float3 swModWaving( float3 location, SwModifierContext context )
{
    const float amplitude = swModParam( context, 0u );
    const uint  fixedAxis = swModParamAxis( context, 3u );
    const uint  moveAxis  = context.axis == fixedAxis ? ( fixedAxis + 1u ) % 3u : context.axis;
    const float start     = swModGet( context.boundsMin, fixedAxis ) + swModParam( context, 4u );
    const float distance  = swModGet( location, fixedAxis ) - start;
    const bool  bActive   = abs( amplitude ) >= kSwModEpsilon && context.boundsValid > 0.5f && distance > 0.0f;
    float3      result    = location;
    if ( bActive )
    {
        const float span   = swModGet( context.boundsMax - context.boundsMin, fixedAxis );
        const float ratio  = distance / ( span > kSwModEpsilon ? span : 1.0f );
        const float phase  = distance / max( swModParam( context, 1u ), 0.001f ) - swModParam( context, 2u ) * context.time;
        const float offset = amplitude * ratio * sin( kSwModTwoPi * phase );
        result             = swModSet( location, moveAxis, swModGet( location, moveAxis ) + offset );
    }
    return result;
}

/** Displace Limits — 위치를 상자(Mode 1) · 구(Mode 2) 안으로 자른다. "Clamp Displacement" 를 켜면 CPU 전용이라 여기 오지 않는다 */
float3 swModDisplaceLimits( float3 location, SwModifierContext context )
{
    const int    mode    = swModParamInt( context, 0u );
    const float3 low     = swModParam3( context, 1u );
    const float3 high    = swModParam3( context, 2u );
    const float3 boxed   = clamp( location, min( low, high ), max( low, high ) );
    const float  radius  = swModParam( context, 3u );
    const float  len     = length( location );
    const bool   bShrink = len > radius && len >= kSwModEpsilon;
    const float3 sphered = bShrink ? location * ( radius / max( len, kSwModEpsilon ) ) : location;
    return mode == 1 ? boxed : ( mode == 2 ? sphered : location );
}

/** 모디파이어 하나의 점 변형(모디파이어 공간). 모르는 종류는 그대로 둔다 */
float3 swModDeformPoint( uint kind, float3 location, float3 surfaceNormal, SwModifierContext context )
{
    float3 result = location;
    [branch] switch ( kind )
    {
        case SW_MODIFIER_BEND:
            result = swModBend( location, context );
            break;
        case SW_MODIFIER_TWIST:
            result = swModTwist( location, context );
            break;
        case SW_MODIFIER_TAPER:
            result = swModTaper( location, context );
            break;
        case SW_MODIFIER_STRETCH:
            result = swModStretch( location, context );
            break;
        case SW_MODIFIER_WAVE:
            result = swModWave( location, context, false );
            break;
        case SW_MODIFIER_RIPPLE:
            result = swModWave( location, context, true );
            break;
        case SW_MODIFIER_NOISE:
            result = swModNoise( location, context );
            break;
        case SW_MODIFIER_MELT:
            result = swModMelt( location, context );
            break;
        case SW_MODIFIER_PUSH:
            result = location + surfaceNormal * swModParam( context, 0u );
            break;
        case SW_MODIFIER_POKE:
            result = swModPoke( location, context );
            break;
        case SW_MODIFIER_DISPLACE:
            result = swModDisplace( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_BUBBLE:
            result = swModBubble( location, context );
            break;
        case SW_MODIFIER_BULGE:
            result = swModBulge( location, context );
            break;
        case SW_MODIFIER_CYLINDRIFY:
            result = swModToRadius( location, context, true );
            break;
        case SW_MODIFIER_GLOBE:
            result = swModToRadius( location, context, false );
            break;
        case SW_MODIFIER_HUMP:
            result = swModHump( location, context );
            break;
        case SW_MODIFIER_RADIALSKEW:
            result = swModRadialSkew( location, context );
            break;
        case SW_MODIFIER_SKEW:
            result = swModSkew( location, context );
            break;
        case SW_MODIFIER_SPHERIFY:
            result = swModSpherify( location, context );
            break;
        case SW_MODIFIER_SQUEEZE:
            result = swModSqueeze( location, context );
            break;
        case SW_MODIFIER_SCALE:
            result = location * swModParam( context, 0u ) * swModParam3( context, 1u );
            break;
        case SW_MODIFIER_SINUSCURVE:
            result = swModSinusCurve( location, context );
            break;
        case SW_MODIFIER_ROLLED:
            result = swModRolled( location, context );
            break;
        case SW_MODIFIER_SIMPLEMOD:
            result = location + swModParam3( context, 0u ) + swModParam3( context, 1u ) * location + swModParam3( context, 2u ) * location * location;
            break;
        case SW_MODIFIER_CRUMPLE:
            result = swModCrumple( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_DYNAMICRIPPLE:
            result = swModDynamicRipple( location, context );
            break;
        case SW_MODIFIER_PAGEFLIP:
            result = swModPageFlip( location, context );
            break;
        case SW_MODIFIER_TREEBEND:
            result = swModTreeBend( location, context );
            break;
        case SW_MODIFIER_VERTNOISE:
            result = swModVertNoise( location, surfaceNormal, context );
            break;
        case SW_MODIFIER_ZSTRETCHNOISE:
            result = swModZstretchNoise( location, context );
            break;
        case SW_MODIFIER_WAVING:
            result = swModWaving( location, context );
            break;
        case SW_MODIFIER_PIVOTADJUST:
            result = location - swModParam3( context, 0u );
            break;
        case SW_MODIFIER_DISPLACELIMITS:
            result = swModDisplaceLimits( location, context );
            break;
        default:
            break;
    }
    return result;
}

float3 swModTransformPoint( float3 location, uint row )
{
    return location.x * g_DeformPrograms[row].xyz + location.y * g_DeformPrograms[row + 1u].xyz + location.z * g_DeformPrograms[row + 2u].xyz + g_DeformPrograms[row + 3u].xyz;
}

float3 swModTransformVector( float3 direction, uint row )
{
    return direction.x * g_DeformPrograms[row].xyz + direction.y * g_DeformPrograms[row + 1u].xyz + direction.z * g_DeformPrograms[row + 2u].xyz;
}

/**
 * 프로그램을 정점 하나에 차례로 건다 — MeshModifier::apply(공간 변환) + 기본 applyToMesh(점 변형 · 야코비안 법선)와 같은 순서다.
 * 가중치 채널은 없다(모두 1). 위치와 법선은 메시 로컬 공간이다.
 */
void swApplyDeformProgram( uint programBase, inout float3 inoutPosition, inout float3 inoutNormal )
{
    const float4 header = g_DeformPrograms[programBase];
    const uint   count  = (uint)header.x;
    uint         cursor = programBase + 1u;
    for ( uint modifierIndex = 0u; modifierIndex < count; ++modifierIndex )
    {
        const float4      record    = g_DeformPrograms[cursor];
        const float4      boundsMin = g_DeformPrograms[cursor + 1u];
        SwModifierContext context;
        context.boundsMin   = boundsMin.xyz;
        context.boundsValid = boundsMin.w;
        context.boundsMax   = g_DeformPrograms[cursor + 2u].xyz;
        context.axis        = (uint)record.z;
        context.time        = header.y;
        context.paramBase   = cursor + SW_MODIFIER_RECORD_FLOAT4;
        const uint  kind    = (uint)record.x;
        const float step    = record.w;

        const float3 location      = swModTransformPoint( inoutPosition, cursor + 3u );
        const float3 surfaceNormal = swModNormalizeOr( swModTransformVector( inoutNormal, cursor + 11u ), float3( 0.0f, 1.0f, 0.0f ) );

        // 법선에 수직인 접선 둘을 조금 밀어 변형된 접선을 얻고, 그 외적이 새 법선이다(t1 × t2 = surfaceNormal).
        // 점 셋(원래 · 접선 1 · 접선 2)을 루프 하나로 건다 - 펼쳐 부르면 종류 분기가 세 벌 복사돼 셰이더가 세 배로 커진다
        const float3 helper   = abs( surfaceNormal.x ) < 0.9f ? float3( 1.0f, 0.0f, 0.0f ) : float3( 0.0f, 1.0f, 0.0f );
        const float3 tangent1 = swModNormalizeOr( cross( surfaceNormal, helper ), float3( 0.0f, 0.0f, 1.0f ) );
        const float3 tangent2 = cross( surfaceNormal, tangent1 );
        float3       moved    = location;
        float3       shifted1 = location;
        float3       shifted2 = location;
        [loop] for ( uint sampleIndex = 0u; sampleIndex < 3u; ++sampleIndex )
        {
            const float3 offset   = sampleIndex == 1u ? tangent1 * step : ( sampleIndex == 2u ? tangent2 * step : float3( 0.0f, 0.0f, 0.0f ) );
            const float3 deformed = swModDeformPoint( kind, location + offset, surfaceNormal, context );
            moved                 = sampleIndex == 0u ? deformed : moved;
            shifted1              = sampleIndex == 1u ? deformed : shifted1;
            shifted2              = sampleIndex == 2u ? deformed : shifted2;
        }
        const float3 movedN = swModNormalizeOr( cross( shifted1 - moved, shifted2 - moved ), surfaceNormal );

        inoutPosition = swModTransformPoint( moved, cursor + 7u );
        inoutNormal   = swModNormalizeOr( swModTransformVector( movedN, cursor + 14u ), float3( 0.0f, 1.0f, 0.0f ) );
        cursor += SW_MODIFIER_RECORD_FLOAT4 + (uint)record.y;
    }
}

#endif // SW_ENGINE_MESHMODIFIER_HLSLI
