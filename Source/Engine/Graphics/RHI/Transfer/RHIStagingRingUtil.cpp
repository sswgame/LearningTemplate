#include "pch.h"

#include "Engine/Graphics/RHI/Transfer/RHIStagingRingUtil.h"

namespace sw
{
    RHISubmitResult RHIStagingRingUtil::completeAllocation( RHIStagingAllocator& tracker, const RHITransferSerials& serials, RHIStagingAllocateResult result, uint64 sizeBytes,
                                                            const utf8* pBackend, const utf8* pCaller, const RHIStagingSpan& span )
    {
        // 잃은 장치면 워터마크가 멈춰 링이 다시 비지 않는다 — Full 도 Busy 가 아니라 Rejected 다(계약 2).
        const RHISubmitResult submitResult = serials.classifyStagingAllocation( result );
        if ( submitResult != RHISubmitResult::Rejected )
            return submitResult;
        if ( result == RHIStagingAllocateResult::Allocated )
            tracker.free( span );
        if ( result == RHIStagingAllocateResult::TooLarge )
        {
            SW_LOG_ERROR_ID( "RHIStagingRingUtil.BytesExceedByteStaging", "%#: %# bytes exceed the %#-byte %# staging ring and the %#-byte single transfer limit; raise gv_rhiStagingSingleMaxMegabytes to transfer it", pCaller,
                             sizeBytes, tracker.getCapacityBytes(), pBackend, tracker.getSoloMaxBytes() );
        }
        else
        {
            SW_LOG_ERROR_ID( "RHIStagingRingUtil.DeviceLostStagingRing", "%#: the %# device is lost; the staging ring no longer drains and the transfer is rejected", pCaller, pBackend );
        }
        return RHISubmitResult::Rejected;
    }

    RHISubmitResult RHIStagingRingUtil::rejectSoloBufferFailure( RHIStagingAllocator& tracker, const RHIStagingSpan& span, const utf8* pBackend, const utf8* pCaller )
    {
        tracker.free( span );
        SW_LOG_ERROR_ID( "RHIStagingRingUtil.ByteSingleTransferBuffer", "%#: a %#-byte %# single transfer buffer could not be created; the transfer is rejected", pCaller, span._sizeBytes, pBackend );
        return RHISubmitResult::Rejected;
    }
} // namespace sw
