/**
 * @file RHIStagingRingUtil.h
 * @brief 스테이징 링 할당의 뒷마무리(결과 분류, 못 받은 구간 놓기, 이유 로그)를 DX12 · Vulkan · GL 이 같이 씁니다(docs/plans/RHIAsyncUploads.md 2.2 "스테이징 링").
 * @details 세 백엔드의 `allocateStagingSpan` 은 장부(`RHIStagingAllocator`)에 구간을 달라고 하고, 차 있으면 끝난 전송을 걷어 한 번 더 부르는 데까지는 자기 몫(걷는 방법이 백엔드마다 다르다)이고,
 *          그 결과를 제출 결과로 옮기고 잡은 구간을 놓고 이유를 남기는 일은 어느 백엔드에서나 같다. 그 뒤쪽을 여기 한 곳에 둬서 로그 문구와 놓는 규칙이 백엔드마다 어긋나지 않게 한다.
 *          단독 구간의 일회성 버퍼를 만드는 일(`vkCreateBuffer`, D3D12 업로드 힙, GL 영속 매핑 PBO)은 백엔드 고유라 여기 없다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/Support/RHIStagingAllocator.h"
#include "Engine/Graphics/RHI/Support/RHITransferSerials.h"

namespace sw
{
    /**
     * @class RHIStagingRingUtil
     * @brief 스테이징 링 할당 결과를 제출 결과로 마무리하는 정적 헬퍼 모음입니다.
     */
    class SW_API RHIStagingRingUtil
    {
    public:
        /**
         * @brief 장부의 할당 결과 @p result 를 제출 결과로 옮기고, 받지 못했으면 잡은 구간을 놓고 이유를 로그로 남깁니다.
         * @details 옮기는 규칙은 `RHITransferSerials::classifyStagingAllocation` 하나다(잃은 장치면 무엇이든 `Rejected`).
         *          `Accepted` 면 @p span 이 유효하다 — 단독 구간(`_bSolo`)이면 부르는 쪽이 그 크기의 일회성 버퍼를 만들어 붙이고, 못 만들면 `rejectSoloBufferFailure` 로 마무리한다.
         *          `Busy` 면 구간도 로그도 없다(앞선 전송이 끝나면 비므로 호출자가 다시 제출한다). `Rejected` 면 `Allocated` 였던 구간은 이미 놓았고 이유는 로그에 있다.
         * @param pBackend 로그에 넣는 백엔드 이름입니다("D3D12", "Vulkan", "GL").
         * @param pCaller 로그에 넣는 제출 함수 이름입니다.
         */
        [[nodiscard]] static RHISubmitResult settleAllocation( RHIStagingAllocator& ledger, const RHITransferSerials& serials, RHIStagingAllocateResult result, uint64 sizeBytes,
                                                               const utf8* pBackend, const utf8* pCaller, const RHIStagingSpan& span );

        /**
         * @brief 단독 구간의 일회성 버퍼를 만들지 못했을 때의 마무리입니다. 잡은 구간을 놓고 이유를 로그로 남기고 `Rejected` 를 돌려줍니다.
         * @details 다시 시도해도 같은 자리에서 실패할 수 있어 `Busy` 가 아니라 `Rejected` 다.
         */
        [[nodiscard]] static RHISubmitResult rejectSoloBufferFailure( RHIStagingAllocator& ledger, const RHIStagingSpan& span, const utf8* pBackend, const utf8* pCaller );
    };
} // namespace sw
