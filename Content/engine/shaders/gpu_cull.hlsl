#include "common.hlsli"
#include "batch_data.hlsli"
#include "gpu_message.hlsli"

/**
 * gpu_cull.hlsl — GPU 컬링 + **드로우 커맨드 생성** (언리얼 FInstanceCullingContext 와 같은 자리).
 *
 * 스레드 하나가 인스턴스 하나를 맡는다. 절두체를 통과하면
 *   1) 자기 배치의 `instanceCount` 를 InterlockedAdd 로 하나 올려 **자리 번호를 받고**,
 *   2) 그 자리에 자기 인스턴스 번호를 적는다 (`g_VisibleInstanceIDs`).
 * 그래서 간접 인자의 개수와 인스턴스 목록이 **함께** 만들어진다. 정점 셰이더는 입력 어셈블러가 주는 인스턴스 슬롯
 * (`SW_INSTANCESLOT` — 간접 인자의 startInstanceLocation + 서수)으로 `g_SwVisibleInstanceIDs[slot]` 을 읽어 자기 인스턴스를 찾는다
 * (`InstanceData::resolveID`).
 *
 * 주의: 보이는 **개수만** 세어 `instanceCount` 에 넣으면 드로우는 늘 배치 앞쪽 N 개를 그린다 — 앞이 안 보이고 뒤가
 * 보이는 상황에서 **보이는 쪽이 사라지고 안 보이는 쪽이 그려진다**. 개수만으로는 무엇을 그릴지 고를 수 없어 압축 목록을 만든다.
 *
 * 개수는 디스패치 전에 0 이어야 한다 — 같은 파일의 되돌리기 커널(`SW_CULL_RESET_ARGS`, C++ RenderPassKind::IndirectArgsReset)이 컬링 바로 앞에서
 * 배치마다 0 을 쓴다. CPU 는 배치 표가 바뀔 때만 인자 전체를 올린다.
 *
 * 바인딩 계약(binding_slots.hlsli): CB b0, 인스턴스 읽기 t0, 배치 구간 읽기 t1, HZB 피라미드 읽기 t2, 차례 → 칸 읽기 t3, 간접 인자 쓰기 u0,
 * 가시 ID 쓰기 u1, LOD 전환 상태 u2, 가려진 차례 표시 u3(2 단계 가림).
 *
 * 인스턴스 버퍼는 **칸 순**이다(원소 번호 = GPUScene 칸, 구멍은 배치 번호가 무효 — C++ GPUSceneSlotBuffer). 스레드 하나가 **그리는 차례** 하나를 맡아
 * t3(g_InstanceOrder)에서 그 차례의 칸을 읽는다. 가시 목록에는 칸을 적는다 — 정점 셰이더는 그 칸으로 인스턴스를 읽는다. 투명 정렬 배치(sortMode 1 · 2)만
 * 차례를 적고 instance_sort.hlsl 이 정렬한 뒤 칸으로 바꾼다(정렬 기준이 차례다).
 *
 * 가림 판정(g_HZBControl.w = 판정 발자국 > 0, C++ HZBOcclusion::fillCullParams): 절두체를 통과한 인스턴스의 경계 상자를 HZB 의 깊이를 그린 뷰-투영(g_HZBViewProj)으로
 * 옮겨 화면 사각형과 가장 가까운 깊이를 구하고, 사각형이 한 축 발자국 텍셀 안에 드는 가장 고운 밉(hzb.hlsli HZB::selectMip)에서 덮는 텍셀의 최대 깊이보다 멀면
 * 그리지 않는다 — 큰 물체는 굵은 밉에서 판정한다(언리얼 HZB 가림과 같다). 모서리 하나라도 카메라 뒤 · 가까운 평면 앞이면, 사각형이 화면 밖이면 판정하지 않는다(보인다).
 * 상자 → 사각형 · 밉 고르기는 C++ HZBScreenRect::projectBox · HZBLayout::selectMip 과 같은 식이다(단위 테스트 HZBPyramidTest).
 *
 * 2 단계 가림(g_CullPhase.x, C++ FrameGraphExecutor::recordLateOcclusion — Adria · VQEngine · Niagara 의 two-phase occlusion):
 *   1 앞 단계 — 프레임 그래프에서 지난 프레임 HZB 로 판정하고, 가려져 빠진 불투명(sortMode 0) 차례를 u3 에 적는다(나머지 차례는 0).
 *   2 뒤 단계 — 깊이 프리패스 뒤 그 깊이로 다시 지은 HZB 로 적힌 차례만 다시 판정해 이제 보이는 것을 같은 간접 인자에 더한다(개수를 되돌리지 않는다 — 앞 단계 개수 위에 센다).
 *   투명 정렬 배치(sortMode 1 · 2)는 앞 단계 판정만 본다 — 뒤 단계가 더하면 정렬(instance_sort.hlsl)이 끝난 목록 뒤에 붙어 블렌드 순서가 틀린다.
 *   0 은 한 단계(2 단계를 끈 설정 · 그림자 · 추가 뷰)다.
 */

// 인스턴스 원소(InstanceData)는 그래픽스와 같은 정의 하나를 쓴다(베낀 구조체는 계약 검사가 보지 않는다).
#include "instance_data.hlsli"

// 간접 인자 원소(SceneDrawArgs)도 같은 정의 하나다 — C++ RHIDrawIndexedIndirectCommand 와 바이트 배치가 같다.
#include "scene_draw_args.hlsli"

// HZB 피라미드 배치 식(밉 크기 · 시작 원소 · 밉 고르기)은 짓는 쪽과 같은 정의 하나다.
#include "hzb.hlsli"

/**
 * 배치의 인스턴스 구간. 간접 인자의 startInstanceLocation 는 **0 이어야 해서**(Vulkan 의 InstanceIndex 가
 * firstInstance 를 포함하므로 셰이더가 루트 상수로 더한다) 컬링이 그 값을 배치 시작점으로 쓸 수 없다.
 * 그래서 시작점은 이 버퍼가 따로 알려준다 — 언리얼이 드로우 커맨드마다 인스턴스 구간을 들고 있는 것과 같다.
 */
// 배치 표 원소는 binding.hlsli 와 같은 BatchData(batch_data.hlsli) 다.

SW_DECLARE_CBUFFER( CullParams, SW_SLOT_COMPUTE_CB )
{
    float4   g_FrustumPlanes[6];
    uint     g_InstanceCount;
    uint     g_BatchCount;
    uint     g_VisibleSlotCount;  // 가시 목록의 칸 수(가시 구간의 합) — 쓰기 범위
    uint     g_InstanceSlotCount; // 칸 순 인스턴스 버퍼의 원소 수(GPUScene 칸 수) — 차례가 가리킨 칸의 범위
    float4x4 g_HZBViewProj;       // HZB 의 깊이를 그린 뷰-투영(행 벡터 규약 — Transform::computeClipPosition 과 같다)
    uint4    g_HZBControl;        // xy HZB 를 지은 깊이 사각형(px), z 밉 수, w 판정 발자국(한 축 텍셀 수 — 0 이면 가림 판정 끔)
    float4   g_HZBParams;         // x 깊이 치우침(NDC — 가장 가까운 깊이가 최대 깊이 + 이 값보다 멀어야 가렸다)
    float4   g_LODDepthRow;       // 인스턴스 LOD 선택의 화면 크기 식 항 — xyz 클립 w 를 만드는 열(뷰-투영의 m14 m24 m34), w = m44
    float4   g_LODView;           // xyz 눈 위치, w 투영 y 배율(LODViewUtil::computeProjectionTerms)
    uint4    g_LODControl;        // x 인스턴스 LOD 선택 켬 1 · 끔 0(끄면 늘 단계 0), y 전환 상태 비우기 1, z 이번 프레임의 전환 진행량(0 ~ 255, 0 이면 전환 없이 바로 바꾸고 상태 버퍼를 읽지 않는다)
    uint4    g_CullPhase;         // x 0 한 단계 · 1 앞 단계(가려진 차례를 u3 에 적는다) · 2 뒤 단계(적힌 차례만 다시 판정한다). 머리말 참고
};

SW_DECLARE_STRUCTURED_BUFFER( InstanceData, g_Instances, 0 );
SW_DECLARE_STRUCTURED_BUFFER( BatchData, g_BatchInfo, 1 );
SW_DECLARE_STRUCTURED_BUFFER( float2, g_HZB, 2 );         // HZB 피라미드(x 최소 · y 최대 깊이 — 판정은 최대만 읽는다)
SW_DECLARE_STRUCTURED_BUFFER( uint, g_InstanceOrder, 3 ); // 그리는 차례 → GPUScene 칸(C++ GPUSceneSnapshot::_pListInstanceSlot)
SW_DECLARE_RW_STRUCTURED_BUFFER( SceneDrawArgs, g_IndirectArgs, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VisibleInstanceIDs, 1 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_LODState, 2 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_OccludedOrders, 3 ); // 차례마다 앞 단계에서 가려져 빠졌으면 1(2 단계 가림의 앞 · 뒤 단계만 읽고 쓴다)

/// 컬링 단계(g_CullPhase.x) — 머리말 참고. 한 단계(0)는 표시 버퍼를 건드리지 않는다.
static const uint kCullPhaseEarly = 1u;
static const uint kCullPhaseLate  = 2u;
/// 이보다 작은 클립 w 는 카메라 뒤로 본다(화면 크기를 눈까지의 거리로 잰다 — 눈이 경계 구 안이면 1 이다).
static const float kMinOcclusionClipW = 1.0e-4f;
/// 이보다 작은 클립 w 는 카메라 뒤 · 눈 가까이로 본다(화면 크기를 1 로 본다 — CPU 의 `LODViewUtil::computeScreenSize` 의 `kEpsilon`).
static const float kMinLODClipW = 1.0e-5f;
/// 더 고운 단계로 돌아갈 때 화면 크기가 임계값보다 이만큼(비율) 커야 한다 — C++ `MeshLODSystem::kRefineHysteresis` 와 같다.
static const float kLODRefineHysteresis = 0.1f;
/// 크로스페이드 코드(가시 항목의 위쪽 5비트, instance_data.hlsli) = (직전 단계면 kLODFadeOutgoing) | 진행 L(1 ~ 15).
static const uint kLODFadeOutgoing = 16u;
/// LOD 전환 상태의 위쪽 8비트에 적어 두는 칸 차례(InstanceData::_slotSerial 의 아래 8비트)의 자리 — 다르면 칸을 다른 물체가 받은 것이라 전환을 처음부터 한다.
static const uint kLODStateSerialShift = 24u;
static const uint kLODStateSerialMask  = 0xFFu;

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

/** @brief 월드 상자(중심 @p center, 반 크기 @p extent)가 절두체 안에 걸치면 true 입니다(평면마다 상자의 가장 앞 꼭짓점 — 법선 절댓값 · 반 크기). */
bool isBoxVisible( float3 center, float3 extent )
{
    SW_UNROLL for ( uint planeIndex = 0; planeIndex < 6; ++planeIndex )
    {
        const float4 plane = g_FrustumPlanes[planeIndex];
        if ( dot( plane.xyz, center ) + plane.w < -dot( abs( plane.xyz ), extent ) )
            return false;
    }
    return true;
}

/**
 * @brief 인스턴스의 월드 상자입니다 — 배치 메시의 메시 공간 상자를 월드 행렬로 옮긴다(Arvo — 행 벡터 규약이라 행 i 가 축 i 의 월드 방향).
 *        컴포넌트 여유(스킨드 · VAT 처럼 메시보다 큰 구)는 인스턴스 구 반지름 ÷ (메시 구 반지름 × 최대 축 스케일)을 1 이상으로 곱한다. 배치에 상자가 없으면 false 다(구로만 판정).
 */
bool computeWorldBox( InstanceData instance, BatchData batch, out float3 outCenter, out float3 outExtent )
{
    const float3 axisX    = instance._world[0].xyz;
    const float3 axisY    = instance._world[1].xyz;
    const float3 axisZ    = instance._world[2].xyz;
    const float  maxScale = sqrt( max( dot( axisX, axisX ), max( dot( axisY, axisY ), dot( axisZ, axisZ ) ) ) );
    const float  meshR    = batch._localBoundsRadius * maxScale;
    const float  margin   = meshR > 0.0f ? max( 1.0f, instance._boundsRadius / meshR ) : 1.0f;
    const float3 local    = batch._localBoundsExtent;
    outCenter             = mul( float4( batch._localBoundsCenter, 1.0f ), instance._world ).xyz;
    outExtent             = ( abs( axisX ) * local.x + abs( axisY ) * local.y + abs( axisZ ) * local.z ) * margin;
    return batch._localBoundsRadius > 0.0f;
}

/// @brief 경계 구가 뷰에 차지하는 화면 크기(지름 / 화면 높이)입니다. CPU `LODViewUtil::computeScreenSize` 와 같은 식이다(절두체 밖 판정만 빼고 — 여기는 이미 통과한 인스턴스다).
float computeLODScreenSize( float3 center, float radius )
{
    const float3 toEye           = center - g_LODView.xyz;
    const float  distanceSquared = dot( toEye, toEye );
    const float  clipW           = dot( center, g_LODDepthRow.xyz ) + g_LODDepthRow.w;
    // 카메라 뒤(클립 w 가 뜻이 없다)는 눈까지의 거리로 잰다 — 1 로 치면 화면 밖에서 그림자만 드리우는 물체가 모두 가장 고운 단계로 그려진다.
    const float viewDepth = clipW > kMinLODClipW ? clipW : sqrt( distanceSquared );
    return distanceSquared <= radius * radius ? 1.0f : radius * g_LODView.w / viewDepth;
}

/**
 * @brief LOD 그룹(머리 배치 @p headBatch)에서 화면 크기에 맞는 단계(0 이 가장 고운 쪽)를 고릅니다. C++ `Mesh::selectLOD` 와 같은 규칙이다 — 화면 크기가 임계값보다 작은 동안
 *        단계를 낮춘다. 단계 k 배치 행(머리 + k)이 이 단계로 넘어가는 임계값을 든다.
 */
uint selectLODLevel( uint headBatch, uint levelCount, float screenSize )
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

/** @brief 월드 상자(중심 @p center, 반 크기 @p extent — 구면 반지름을 세 축에 넣는다)가 HZB 깊이 뒤에 숨었으면 true 입니다. 머리말의 가림 판정 참고. */
bool isOccluded( float3 center, float3 extent )
{
    const uint footprint = g_HZBControl.w;
    if ( footprint == 0u )
        return false;
    float2 minUv    = float2( 1.0f, 1.0f );
    float2 maxUv    = float2( 0.0f, 0.0f );
    float  minDepth = 1.0f;
    SW_UNROLL for ( uint corner = 0u; corner < 8u; ++corner )
    {
        const float3 offset = float3( ( corner & 1u ) != 0u ? extent.x : -extent.x, ( corner & 2u ) != 0u ? extent.y : -extent.y, ( corner & 4u ) != 0u ? extent.z : -extent.z );
        const float4 clip   = mul( float4( center + offset, 1.0f ), g_HZBViewProj );
        if ( clip.w <= kMinOcclusionClipW )
            return false;
        const float3 ndc = clip.xyz / clip.w;
        const float2 uv  = ndc.xy * float2( 0.5f, -0.5f ) + 0.5f;
        minUv            = min( minUv, uv );
        maxUv            = max( maxUv, uv );
        minDepth         = min( minDepth, ndc.z );
    }
    // 가까운 평면을 걸치거나 HZB 화면 밖이면 그 깊이가 자리를 모른다.
    const bool bOutsideHZB = minDepth <= 0.0f || maxUv.x <= 0.0f || maxUv.y <= 0.0f || minUv.x >= 1.0f || minUv.y >= 1.0f;
    if ( bOutsideHZB )
        return false;
    // 사각형이 한 축 발자국 텍셀 안에 드는 가장 고운 밉에서 덮는 텍셀의 최대 깊이를 모은다(발자국 상한이 넷이라 최대 4 x 4 번 읽는다).
    const uint2 depthSize  = g_HZBControl.xy;
    const uint  mipCount   = g_HZBControl.z;
    const uint2 firstPixel = HZB::toPixel( minUv, depthSize );
    const uint2 lastPixel  = HZB::toPixel( maxUv, depthSize );
    const uint  mip        = HZB::selectMip( firstPixel, lastPixel, footprint, mipCount );
    if ( mip >= mipCount )
        return false;
    const uint  shift      = mip + 1u;
    const uint2 size       = HZB::computeMipSize( depthSize, mip );
    const uint  offset     = HZB::computeMipOffset( depthSize, mip );
    const uint2 firstTexel = min( firstPixel >> shift, size - uint2( 1u, 1u ) );
    const uint2 lastTexel  = min( lastPixel >> shift, size - uint2( 1u, 1u ) );
    float       maxDepth   = 0.0f;
    for ( uint texelY = firstTexel.y; texelY <= lastTexel.y; ++texelY )
    {
        for ( uint texelX = firstTexel.x; texelX <= lastTexel.x; ++texelX )
        {
            maxDepth = max( maxDepth, g_HZB[offset + texelY * size.x + texelX].y );
        }
    }
    return minDepth > maxDepth + g_HZBParams.x;
}

/**
 * @brief 가시 목록에 항목 하나를 적습니다 — 배치 @p batchIndex 의 자리 하나를 예약하고 그 자리에 @p entry(인스턴스 번호 + 크로스페이드 코드)를 씁니다.
 * @details 자리 번호는 배치 안에서의 순서라 배치의 가시 구간 시작을 더해야 전역 자리가 된다.
 */
void appendVisible( uint batchIndex, uint entry )
{
    // 배치 번호가 표 밖이면 아래 g_BatchInfo · g_IndirectArgs 가 범위 밖을 읽고 쓴다 — 여기서 알린다(막지는 않는다, 백엔드가 범위 밖을 다르게 다룬다).
    SW_GPU_ASSERT( batchIndex < g_BatchCount, SW_GPU_ASSERT_ID_CULL_BATCH_RANGE, batchIndex );
    uint slot = 0;
#if defined( DX11 ) || defined( SW_NO_WAVE_OPS )
    // SM5.0 에는 웨이브 인트린식이 없다 — 스레드마다 원자 연산을 한다. WebGPU 는 subgroups 가 선택 기능이라 쿠킹이 SW_NO_WAVE_OPS 로 뺀다.
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
    // 가시 구간은 배치의 개수만큼 잡혀 있어야 한다 — 넘으면 항목을 버린 것이다(그 인스턴스가 이번 프레임에 안 그려진다).
    SW_GPU_ASSERT( writeAt < g_VisibleSlotCount, SW_GPU_ASSERT_ID_CULL_VISIBLE_RANGE, writeAt );
    if ( writeAt < g_VisibleSlotCount )
        g_VisibleInstanceIDs[writeAt] = entry;
}

/**
 * @brief LOD 그룹 인스턴스의 단계 전환 상태를 한 프레임 진행하고 이번 프레임에 그릴 것을 정합니다.
 * @details 상태(`g_LODState[칸]`)는 뷰마다 하나이고 칸에 묶인다(칸이 고정이라 배치를 다시 만들어도 이어진다 — C++ 은 상태 버퍼를 새로 만든 프레임 · 뷰 컷에만 `g_LODControl.y` 로 비운다).
 *          비트: 0..7 지금 단계, 8..15 직전 단계, 16..23 전환 진행(0..255, 255 = 끝), 24..31 칸 차례(`@p slotSerial` 의 아래 8비트).
 *          칸 차례가 다르면 그 칸을 다른 물체가 받은 것이다(지우고 다시 등록) — 지난 물체의 단계 · 크로스페이드를 이어받지 않고 처음 보는 인스턴스처럼 바로 그 단계다.
 *
 *          전환은 상보 디더 크로스페이드다 — 새 단계는 진행 비율만큼의 픽셀(문턱 <= L / 16), 직전 단계는 나머지 픽셀(문턱 >= L / 16)을 그려 둘이 겹치지도 비지도 않는다
 *          (binding.hlsli `Appearance::clipDither` 가 음수 등장 비율을 상보 패턴으로 읽는다). 진행 도중 단계가 또 바뀌면 지금의 새 단계가 직전 단계가 되어 새로 시작한다.
 *          고운 쪽으로 돌아가는 것은 CPU 선택과 같이 임계값을 `kLODRefineHysteresis` 만큼 넘어야 한다. 등장 디더 중인 인스턴스(`_appearance < 1`)는 같은 디더를 쓰므로 전환을 건너뛰고 바로 바꾼다.
 *
 * @param outLevel      그릴 새(현재) 단계
 * @param outPrevious   직전 단계(전환 중일 때만 쓴다)
 * @param outFadeLevel  0 ~ 16. 16 = 전환 없음(새 단계만, 코드 없음), 0 = 직전 단계만, 1 ~ 15 = 둘 다(새 단계는 L, 직전 단계는 상보)
 */
void advanceLODFade( uint instanceId, uint slotSerial, uint headBatch, uint levelCount, float screenSize, float appearance, out uint outLevel, out uint outPrevious, out uint outFadeLevel )
{
    const bool bStateful   = g_LODControl.z != 0u;
    const uint state       = bStateful ? g_LODState[instanceId] : 0u;
    const uint serialBits  = slotSerial & kLODStateSerialMask;
    const bool bSameSerial = ( ( state >> kLODStateSerialShift ) & kLODStateSerialMask ) == serialBits;
    uint       current     = state & 0xFFu;
    uint       previous    = ( state >> 8u ) & 0xFFu;
    uint       progress    = ( state >> 16u ) & 0xFFu;
    uint       target      = selectLODLevel( headBatch, levelCount, screenSize );
    const bool bReset      = g_LODControl.y != 0u || bStateful == false || bSameSerial == false || current >= levelCount || previous >= levelCount;
    if ( bReset == false && target < current )
        target = min( selectLODLevel( headBatch, levelCount, screenSize / ( 1.0f + kLODRefineHysteresis ) ), current );

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
        progress = min( 255u, progress + g_LODControl.z );
    }
    if ( bStateful )
        g_LODState[instanceId] = current | ( previous << 8u ) | ( progress << 16u ) | ( serialBits << kLODStateSerialShift );

    outLevel     = current;
    outPrevious  = previous;
    outFadeLevel = ( progress == 255u ) ? 16u : ( progress >> 4u );
}

#if defined( SW_CULL_RESET_ARGS )
// 되돌리기 커널 — 배치마다 스레드 하나. 컬링이 이 위에서 InterlockedAdd 로 센다.
SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint batchIndex = dispatchThreadID.x;
    if ( batchIndex >= g_BatchCount )
        return;
    g_IndirectArgs[batchIndex]._instanceCount = 0u;
}
#else
SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    SW_GPU_MESSAGE_THREAD( dispatchThreadID.x );
    // 스레드 하나가 그리는 차례 하나다. 인스턴스(칸)는 차례 → 칸 목록에서 읽는다 — 칸 순 버퍼라 차례와 칸이 다르다.
    const uint orderIndex = dispatchThreadID.x;
    if ( orderIndex >= g_InstanceCount )
        return;
    // 2 단계 가림: 앞 단계는 표시를 먼저 지운다(아래 어느 길로 돌아가도 0 이다). 뒤 단계는 앞 단계가 가려졌다고 적은 차례만 본다 — 그 차례는 불투명이라
    // 아래 투명 길에 닿지 않고, LOD 전환도 앞 단계에서 진행하지 않았다(가림 판정이 LOD 앞이다).
    const uint phase = g_CullPhase.x;
    if ( phase == kCullPhaseEarly )
        g_OccludedOrders[orderIndex] = 0u;
    if ( phase == kCullPhaseLate && g_OccludedOrders[orderIndex] == 0u )
        return;
    const uint instanceId = g_InstanceOrder[orderIndex];
    if ( instanceId >= g_InstanceSlotCount )
        return;
    SW_GPU_ASSERT( instanceId <= kVisibleEntryIDMask, SW_GPU_ASSERT_ID_CULL_INSTANCE_ID, instanceId );

    const InstanceData instance  = g_Instances[instanceId];
    const uint         headBatch = instance._meshBatchIndex;
    if ( headBatch >= g_BatchCount )
        return;

    const BatchData headInfo = g_BatchInfo[headBatch];
    // 빈 칸(GPU 메시 입자의 죽은 칸 — vfx_mesh_instance.hlsl)은 세지 않는다. 그래서 간접 인자의 인스턴스 수가 살아 있는 수이고 CPU 는 그 수를 모른다(Niagara · VFX Graph 와 같다).
    if ( ( instance._instanceFlags & kInstanceFlagEmpty ) != 0u )
        return;
    // 투명 정렬 배치(sortMode 1 · 2)는 차례를 적는다 — instance_sort.hlsl 이 차례로 정렬한 뒤 칸으로 바꾼다. 나머지는 칸을 바로 적는다.
    const uint visibleId = ( headInfo._sortMode != 0u ) ? orderIndex : instanceId;

    // 바운드 중심은 이미 월드 공간이다 (GPUScene 이 월드 행렬의 이동 성분으로 채우고, instance_animation 도
    // 회전 뒤에 다시 맞춘다). 여기서 또 world 를 곱하면 이동이 두 번 들어간다.
    if ( isVisible( instance._boundsCenter, instance._boundsRadius ) == false )
        return;
    // 구 다음 상자(배치 메시의 경계) — 둘 다 보수적이라 둘 중 하나라도 밖이면 밖이다. 가림도 상자로 판정한다(가늘고 긴 물체는 구가 몇 배 넓다).
    // 스킨 인스턴스는 상자를 쓰지 않는다 — 상자는 바인드 포즈라 포즈가 정점을 축 사이로 옮기면(IK 로 앞으로 뻗은 T 포즈 팔) 덮지 못한다. 구는 포즈 여유를 넣은
    // 반지름이다(`MeshLODSettings::_skinnedBoundsScale`). 언리얼은 스켈레탈 경계를 포즈에서(피직스 에셋) 다시 구한다 — 포즈 상자가 생기면 그것으로 바꾼다.
    float3     boxCenter = instance._boundsCenter;
    float3     boxExtent = instance._boundsRadius.xxx;
    const bool bHasBox   = instance._skinSlot == kInvalidIndex && computeWorldBox( instance, headInfo, boxCenter, boxExtent );
    if ( bHasBox && isBoxVisible( boxCenter, boxExtent ) == false )
        return;

    // HZB 깊이 뒤에 숨은 인스턴스는 그리지 않는다. 앞 단계는 빠진 불투명 차례를 뒤 단계에 넘긴다.
    if ( bHasBox ? isOccluded( boxCenter, boxExtent ) : isOccluded( instance._boundsCenter, instance._boundsRadius.xxx ) )
    {
        if ( phase == kCullPhaseEarly && headInfo._sortMode == 0u )
            g_OccludedOrders[orderIndex] = 1u;
        return;
    }

    // LOD 그룹이 아니면 머리 배치에 한 번 쓴다.
    if ( g_LODControl.x == 0u || headInfo._lodLevelCount <= 1u )
    {
        appendVisible( headBatch, visibleId );
        return;
    }

    // LOD 그룹: 화면 크기로 단계를 골라 그 단계 배치(머리 + 단계)의 가시 구간에 쓴다. 단계 배치는 머리와 같은 머티리얼 · 메시이고 정점 구간만 다르며, 인스턴스는 머리에 한 번만 있다.
    // 그룹은 정적 불투명뿐이라 투명 정렬과 만나지 않는다. 전환 중이면 두 단계에 모두 쓴다(항목의 위쪽 비트가 상보 디더 코드).
    uint level     = 0u;
    uint previous  = 0u;
    uint fadeLevel = 16u;
    advanceLODFade( instanceId, instance._slotSerial, headBatch, headInfo._lodLevelCount, computeLODScreenSize( instance._boundsCenter, instance._boundsRadius ), instance._appearance, level, previous,
                    fadeLevel );
    // LOD 단계 번호 — 그룹의 단계 수 안이어야 하고(배치 표의 머리 + 단계 행), 가시 항목의 3 비트 칸 · 크로스페이드 4 비트 칸에 들어가야 한다.
    SW_GPU_ASSERT( level < headInfo._lodLevelCount, SW_GPU_ASSERT_ID_CULL_LOD_LEVEL, level );
    SW_GPU_ASSERT( previous < headInfo._lodLevelCount, SW_GPU_ASSERT_ID_CULL_LOD_PREVIOUS, previous );
    SW_GPU_ASSERT( level <= kVisibleEntryLevelMask && previous <= kVisibleEntryLevelMask, SW_GPU_ASSERT_ID_CULL_LOD_LEVEL_BITS, max( level, previous ) );
    SW_GPU_ASSERT( fadeLevel <= 16u, SW_GPU_ASSERT_ID_CULL_FADE_LEVEL, fadeLevel );
    if ( fadeLevel >= 16u )
    {
        appendVisible( headBatch + level, instanceId | ( level << kVisibleEntryLevelShift ) );
    }
    else if ( fadeLevel == 0u )
    {
        appendVisible( headBatch + previous, instanceId | ( previous << kVisibleEntryLevelShift ) ); // 전환이 막 시작했다 — 직전 단계가 아직 다 보인다
    }
    else
    {
        appendVisible( headBatch + level, instanceId | ( level << kVisibleEntryLevelShift ) | ( fadeLevel << kVisibleEntryFadeShift ) );
        appendVisible( headBatch + previous, instanceId | ( previous << kVisibleEntryLevelShift ) | ( ( kLODFadeOutgoing | fadeLevel ) << kVisibleEntryFadeShift ) );
    }
}
#endif // SW_CULL_RESET_ARGS
