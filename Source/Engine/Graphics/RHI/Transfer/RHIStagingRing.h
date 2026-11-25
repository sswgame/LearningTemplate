/**
 * @file RHIStagingRing.h
 * @brief 영속 매핑한 스테이징 버퍼 하나와 그 구간 할당기, 링보다 큰 한 장을 담는 단독 버퍼입니다. 업로드 링 · 되읽기 링이 하나씩 든다(DX12 · Vulkan).
 * @details 두 백엔드가 같은 순서를 손으로 되풀이했다 — 구간을 달라고 하고, 차 있으면 끝난 전송을 걷어 한 번 더, 결과를 제출 결과로 옮기고, 단독 구간이면 앞 단독 버퍼를 지우고
 *          그 크기로 새로 만들고, 할당기가 돌려받은 단독 버퍼는 회수가 지운다. 순서 · 잠금은 여기 하나이고 백엔드는 버퍼를 만들고 지우는 일만 준다.
 *          버퍼 형식 @p TBuffer 는 `isValid()` 를 갖는 값이다(네이티브 버퍼 + 매핑). 템플릿이라 간접 호출이 없다.
 *          GL 링은 따로다: 단독 버퍼를 만들 때 컨텍스트를 빌리는데, 회수는 컨텍스트를 쥔 채 단독 락을 잡아 같은 잠금 순서로 묶으면 서로 기다린다.
 *          Godot `RenderingDevice` 의 스테이징 블록 · 예산 한 벌과 같은 자리다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/ScopedDeferredLog.h"

#include "Engine/Graphics/RHI/Transfer/RHIStagingAllocator.h"
#include "Engine/Graphics/RHI/Transfer/RHIStagingRingUtil.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"

namespace sw
{
    /**
     * @class RHIStagingRing
     * @brief 파일 머리말 참고. 구간은 업로드면 시리얼을 붙여(`markSubmitted`) 워터마크가 지나면, 되읽기면 결과를 가져갈 때 돌아온다.
     */
    template <typename TBuffer>
    class RHIStagingRing
    {
    public:
        /** @brief @p fnCreate( 바이트, 버퍼& ) 로 링 버퍼를 만들고 할당기를 세웁니다. 만들지 못하면 false 입니다(이유는 만드는 쪽이 남긴다). */
        template <typename FnCreate>
        [[nodiscard]] bool initialize( uint64 capacityBytes, uint64 alignmentBytes, uint64 soloMaxBytes, FnCreate&& fnCreate )
        {
            if ( fnCreate( capacityBytes, _buffer ) == false )
                return false;
            _allocator.initialize( capacityBytes, alignmentBytes, soloMaxBytes );
            return true;
        }

        /** @brief 단독 버퍼와 링 버퍼를 @p fnDestroy( 버퍼& ) 로 지우고 할당기를 내립니다. GPU 는 쉬었다(종료 2단계). */
        template <typename FnDestroy>
        void shutdown( FnDestroy&& fnDestroy )
        {
            {
                ScopedLock<mutex> soloLock{ _soloMutex };
                fnDestroy( _soloBuffer );
            }
            fnDestroy( _buffer );
            _allocator.shutdown();
        }

        /**
         * @brief @p sizeBytes 구간을 잡습니다. 차 있으면 @p fnRetire() 로 끝난 전송을 걷고 한 번 더 본다. 단독 구간이면 @p fnCreate 로 그 크기의 단독 버퍼를 만든다.
         * @details 결과 옮기기 · 못 받은 구간 놓기 · 이유 로그는 `RHIStagingRingUtil` 한 곳이다(잃은 장치면 `Full` 도 `Rejected`, 계약 2). 단독 버퍼를 만들지 못하면 `Rejected` 다.
         */
        template <typename FnRetire, typename FnCreate, typename FnDestroy>
        [[nodiscard]] RHISubmitResult allocate( uint64 sizeBytes, const RHITransferSerials& serials, const utf8* pBackend, const utf8* pCaller, FnRetire&& fnRetire, FnCreate&& fnCreate,
                                                FnDestroy&& fnDestroy, RHIStagingSpan& outSpan )
        {
            RHIStagingAllocateResult result = _allocator.allocate( sizeBytes, serials.getWatermark(), outSpan );
            if ( result == RHIStagingAllocateResult::Full )
            {
                fnRetire();
                result = _allocator.allocate( sizeBytes, serials.getWatermark(), outSpan );
            }
            const RHISubmitResult submitResult = RHIStagingRingUtil::completeAllocation( _allocator, serials, result, sizeBytes, pBackend, pCaller, outSpan );
            if ( submitResult != RHISubmitResult::Accepted || outSpan.isSolo() == false )
                return submitResult;
            // 할당기가 단독 구간을 내줬다면 앞선 단독 구간은 이미 돌아왔다 — 남은 버퍼(아직 회수가 지우지 않은 것)를 GPU 가 더 쓰지 않는다.
            const ScopedDeferredLog deferredLog;
            ScopedLock<mutex>       soloLock{ _soloMutex };
            fnDestroy( _soloBuffer );
            if ( fnCreate( outSpan.getSizeBytes(), _soloBuffer ) )
                return RHISubmitResult::Accepted;
            return RHIStagingRingUtil::rejectSoloBufferFailure( _allocator, outSpan, pBackend, pCaller );
        }

        /** @brief 할당기가 단독 구간을 돌려받았으면 그 일회성 버퍼를 지웁니다(링 하나 크기 넘는 메모리를 쥐고 있지 않는다). 어느 스레드에서나 부른다. */
        template <typename FnDestroy>
        void destroyIdleSolo( const RHITransferSerials& serials, FnDestroy&& fnDestroy )
        {
            // 판정은 락 안에서 한다 — 밖에서 보면 그 사이 다른 스레드가 새 단독 구간을 받아 버퍼를 만들 수 있다.
            ScopedLock<mutex> soloLock{ _soloMutex };
            if ( _soloBuffer.isValid() == false )
                return;
            _allocator.freeCompleted( serials.getWatermark() );
            if ( _allocator.hasLiveSolo() == false )
                fnDestroy( _soloBuffer );
        }

        RHIStagingAllocator& getAllocator() { return _allocator; }
        /** @brief @p span 을 담은 버퍼입니다(단독 구간이면 단독 버퍼). 구간 안의 오프셋은 `span.getOffsetBytes()` 에서 센다. 구간을 쥔 동안 바뀌지 않는다. */
        [[nodiscard]] const TBuffer& getSpanBuffer( const RHIStagingSpan& span ) const { return span.isSolo() ? _soloBuffer : _buffer; }

    private:
        RHIStagingAllocator _allocator;
        TBuffer             _buffer{};
        mutex               _soloMutex; ///< 단독 버퍼 칸을 지키는 잎 락. 만드는 쪽(제출 스레드)과 지우는 쪽(아무 스레드의 회수)이 다르다
        TBuffer             _soloBuffer{};
    };
} // namespace sw
