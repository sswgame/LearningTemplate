/**
 * @file RHIStructuredBuffer.h
 * @brief 구조 버퍼 하나와 그 뷰(SRV/UAV), 용량을 함께 보관합니다(언리얼 `FRWBufferStructured` 자리).
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @struct RHIStructuredBuffer
     * @brief 매 프레임 크기가 변할 수 있는 구조 버퍼의 수명과 뷰 등록을 한 곳에 모읍니다.
     * @details GPU 드리븐 경로는 같은 패턴을 계속 반복합니다: "용량이 모자라면 이전 뷰를 등록 해제하고
     *          버퍼를 다시 만들고 새 뷰를 등록한다". GPUScene 만 해도 인스턴스, 가시 목록, 배치 구간,
     *          간접 인자, 머티리얼 데이터로 다섯 번이고, 그중 하나에서 UAV 등록을 빠뜨려도 컴파일은
     *          통과합니다. 여기 모아 두면 그럴 여지가 없습니다.
     *
     *          **뷰를 먼저 등록 해제하고 버퍼를 지우는 순서**가 중요합니다. 반대로 하면 bindless
     *          레지스트리에 죽은 핸들을 가리키는 항목이 남습니다.
     *
     * @note 이 타입은 RHI ABI 가 아니라 그 위의 편의 계층입니다. 백엔드 모듈이 아니라 엔진이 씁니다.
     */
    struct SW_API RHIStructuredBuffer
    {
        RHIBufferHandle    _buffer{ 0 };
        RHIDescriptorIndex _srv = kInvalidDescriptorIndex;
        RHIDescriptorIndex _uav = kInvalidDescriptorIndex;
        /// @brief 지금 버퍼가 담을 수 있는 원소 수입니다. 요청이 이보다 크면 다시 만듭니다.
        uint32 _capacityElements{ 0 };
        uint32 _elementSize{ 0 };

        /** @brief 버퍼가 만들어져 있는지 확인합니다. */
        bool isValid() const { return _buffer != 0; }

        /**
         * @brief 용량을 확보합니다. 모자라면 다시 만들고, 충분하면 그대로 둡니다.
         * @details 요청한 usage 그대로 한 번만 만듭니다. 못 만들면 크기와 usage 를 오류로 남기고 false 이며, UAV 를 뺀 버퍼나 다른 생성 함수로
         *          대신 만들지 않습니다 — 컴퓨트(UAV)를 쓸 수 없는 백엔드는 부르는 쪽이 `RHICapabilities::_bCompute` 로 가려 UAV 를 요청하지 않습니다.
         * @param elementCount 0 이면 버퍼를 해제합니다.
         * @param pInitialData 만들 때 넣을 내용입니다(없으면 nullptr).
         * @return 요청한 용량을 확보했으면 true 입니다.
         */
        [[nodiscard]] bool ensureCapacity( IRHIDevice* pDevice, uint32 elementSize, uint32 elementCount, RHIBufferUsage usage,
                                           bool bNeedsSRV, bool bNeedsUAV, const void* pInitialData );

        /** @brief `byteOffset` 부터 byteSize 만큼 업로드합니다(버퍼가 없으면 아무것도 하지 않습니다). */
        void upload( IRHIDevice* pDevice, const void* pData, uint32 byteSize, uint32 byteOffset = 0 ) const;
        /**
         * @brief 여러 조각을 한 번의 호출로 업로드합니다(`IRHIResourceFactory::updateStructuredBufferRegions`). 조각의 `_srcOffset` 은 @p pBaseSource 기준입니다.
         * @return 올린 바이트 수입니다(버퍼 · 조각이 없으면 0).
         */
        uint32 uploadRegions( IRHIDevice* pDevice, const void* pBaseSource, const vector<RHIBufferCopyRegion>& listRegion ) const;

        /**
         * @brief 지난번에 올린 원소 배열과 이번 배열을 원소 단위로 견주어 바뀐 구간만 조각으로 냅니다(인스턴스 버퍼의 바뀐 구간 업로드와 같은 일을 라이트 · 프로브도 한다).
         * @details 두 배열은 같은 원소 크기 @p stride 이고 같은 자리가 같은 버퍼 칸이다. 이번이 더 길면 늘어난 꼬리는 모두 바뀐 것이다. 사이가 @p mergeGapElements 원소 이하인
         *          두 구간은 하나로 붙인다(조각마다 백엔드가 복사 명령을 하나 더 낸다). 원본과 목적 오프셋은 같다(이번 배열이 원본 블롭이다).
         * @param outListRegion 앞 내용은 지운다. 바뀐 것이 없으면 빈다.
         */
        static void collectChangedRegions( const void* pPrevious, uint32 previousCount, const void* pCurrent, uint32 currentCount, uint32 stride, uint32 mergeGapElements,
                                           vector<RHIBufferCopyRegion>& outListRegion );

        /** @brief 뷰를 등록 해제하고 버퍼를 지웁니다. 여러 번 불러도 안전합니다. */
        void release( IRHIDevice* pDevice );
    };
} // namespace sw
