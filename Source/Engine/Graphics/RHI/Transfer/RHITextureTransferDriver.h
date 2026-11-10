/**
 * @file RHITextureTransferDriver.h
 * @brief 텍스처 전송(업로드 · 구간 갱신 · 되읽기)을 스테이징 구간을 잡은 뒤 큐에 내기까지, 그리고 되읽기 구간을 돌려주기까지의 조립 순서를 한 곳에 둡니다.
 * @details DX12 와 Vulkan 의 전송 여섯이 같은 순서를 손으로 되풀이했다. 실패 갈래마다 스테이징 구간을 돌려줘야 하고(놓치면 링이 샌다),
 *          업로드는 제출 시리얼을 구간에 붙인 **뒤에** 레코드를 고쳐야 하며(앞서면 폴링이 끝나지 않은 전송을 끝났다고 본다),
 *          되읽기는 구간을 결과를 가져가거나 버릴 때까지 쥔다(버릴 때 복사가 아직 돌면 시리얼을 붙여 워터마크 뒤에 돌아오게 한다). 그 순서를 템플릿이 지킨다.
 *          백엔드는 작업 열기 · 네이티브 기록 · 큐 제출과 레코드 갱신만 훅으로 준다. 언리얼 `FRHICommandListImmediate::UpdateTexture2D` 뒤의
 *          업로드 배처와 `FRHIGPUTextureReadback`(스테이징 + 펜스 + 나중에 Lock)과 같은 자리다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Transfer/RHIStagingAllocator.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"

namespace sw
{
    /**
     * @class RHITextureTransferDriver
     * @brief 스테이징 구간 @p span 을 쥔 전송 하나를 큐에 냅니다. 업로드는 소스를 이미 구간에 복사했다(계약 1).
     */
    class RHITextureTransferDriver
    {
    public:
        /**
         * @brief 업로드: 작업을 열고(@p fnBegin), 기록하고(@p fnRecord), 내고(@p fnSubmit → 시리얼, 0 이면 실패), 구간에 시리얼을 붙인 뒤 @p fnCommit( serial ) 로 레코드를 고칩니다.
         * @details 열기 · 제출이 실패하면 구간을 돌려주고 `Rejected` 다. 큐 제출 잠금이 필요한 백엔드(Vulkan `_transferMutex`)는 부르기 전에 잡아 둔다.
         */
        template <typename TJob, typename TRing, typename FnBegin, typename FnRecord, typename FnSubmit, typename FnCommit>
        static RHITransferSubmit runJob( TRing& ring, const RHIStagingSpan& span, RHITransferSerials& serials, FnBegin&& fnBegin, FnRecord&& fnRecord, FnSubmit&& fnSubmit,
                                         FnCommit&& fnCommit )
        {
            return run<TJob>( ring, span, serials, true, fnBegin, fnRecord, fnSubmit, fnCommit );
        }

        /**
         * @brief 되읽기: `runJob` 과 같은 순서지만 구간에 시리얼을 붙이지 않습니다 — 결과가 가져갈 때까지 구간에 남아야 한다.
         *        @p fnCommit( serial ) 이 보관표에 구간을 적는다. 구간은 `releaseTakenReadback` · `releaseDiscardedReadback` 가 돌려준다.
         */
        template <typename TJob, typename TRing, typename FnBegin, typename FnRecord, typename FnSubmit, typename FnCommit>
        static RHITransferSubmit runReadbackJob( TRing& ring, const RHIStagingSpan& span, RHITransferSerials& serials, FnBegin&& fnBegin, FnRecord&& fnRecord,
                                                 FnSubmit&& fnSubmit, FnCommit&& fnCommit )
        {
            return run<TJob>( ring, span, serials, false, fnBegin, fnRecord, fnSubmit, fnCommit );
        }

        /**
         * @brief 가져간 되읽기의 구간을 돌려줍니다(복사는 끝났다 — 가져가기는 `Complete` 뒤에만 된다).
         * @details 링 구간으로 받는 세 백엔드(Vulkan · DX12 · GL)가 같이 쓴다. DX11 은 D3D11 이 텍스처를 버퍼로 복사하지 못해 되읽기마다 스테이징 텍스처이고
         *          COM 참조가 GPU 가 끝날 때까지 붙든다. WebGPU 는 되읽기마다 버퍼이고 버릴 때 매핑 콜백이 놓는다 — 둘 다 "끝나기 전에 다시 쓰지 않는다" 를 자원 수명이 지킨다.
         */
        static void releaseTakenReadback( RHIStagingAllocator& allocator, const RHIStagingSpan& span ) { allocator.free( span ); }

        /** @brief 버린 되읽기의 구간을 돌려줍니다. 복사가 아직 돌면 시리얼을 붙여 워터마크가 지난 뒤에 돌아오게 한다(다음 되읽기가 같은 자리를 받지 않는다). */
        static void releaseDiscardedReadback( RHIStagingAllocator& allocator, const RHIStagingSpan& span, const RHITransferSerials& serials, uint64 serial )
        {
            if ( serials.isComplete( serial ) )
                allocator.free( span );
            else
                allocator.markSubmitted( span, serial );
        }

    private:
        template <typename TJob, typename TRing, typename FnBegin, typename FnRecord, typename FnSubmit, typename FnCommit>
        static RHITransferSubmit run( TRing& ring, const RHIStagingSpan& span, RHITransferSerials& serials, bool bReleaseSpanAtSerial, FnBegin& fnBegin, FnRecord& fnRecord,
                                      FnSubmit& fnSubmit, FnCommit& fnCommit )
        {
            TJob job{};
            if ( fnBegin( job ) == false )
            {
                ring._allocator.free( span );
                return RHITransferSubmit::rejected();
            }
            fnRecord( job );
            const uint64 serial = fnSubmit( job );
            if ( serial == 0 )
            {
                ring._allocator.free( span );
                return RHITransferSubmit::rejected();
            }
            if ( bReleaseSpanAtSerial )
                ring._allocator.markSubmitted( span, serial );
            fnCommit( serial );
            return serials.makeAccepted( serial );
        }
    };
} // namespace sw
