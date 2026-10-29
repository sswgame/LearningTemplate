#include "pch.h"

#include "Engine/Renderer/Canvas/CanvasMarkerProjection.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/BitUtil.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Debug/GPUAssert.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Canvas/CanvasQuadBuffer.h"

namespace sw
{
    SW_LOG_CALLER( "Canvas" );

    namespace
    {
        struct CanvasMarkerProjectionInternal
        {
            /** @brief 마커 투영 컴퓨트의 입력 행 배치입니다 — canvasmarker.hlsl 의 kRow* · kMarkerFlagActive · numthreads 와 같아야 합니다. */
            static constexpr uint32 kRowViewProjection     = 0;
            static constexpr uint32 kRowCamera             = 4;
            static constexpr uint32 kRowViewport           = 5;
            static constexpr uint32 kRowLimits             = 6;
            static constexpr uint32 kRowFirstMarker        = 8;
            static constexpr uint32 kMarkerRowCount        = 3;
            static constexpr uint32 kMarkerFlagActive      = 0x80000000u;
            static constexpr uint32 kMarkerThreadGroupSize = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    CanvasMarkerProjection::CanvasMarkerProjection()
        : _listMarkerRowScratch{}
        , _markerRowBuffer{}
        , _markerCount{ 0 }
        , _bMarkerReadsInstances{ false }
    {
    }

    CanvasMarkerProjection::~CanvasMarkerProjection() = default;

    void CanvasMarkerProjection::prepare( IRHIDevice& device, const CanvasFrameData& frame )
    {
        using Internal         = CanvasMarkerProjectionInternal;
        _markerCount           = 0;
        _bMarkerReadsInstances = false;
        if ( frame._listMarker.empty() )
            return;
        // 입력 행 배치는 canvasmarker.hlsl 의 kRow* 와 1:1 이다(뷰 · 투영 4행 · 카메라 · 뷰포트 · 한계 · 여백 1행 뒤에 마커마다 두 행).
        const CanvasMarkerView& view        = frame._markerView;
        const uint32            markerCount = static_cast<uint32>( frame._listMarker.size() );
        const uint32            rowCount    = Internal::kRowFirstMarker + markerCount * Internal::kMarkerRowCount;
        _listMarkerRowScratch.assign( rowCount, float4{} );
        const float32* pMatrix = view._viewProjection.data(); // 행 우선 16 개 — 행 벡터 규약 그대로 올린다(셰이더가 행 곱으로 푼다)
        for ( uint32 row = 0; row < 4; ++row )
        {
            _listMarkerRowScratch[Internal::kRowViewProjection + row] = float4{ pMatrix[row * 4], pMatrix[row * 4 + 1], pMatrix[row * 4 + 2], pMatrix[row * 4 + 3] };
        }
        _listMarkerRowScratch[Internal::kRowCamera]   = float4{ view._cameraPosition._x, view._cameraPosition._y, view._cameraPosition._z, BitUtil::bitsToFloat( markerCount ) };
        _listMarkerRowScratch[Internal::kRowViewport] = float4{ view._viewportSize._x, view._viewportSize._y, view._uiScale, view._edgeMargin };
        _listMarkerRowScratch[Internal::kRowLimits]   = float4{ view._minClipW, view._minDistanceScale, view._maxDistanceScale, 0.0f };
        bool bAnyInstanceSlot                         = false;
        for ( uint32 index = 0; index < markerCount; ++index )
        {
            const CanvasMarkerRecord& record = frame._listMarker[index];
            const uint32              rowAt  = Internal::kRowFirstMarker + index * Internal::kMarkerRowCount;
            const uint32              bits   = record._flags | ( record._bActive != 0 ? Internal::kMarkerFlagActive : 0u );
            _listMarkerRowScratch[rowAt]     = float4{ record._worldPosition._x, record._worldPosition._y, record._worldPosition._z, record._maxDistance };
            _listMarkerRowScratch[rowAt + 1] = float4{ record._screenOffset._x, record._screenOffset._y, record._referenceDistance, BitUtil::bitsToFloat( bits ) };
            _listMarkerRowScratch[rowAt + 2] = float4{ record._worldOffset._x, record._worldOffset._y, record._worldOffset._z, BitUtil::bitsToFloat( record._instanceSlot ) };
            bAnyInstanceSlot                 = bAnyInstanceSlot || record._instanceSlot != invalid_index::kUint32;
        }
        constexpr RHIBufferUsage kRowUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        if ( _markerRowBuffer.ensureCapacity( &device, sizeof( float4 ), rowCount, kRowUsage, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR( "Failed to create the canvas marker input buffer (%# rows) - GPU-projected markers are not placed", rowCount );
            return;
        }
        _markerRowBuffer.upload( &device, _listMarkerRowScratch.data(), rowCount * static_cast<uint32>( sizeof( float4 ) ) );
        _markerCount           = markerCount;
        _bMarkerReadsInstances = bAnyInstanceSlot;
    }

    void CanvasMarkerProjection::record( IRHICommandList& cmd, const CanvasQuadBuffer& quadBuffer, RHIPipelineStateHandle pso, RHIBufferHandle instanceBuffer, RHIDescriptorIndex instanceSRV,
                                         RHIDescriptorIndex placeholderSRV, GPUAssertMonitor& inoutAssert, bool bShowInstanceSlots ) const
    {
        if ( _markerCount == 0 )
            return;
        if ( pso == 0 )
        {
            // 셰이더가 쿠킹되지 않았거나 컴퓨트 능력이 없는 백엔드다. CPU 투영으로 대신하지 않는다 — 켜진 마커의 앵커 배율은 CPU 가 0 으로 올렸으므로 그려지지 않는다.
            SW_LOG_ERROR_ONCE( "The canvas marker projection compute PSO is missing (shader not cooked or no compute support) - GPU-projected markers are not drawn" );
            return;
        }
        const RHIStructuredBufferSlot& quads = quadBuffer.getSlot();
        if ( quads.isValid() == false || quads._uav == kInvalidDescriptorIndex || _markerRowBuffer.isValid() == false )
        {
            SW_LOG_ERROR_ONCE( "The canvas quad buffer has no UAV for the marker projection - GPU-projected markers are not drawn" );
            return;
        }
        // 인스턴스 칸을 읽는 마커가 있으면 GPUScene 인스턴스 버퍼(컬링 · 인스턴스 애니메이션이 이미 끝났다)를 읽는다. 읽을 버퍼가 없으면 칸을 해석할 수 없으므로 멈춘다.
        if ( _bMarkerReadsInstances && ( instanceBuffer == 0 || instanceSRV == kInvalidDescriptorIndex ) )
        {
            SW_LOG_ERROR_ONCE( "GPU-projected markers reference GPUScene instances but the instance buffer is not available - the markers are not placed" );
            return;
        }
        using Internal = CanvasMarkerProjectionInternal;
        SW_PROFILE_SCOPE( "RT.Canvas.MarkerProject" );
        // 앵커 원소는 사각형 버퍼의 맨 앞이다. 컴퓨트가 쓰고 나면 캔버스 그리기가 읽기로 본다(프로브 갱신과 같은 상태 전환).
        cmd.transitionBuffer( quads._buffer, RHIBufferState::UnorderedAccess );
        cmd.setComputePipelineState( pso );
        // g_MarkerRows(t0) / g_MarkerInstances(t1) / g_MarkerQuadsRW(u0). canvasmarker.hlsl 레지스터와 1:1 이다.
        cmd.bindComputeShaderResource( _markerRowBuffer._srv, 0 );
        if ( _bMarkerReadsInstances )
            cmd.transitionBuffer( instanceBuffer, RHIBufferState::ShaderResource );
        cmd.bindComputeShaderResource( _bMarkerReadsInstances ? instanceSRV : placeholderSRV, 1 );
        cmd.bindComputeUAV( quads._uav, 0 );
        // 루트 상수 하나(canvasmarker.hlsl g_MarkerDebugMode) — 1 이면 앵커의 네 번째 값에 인스턴스 칸 유무 코드를 쓴다(보기 모드 MarkerSlots).
        const uint32 debugMode = bShowInstanceSlots ? 1u : 0u;
        cmd.setComputeRootConstants( 0, 1, &debugMode );
        inoutAssert.bind( cmd ); // canvasmarker.hlsl 의 assert(u4) — assert 변형 PSO 일 때만 버퍼가 있다
        cmd.dispatchCompute( ( _markerCount + Internal::kMarkerThreadGroupSize - 1u ) / Internal::kMarkerThreadGroupSize, 1, 1 );
        cmd.transitionBuffer( quads._buffer, RHIBufferState::ShaderResource );
    }

    void CanvasMarkerProjection::release( IRHIDevice* pDevice )
    {
        _markerRowBuffer.release( pDevice );
        _markerCount = 0;
    }
} // namespace sw
