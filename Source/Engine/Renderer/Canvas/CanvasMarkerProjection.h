/**
 * @file CanvasMarkerProjection.h
 * @brief 화면 마커 투영 컴퓨트(`canvas_marker.hlsl`)의 입력 행 버퍼와 그 디스패치입니다(렌더 스레드).
 * @details `CanvasRenderer` 가 값으로 소유합니다. 컴퓨트는 사각형 버퍼(`CanvasQuadBuffer`) 맨 앞의 앵커 원소를 화면 좌표 · 배율로 덮어씁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct CanvasFrameData;

    class CanvasQuadBuffer;
    class GPUAssertMonitor;
    class IRHICommandList;
    class IRHIDevice;

    /**
     * @class CanvasMarkerProjection
     * @brief GPU 가 투영하는 화면 마커입니다. 기록 전(`prepare`)에 입력 행을 올리고, 컴퓨트 프리패스(`record`)에서 사각형 버퍼의 앵커 원소를 씁니다.
     */
    class SW_API CanvasMarkerProjection
    {
    public:
        CanvasMarkerProjection();
        ~CanvasMarkerProjection();

        CanvasMarkerProjection( const CanvasMarkerProjection& )            = delete;
        CanvasMarkerProjection& operator=( const CanvasMarkerProjection& ) = delete;

        /** @brief 마커 입력 행을 만들어 올립니다. 켜진 마커가 없거나 버퍼를 만들지 못하면 마커 수를 0 으로 둡니다(컴퓨트를 돌리지 않는다). */
        void prepare( IRHIDevice& device, const CanvasFrameData& frame );
        /**
         * @brief 화면 마커 투영 컴퓨트를 기록합니다. 마커 입력이 있는 프레임에만 돌고, 렌더 패스 밖(컴퓨트 프리패스)에서 부릅니다 —
         *        @p quadBuffer 에 올린 앵커 원소 중 켜진 마커의 것을 화면 좌표 · 배율로 덮어쓰고, 캔버스 그리기(셰이더 읽기)를 위해 읽기 상태로 돌려놓는다.
         * @details 인자는 `CanvasRenderer::recordMarkerProjection` 과 같습니다.
         */
        void record( IRHICommandList& cmd, const CanvasQuadBuffer& quadBuffer, RHIPipelineStateHandle pso, RHIBufferHandle instanceBuffer, RHIDescriptorIndex instanceSRV,
                     RHIDescriptorIndex placeholderSRV, GPUAssertMonitor& inoutAssert, bool bShowInstanceSlots ) const;
        /** @brief 입력 행 버퍼를 해제합니다(디바이스가 없으면 핸들만 잊습니다). */
        void release( IRHIDevice* pDevice );

        /** @brief 이번 프레임 컴퓨트가 돌릴 마커 기록 수입니다(0 이면 돌리지 않는다 — 사각형 버퍼에 UAV 가 필요한지도 이것으로 정한다). */
        uint32 getMarkerCount() const { return _markerCount; }

    private:
        vector<float4>          _listMarkerRowScratch;  ///< 마커 입력 행의 사본입니다(`canvas_marker.hlsl` 의 kRow* 배치)
        RHIStructuredBufferSlot _markerRowBuffer;       ///< 마커 입력 행(float4)입니다. 마커가 있는 프레임마다 다시 올린다.
        uint32                  _markerCount;           ///< 이번 프레임 컴퓨트가 돌릴 마커 기록 수(0 이면 돌리지 않는다)
        bool                    _bMarkerReadsInstances; ///< 이번 프레임 마커 중 GPUScene 인스턴스 칸을 읽는 것이 있다(인스턴스 버퍼를 읽기 상태로 둔다).
    };
} // namespace sw
