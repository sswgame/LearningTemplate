#include "common.hlsli"
#include "canvas_quad.hlsli"
#include "instance_data.hlsli"
#include "gpu_assert.hlsli"

/**
 * canvas_marker.hlsl — 화면 마커(이름표 · HP 바 · 목표 표시)의 투영 컴퓨트(RenderPassType::CanvasMarker, docs/plans UI 후보 MarkerComputeProjection 의 구현).
 * 언리얼 위젯 컴포넌트의 일괄 월드 UI, 유니티 UI Toolkit 월드 스페이스와 같은 선이다 — 월드 점을 화면에 놓는 일은 GPU 가 하고 위젯은 앵커 원점 기준으로 한 번만 배치 · 칠한다.
 *
 * 스레드 하나가 마커 하나다. 앵커 번호(= 마커 기록 번호 = 구조 버퍼의 앞 원소 번호)가 마커와 GPU 인스턴스의 대응이다. 월드 점 · 앵커 → 화면 좌표 · 가시 판정을 C++
 * `WidgetComponent::computeMarkerPlacement` 와 **같은 식**으로 구해(식을 고치면 두 곳을 함께 고친다) 캔버스 사각형 버퍼의 앵커 원소 `_rect` 에 (x, y, 배율, 0) 을 쓴다.
 * 보이지 않는 마커(카메라 뒤 · 최대 거리 밖 · 화면 밖이고 가장자리에 붙이지 않음)는 배율 0 을 쓴다 — 사각형이 한 점으로 접혀 그려지지 않는다(위젯 가시성은 GPU 결과를 모르는 게임 스레드가 건드리지 않는다).
 * 켜지지 않은 기록(CPU 가 투영하는 마커 · 숨긴 마커)은 건드리지 않는다.
 *
 * 바인딩 계약: 읽기 t0 입력 행(g_MarkerRows, float4), 쓰기 u0 캔버스 사각형 버퍼(CanvasQuad — canvas.hlsl 이 읽는 같은 버퍼).
 * 입력 행 배치는 아래 kRow*, C++: CanvasMarkerProjection::record — dispatchCompute( ceil(마커 수 / 64), 1, 1 ).
 */

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_MarkerDebugMode; // 1 = 보기 모드 MarkerSlots — 앵커의 네 번째 값에 인스턴스 칸 유무 코드를 쓴다(canvas.hlsl 이 위젯을 칠한다). 0 = 끔
};
SW_ROOT_CONSTANTS_END

SW_DECLARE_STRUCTURED_BUFFER( float4, g_MarkerRows, 0 );
SW_DECLARE_STRUCTURED_BUFFER( InstanceData, g_MarkerInstances, 1 ); // GPUScene 인스턴스 버퍼 — 인스턴스 칸이 있는 마커만 읽는다(C++ 은 칸이 없으면 자리표를 건다)
SW_DECLARE_RW_STRUCTURED_BUFFER( CanvasQuad, g_MarkerQuadsRW, 0 );

// 입력 행 배치(C++ CanvasMarkerProjection::prepare 가 같은 순서로 올린다).
static const uint kRowViewProjection = 0u; // 행 0..3 — 행 벡터 규약 뷰 · 투영 행렬의 행(clip = p.x * r0 + p.y * r1 + p.z * r2 + p.w * r3)
static const uint kRowCamera         = 4u; // 카메라 xyz, asuint( 마커 수 )
static const uint kRowViewport       = 5u; // UI 뷰포트 xy, UI 배율, 가장자리 여백(UI 단위)
static const uint kRowLimits         = 6u; // 카메라 뒤 판정 w, 거리 배율 최소, 최대, 0
static const uint kRowFirstMarker    = 8u; // 마커마다 세 행 — A: 월드 점 xyz, 최대 거리 · B: 화면 오프셋 xy, 기준 거리, asuint( 비트 ) · C: 월드 오프셋 xyz, asuint( 인스턴스 칸, 없음 = kInvalidIndex )
static const uint kMarkerRowCount    = 3u;
static const uint kNoInstance        = 0xFFFFFFFFu;

// 마커 비트(C++ CanvasMarkerFlag + 켜짐 비트).
static const uint kMarkerFlagClamp  = 1u;
static const uint kMarkerFlagActive = 0x80000000u;

static const float kBigReach = 3.402823e38f;

/// assert 의 범위 검사가 쓰는 버퍼 원소 수입니다(assert 변형에서만 평가된다).
uint getMarkerRowCount()
{
    uint count  = 0u;
    uint stride = 0u;
    g_MarkerRows.GetDimensions( count, stride );
    return count;
}

uint getMarkerInstanceCount()
{
    uint count  = 0u;
    uint stride = 0u;
    g_MarkerInstances.GetDimensions( count, stride );
    return count;
}

SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchID : SV_DispatchThreadID )
{
    SW_GPU_ASSERT_THREAD( dispatchID.x );
    const float4 cameraRow   = g_MarkerRows[kRowCamera];
    const uint   markerCount = asuint( cameraRow.w );
    const uint   index       = dispatchID.x;
    if ( index >= markerCount )
        return;
    const uint rowAt = kRowFirstMarker + index * kMarkerRowCount;
    SW_GPU_ASSERT( rowAt + kMarkerRowCount <= getMarkerRowCount(), SW_GPU_ASSERT_ID_MARKER_ROW_RANGE, rowAt );
    const float4 rowA = g_MarkerRows[rowAt];
    const float4 rowB = g_MarkerRows[rowAt + 1u];
    const uint   bits = asuint( rowB.w );
    const float4 rowC = g_MarkerRows[rowAt + 2u];
    if ( ( bits & kMarkerFlagActive ) == 0u )
        return;

    const float4 viewport = g_MarkerRows[kRowViewport];
    const float4 limits   = g_MarkerRows[kRowLimits];
    const float2 viewSize = viewport.xy;
    const float  uiScale  = viewport.z;
    const float  margin   = viewport.w;

    // 인스턴스 칸이 있으면 GPUScene 인스턴스 변환의 위치(행 벡터 행렬의 4 번째 행) + 월드 오프셋이다 — GPU 가 옮기는 인스턴스도 따라간다. 없으면 게임 스레드가 보낸 월드 점이다.
    const uint instanceSlot = asuint( rowC.w );
    SW_GPU_ASSERT( instanceSlot == kNoInstance || instanceSlot < getMarkerInstanceCount(), SW_GPU_ASSERT_ID_MARKER_INSTANCE_RANGE, instanceSlot );
    const float3 world = instanceSlot != kNoInstance ? g_MarkerInstances[instanceSlot]._world[3].xyz + rowC.xyz : rowA.xyz;
    SW_GPU_ASSERT( GPUAssert::isFinite( world ), SW_GPU_ASSERT_ID_MARKER_WORLD_NAN, world.x );
    const float dist    = length( cameraRow.xyz - world );
    float4      rect    = float4( 0.0f, 0.0f, 0.0f, 0.0f ); // 배율 0 = 접힘(그려지지 않는다)
    const bool  bTooFar = rowA.w > 0.0f && dist > rowA.w;
    if ( bTooFar == false )
    {
        float scale = 1.0f;
        if ( rowB.z > 0.0f && dist > 0.0f )
            scale = clamp( rowB.z / dist, limits.y, limits.z );

        const float4 world4 = float4( world, 1.0f );
        const float4 clip   = world4.x * g_MarkerRows[kRowViewProjection] + world4.y * g_MarkerRows[kRowViewProjection + 1u] + world4.z * g_MarkerRows[kRowViewProjection + 2u] +
                            world4.w * g_MarkerRows[kRowViewProjection + 3u];
        SW_GPU_ASSERT( GPUAssert::isFinite( clip ), SW_GPU_ASSERT_ID_MARKER_CLIP_NAN, clip.w );
        const bool   bBehind = clip.w < limits.x;
        const float2 center  = viewSize * 0.5f;
        float2       screen  = float2( 0.0f, 0.0f );
        if ( bBehind == false )
        {
            const float invW = 1.0f / clip.w;
            screen           = float2( ( clip.x * invW * 0.5f + 0.5f ) * viewSize.x, ( 0.5f - clip.y * invW * 0.5f ) * viewSize.y ); // NDC(y 위가 +) → UI 단위(y 아래가 +)
        }
        screen += rowB.xy;
        const bool bOnScreen = bBehind == false && 0.0f <= screen.x && screen.x <= viewSize.x && 0.0f <= screen.y && screen.y <= viewSize.y;
        float2     position  = screen;
        bool       bVisible  = bOnScreen;
        if ( bOnScreen == false && ( bits & kMarkerFlagClamp ) != 0u )
        {
            // 가장자리에 붙인다 — 화면 가운데에서 그 점 쪽으로 가다가 안쪽 사각형 변에 닿는 위치다. 카메라 뒤면 나누기 전 클립 좌표의 방향을 쓴다.
            float2 direction = bBehind ? float2( clip.x, -clip.y ) : float2( screen.x - center.x, screen.y - center.y );
            if ( direction.x == 0.0f && direction.y == 0.0f )
                direction = float2( 0.0f, 1.0f );
            const float halfWidth  = max( 0.0f, center.x - margin );
            const float halfHeight = max( 0.0f, center.y - margin );
            const float scaleX     = direction.x != 0.0f ? halfWidth / abs( direction.x ) : kBigReach;
            const float scaleY     = direction.y != 0.0f ? halfHeight / abs( direction.y ) : kBigReach;
            const float reach      = min( scaleX, scaleY );
            position               = float2( center.x + direction.x * reach, center.y + direction.y * reach );
            bVisible               = true;
        }
        if ( bVisible )
            rect = float4( round( position.x * uiScale ), round( position.y * uiScale ), scale, 0.0f ); // 물리 픽셀 정수(글자가 흐려지지 않게 — UIScreenMarkers::publishCanvasAnchors 와 같다)
    }
    // 보기 모드 MarkerSlots — 1 인스턴스 칸이 있고 인스턴스 버퍼 안이다 · 2 칸이 없다(월드 점을 게임 스레드가 보낸다) · 3 칸이 인스턴스 버퍼 밖이다. 0 이면 끔이다.
    if ( SW_ROOT( g_MarkerDebugMode ) != 0u )
        rect.w = instanceSlot == kNoInstance ? 2.0f : ( instanceSlot < getMarkerInstanceCount() ? 1.0f : 3.0f );
    g_MarkerQuadsRW[index]._rect = rect;
}
