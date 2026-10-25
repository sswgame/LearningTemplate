/**
 * @file RHIStagingRing.h
 * @brief 고정 용량 스테이징 버퍼(업로드 링 · 되읽기 링)의 구간 장부입니다(구간 할당과 구간마다의 회수). GPU 메모리는 백엔드가 쥐고, 이 장부는 오프셋만 다룹니다(docs/plans/RhiAsyncUploads.md 2.2 "스테이징 링").
 * @details 백엔드마다 다른 것은 링 메모리(GL 영속 매핑 PBO, DX12 업로드 힙, Vulkan 호스트 가시 버퍼)뿐이고, 구간을 어디에 잡고 언제 돌려받는지는 여기 한 곳이 정한다.
 *          그래서 GPU 없이 단위 테스트로 장부의 규칙(가득 참, 구간마다의 회수, 빈 자리 재사용)을 확인할 수 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/SpanAllocator.h"

namespace sw
{
    /** @brief 링에서 잡은 구간 하나입니다. `RHIStagingRing::allocate` 가 채우고, 같은 값을 `markSubmitted` 나 `free` 에 한 번만 돌려줍니다. */
    struct RHIStagingSpan
    {
        uint64 _offsetBytes{ 0 }; ///< 링 시작에서의 오프셋. 링의 정렬 단위의 배수다
        uint64 _sizeBytes{ 0 };   ///< 요청한 크기. 0 이면 잡지 못한 구간이다
    };
} // namespace sw

namespace sw
{
    /** @brief `RHIStagingRing::allocate` 의 결과입니다. `RHITransferSerials::classifyStagingAllocation` 한 곳이 `RHISubmitResult` 로 옮깁니다(계약 2, 장치를 잃었으면 모두 `Rejected`). */
    enum class RHIStagingAllocateResult : uint8
    {
        Allocated, ///< 구간을 잡았다
        Full,      ///< 지금은 자리가 없지만 앞선 전송이 끝나면 생긴다 → `Busy`
        TooLarge,  ///< 링 용량보다 커서 기다려도 못 받는다(또는 0 바이트) → `Rejected`
    };
} // namespace sw

namespace sw
{
    /**
     * @class RHIStagingRing
     * @brief 고정 용량 버퍼에서 구간을 잡고, 구간마다 따로 돌려받습니다. 돌려받은 자리는 버퍼 어디에 있든 다음 할당이 다시 씁니다.
     * @details **회수는 구간 단위다.** 시리얼을 붙인 구간(`markSubmitted`)은 그 시리얼이 완료 워터마크 이하가 되면 돌아온다. 워터마크는 연속으로 끝난 시리얼의 끝이므로
     *          할당 순서와 시리얼 순서가 달라도(스레드 A 가 먼저 잡고 B 가 먼저 제출) 그 구간을 GPU 가 다 쓴 것이 보장된다. 시리얼 없이 놓은 구간(`free`)은 바로 돌아온다.
     *          아직 제출하지 않았거나 가져가지 않은 구간은 자기 바이트만 쥐고 다른 구간의 회수를 막지 않는다 — 가져가지 않은 되읽기 하나가 버퍼 전체를 막으면
     *          그 티켓을 쥔 스레드의 다음 읽기(`readbackTexture2DAndWait`)가 영원히 `Busy` 다. 그래도 쥔 구간들이 남은 자리를 다 차지하면 `Full`(`Busy`)이다.
     *          빈 자리는 정렬 단위를 한 칸으로 하는 `SpanAllocator`(가장 잘 맞는 빈 구간, 이웃 합치기, 시리얼을 번호로 쓰는 미뤄 돌려받기)가 쥔다.
     *          자기 뮤텍스는 잎(leaf)이라 백엔드의 어느 락 안에서나 부른다. 할당과 복사는 GPU 컨텍스트 밖에서 하고, 시리얼은 제출 뒤에 `markSubmitted` 로 붙인다.
     */
    class SW_API RHIStagingRing
    {
    public:
        RHIStagingRing();

        RHIStagingRing( const RHIStagingRing& )            = delete;
        RHIStagingRing& operator=( const RHIStagingRing& ) = delete;

        /**
         * @brief 용량과 정렬 단위를 정하고 장부를 비웁니다.
         * @param capacityBytes 링 전체 크기입니다. 정렬 단위의 배수로 내려 씁니다. 0 이면 모든 할당이 `TooLarge` 입니다.
         * @param alignmentBytes 구간 시작 오프셋의 정렬 단위이자 장부의 한 칸입니다. 2 의 거듭제곱이어야 하고, 칸 수(`capacityBytes / alignmentBytes`)는 uint32 에 담겨야 합니다.
         */
        void initialize( uint64 capacityBytes, uint64 alignmentBytes );
        /** @brief 장부를 비우고 용량을 0 으로 되돌립니다. 링 메모리를 놓기 전에 부릅니다. */
        void shutdown();

        /**
         * @brief @p sizeBytes 를 담을 구간을 잡습니다. 먼저 @p completedSerial 이하로 끝난 구간을 모두 돌려받습니다.
         * @param completedSerial 전송 완료 워터마크입니다(`RHITransferSerials::getWatermark`).
         * @return `Allocated` 면 @p outSpan 이 유효합니다. `Full` 과 `TooLarge` 면 @p outSpan 은 빈 구간입니다.
         */
        [[nodiscard]] RHIStagingAllocateResult allocate( uint64 sizeBytes, uint64 completedSerial, RHIStagingSpan& outSpan );

        /**
         * @brief 구간을 실은 전송의 시리얼을 붙입니다. 워터마크가 이 시리얼을 지나면 구간이 돌아옵니다.
         * @details 되읽기 링은 제출 때 붙이지 않는다(GPU 가 끝나도 CPU 가 아직 읽지 않았다). 결과를 버릴 때(`discardTextureReadback`)만 붙여 GPU 가 그 구간에 다 쓴 뒤 돌려받는다.
         */
        void markSubmitted( const RHIStagingSpan& span, uint64 serial );

        /** @brief 더 쓰지 않는 구간을 곧바로 돌려받습니다: 제출하지 못한 업로드 구간(컨텍스트를 얻지 못해 `Busy`), CPU 가 다 읽은 되읽기 구간(`takeTextureReadback`). */
        void free( const RHIStagingSpan& span );

        /** @brief @p completedSerial 이하로 끝난 구간을 모두 돌려받습니다. 기다리지 않습니다. */
        void freeCompleted( uint64 completedSerial );

        /** @brief 쓸 수 있는 용량입니다(정렬 단위의 배수로 내린 값). 이보다 큰 할당은 `TooLarge` 입니다. */
        [[nodiscard]] uint64 getCapacityBytes() const;
        /** @brief 아직 돌려받지 않은 구간 수입니다(시리얼을 붙였지만 워터마크가 아직 지나지 않은 구간 포함). */
        [[nodiscard]] uint32 getLiveCount() const;

    private:
        /** @brief 바이트 크기를 장부의 칸 수로 올림합니다. */
        [[nodiscard]] uint32 toSlotCount( uint64 sizeBytes ) const;
        /** @brief `_mutex` 를 쥔 채 부릅니다. */
        void freeCompletedLocked( uint64 completedSerial );

        mutable mutex _mutex;
        SpanAllocator _slots; ///< 한 칸 = `_alignmentBytes`. 시리얼을 붙인 구간은 시리얼을 번호로 미뤄 돌려받는다
        uint64        _capacityBytes;
        uint64        _alignmentBytes;
        uint32        _liveCount;
    };
} // namespace sw
