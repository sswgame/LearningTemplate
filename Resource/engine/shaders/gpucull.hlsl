#include "common.hlsli"
#include "batchdata.hlsli"

/**
 * gpucull.hlsl — GPU 컬링 + **드로우 커맨드 생성** (언리얼 FInstanceCullingContext 와 같은 자리).
 *
 * 스레드 하나가 인스턴스 하나를 맡는다. 절두체를 통과하면
 *   1) 자기 배치의 `instanceCount` 를 InterlockedAdd 로 하나 올려 **자리 번호를 받고**,
 *   2) 그 자리에 자기 인스턴스 번호를 적는다 (`g_VisibleInstanceIDs`).
 * 그래서 간접 인자의 개수와 인스턴스 목록이 **함께** 만들어진다. 정점 셰이더는 입력 어셈블러가 주는 인스턴스 슬롯
 * (`SW_INSTANCESLOT` — 간접 인자의 startInstanceLocation + 서수)으로 `g_SwVisibleInstanceIDs[slot]` 을 읽어 자기 인스턴스를 찾는다
 * (`InstanceData::resolveId`).
 *
 * 주의: 보이는 **개수만** 세어 `instanceCount` 에 넣으면 드로우는 늘 배치 앞쪽 N 개를 그린다 — 앞이 안 보이고 뒤가
 * 보이는 상황에서 **보이는 쪽이 사라지고 안 보이는 쪽이 그려진다**. 개수만으로는 무엇을 그릴지 고를 수 없어 압축 목록을 만든다.
 *
 * 개수는 디스패치 전에 0 이어야 한다 — GPUScene 이 컬링이 켜져 있을 때 0 을 올린다.
 *
 * 바인딩 계약(bindingslots.hlsli): CB b0, 인스턴스 읽기 t0, 배치 구간 읽기 t1, 가림 타일 읽기 t2, 간접 인자 쓰기 u0,
 * 가시 ID 쓰기 u1.
 *
 * 가림 판정(g_OcclusionGrid.w = 1, C++ FrameRenderer::fillOcclusionParams): 절두체를 통과한 인스턴스의 경계 구를 감싼 상자를 지난 프레임 장면 깊이를 그린
 * 뷰-투영(g_OcclusionViewProj)으로 옮겨 화면 사각형과 가장 가까운 깊이를 구하고, 사각형이 덮는 가림 타일(occlusiontiles.hlsl — 타일마다 최대 깊이)보다 모두
 * 멀면 그리지 않는다. 모서리 하나라도 카메라 뒤 · 가까운 평면 앞이면, 사각형이 화면 밖이면, 타일 kMaxOcclusionTestTiles 줄을 넘게 덮으면(큰 물체) 판정하지 않는다(보인다).
 */

// 인스턴스 원소(InstanceData)는 그래픽스와 같은 정의 하나를 쓴다(베낀 구조체는 계약 검사가 보지 않는다).
#include "instancedata.hlsli"

// C++ RHIDrawIndirectCommand 와 레이아웃 일치. 여기서 쓰는 것은 instanceCount 뿐이다 — startVertexLocation 는 정점 풀 시작,
// startInstanceLocation 는 배치의 인스턴스 시작(인스턴스 슬롯 스트림의 원소를 그만큼 건너뛴다).
struct RHIDrawIndirectCommand
{
    uint _vertexCount;
    uint _instanceCount;
    uint _startVertexLocation;
    uint _startInstanceLocation;
};

/**
 * 배치의 인스턴스 구간. 간접 인자의 startInstanceLocation 는 **0 이어야 해서**(Vulkan 의 InstanceIndex 가
 * firstInstance 를 포함하므로 셰이더가 루트 상수로 더한다) 컬링이 그 값을 배치 시작점으로 쓸 수 없다.
 * 그래서 시작점은 이 버퍼가 따로 알려준다 — 언리얼이 드로우 커맨드마다 인스턴스 구간을 들고 있는 것과 같다.
 */
// 배치 표 원소는 binding.hlsli 와 같은 BatchData(batchdata.hlsli) 다.

SW_DECLARE_CBUFFER( CullParams, SW_SLOT_COMPUTE_CB )
{
    float4   g_FrustumPlanes[6];
    uint     g_InstanceCount;
    uint     g_BatchCount;
    uint     g_VisibleSlotCount; // 가시 목록의 칸 수(가시 구간의 합) — 쓰기 범위
    uint     g_Pad;
    float4x4 g_OcclusionViewProj;  // 가림 타일의 깊이를 그린 프레임의 뷰-투영(행 벡터 규약 — Transform::computeClipPosition 과 같다)
    uint4    g_OcclusionGrid;      // x 열, y 행, z 타일 한 변(px), w 가림 판정 켬 1 · 끔 0
    float4   g_OcclusionDepthSize; // xy 깊이 히스토리 크기(px)
    float4   g_LodDepthRow;        // 인스턴스 LOD 선택의 화면 크기 식 항 — xyz 클립 w 를 만드는 열(뷰-투영의 m14 m24 m34), w = m44
    float4   g_LodView;            // xyz 눈 위치, w 투영 y 배율(AnimationLODUtil::computeProjectionTerms)
    uint4    g_LodControl;         // x 인스턴스 LOD 선택 켬 1 · 끔 0(끄면 늘 단계 0), y 전환 상태 비우기 1, z 이번 프레임의 전환 진행량(0 ~ 255, 0 이면 전환 없이 바로 바꾸고 상태 버퍼를 읽지 않는다)
};

SW_DECLARE_STRUCTURED_BUFFER( InstanceData, g_Instances, 0 );
SW_DECLARE_STRUCTURED_BUFFER( BatchData, g_BatchInfo, 1 );
SW_DECLARE_STRUCTURED_BUFFER( float, g_OcclusionTiles, 2 );
SW_DECLARE_RW_STRUCTURED_BUFFER( RHIDrawIndirectCommand, g_IndirectArgs, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VisibleInstanceIDs, 1 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_LodState, 2 );

/// 가림 판정이 읽는 타일의 한 축 상한 — 화면에서 이보다 넓게 덮는 물체는 판정하지 않는다(스레드 하나가 최대 8 x 8 = 64 번 읽는다).
static const uint kMaxOcclusionTestTiles = 8u;
/// 이보다 작은 클립 w 는 카메라 뒤 · 눈 가까이로 본다(나누면 화면 사각형이 뒤집힌다).
static const float kMinOcclusionClipW = 1.0e-4f;
/// 이보다 작은 클립 w 는 카메라 뒤 · 눈 가까이로 본다(화면 크기를 1 로 본다 — CPU 의 `AnimationLODUtil::computeScreenSize` 의 `kEpsilon`).
static const float kMinLodClipW = 1.0e-5f;
/// 더 고운 단계로 돌아갈 때 화면 크기가 임계값보다 이만큼(비율) 커야 한다 — C++ `MeshLODSystem::kRefineHysteresis` 와 같다.
static const float kLodRefineHysteresis = 0.1f;
/// 크로스페이드 코드(가시 항목의 위쪽 5비트, instancedata.hlsli) = (직전 단계면 kLodFadeOutgoing) | 진행 L(1 ~ 15).
static const uint kLodFadeOutgoing = 16u;

bool isVisible( float3 center, float radius )
{
    SW_UNROLL for ( uint planeIndex = 0; planeIndex < 6; ++planeIndex )
    {
        float planeDistance = dot( g_FrustumPlanes[planeIndex].xyz, center ) + g_FrustumPlanes[planeIndex].w;
        if ( planeDistance < -radius )
            return false;
    }
    return true;
}

/// @brief 경계 구가 뷰에 차지하는 화면 크기(지름 / 화면 높이)입니다. CPU `AnimationLODUtil::computeScreenSize` 와 같은 식이다(절두체 밖 판정만 빼고 — 여기는 이미 통과한 인스턴스다).
float computeLodScreenSize( float3 center, float radius )
{
    const float3 toEye  = center - g_LodView.xyz;
    const float  clipW  = dot( center, g_LodDepthRow.xyz ) + g_LodDepthRow.w;
    const bool   bClose = dot( toEye, toEye ) <= radius * radius || clipW <= kMinLodClipW;
    return bClose ? 1.0f : radius * g_LodView.w / clipW;
}

/**
 * @brief LOD 그룹(머리 배치 @p headBatch)에서 화면 크기에 맞는 단계(0 이 가장 고운 쪽)를 고릅니다. C++ `Mesh::selectLOD` 와 같은 규칙이다 — 화면 크기가 임계값보다 작은 동안
 *        단계를 낮춘다. 단계 k 배치 행(머리 + k)이 이 단계로 넘어가는 임계값을 든다.
 */
uint selectLodLevel( uint headBatch, uint levelCount, float screenSize )
{
    uint level = 0u;
    for ( uint nextLevel = 1u; nextLevel < levelCount; ++nextLevel )
    {
        if ( screenSize >= g_BatchInfo[headBatch + nextLevel]._lodScreenSize )
            break;
        level = nextLevel;
    }
    return level;
}

bool isOccluded( float3 center, float radius )
{
    if ( g_OcclusionGrid.w == 0u )
        return false;
    float2 minUv    = float2( 1.0f, 1.0f );
    float2 maxUv    = float2( 0.0f, 0.0f );
    float  minDepth = 1.0f;
    SW_UNROLL for ( uint corner = 0u; corner < 8u; ++corner )
    {
        const float3 offset = float3( ( corner & 1u ) != 0u ? radius : -radius, ( corner & 2u ) != 0u ? radius : -radius, ( corner & 4u ) != 0u ? radius : -radius );
        const float4 clip   = mul( float4( center + offset, 1.0f ), g_OcclusionViewProj );
        if ( clip.w <= kMinOcclusionClipW )
            return false;
        const float3 ndc = clip.xyz / clip.w;
        const float2 uv  = ndc.xy * float2( 0.5f, -0.5f ) + 0.5f;
        minUv            = min( minUv, uv );
        maxUv            = max( maxUv, uv );
        minDepth         = min( minDepth, ndc.z );
    }
    // 가까운 평면을 걸치거나 지난 화면 밖이면 지난 깊이가 그 자리를 모른다.
    const bool bOutsideHistory = minDepth <= 0.0f || maxUv.x <= 0.0f || maxUv.y <= 0.0f || minUv.x >= 1.0f || minUv.y >= 1.0f;
    if ( bOutsideHistory )
        return false;
    const float2 depthSize = g_OcclusionDepthSize.xy;
    const uint2  lastTile  = g_OcclusionGrid.xy - 1u;
    const uint2  firstTile = min( uint2( saturate( minUv ) * depthSize ) / g_OcclusionGrid.z, lastTile );
    const uint2  endTile   = min( uint2( saturate( maxUv ) * depthSize ) / g_OcclusionGrid.z, lastTile );
    if ( endTile.x - firstTile.x >= kMaxOcclusionTestTiles || endTile.y - firstTile.y >= kMaxOcclusionTestTiles )
        return false;
    float maxTileDepth = 0.0f;
    for ( uint tileY = firstTile.y; tileY <= endTile.y; ++tileY )
    {
        for ( uint tileX = firstTile.x; tileX <= endTile.x; ++tileX )
        {
            maxTileDepth = max( maxTileDepth, g_OcclusionTiles[tileY * g_OcclusionGrid.x + tileX] );
        }
    }
    return minDepth > maxTileDepth;
}

/**
 * @brief 가시 목록에 항목 하나를 적습니다 — 배치 @p batchIndex 의 자리 하나를 예약하고 그 자리에 @p entry(인스턴스 번호 + 크로스페이드 코드)를 씁니다.
 * @details 자리 번호는 배치 안에서의 순서라 배치의 가시 구간 시작을 더해야 전역 자리가 된다.
 */
void appendVisible( uint batchIndex, uint entry )
{
    uint slot = 0;
#if defined( DX11 )
    // SM5.0 에는 웨이브 인트린식이 없다 — 스레드마다 원자 연산을 한다.
    // (DX11 은 간접 인자 제약으로 컬링 자체를 돌리지 않으므로 이 경로는 실제로 쓰이지 않는다.)
    InterlockedAdd( g_IndirectArgs[batchIndex]._instanceCount, 1u, slot );
#else
    // 웨이브 안에서 먼저 모으고 **웨이브당 한 번만** 원자 연산을 한다. 같은 배치에 인스턴스가 많을수록
    // (인스턴스드 렌더링에서는 그게 정상이다) 같은 주소에 대한 경합이 스레드 수만큼 쌓이기 때문이다.
    // 언리얼의 인스턴스 컬링도 같은 이유로 웨이브 단위로 접는다.
    //
    // 한 웨이브 안에 **여러 배치**가 섞일 수 있으므로 batchIndex 가 같은 레인끼리만 묶는다.
    // WaveMatch 는 SM6.5+ 라 쓰지 않고, 같은 값끼리 모으는 표준 관용구를 쓴다.
    const uint firstBatch = WaveReadLaneFirst( batchIndex );
    if ( firstBatch == batchIndex )
    {
        // 이 웨이브에서 batchIndex 가 첫 레인과 같은 레인들만 여기 들어온다.
        const uint laneCount = WaveActiveCountBits( true );
        const uint laneRank  = WavePrefixCountBits( true );
        uint       waveBase  = 0;
        if ( WaveIsFirstLane() )
            InterlockedAdd( g_IndirectArgs[batchIndex]._instanceCount, laneCount, waveBase );
        slot = WaveReadLaneFirst( waveBase ) + laneRank;
    }
    else
    {
        // 첫 레인과 배치가 다른 레인들 — 드문 경우라 그냥 각자 원자 연산을 한다.
        InterlockedAdd( g_IndirectArgs[batchIndex]._instanceCount, 1u, slot );
    }
#endif

    const uint writeAt = g_BatchInfo[batchIndex]._visibleBase + slot;
    if ( writeAt < g_VisibleSlotCount )
        g_VisibleInstanceIDs[writeAt] = entry;
}

/**
 * @brief LOD 그룹 인스턴스의 단계 전환 상태를 한 프레임 진행하고 이번 프레임에 그릴 것을 정합니다.
 * @details 상태(`g_LodState[인스턴스]`)는 뷰마다 하나이고 인스턴스 번호가 안정적인 동안 이어진다(배치를 다시 만들면 C++ 이 `g_LodControl.y` 로 비운다).
 *          비트: 0..7 지금 단계, 8..15 직전 단계, 16..23 전환 진행(0..255, 255 = 끝).
 *
 *          전환은 상보 디더 크로스페이드다 — 새 단계는 진행 비율만큼의 픽셀(문턱 <= L / 16), 직전 단계는 나머지 픽셀(문턱 >= L / 16)을 그려 둘이 겹치지도 비지도 않는다
 *          (binding.hlsli `Appearance::clipDither` 가 음수 등장 비율을 상보 패턴으로 읽는다). 진행 도중 단계가 또 바뀌면 지금의 새 단계가 직전 단계가 되어 새로 시작한다.
 *          고운 쪽으로 돌아가는 것은 CPU 선택과 같이 임계값을 `kLodRefineHysteresis` 만큼 넘어야 한다. 등장 디더 중인 인스턴스(`_appearance < 1`)는 같은 디더를 쓰므로 전환을 건너뛰고 바로 바꾼다.
 *
 * @param outLevel      그릴 새(현재) 단계
 * @param outPrevious   직전 단계(전환 중일 때만 쓴다)
 * @param outFadeLevel  0 ~ 16. 16 = 전환 없음(새 단계만, 코드 없음), 0 = 직전 단계만, 1 ~ 15 = 둘 다(새 단계는 L, 직전 단계는 상보)
 */
void advanceLodFade( uint instanceId, uint headBatch, uint levelCount, float screenSize, float appearance, out uint outLevel, out uint outPrevious, out uint outFadeLevel )
{
    const bool bStateful = g_LodControl.z != 0u;
    const uint state     = bStateful ? g_LodState[instanceId] : 0u;
    uint       current   = state & 0xFFu;
    uint       previous  = ( state >> 8u ) & 0xFFu;
    uint       progress  = ( state >> 16u ) & 0xFFu;
    uint       target    = selectLodLevel( headBatch, levelCount, screenSize );
    const bool bReset    = g_LodControl.y != 0u || bStateful == false || current >= levelCount || previous >= levelCount;
    if ( bReset == false && target < current )
        target = min( selectLodLevel( headBatch, levelCount, screenSize / ( 1.0f + kLodRefineHysteresis ) ), current );

    const bool bFadeOn = bStateful && appearance >= 1.0f;
    if ( bReset || bFadeOn == false )
    {
        // 처음 보는 인스턴스 · 비운 뒤 · 전환을 쓰지 않는 인스턴스는 바로 그 단계다.
        current  = target;
        previous = target;
        progress = 255u;
    }
    else if ( target != current )
    {
        previous = current;
        current  = target;
        progress = 0u;
    }
    else if ( progress < 255u )
    {
        progress = min( 255u, progress + g_LodControl.z );
    }
    if ( bStateful )
        g_LodState[instanceId] = current | ( previous << 8u ) | ( progress << 16u );

    outLevel     = current;
    outPrevious  = previous;
    outFadeLevel = ( progress == 255u ) ? 16u : ( progress >> 4u );
}

SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchThreadId : SV_DispatchThreadID )
{
    const uint instanceId = dispatchThreadId.x;
    if ( instanceId >= g_InstanceCount )
        return;

    const InstanceData instance  = g_Instances[instanceId];
    const uint         headBatch = instance._meshBatchIndex;
    if ( headBatch >= g_BatchCount )
        return;

    // 바운드 중심은 이미 월드 공간이다 (GPUScene 이 월드 행렬의 이동 성분으로 채우고, instanceanimation 도
    // 회전 뒤에 다시 맞춘다). 여기서 또 world 를 곱하면 이동이 두 번 들어간다.
    if ( isVisible( instance._boundsCenter, instance._boundsRadius ) == false )
        return;

    // sortMode 1 = 압축을 포기하는 배치. 인스턴스는 배치마다 연속으로 놓이므로 (instanceId - 배치 인스턴스 시작)이 곧 배치 안 자리다 —
    // 제자리 매핑을 가시 구간에 적고 개수는 CPU 가 채운 값을 그대로 둔다. GPU 정렬 한계(SW_SORT_MAX_ELEMENTS)를 넘는
    // 큰 투명 배치만 여기로 온다. 나머지 투명(sortMode 2)은 압축한 뒤 instancesort.hlsl 이 CPU 정렬 순서로 되돌린다.
    const BatchData headInfo = g_BatchInfo[headBatch];
    if ( headInfo._sortMode == 1u )
    {
        g_VisibleInstanceIDs[headInfo._visibleBase + ( instanceId - headInfo._instanceBase )] = instanceId;
        return;
    }

    // 지난 프레임 장면 깊이 뒤에 숨은 인스턴스는 그리지 않는다(위 압축 포기 배치는 개수가 CPU 값이라 거르지 않는다).
    if ( isOccluded( instance._boundsCenter, instance._boundsRadius ) )
        return;

    // LOD 그룹이 아니면 머리 배치에 한 번 쓴다.
    if ( g_LodControl.x == 0u || headInfo._lodLevelCount <= 1u )
    {
        appendVisible( headBatch, instanceId );
        return;
    }

    // LOD 그룹: 화면 크기로 단계를 골라 그 단계 배치(머리 + 단계)의 가시 구간에 쓴다. 단계 배치는 머리와 같은 머티리얼 · 메시이고 정점 구간만 다르며, 인스턴스는 머리에 한 번만 있다.
    // 그룹은 정적 불투명뿐이라 위의 압축 포기(sortMode 1)나 투명 정렬과 만나지 않는다. 전환 중이면 두 단계에 모두 쓴다(항목의 위쪽 비트가 상보 디더 코드).
    uint level     = 0u;
    uint previous  = 0u;
    uint fadeLevel = 16u;
    advanceLodFade( instanceId, headBatch, headInfo._lodLevelCount, computeLodScreenSize( instance._boundsCenter, instance._boundsRadius ), instance._appearance, level, previous, fadeLevel );
    if ( fadeLevel >= 16u )
    {
        appendVisible( headBatch + level, instanceId );
    }
    else if ( fadeLevel == 0u )
    {
        appendVisible( headBatch + previous, instanceId ); // 전환이 막 시작했다 — 직전 단계가 아직 다 보인다
    }
    else
    {
        appendVisible( headBatch + level, instanceId | ( fadeLevel << kVisibleEntryFadeShift ) );
        appendVisible( headBatch + previous, instanceId | ( ( kLodFadeOutgoing | fadeLevel ) << kVisibleEntryFadeShift ) );
    }
}
