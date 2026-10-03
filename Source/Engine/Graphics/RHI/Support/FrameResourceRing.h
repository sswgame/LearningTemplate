/**
 * @file FrameResourceRing.h
 * @brief N-버퍼 프레임 슬롯과 슬롯별 펜스 값입니다(DX12 가 프레임 리소스 고르기에 씁니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class FrameResourceRing
     * @brief 프레임별 펜스 값을 들고 다음 슬롯으로 넘어갈지 정하는 링입니다(N = constant::kMaxFrameCountInFlight).
     * @details GPU 자원은 소유하지 않습니다. 부르는 쪽이 currentIndex() 로 자기 프레임 자원을 고릅니다.
     */
    class SW_API FrameResourceRing
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — reset
        // ------------------------------------------------------------------------------
        /** @brief 펜스 값이 모두 0 인 링으로 만듭니다. 첫 beginFrame 이 슬롯 0 으로 진행합니다. */
        FrameResourceRing();

        /** @brief 슬롯 펜스 값을 0 으로 지우고 링을 처음 상태로 되돌립니다. */
        void reset();

        // ------------------------------------------------------------------------------
        // 2) 프레임 진행 — 펜스가 슬롯을 덮으면 다음 슬롯
        // ------------------------------------------------------------------------------
        /**
         * @brief @p completedFenceValue 가 다음 슬롯의 펜스를 덮으면 그 슬롯으로 진행합니다.
         * @return 진행했으면 true. GPU 가 아직 그 슬롯을 쓰는 중이면 false.
         */
        bool beginFrame( uint64 completedFenceValue );

        // ------------------------------------------------------------------------------
        // 3) 슬롯 · 펜스
        // ------------------------------------------------------------------------------
        /** @brief 현재 링 슬롯 인덱스를 반환합니다. */
        uint32 currentIndex() const { return _frameIndex; }
        /** @brief 슬롯의 펜스 값을 반환합니다. */
        uint64 getFenceValue( uint32 index ) const;
        /** @brief 슬롯의 펜스 값을 설정합니다. */
        void setFenceValue( uint32 index, uint64 fenceValue );

    private:
        uint64 _arrFenceValue[constant::kMaxFrameCountInFlight];
        uint32 _frameIndex;
    };
} // namespace sw
