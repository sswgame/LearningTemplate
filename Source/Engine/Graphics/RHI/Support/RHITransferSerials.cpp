#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHITransferSerials.h"

namespace sw
{
    SW_LOG_CALLER( "RHITransferSerials" );

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

    void RHITransferSerials::markLost()
    {
        _bLost.store( SW_TRUE, std::memory_order_release );
    }

    RHITransferStatus RHITransferSerials::classify( RHITransferTicket ticket ) const
    {
        if ( ticket.isValid() == false )
        {
            SW_LOG_ERROR( "pollTransfer: the ticket is invalid (serial 0): the submit was not accepted" );
            return RHITransferStatus::Failed;
        }
        if ( ticket._deviceGeneration != _generation )
        {
            SW_LOG_ERROR( "pollTransfer: the ticket %# is from device generation %# but this device is generation %# (the backend was replaced)", ticket._serial,
                          ticket._deviceGeneration, _generation );
            return RHITransferStatus::Failed;
        }
        if ( ticket._serial > getLastIssued() )
        {
            SW_LOG_ERROR( "pollTransfer: the ticket %# was never issued (last issued %#)", ticket._serial, getLastIssued() );
            return RHITransferStatus::Failed;
        }
        if ( isComplete( ticket._serial ) )
            return RHITransferStatus::Complete;
        if ( _bLost.load( std::memory_order_acquire ) == SW_TRUE )
            return RHITransferStatus::Failed;
        return RHITransferStatus::Pending;
    }

    bool RHITransferSerials::isTextureReadyToRegister( uint64 lastTransferSerial, RHITextureHandle texture ) const
    {
        if ( lastTransferSerial == 0 || isComplete( lastTransferSerial ) )
            return true;
        SW_LOG_ERROR( "registerBindlessTexture: texture %# has an unfinished transfer (serial %#, watermark %#): register only after pollTransfer returns Complete", texture,
                      lastTransferSerial, getWatermark() );
        return false;
    }
} // namespace sw
