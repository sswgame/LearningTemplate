#include "pch.h"

#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"

#include "Core/Diagnostics/CallStackCapture.h"

#include "Engine/Graphics/RHI/Transfer/RHIStagingAllocator.h"

namespace sw
{
    RHITransferSerials::RHITransferSerials()
        : _lastIssued{ 0 }
        , _watermark{ 0 }
        , _generation{ allocateRHIDeviceGeneration() }
        , _bLost{ SW_FALSE }
    {
    }

    uint64 RHITransferSerials::issue()
    {
        return _lastIssued.fetch_add( 1, std::memory_order_acq_rel ) + 1;
    }

    void RHITransferSerials::advanceWatermark( uint64 serial )
    {
        _watermark.fetch_max( serial, std::memory_order_acq_rel );
    }

    bool RHITransferSerials::markLost()
    {
        return _bLost.exchange( SW_TRUE, std::memory_order_acq_rel ) == SW_FALSE;
    }

    RHISubmitResult RHITransferSerials::classifyStagingAllocation( RHIStagingAllocateResult result ) const
    {
        if ( isLost() )
            return RHISubmitResult::Rejected;
        switch ( result )
        {
            case RHIStagingAllocateResult::Allocated:
            {
                return RHISubmitResult::Accepted;
            }
            case RHIStagingAllocateResult::Full:
            {
                return RHISubmitResult::Busy;
            }
            case RHIStagingAllocateResult::TooLarge:
            {
                return RHISubmitResult::Rejected;
            }
        }
        return RHISubmitResult::Rejected;
    }

    RHITransferStatus RHITransferSerials::classify( RHITransferTicket ticket ) const
    {
        if ( ticket.isValid() == false )
        {
            SW_LOG_ERROR_ID( "RHI.TransferTicketInvalid", "pollTransfer: the ticket is invalid (serial 0): the submit was not accepted" );
            return RHITransferStatus::Failed;
        }
        if ( ticket._deviceGeneration != _generation )
        {
            SW_LOG_ERROR_ID( "RHI.TransferTicketInvalid", "pollTransfer: the ticket %# is from device generation %# but this device is generation %# (the backend was replaced)", ticket._serial,
                             ticket._deviceGeneration, _generation );
            return RHITransferStatus::Failed;
        }
        if ( ticket._serial > getLastIssued() )
        {
            SW_LOG_ERROR_ID( "RHI.TransferTicketInvalid", "pollTransfer: the ticket %# was never issued (last issued %#)", ticket._serial, getLastIssued() );
            return RHITransferStatus::Failed;
        }
        if ( isComplete( ticket._serial ) )
            return RHITransferStatus::Complete;
        if ( isLost() )
            return RHITransferStatus::Failed;
        return RHITransferStatus::Pending;
    }

    bool RHITransferSerials::isFirstUploadComplete( const RHITextureUploadSerials& marks ) const
    {
        const uint64 firstUploadSerial = marks._firstSerial.load(); // 첫 업로드만 본다(RHITextureUploadSerials)
        return firstUploadSerial == 0 || isComplete( firstUploadSerial );
    }

    bool RHITransferSerials::isTextureReadyToRegister( const RHITextureUploadSerials& marks, RHITextureHandle texture ) const
    {
        if ( isFirstUploadComplete( marks ) )
            return true;
        const uint64 firstUploadSerial = marks._firstSerial.load();
        // 부른 자리를 함께 남긴다 — 텍스처를 넘긴 쪽(패널 · 캐시)이 곧 완료를 기다리지 않은 쪽이다.
        CallStack stack{};
        CallStackCapture::capture( stack, 2 );
        SW_LOG_ERROR_ID( "RHI.RegisterBeforeTransferDone",
                         "registerBindlessTexture: texture %# has an unfinished first upload (serial %#, watermark %#): register only after pollTransfer returns Complete\n%#",
                         texture, firstUploadSerial, getWatermark(), CallStackCapture::symbolize( stack ).c_str() );
        return false;
    }
} // namespace sw
