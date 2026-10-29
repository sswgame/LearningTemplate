#include "common.hlsli"
#include "fogcommon.hlsli"
#include "lightdata.hlsli"
#include "lightgridcommon.hlsli"

/**
 * volumetricfog.hlsl — 볼륨 안개의 froxel 컴퓨트(RenderPassType::VolumetricFog). 언리얼 Volumetric Fog, 유니티 HDRP Volumetric Fog 에 해당한다.
 *
 * 주 시점의 화면 칸(SW_FOG_VOLUME_DIM_X × DIM_Y)마다 스레드 하나가 시선을 거리의 로그 조각(DIM_Z 개, FogMath::computeBoundaryDistance)으로 앞에서 뒤로 걸으며
 * 조각마다 (1) 그 높이의 안개 밀도로 투과율을 곱하고 (2) 그 자리의 빛을 구해 산란을 더한다. 결과는 조각 끝까지의 누적 (산란 rgb, 투과율)을 구조 버퍼에 쓴다 —
 * 안개 패스(fog.hlsl)가 픽셀의 시선 거리까지의 누적을 읽는다. 열 하나가 스레드 하나라 조각 사이 동기화가 없다.
 *
 * 조각 하나의 빛 L = 1(앰비언트: 안개 색이 곧 앰비언트라 빛이 없으면 평범한 안개와 같다) + 산란 배율 × ( 해 + 지역 빛 ).
 *   해: 키 라이트 색 × 세기 × 위상 함수(FogMath::computePhase — 해를 바라볼수록 이방성이 0 보다 크면 밝다). 방향광의 그림자는 아직 없다(후속: 그림자 아틀라스 샘플).
 *   지역 빛: 점광 · 스폿을 라이트 그리드에서 읽는다 — 그 자리의 클러스터 목록(조명 식의 커서 LightCursor::begin 과 같은 순서, 방향광은 해 하나로 이미 셌으므로 건너뛴다).
 *   감쇠는 lighting.hlsli LightData::computeIncidence 와 같은 식이다(반경에서 자르는 역제곱 + 스폿 원뿔). 그림자 없음.
 * 조각의 산란 = 안개 색 × L × ( 1 - 조각 투과율 ) × 지금까지의 투과율 — 빛이 1 이면 합이 안개 색 × (1 - 전체 투과율) 이라 평범한 안개 식(fog.hlsl)과 같다.
 * 시간 누적(재투영)은 아직 없다 — 조각 사이 선형 보간이 줄무늬를 줄인다.
 *
 * 바인딩 계약: 읽기 t0 라이트(g_SwLights 와 같은 버퍼) · t1 입력 행(g_FogParams, 배치는 아래 kRow*) · t2 라이트 그리드 단어(뷰의 그리드가 없으면 자리표), 쓰기 u0 볼륨.
 * 라이트 수(행 14 의 x)가 0 이면 라이트 버퍼를 읽지 않는다. 상수 버퍼는 없다. C++: FramePostProcess::dispatchVolumetricFog — dispatchCompute( ceil(DIM_X / 8), ceil(DIM_Y / 8), 1 ).
 */

SW_DECLARE_STRUCTURED_BUFFER( LightData, g_FogLights, 0 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_FogParams, 1 );
SW_DECLARE_STRUCTURED_BUFFER( uint, g_FogLightGrid, 2 );
SW_DECLARE_RW_STRUCTURED_BUFFER( float4, g_FogVolumeRW, 0 );

// 입력 행 배치(C++ FramePostProcess::prepareVolumetricFog 가 같은 순서로 올린다). 행렬은 행벡터 규약의 행 넷이다.
static const uint kRowInverseViewProj = 0u;  // 0..3
static const uint kRowViewProj        = 4u;  // 4..7
static const uint kRowEyeNear         = 8u;  // 눈 xyz, 가까운 거리
static const uint kRowFarSlices       = 9u;  // 먼 거리, 조각 수, 칸 수 X, 칸 수 Y
static const uint kRowFogColorDensity = 10u; // 안개 색 rgb, 밀도
static const uint kRowFogHeight       = 11u; // 높이 감쇠, 기준 높이, 시작 거리, 이방성
static const uint kRowSunDirection    = 12u; // 해 쪽 단위 방향 xyz, 산란 배율
static const uint kRowSunRadiance     = 13u; // 해 색 × 세기 rgb, 원근이면 1 · 직교면 0
static const uint kRowLightGrid       = 14u; // asuint( 라이트 수, 그리드 첫 단어, 그리드가 있으면 1, 0 )

/** @brief 월드 위치가 든 클러스터 번호입니다. binding.hlsli LightGrid::computeCluster 와 같은 식이고 뷰-투영은 입력 행에서 온다. */
uint computeFogCluster( float3 worldPosition, uint gridBase, float4x4 viewProj )
{
    const uint   dimX          = g_FogLightGrid[gridBase + SW_LIGHT_GRID_WORD_DIM_X];
    const uint   dimY          = g_FogLightGrid[gridBase + SW_LIGHT_GRID_WORD_DIM_Y];
    const uint   dimZ          = g_FogLightGrid[gridBase + SW_LIGHT_GRID_WORD_DIM_Z];
    const bool   bPerspective  = g_FogLightGrid[gridBase + SW_LIGHT_GRID_WORD_PERSPECTIVE] != 0u;
    const float  nearDepth     = asfloat( g_FogLightGrid[gridBase + SW_LIGHT_GRID_WORD_NEAR_DEPTH] );
    const float  sliceScale    = asfloat( g_FogLightGrid[gridBase + SW_LIGHT_GRID_WORD_SLICE_SCALE] );
    const float4 clip          = mul( float4( worldPosition, 1.0f ), viewProj );
    const float3 ndc           = clip.xyz / clip.w;
    const uint   tileX         = LightGrid::computeTile( ndc.x, dimX );
    const uint   tileY         = LightGrid::computeTile( ndc.y, dimY );
    const float  slicePosition = LightGrid::computeSlicePosition( bPerspective ? clip.w : ndc.z, bPerspective, nearDepth, sliceScale );
    const uint   slice         = min( (uint)max( slicePosition, 0.0f ), dimZ - 1u );
    return ( slice * dimY + tileY ) * dimX + tileX;
}

/**
 * @brief 점광 · 스폿 하나가 이 위치에 비추는 산란 기여(rgb)입니다. 다른 종류(방향광 · 2D 빛 · 프로브)는 0 이다. 감쇠는 lighting.hlsli LightData::computeIncidence 와 같다.
 */
float3 computeFogLocalLight( LightData light, float3 worldPosition, float3 viewDirection, float anisotropy )
{
    const uint   lightType  = (uint)( light._directionType.w + 0.5f );
    const bool   bLocal     = lightType == SW_LIGHT_TYPE_POINT || lightType == SW_LIGHT_TYPE_SPOT;
    const float3 delta      = light._positionRadius.xyz - worldPosition;
    const float  distance   = length( delta );
    const float  radius     = max( light._positionRadius.w, 1.0e-4f );
    const float3 toLight    = delta / max( distance, 1.0e-4f );
    const float  normalized = saturate( 1.0f - distance / radius );
    const float  cosAngle   = dot( light._directionType.xyz, -toLight );
    const float  cone       = saturate( ( cosAngle - light._params.y ) / max( light._params.z - light._params.y, 1.0e-4f ) );
    const float  spot       = lightType == SW_LIGHT_TYPE_SPOT ? cone * cone : 1.0f;
    const float  phase      = FogMath::computePhase( dot( viewDirection, toLight ), anisotropy );
    const float  weight     = bLocal ? normalized * normalized * spot * phase * light._colorIntensity.a : 0.0f;
    return light._colorIntensity.rgb * weight;
}

SW_NUMTHREADS( 8, 8, 1 )
void CSMain( uint3 dispatchID : SV_DispatchThreadID )
{
    const float4   farSlices       = g_FogParams[kRowFarSlices];
    const uint     dimX            = (uint)farSlices.z;
    const uint     dimY            = (uint)farSlices.w;
    const bool     bInside         = dispatchID.x < dimX && dispatchID.y < dimY;
    const float    farDistance     = farSlices.x;
    const uint     sliceCount      = (uint)farSlices.y;
    const float4   eyeNear         = g_FogParams[kRowEyeNear];
    const float4   fogColorDens    = g_FogParams[kRowFogColorDensity];
    const float4   fogHeight       = g_FogParams[kRowFogHeight];
    const float4   sunDirection    = g_FogParams[kRowSunDirection];
    const float4   sunRadiance     = g_FogParams[kRowSunRadiance];
    const uint4    lightGrid       = asuint( g_FogParams[kRowLightGrid] );
    const float4   heightParams    = float4( fogHeight.x, fogHeight.y, fogHeight.z, 1.0f );
    const float4x4 inverseViewProj = float4x4( g_FogParams[kRowInverseViewProj], g_FogParams[kRowInverseViewProj + 1u], g_FogParams[kRowInverseViewProj + 2u],
                                               g_FogParams[kRowInverseViewProj + 3u] );
    const float4x4 viewProj        = float4x4( g_FogParams[kRowViewProj], g_FogParams[kRowViewProj + 1u], g_FogParams[kRowViewProj + 2u], g_FogParams[kRowViewProj + 3u] );

    // 이 칸의 시선 — 원근은 눈에서, 직교는 화면 점의 가까운 면 점에서 쏜다(fog.hlsl 과 같다).
    const float2 uv              = ( float2( (float)dispatchID.x, (float)dispatchID.y ) + 0.5f ) / float2( (float)max( dimX, 1u ), (float)max( dimY, 1u ) );
    const float2 ndcXy           = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 nearHomogeneous = mul( float4( ndcXy, 0.0f, 1.0f ), inverseViewProj );
    const float4 farHomogeneous  = mul( float4( ndcXy, 1.0f, 1.0f ), inverseViewProj );
    const float3 nearPoint       = nearHomogeneous.xyz / nearHomogeneous.w;
    const float3 farPoint        = farHomogeneous.xyz / farHomogeneous.w;
    const bool   bPerspective    = sunRadiance.w > 0.5f;
    const float3 origin          = bPerspective ? eyeNear.xyz : nearPoint;
    const float3 direction       = normalize( farPoint - origin );
    const float  sunPhase        = FogMath::computePhase( dot( direction, sunDirection.xyz ), fogHeight.w );

    float4 accumulated = float4( 0.0f, 0.0f, 0.0f, 1.0f );
    if ( bInside )
    {
        SW_LOOP for ( uint slice = 0u; slice < sliceCount; ++slice )
        {
            const float  startDistance = FogMath::computeBoundaryDistance( (float)slice, farDistance );
            const float  endDistance   = FogMath::computeBoundaryDistance( (float)slice + 1.0f, farDistance );
            const float  middle        = 0.5f * ( startDistance + endDistance );
            const float3 position      = origin + direction * middle;
            // 시작 거리 안쪽은 안개가 없다 — 조각 중 시작 거리 밖의 길이만 센다.
            const float segmentLength = max( endDistance - max( startDistance, fogHeight.z ), 0.0f );
            const float density       = FogMath::computeDensityAt( fogColorDens, heightParams, position.y );
            const float sliceTransmit = exp( -density * segmentLength );

            // 빛 — 앰비언트 1 + 산란 배율 × ( 해 + 지역 빛 ).
            float3 light = sunRadiance.rgb * sunPhase;
            if ( lightGrid.x > 0u )
            {
                if ( lightGrid.z != 0u )
                {
                    const uint base         = lightGrid.y;
                    const uint cluster      = computeFogCluster( position, base, viewProj );
                    const uint indexBase    = base + g_FogLightGrid[base + SW_LIGHT_GRID_WORD_INDEX_BASE];
                    const uint clusterWord  = base + g_FogLightGrid[base + SW_LIGHT_GRID_WORD_CLUSTER_BASE] + cluster * SW_LIGHT_GRID_WORDS_PER_CLUSTER;
                    const uint clusterStart = indexBase + g_FogLightGrid[clusterWord];
                    const uint clusterCount = g_FogLightGrid[clusterWord + 1u];
                    SW_LOOP for ( uint entry = 0u; entry < clusterCount; ++entry )
                    {
                        const uint lightIndex = g_FogLightGrid[clusterStart + entry];
                        light += computeFogLocalLight( g_FogLights[min( lightIndex, lightGrid.x - 1u )], position, direction, fogHeight.w );
                    }
                }
                else
                {
                    SW_LOOP for ( uint lightIndex = 0u; lightIndex < lightGrid.x; ++lightIndex )
                    {
                        light += computeFogLocalLight( g_FogLights[lightIndex], position, direction, fogHeight.w );
                    }
                }
            }
            const float3 lighting = float3( 1.0f, 1.0f, 1.0f ) + sunDirection.w * light;

            accumulated.rgb += accumulated.a * fogColorDens.rgb * lighting * ( 1.0f - sliceTransmit );
            accumulated.a *= sliceTransmit;
            g_FogVolumeRW[( slice * dimY + dispatchID.y ) * dimX + dispatchID.x] = accumulated;
        }
    }
}
