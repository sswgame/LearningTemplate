/**
 * @file RHIStagingRing.h
 * @brief 고정 용량 업로드 스테이징 링의 장부입니다(구간 할당과 전송 시리얼 기준 회수). GPU 메모리는 백엔드가 쥐고, 이 장부는 오프셋만 다룹니다(docs/plans/RhiAsyncUploads.md 2.2 "스테이징 링").
 * @details 백엔드마다 다른 것은 링 메모리(GL 영속 매핑 PBO, DX12 업로드 힙, Vulkan 호스트 가시 버퍼)뿐이고, 구간을 어디에 잡고 언제 돌려받는지는 여기 한 곳이 정한다.
 *          그래서 GPU 없이 단위 테스트로 링의 규칙(감기, 가득 참, 회수 순서)을 확인할 수 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"

namespace sw
{
    /** @brief 링에서 잡은 구간 하나입니다. `RHIStagingRing::allocate` 가 채우고, 같은 값을 `markSubmitted` 나 `free` 에 돌려줍니다. */
    struct RHIStagingSpan
    {
        uint64 _offsetBytes{ 0 }; ///< 링 시작에서의 오프셋. 링의 정렬 단위의 배수다
        uint64 _sizeBytes{ 0 };   ///< 요청한 크기
        uint64 _id{ 0 };          ///< 장부 안에서 이 구간을 찾는 번호. 0 이면 잡지 못한 구간이다
    };
} // namespace sw

namespace sw
{
    /** @brief `RHIStagingRing::allocate` 의 결과입니다. 제출 함수가 `RHISubmitResult` 로 옮깁니다(계약 2). */
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
     * @brief 고정 용량 링에서 업로드 구간을 할당 순서대로 잡고, 전송 시리얼이 완료 워터마크 아래로 들어간 구간을 앞에서부터 돌려받습니다.
     * @details **회수는 할당 순서(링 순서)로만 한다.** 시리얼은 컨텍스트 · 큐 락 안에서 매기므로 할당 순서와 시리얼 순서가 다를 수 있다(스레드 A 가 먼저 잡고
     *          B 가 먼저 제출). 그래서 맨 앞 구간의 시리얼이 워터마크 아래일 때만 앞을 민다 — 뒤 구간이 먼저 끝나도 앞이 끝날 때까지 기다리고, 아직 제출하지 않은
     *          구간(시리얼 0)이 앞에 있으면 거기서 멈춘다. 늦게 회수할 뿐 쓰고 있는 구간을 덮지 않는다.
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
         * @param capacityBytes 링 전체 크기입니다. 0 이면 모든 할당이 `TooLarge` 입니다.
         * @param alignmentBytes 구간 시작 오프셋의 정렬 단위입니다. 2 의 거듭제곱이어야 합니다.
         */
        void initialize( uint64 capacityBytes, uint64 alignmentBytes );
        /** @brief 장부를 비우고 용량을 0 으로 되돌립니다. 링 메모리를 놓기 전에 부릅니다. */
        void shutdown();

        /**
         * @brief @p sizeBytes 를 담을 구간을 잡습니다. 먼저 @p completedSerial 이하로 끝난 구간을 앞에서부터 돌려받습니다.
         * @param completedSerial 전송 완료 워터마크입니다(`RHITransferSerials::getWatermark`).
         * @return `Allocated` 면 @p outSpan 이 유효합니다. `Full` 과 `TooLarge` 면 @p outSpan 은 빈 구간입니다.
         */
        [[nodiscard]] RHIStagingAllocateResult allocate( uint64 sizeBytes, uint64 completedSerial, RHIStagingSpan& outSpan );

        /**
         * @brief 구간을 실은 전송의 시리얼을 붙입니다. 워터마크가 이 시리얼을 지나면 구간이 돌아옵니다.
         * @details 되읽기 링은 제출 때 붙이지 않는다(GPU 가 끝나도 CPU 가 아직 읽지 않았다). 결과를 버릴 때(`discardTextureReadback`)만 붙여 GPU 가 그 구간에 다 쓴 뒤 돌려받는다.
         */
        void markSubmitted( const RHIStagingSpan& span, uint64 serial );

        /** @brief 더 쓰지 않는 구간을 놓습니다: 제출하지 못한 업로드 구간(컨텍스트를 얻지 못해 `Busy`), CPU 가 다 읽은 되읽기 구간(`takeTextureReadback`). 맨 앞에 오면 돌려받습니다. */
        void free( const RHIStagingSpan& span );

        /** @brief @p completedSerial 이하로 끝난 구간을 앞에서부터 돌려받습니다. 기다리지 않습니다. */
        void freeCompleted( uint64 completedSerial );

        [[nodiscard]] uint64 getCapacityBytes() const;
        /** @brief 아직 돌려받지 않은 구간 수입니다(놓았지만 앞에 막혀 있는 구간 포함). */
        [[nodiscard]] uint32 getLiveCount() const;

    private:
        struct Entry
        {
            uint64 _id;
            uint64 _offsetBytes;
            uint64 _sizeBytes;
            uint64 _serial; ///< 0 이면 아직 제출하지 않았다
            bool   _bFreed; ///< 제출하지 않고 놓았다
        };

        /** @brief `_mutex` 를 쥔 채 부릅니다. */
        void freeCompletedLocked( uint64 completedSerial );
        /** @brief `_mutex` 를 쥔 채 부릅니다. 없으면 nullptr 입니다. */
        Entry* findEntryLocked( uint64 id );

        mutable mutex _mutex;
        deque<Entry>  _listEntry;
        uint64        _capacityBytes;
        uint64        _alignmentBytes;
        uint64        _nextID;
    };
} // namespace sw
