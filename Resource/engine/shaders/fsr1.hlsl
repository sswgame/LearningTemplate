/**
 * @file fsr1.hlsl
 * @brief AMD FidelityFX Super Resolution 1(FSR 1)입니다 — EASU(가장자리 적응 공간 업샘플)와 RCAS(대비 적응 선명화). Present 가 해상도 배율로 그린 뷰를
 *        출력 크기로 키울 때 선형 블릿 대신 쓴다(사용자 설정 graphics.upscaler = spatial, `gv_upscaler` 1).
 * @details 원본은 AMD GPUOpen-Effects/FidelityFX-FSR v1.20210629 의 ffx_fsr1.h 32 비트 판(FsrEasuCon · FsrEasuF · FsrRcasF)과 ffx_a.h 의 근사 역수(APrx*)이고
 *          MIT 라이선스다(아래 고지, 전문은 ThirdParty/fidelityfx-fsr1/LICENSE.md). 이 엔진의 셰이더 규칙(이름 · 루트 상수 · bindless/에뮬 읽기)에 맞춰 옮겼고 계산은 같다.
 *          두 단계는 퍼뮤테이션으로 고른다 — 기본은 EASU(배율 크기 원본 → 출력 크기 중간 RT), `SW_PASS_FSR_RCAS` 는 RCAS(중간 RT → Present 대상의 뷰포트).
 *          EASU 는 톤맵 뒤 지각 공간의 [0,1] 색을 받아야 한다(AMD 권고) — 그래서 Present 가 톤맵하지 않는 파이프라인에서만 쓴다(FrameRenderer::prepareUpscale).
 *          원본 · 중간 RT 는 네이티브(DX12 · Vulkan)는 bindless 번호(루트 상수), 에뮬은 엔진 텍스처 슬롯 0(t0 — C++ 가 bindShaderResource 로 건다)이다.
 *          C++: FrameRenderer::recordUpscale(FrameRendererUpscale.cpp).
 *
 * FidelityFX Super Resolution — Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"

SW_ROOT_CONSTANTS_BEGIN
    uint  g_FsrSource;     // 원본(EASU: 배율 크기 장면, RCAS: 중간 RT)의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
    float g_FsrInputWidth; // EASU: 원본 픽셀 크기
    float g_FsrInputHeight;
    float g_FsrOutputWidth; // EASU: 출력(중간 RT) 픽셀 크기
    float g_FsrOutputHeight;
    float g_FsrViewportX; // RCAS: Present 대상에서 뷰포트 왼쪽 위(px) — 대상 픽셀 위치를 중간 RT 픽셀로 옮긴다
    float g_FsrViewportY;
    float g_FsrSharpness; // RCAS: 선명도를 줄이는 단계(0 = 가장 선명, 1 단계마다 절반 — AMD FsrRcasCon 의 sharpness)
SW_ROOT_CONSTANTS_END

/// RCAS 의 엽(lobe) 상한이다(AMD FSR_RCAS_LIMIT — 0.25 - 1/16).
static const float kRcasLimit = 0.25f - ( 1.0f / 16.0f );

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain( SwVertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input.position            = input.position;
    const float2 clipPosition = float2( ( vertexId == 1u ) ? 3.0f : -1.0f, ( vertexId == 2u ) ? 3.0f : -1.0f );
    output.position           = float4( clipPosition, 0.0f, 1.0f );
    return output;
}

// ---- 근사 역수(ffx_a.h APrxLoRcpF1 · APrxMedRcpF1 · APrxLoRsqF1 — 비트 트릭, 결과가 원본과 같아야 그림이 같다) ----
float approximateReciprocalLow( float value )
{
    return asfloat( 0x7ef07ebbu - asuint( value ) );
}

float approximateReciprocalMedium( float value )
{
    const float estimate = asfloat( 0x7ef19fffu - asuint( value ) );
    return estimate * ( -estimate * value + 2.0f );
}

float approximateReciprocalSqrtLow( float value )
{
    return asfloat( 0x5f347d74u - ( asuint( value ) >> 1u ) );
}

float max3( float first, float second, float third )
{
    return max( first, max( second, third ) );
}

float min3( float first, float second, float third )
{
    return min( first, min( second, third ) );
}

// ---- 원본 읽기 ----
float4 gatherSourceChannel( float2 uv, uint channel )
{
#if defined( SW_NATIVE_BINDLESS )
    const uint slot = swBindlessTextureSlot( SW_ROOT( g_FsrSource ) );
    // 게더는 필터를 보지 않고 주소 모드만 본다 — 점 · 클램프 샘플러(3 번, swGatherDepthRed 와 같다)로 가장자리 밖을 반대편에서 읽지 않는다.
    #if defined( __spirv__ )
    if ( channel == 0u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherRed( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
    if ( channel == 1u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherGreen( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherBlue( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
    #else
    if ( channel == 0u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherRed( g_SwSampler3, uv );
    if ( channel == 1u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherGreen( g_SwSampler3, uv );
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherBlue( g_SwSampler3, uv );
    #endif
#else
    if ( channel == 0u )
        return g_SwSlot0.GatherRed( g_SwSlot0Sampler, uv );
    if ( channel == 1u )
        return g_SwSlot0.GatherGreen( g_SwSlot0Sampler, uv );
    return g_SwSlot0.GatherBlue( g_SwSlot0Sampler, uv );
#endif
}

float3 loadSourceClamped( int2 texel )
{
    float2 size;
#if defined( SW_NATIVE_BINDLESS )
    const uint slot = swBindlessTextureSlot( SW_ROOT( g_FsrSource ) );
    g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GetDimensions( size.x, size.y );
    const int2 clamped = clamp( texel, int2( 0, 0 ), int2( size ) - int2( 1, 1 ) );
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Load( int3( clamped, 0 ) ).rgb;
#else
    g_SwSlot0.GetDimensions( size.x, size.y );
    const int2 clamped = clamp( texel, int2( 0, 0 ), int2( size ) - int2( 1, 1 ) );
    return g_SwSlot0.Load( int3( clamped, 0 ) ).rgb;
#endif
}

// ---- EASU (ffx_fsr1.h FsrEasuTapF · FsrEasuSetF · FsrEasuF) ----

/** @brief 탭 하나를 근사 lanczos(2) 무게로 더합니다. offset 은 해석 위치에서 탭까지, direction 은 기울기 방향, stretch 는 방향별 길이 배율이다. */
void accumulateEasuTap( inout float3 inoutColor, inout float inoutWeight, float2 offset, float2 direction, float2 stretch, float negativeLobe, float clipPoint, float3 tapColor )
{
    float2 rotated;
    rotated.x = ( offset.x * direction.x ) + ( offset.y * direction.y );
    rotated.y = ( offset.x * ( -direction.y ) ) + ( offset.y * direction.x );
    rotated *= stretch;
    const float distanceSquared = min( rotated.x * rotated.x + rotated.y * rotated.y, clipPoint );
    // (25/16 * (2/5 * x^2 - 1)^2 - (25/16 - 1)) * (lob * x^2 - 1)^2 — sin · rcp · sqrt 없는 lanczos2 근사.
    float baseWeight   = ( 2.0f / 5.0f ) * distanceSquared - 1.0f;
    float windowWeight = negativeLobe * distanceSquared - 1.0f;
    baseWeight *= baseWeight;
    windowWeight *= windowWeight;
    baseWeight            = ( 25.0f / 16.0f ) * baseWeight - ( 25.0f / 16.0f - 1.0f );
    const float tapWeight = baseWeight * windowWeight;
    inoutColor += tapColor * tapWeight;
    inoutWeight += tapWeight;
}

/** @brief 2x2 중 한 칸(쌍선형 무게 @p bilinearWeight)의 '+' 이웃 휘도로 기울기 방향과 길이를 더합니다(위 · 왼 · 가운데 · 오른 · 아래). */
void accumulateEasuDirection( inout float2 inoutDirection, inout float inoutEdgeLength, float bilinearWeight, float lumaUp, float lumaLeft, float lumaCenter,
                              float lumaRight, float lumaDown )
{
    const float rightDelta = lumaRight - lumaCenter;
    const float leftDelta  = lumaCenter - lumaLeft;
    float       lengthX    = approximateReciprocalLow( max( abs( rightDelta ), abs( leftDelta ) ) );
    const float directionX = lumaRight - lumaLeft;
    inoutDirection.x += directionX * bilinearWeight;
    lengthX = saturate( abs( directionX ) * lengthX );
    lengthX *= lengthX;
    inoutEdgeLength += lengthX * bilinearWeight;

    const float downDelta  = lumaDown - lumaCenter;
    const float upDelta    = lumaCenter - lumaUp;
    float       lengthY    = approximateReciprocalLow( max( abs( downDelta ), abs( upDelta ) ) );
    const float directionY = lumaDown - lumaUp;
    inoutDirection.y += directionY * bilinearWeight;
    lengthY = saturate( abs( directionY ) * lengthY );
    lengthY *= lengthY;
    inoutEdgeLength += lengthY * bilinearWeight;
}

float3 computeEasu( uint2 outputPixel )
{
    const float2 inputSize  = float2( SW_ROOT( g_FsrInputWidth ), SW_ROOT( g_FsrInputHeight ) );
    const float2 outputSize = float2( SW_ROOT( g_FsrOutputWidth ), SW_ROOT( g_FsrOutputHeight ) );
    const float2 inputTexel = 1.0f / inputSize;
    // FsrEasuCon — 출력 정수 위치를 원본 픽셀 위치로(con0), 'F' 탭의 왼쪽 위와 게더 넷의 가운데(con1 · con2 · con3).
    const float2 scale       = inputSize / outputSize;
    float2       position    = float2( outputPixel ) * scale + ( 0.5f * scale - 0.5f );
    const float2 floorCorner = floor( position );
    position -= floorCorner;

    // 12 탭 — 게더 넷: (0) b c, (1) i j f e, (2) k l h g, (3) o n.
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    const float2 gather0   = floorCorner * inputTexel + float2( inputTexel.x, -inputTexel.y );
    const float2 gather1   = gather0 + float2( -inputTexel.x, 2.0f * inputTexel.y );
    const float2 gather2   = gather0 + float2( inputTexel.x, 2.0f * inputTexel.y );
    const float2 gather3   = gather0 + float2( 0.0f, 4.0f * inputTexel.y );
    const float4 bczzRed   = gatherSourceChannel( gather0, 0u );
    const float4 bczzGreen = gatherSourceChannel( gather0, 1u );
    const float4 bczzBlue  = gatherSourceChannel( gather0, 2u );
    const float4 ijfeRed   = gatherSourceChannel( gather1, 0u );
    const float4 ijfeGreen = gatherSourceChannel( gather1, 1u );
    const float4 ijfeBlue  = gatherSourceChannel( gather1, 2u );
    const float4 klhgRed   = gatherSourceChannel( gather2, 0u );
    const float4 klhgGreen = gatherSourceChannel( gather2, 1u );
    const float4 klhgBlue  = gatherSourceChannel( gather2, 2u );
    const float4 zzonRed   = gatherSourceChannel( gather3, 0u );
    const float4 zzonGreen = gatherSourceChannel( gather3, 1u );
    const float4 zzonBlue  = gatherSourceChannel( gather3, 2u );

    // 가장 단순한 휘도 근사(휘도 × 2).
    const float4 bczzLuma = bczzBlue * 0.5f + ( bczzRed * 0.5f + bczzGreen );
    const float4 ijfeLuma = ijfeBlue * 0.5f + ( ijfeRed * 0.5f + ijfeGreen );
    const float4 klhgLuma = klhgBlue * 0.5f + ( klhgRed * 0.5f + klhgGreen );
    const float4 zzonLuma = zzonBlue * 0.5f + ( zzonRed * 0.5f + zzonGreen );
    const float  lumaB    = bczzLuma.x;
    const float  lumaC    = bczzLuma.y;
    const float  lumaI    = ijfeLuma.x;
    const float  lumaJ    = ijfeLuma.y;
    const float  lumaF    = ijfeLuma.z;
    const float  lumaE    = ijfeLuma.w;
    const float  lumaK    = klhgLuma.x;
    const float  lumaL    = klhgLuma.y;
    const float  lumaH    = klhgLuma.z;
    const float  lumaG    = klhgLuma.w;
    const float  lumaO    = zzonLuma.z;
    const float  lumaN    = zzonLuma.w;

    // 쌍선형 무게로 네 칸(f g / j k)의 방향 · 길이를 모은다.
    float2 direction  = float2( 0.0f, 0.0f );
    float  edgeLength = 0.0f;
    accumulateEasuDirection( direction, edgeLength, ( 1.0f - position.x ) * ( 1.0f - position.y ), lumaB, lumaE, lumaF, lumaG, lumaJ );
    accumulateEasuDirection( direction, edgeLength, position.x * ( 1.0f - position.y ), lumaC, lumaF, lumaG, lumaH, lumaK );
    accumulateEasuDirection( direction, edgeLength, ( 1.0f - position.x ) * position.y, lumaF, lumaI, lumaJ, lumaK, lumaN );
    accumulateEasuDirection( direction, edgeLength, position.x * position.y, lumaG, lumaJ, lumaK, lumaL, lumaO );

    // 근사로 정규화하고 0 가까이는 정리한다.
    const float2 directionSquared = direction * direction;
    float        directionLength  = directionSquared.x + directionSquared.y;
    const bool   bNearZero        = directionLength < ( 1.0f / 32768.0f );
    directionLength               = approximateReciprocalSqrtLow( directionLength );
    directionLength               = bNearZero ? 1.0f : directionLength;
    direction.x                   = bNearZero ? 1.0f : direction.x;
    direction *= directionLength;
    // {0..2} → {0..1} 로 옮기고 제곱으로 모양을 낸다.
    edgeLength = edgeLength * 0.5f;
    edgeLength *= edgeLength;
    // 가로 · 세로 1 에서 대각선 sqrt(2) 로 늘인다.
    const float  diagonalStretch = ( direction.x * direction.x + direction.y * direction.y ) * approximateReciprocalLow( max( abs( direction.x ), abs( direction.y ) ) );
    const float2 stretch         = float2( 1.0f + ( diagonalStretch - 1.0f ) * edgeLength, 1.0f - 0.5f * edgeLength );
    // 가장자리 정도에 따라 창이 sqrt(2) 에서 2 조금 넘게까지 옮겨 간다.
    const float negativeLobe = 0.5f + ( ( 1.0f / 4.0f - 0.04f ) - 0.5f ) * edgeLength;
    const float clipPoint    = approximateReciprocalLow( negativeLobe );

    // 가장 가까운 넷(f g j k)의 최소 · 최대로 링잉을 자른다.
    const float3 colorF  = float3( ijfeRed.z, ijfeGreen.z, ijfeBlue.z );
    const float3 colorG  = float3( klhgRed.w, klhgGreen.w, klhgBlue.w );
    const float3 colorJ  = float3( ijfeRed.y, ijfeGreen.y, ijfeBlue.y );
    const float3 colorK  = float3( klhgRed.x, klhgGreen.x, klhgBlue.x );
    const float3 minimum = min( min( colorF, min( colorG, colorJ ) ), colorK );
    const float3 maximum = max( max( colorF, max( colorG, colorJ ) ), colorK );

    float3 accumulatedColor  = float3( 0.0f, 0.0f, 0.0f );
    float  accumulatedWeight = 0.0f;
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 0.0f, -1.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( bczzRed.x, bczzGreen.x, bczzBlue.x ) ); // b
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 1.0f, -1.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( bczzRed.y, bczzGreen.y, bczzBlue.y ) ); // c
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( -1.0f, 1.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( ijfeRed.x, ijfeGreen.x, ijfeBlue.x ) ); // i
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 0.0f, 1.0f ) - position, direction, stretch, negativeLobe, clipPoint, colorJ );                                        // j
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 0.0f, 0.0f ) - position, direction, stretch, negativeLobe, clipPoint, colorF );                                        // f
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( -1.0f, 0.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( ijfeRed.w, ijfeGreen.w, ijfeBlue.w ) ); // e
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 1.0f, 1.0f ) - position, direction, stretch, negativeLobe, clipPoint, colorK );                                        // k
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 2.0f, 1.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( klhgRed.y, klhgGreen.y, klhgBlue.y ) );  // l
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 2.0f, 0.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( klhgRed.z, klhgGreen.z, klhgBlue.z ) );  // h
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 1.0f, 0.0f ) - position, direction, stretch, negativeLobe, clipPoint, colorG );                                        // g
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 1.0f, 2.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( zzonRed.z, zzonGreen.z, zzonBlue.z ) );  // o
    accumulateEasuTap( accumulatedColor, accumulatedWeight, float2( 0.0f, 2.0f ) - position, direction, stretch, negativeLobe, clipPoint, float3( zzonRed.w, zzonGreen.w, zzonBlue.w ) );  // n
    return min( maximum, max( minimum, accumulatedColor * rcp( accumulatedWeight ) ) );
}

// ---- RCAS (ffx_fsr1.h FsrRcasF — 잡음 제거 · 알파 통과는 끈 기본형) ----

float3 computeRcas( int2 pixel )
{
    //    b
    //  d e f
    //    h
    const float3 colorB = loadSourceClamped( pixel + int2( 0, -1 ) );
    const float3 colorD = loadSourceClamped( pixel + int2( -1, 0 ) );
    const float3 colorE = loadSourceClamped( pixel );
    const float3 colorF = loadSourceClamped( pixel + int2( 1, 0 ) );
    const float3 colorH = loadSourceClamped( pixel + int2( 0, 1 ) );
    // 고리(b d f h)의 최소 · 최대.
    const float3 ringMinimum = min( min( colorB, min( colorD, colorF ) ), colorH );
    const float3 ringMaximum = max( max( colorB, max( colorD, colorF ) ), colorH );
    // 엽의 한계 — 정밀한 역수가 필요하다.
    const float2 peak       = float2( 1.0f, -1.0f * 4.0f );
    const float3 hitMinimum = min( ringMinimum, colorE ) * rcp( 4.0f * ringMaximum );
    const float3 hitMaximum = ( peak.x - max( ringMaximum, colorE ) ) * rcp( 4.0f * ringMinimum + peak.y );
    const float3 lobeRgb    = max( -hitMinimum, hitMaximum );
    const float  sharpness  = exp2( -SW_ROOT( g_FsrSharpness ) );
    const float  lobe       = max( -kRcasLimit, min( max3( lobeRgb.r, lobeRgb.g, lobeRgb.b ), 0.0f ) ) * sharpness;
    // 해석 — 톤이 바뀌지 않게 중간 정밀도 근사 역수.
    const float resolveScale = approximateReciprocalMedium( 4.0f * lobe + 1.0f );
    return ( lobe * colorB + lobe * colorD + lobe * colorH + lobe * colorF + colorE ) * resolveScale;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
#if defined( SW_PASS_FSR_RCAS )
    const int2 pixel = int2( floor( input.position.xy - float2( SW_ROOT( g_FsrViewportX ), SW_ROOT( g_FsrViewportY ) ) ) );
    return float4( computeRcas( pixel ), 1.0f );
#else
    return float4( computeEasu( uint2( input.position.xy ) ), 1.0f );
#endif
}
