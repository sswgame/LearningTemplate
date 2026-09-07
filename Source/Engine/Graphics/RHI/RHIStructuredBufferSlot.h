/**
 * @file RHIStructuredBufferSlot.h
 * @brief 구조버퍼 하나와 그 뷰(SRV/UAV)·용량을 함께 들고 다니는 자리
 */
#pragma once
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct RHIStructuredBufferSlot
     * @brief 매 프레임 크기가 변할 수 있는 구조버퍼의 수명·뷰 등록을 한 자리에 모읍니다.
     * @details GPU 드리븐 경로는 같은 모양을 계속 반복한다 — "용량이 모자라면 옛 뷰를 등록 해제하고
     *          버퍼를 다시 만들고 새 뷰를 등록한다". GpuScene 만 해도 인스턴스·가시 목록·배치 구간·
     *          간접 인자·머티리얼 데이터로 다섯 번이었고, 그중 하나에서 UAV 등록을 빠뜨려도 컴파일은
     *          통과한다(실제로 그런 실수를 했다). 여기 모아 두면 그럴 자리가 없다.
     *
     *          **뷰를 먼저 등록 해제하고 버퍼를 지우는 순서**가 중요하다. 반대로 하면 bindless
     *          레지스트리에 죽은 핸들을 가리키는 항목이 남는다.
     *
     * @note 이 타입은 RHI ABI 가 아니라 그 위의 편의 계층이다 — 백엔드 모듈이 아니라 엔진이 쓴다.
     */
    struct RHIStructuredBufferSlot
    {
        RHIBufferHandle    _buffer{ 0 };
        RHIDescriptorIndex _srv = kInvalidDescriptorIndex;
        RHIDescriptorIndex _uav = kInvalidDescriptorIndex;
        /// @brief 지금 버퍼가 담을 수 있는 원소 수. 요청이 이보다 크면 다시 만든다.
        uint32 _capacityElements{ 0 };
        uint32 _elementSize{ 0 };

        /** @brief 버퍼가 만들어져 있는가. */
        bool isValid() const { return _buffer != 0; }

        /**
         * @brief 용량을 확보합니다. 모자라면 다시 만들고, 충분하면 그대로 둡니다.
         * @details 만들기는 세 단계로 물러선다. (1) 요청한 usage 그대로, (2) UnorderedAccess 를 뺀 것,
         *          (3) createStructuredBuffer. (2)가 있는 이유는 백엔드·드라이버가 구조버퍼 UAV 를 거절할
         *          수 있어서다 — 그때도 **그리기는 살려야 한다**(컴퓨트 경로만 꺼진다).
         * @param elementCount 0 이면 버퍼를 놓아준다.
         * @param pInitialData 만들 때 넣을 내용 (없으면 nullptr).
         * @return 요청한 용량을 확보했으면 true.
         */
        bool ensureCapacity( IRHIDevice* pDevice, uint32 elementSize, uint32 elementCount, RHIBufferUsage usage,
                             bool bNeedsSrv, bool bNeedsUav, const void* pInitialData );

        /** @brief 앞에서부터 byteSize 만큼 올립니다 (버퍼가 없으면 아무것도 하지 않습니다). */
        void upload( IRHIDevice* pDevice, const void* pData, uint32 byteSize ) const;

        /** @brief 뷰를 등록 해제하고 버퍼를 지웁니다. 여러 번 불러도 안전합니다. */
        void release( IRHIDevice* pDevice );
    };
} // namespace sw
